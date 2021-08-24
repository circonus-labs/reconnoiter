/*
 * Copyright (c) 2012, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2013-2017, Circonus, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include <mtev_defines.h>

#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>
#include <arpa/inet.h>

#if defined __linux__
#include <sys/socket.h>
#endif

#include <mtev_hash.h>
#include <mtev_rand.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_mtev_bridge.h"
#include "noit_socket_listener.h"

#define MAX_CHECKS 3
#define RECV_BUFFER_CAPACITY (128*1024)

static size_t Cstat, Mstat, Gstat, Pstat;
static mtev_log_stream_t nlerr = NULL;
static mtev_log_stream_t nldeb = NULL;
static mtev_log_stream_t nlperf = NULL;
static const char *COUNTER_STRING = "c";

typedef union {
  struct sockaddr_in in;
  struct sockaddr_in6 in6;
} addr_t;

typedef struct statsd_mod_config {
  mtev_hash_table *options;
  int packets_per_cycle;
  unsigned short port;
  uuid_t primary;
  int primary_active;
  noit_check_t *check;
  int ipv4_fd;
  int ipv6_fd;
  size_t payload_len;
  struct iovec *payload;
  addr_t *addr;
  bool use_recvmmsg;
} statsd_mod_config_t;

typedef struct {
  noit_module_t *self;
  int stats_count;
} statsd_closure_t;

static int
statsd_submit(noit_module_t *self, noit_check_t *check,
              noit_check_t *cause) {
  statsd_closure_t *ccl;
  struct timeval now, duration;
  statsd_mod_config_t *conf;

  conf = noit_module_get_userdata(self);
  if(!conf->primary_active) conf->check = NULL;
  if(0 == memcmp(conf->primary, check->checkid, sizeof(uuid_t))) {
    conf->check = check;
    if(NOIT_CHECK_DISABLED(check) || NOIT_CHECK_KILLED(check)) {
      conf->check = NULL;
      return 0;
    }
  }

  /* We are passive, so we don't do anything for transient checks */
  if(check->flags & NP_TRANSIENT) return 0;

  if(!check->closure) {
    ccl = check->closure = calloc(1, sizeof(*ccl));
    ccl->self = self;
  } else {
    // Don't count the first run
    char human_buffer[256];
    ccl = (statsd_closure_t*)check->closure;
    mtev_gettimeofday(&now, NULL);
    sub_timeval(now, check->last_fire_time, &duration);
    noit_stats_set_whence(check, &now);
    noit_stats_set_duration(check, duration.tv_sec * 1000 + duration.tv_usec / 1000);

    snprintf(human_buffer, sizeof(human_buffer),
             "dur=%ld,run=%d,stats=%d", duration.tv_sec * 1000 + duration.tv_usec / 1000,
             check->generation, ccl->stats_count);
    mtevL(nldeb, "statsd(%s) [%s]\n", check->target, human_buffer);

    // Not sure what to do here
    noit_stats_set_available(check, (ccl->stats_count > 0) ?
        NP_AVAILABLE : NP_UNAVAILABLE);
    noit_stats_set_state(check, (ccl->stats_count > 0) ?
        NP_GOOD : NP_BAD);
    noit_stats_set_status(check, human_buffer);
    if(check->last_fire_time.tv_sec)
      noit_check_passive_set_stats(check);

    memcpy(&check->last_fire_time, &now, sizeof(duration));
  }
  ccl->stats_count = 0;
  return 0;
}

static void
update_check(noit_check_t *check, const char *key, char type,
             double diff, double sample) {
  char buff[MAX_METRIC_TAGGED_NAME + 24];

  if (sample == 0.0) return; /* would be a div-by-zero */
  if (check->closure == NULL) return;

  switch(type) {
    case 'c':
      snprintf(buff, sizeof(buff), "%s|ST[statsd_type:count]", key);
      break;
    case 'g':
      snprintf(buff, sizeof(buff), "%s|ST[statsd_type:gauge]", key);
      break;
    case 'm':
      snprintf(buff, sizeof(buff), "%s|ST[statsd_type:timing]", key);
      break;
    default:
      strlcpy(buff, key, sizeof(buff));
      break;
  }
  if(type == 'c') {
    uint64_t count = diff / sample;
    double bin = 0;
    noit_stats_set_metric_histogram(check, buff, mtev_false, METRIC_DOUBLE, &bin, count);
    Cstat++;
  } else if(type == 'm') {
    noit_stats_set_metric_histogram(check, buff, mtev_false, METRIC_DOUBLE, &diff, (uint64_t)(1.0/sample));
    Mstat++;
  } else {
    noit_stats_set_metric(check, buff, METRIC_DOUBLE, &diff);
    Gstat++;
  }
}

static void
statsd_handle_payload(noit_check_t **checks, int nchecks,
                      char *payload, int len) {
  char *cp, *ecp, *endptr;
  cp = ecp = payload;
  endptr = payload + len;
  while(ecp != NULL && ecp < endptr) {
    char *line_end = endptr;
    int i, idx = 0, last_space = 0;
    char key[MAX_METRIC_TAGGED_NAME], *value;
    const char *type = NULL;
    ecp = memchr(ecp, '\n', len - (ecp - payload));
    if(ecp) {
      *ecp++ = '\0';
      line_end = ecp;
    }
    mtev_boolean tags = mtev_false;
    while(idx < sizeof(key) - 6 /* |ST[]\0 */ && *cp != '\0' && *cp != ':') {
      if(isspace(*cp)) {
        if(!last_space) key[idx++] = '_';
        cp++;
        last_space = 1;
        continue;
      }
      else if(*cp == '/') key[idx++] = '-';
      else if((*cp >= 'a' && *cp <= 'z') ||
              (*cp >= 'A' && *cp <= 'Z') ||
              (*cp >= '0' && *cp <= '9') ||
              (*cp == '=' && !tags) ||
              *cp == '`' || *cp == '.' || *cp == '_' || *cp == '-') {
        key[idx++] = *cp;
      } else if(tags && *cp == '=') {
        key[idx++] = ':';
      } else if(*cp == ';' || *cp == ',') {
        if(!tags) {
          key[idx++] = '|';
          key[idx++] = 'S';
          key[idx++] = 'T';
          key[idx++] = '[';
          tags = mtev_true;
        } else {
          key[idx++] = ',';
        }
      }
      last_space = 0;
      cp++;
    }

    /* We've finished graphite-style tags, now we skip to the end to eat up
     * DD-style trailing tags.
     */
    char *ddtags = memchr(cp, '#', line_end - cp);
    if(ddtags) {
      *ddtags = '\0';
      ddtags++;
      ssize_t len = line_end - ddtags - 1;
      if(len > 0) {
        mtevL(nldeb, "found DD tags: %.*s\n", (int)len, ddtags);
        if(tags && len < sizeof(key) - idx - 3) {
          key[idx++] = ',';
          memcpy(key+idx, ddtags, len);
          idx += len;
        }
        else if(!tags && len < sizeof(key) - idx - sizeof("|ST[]")) {
          key[idx++] = '|';
          key[idx++] = 'S';
          key[idx++] = 'T';
          key[idx++] = '[';
          tags = mtev_true;
          memcpy(key+idx, ddtags, len);
          idx += len;
        } else {
          mtevL(nlerr, "found DD tags (too long): %.*s\n", (int)len, ddtags);
        }
      }
    }

    if(tags) key[idx++] = ']';
    key[idx] = '\0';

    while((NULL != cp) && NULL != (value = strchr(cp, ':'))) {
      double sampleRate = 1.0;
      double diff = 1.0;
      if(value) {
        *value++ = '\0';
        cp = strchr(value, '|');
        if(cp) {
          char *sample_string;
          *cp++ = '\0';
          type = cp;
          sample_string = strchr(type, '|');
          if(sample_string) {
            *sample_string++ = '\0';
            if(*sample_string == '@')
              sampleRate = strtod(sample_string + 1, NULL);
          }
          if(*type == 'g') {
            diff = 0.0;
          }
          else if(0 == strcmp(type, "ms")) {
            diff = 0.0;
          }
          else {
            type = NULL;
          }
        }
        diff = strtod(value, NULL);
      }
      if(type == NULL) type = COUNTER_STRING;

      switch(*type) {
        case 'g':
        case 'c':
        case 'm':
          for(i=0;i<nchecks;i++) {
            update_check(checks[i], key, *type, diff, sampleRate);
          }
          break;
        default:
          break;
      }
      cp = value;
    }

    cp = ecp;
  }
}

static void
statsd_handle_single_message(noit_module_t *const self,
                             noit_check_t *parent,
                             struct iovec *payload,
                             addr_t *addr) {
  noit_check_t *checks[MAX_CHECKS];
  char ip[INET6_ADDRSTRLEN];
  int nchecks = 0;

  switch(addr->in.sin_family) {
  case AF_INET:
    inet_ntop(AF_INET, &addr->in.sin_addr, ip, INET6_ADDRSTRLEN);
    break;
  case AF_INET6:
    inet_ntop(AF_INET6, &addr->in6.sin6_addr, ip, INET6_ADDRSTRLEN);
    break;
  default:
    ip[0] = '\0';
  }

  if(*ip)
    nchecks = noit_poller_lookup_by_ip_module(ip, self->hdr.name,
                                              checks, MAX_CHECKS-1);
  nchecks += noit_poller_lookup_by_ip_module("0.0.0.0", self->hdr.name,
                                              checks+nchecks, MAX_CHECKS-1-nchecks);
  mtevL(nldeb, "statsd(%zu bytes) from '%s' -> %d checks%s\n", payload->iov_len,
        ip, (int)nchecks, parent ? " + a parent" : "");
  if(parent) checks[nchecks++] = parent;
  if(nchecks) {
    statsd_handle_payload(checks, nchecks, payload->iov_base, payload->iov_len);
    for (size_t i = 0; i < nchecks; i++) {
      noit_check_deref(checks[i]);
    }
  }
}

static int
statsd_handler(eventer_t e, int mask, void *closure,
               struct timeval *now) {
  noit_module_t *self = (noit_module_t *)closure;
  statsd_mod_config_t *const conf = noit_module_get_userdata(self);
  noit_check_t *parent = NULL;

  if(conf->primary_active) parent = noit_poller_lookup(conf->primary);

  for(int packets_per_cycle = MAX(conf->packets_per_cycle, 1); packets_per_cycle > 0; packets_per_cycle--) {
    const int fd = eventer_get_fd(e);

    if (conf->use_recvmmsg) {
#if defined __linux__
      struct mmsghdr msgs[conf->payload_len];
      int nmmsgs;

      memset(msgs, 0, sizeof(msgs));

      for (size_t i = 0; i < conf->payload_len; i++) {
        conf->payload[i].iov_len = RECV_BUFFER_CAPACITY - 1;
        msgs[i].msg_hdr.msg_iov = &conf->payload[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &conf->addr[i];
        msgs[i].msg_hdr.msg_namelen = sizeof(addr_t);
      }

      nmmsgs = recvmmsg(fd, msgs, conf->payload_len, 0, NULL);

      if (nmmsgs == -1) {
        if (errno != EAGAIN) {
          mtevL(nlerr, "statsd: recvmmsg() -> %s\n", strerror(errno));
        }

        break;
      }

      for (size_t i = 0; i < nmmsgs; i++) {
        ((char*)conf->payload[i].iov_base)[msgs[i].msg_len] = 0;
        conf->payload[i].iov_len = msgs[i].msg_len;
        statsd_handle_single_message(self, parent, &conf->payload[i], &conf->addr[i]);
        Pstat++;
      }
#else
      conf->use_recvmmsg = false;
#endif
    }

    if (!conf->use_recvmmsg) {
      socklen_t addrlen = sizeof(addr_t);
      ssize_t len;
      len = recvfrom(fd, conf->payload[0].iov_base, conf->payload[0].iov_len-1, 0,
                      (struct sockaddr *)&conf->addr[0], &addrlen);

      if(len < 0) {
        if(errno != EAGAIN)
          mtevL(nlerr, "statsd: recvfrom() -> %s\n", strerror(errno));
        break;
      }

      ((char*)conf->payload[0].iov_base)[len] = 0;
      conf->payload[0].iov_len = len;
      statsd_handle_single_message(self, parent, &conf->payload[0], &conf->addr[0]);
      Pstat++;
    }
  }
  return EVENTER_READ | EVENTER_EXCEPTION;
}

static int noit_statsd_initiate_check(noit_module_t *self,
                                        noit_check_t *check,
                                        int once, noit_check_t *cause) {
  check->flags |= NP_PASSIVE_COLLECTION;
  if (check->flags & NP_TRANSIENT) return 0;
  if (check->closure == NULL) {
    statsd_closure_t *ccl;
    ccl = check->closure = (void *)calloc(1, sizeof(statsd_closure_t));
    ccl->self = self;
  }
  INITIATE_CHECK(statsd_submit, self, check, cause);
  return 0;
}

static int noit_statsd_config(noit_module_t *self, mtev_hash_table *options) {
  statsd_mod_config_t *conf;
  conf = noit_module_get_userdata(self);
  if(conf) {
    if(conf->options) {
      mtev_hash_destroy(conf->options, free, free);
      free(conf->options);
    }
  }
  else
    conf = calloc(1, sizeof(*conf));
  conf->options = options;
  noit_module_set_userdata(self, conf);
  return 1;
}

static int noit_statsd_onload(mtev_image_t *self) {
  nlerr = mtev_log_stream_find("error/statsd");
  nldeb = mtev_log_stream_find("debug/statsd");
  nlperf = mtev_log_stream_find("debug/statsd/performance");
  return 0;
}

static void report(void) {
  size_t last = 0, Plast = 0;
  while(1) {
    eventer_aco_sleep(&(struct timeval){ 1UL, 0UL });
    size_t current = Cstat + Mstat + Gstat;
    mtevL(nlperf, "STATSD: C=%zu, M=%zu, G=%zu, (%zu/s) packets: %zu (%zu/s)\n",
          Cstat, Mstat, Gstat, current - last, Pstat, Pstat - Plast);
    last = current;
    Plast = Pstat;
  }
}

static int noit_statsd_init(noit_module_t *self) {
  unsigned short port = 8125;
  int packets_per_cycle = 1000;
  size_t recv_buffer_len = 20;
  bool use_recvmmsg = true;
  struct sockaddr_in skaddr;
  int sockaddr_len;
  const char *config_val;
  statsd_mod_config_t *conf;
  eventer_aco_start(report, NULL);
  conf = noit_module_get_userdata(self);

  eventer_name_callback("statsd/statsd_handler", statsd_handler);

  if(mtev_hash_retr_str(conf->options, "check", strlen("check"),
                        (const char **)&config_val)) {
    if(mtev_uuid_parse((char *)config_val, conf->primary) != 0)
      mtevL(noit_error, "statsd check isn't a UUID\n");
    conf->primary_active = 1;
    conf->check = NULL;
  }
  if(mtev_hash_retr_str(conf->options, "port", strlen("port"),
                        (const char **)&config_val)) {
    port = atoi(config_val);
  }
  conf->port = port;

  socklen_t desired_rcvbuf = 1024*1024*4;
  if(mtev_hash_retr_str(conf->options, "rcvbuf",
                        strlen("rcvbuf"),
                        (const char **)&config_val)) {
    desired_rcvbuf = atoi(config_val);
  }

  if(mtev_hash_retr_str(conf->options, "packets_per_cycle",
                        strlen("packets_per_cycle"),
                        (const char **)&config_val)) {
    packets_per_cycle = atoi(config_val);
  }
  conf->packets_per_cycle = packets_per_cycle;

  if(mtev_hash_retr_str(conf->options, "use_recvmmsg",
                        strlen("use_recvmmsg"),
                        (const char **)&config_val)) {
    use_recvmmsg = atoi(config_val);
  }
  conf->use_recvmmsg = use_recvmmsg;
  if(mtev_hash_retr_str(conf->options, "recv_buffer_len",
                        strlen("recv_buffer_len"),
                        (const char **)&config_val)) {
    recv_buffer_len = atoll(config_val);
  }

  conf->payload_len = use_recvmmsg ? recv_buffer_len : 1;
  conf->payload = calloc(conf->payload_len, sizeof(struct iovec));
  conf->addr = calloc(conf->payload_len, sizeof(addr_t));

  for (size_t i = 0; i < conf->payload_len; i++) {
    conf->payload[i].iov_len = RECV_BUFFER_CAPACITY;
    conf->payload[i].iov_base = malloc(RECV_BUFFER_CAPACITY);
    if(!conf->payload[i].iov_base) {
      for (size_t j = 0; j < i; j++) {
        free(conf->payload[j].iov_base);
      }

      free(conf->payload);
      free(conf->addr);
      mtevL(noit_error, "statsd malloc() failed\n");
      return -1;
    }
  }

  conf->ipv4_fd = socket(PF_INET, NE_SOCK_CLOEXEC|SOCK_DGRAM, IPPROTO_UDP);
  if(conf->ipv4_fd < 0) {
    mtevL(noit_error, "statsd: socket failed: %s\n", strerror(errno));
    return -1;
  }
  else {
    if(eventer_set_fd_nonblocking(conf->ipv4_fd)) {
      close(conf->ipv4_fd);
      conf->ipv4_fd = -1;
      mtevL(noit_error,
            "collectd: could not set socket non-blocking: %s\n",
            strerror(errno));
      return -1;
    }
  }
  socklen_t reuse = 1;
  if(setsockopt(conf->ipv4_fd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(reuse)) != 0) {
    mtevL(nlerr, "statsd listener(IPv4) failed(%s) to set REUSEADDR (doing our best)\n", strerror(errno));
  }
#ifdef SO_REUSEPORT
  reuse = 1;
  if(setsockopt(conf->ipv4_fd, SOL_SOCKET, SO_REUSEPORT, (void *)&reuse, sizeof(reuse)) != 0) {
    mtevL(nlerr, "statsd listener(IPv4) failed(%s) to set REUSEPORT (doing our best)\n", strerror(errno));
  }
#endif
  if(setsockopt(conf->ipv4_fd, SOL_SOCKET, SO_RCVBUF, &desired_rcvbuf, sizeof(desired_rcvbuf)) < 0) {
    mtevL(nlerr, "statsd listener(IPv4) failed(%s) to set recieve buffer\n", strerror(errno));
  }
  memset(&skaddr, 0, sizeof(skaddr));
  skaddr.sin_family = AF_INET;
  skaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  skaddr.sin_port = htons(conf->port);
  sockaddr_len = sizeof(skaddr);
  if(bind(conf->ipv4_fd, (struct sockaddr *)&skaddr, sockaddr_len) < 0) {
    mtevL(noit_error, "bind failed[%d]: %s\n", conf->port, strerror(errno));
    close(conf->ipv4_fd);
    return -1;
  }

  if(conf->ipv4_fd >= 0) {
    eventer_t newe;
    newe = eventer_alloc_fd(statsd_handler, self, conf->ipv4_fd,
                            EVENTER_READ | EVENTER_EXCEPTION);
    eventer_pool_t *dp = noit_check_choose_pool_by_module(self->hdr.name);
    if(dp) eventer_set_owner(newe, eventer_choose_owner_pool(dp, mtev_rand()));
    eventer_add(newe);
  }

  conf->ipv6_fd = socket(AF_INET6, NE_SOCK_CLOEXEC|SOCK_DGRAM, IPPROTO_UDP);
  if(conf->ipv6_fd < 0) {
    mtevL(noit_error, "statsd: IPv6 socket failed: %s\n",
          strerror(errno));
  }
  else {
    if(eventer_set_fd_nonblocking(conf->ipv6_fd)) {
      close(conf->ipv6_fd);
      conf->ipv6_fd = -1;
      mtevL(noit_error,
            "statsd: could not set socket non-blocking: %s\n",
            strerror(errno));
    }
    else {
      struct sockaddr_in6 skaddr6;
      struct in6_addr in6addr_any;
      sockaddr_len = sizeof(skaddr6);
      memset(&skaddr6, 0, sizeof(skaddr6));
      skaddr6.sin6_family = AF_INET6;
      memset(&in6addr_any, 0, sizeof(in6addr_any));
      skaddr6.sin6_addr = in6addr_any;
      skaddr6.sin6_port = htons(conf->port);

      socklen_t reuse = 1;
      if(setsockopt(conf->ipv6_fd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(reuse)) != 0) {
        mtevL(nlerr, "statsd listener(IPv6) failed(%s) to set REUSEADDR (doing our best)\n", strerror(errno));
      }
#ifdef SO_REUSEPORT
      reuse = 1;
      if(setsockopt(conf->ipv6_fd, SOL_SOCKET, SO_REUSEPORT, (void *)&reuse, sizeof(reuse)) != 0) {
        mtevL(nlerr, "statsd listener(IPv6) failed(%s) to set REUSEPORT (doing our best)\n", strerror(errno));
      }
#endif
      if(setsockopt(conf->ipv6_fd, SOL_SOCKET, SO_RCVBUF, &desired_rcvbuf, sizeof(desired_rcvbuf)) < 0) {
        mtevL(nlerr, "statsd listener(IPv6) failed(%s) to set recieve buffer\n", strerror(errno));
      }
      if(bind(conf->ipv6_fd, (struct sockaddr *)&skaddr6, sockaddr_len) < 0) {
        mtevL(noit_error, "bind(IPv6) failed[%d]: %s\n",
              conf->port, strerror(errno));
        close(conf->ipv6_fd);
        conf->ipv6_fd = -1;
      }
    }
  }

  if(conf->ipv6_fd >= 0) {
    eventer_t newe;
    newe = eventer_alloc_fd(statsd_handler, self, conf->ipv6_fd,
                            EVENTER_READ | EVENTER_EXCEPTION);
    eventer_pool_t *dp = noit_check_choose_pool_by_module(self->hdr.name);
    if(dp) eventer_set_owner(newe, eventer_choose_owner_pool(dp, mtev_rand()));
    eventer_add(newe);
  }

  noit_module_set_userdata(self, conf);
  return 0;
}

#include "statsd.xmlh"
noit_module_t statsd = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "statsd",
    .description = "statsd collection",
    .xml_description = statsd_xml_description,
    .onload = noit_statsd_onload
  },
  noit_statsd_config,
  noit_statsd_init,
  noit_statsd_initiate_check,
  NULL
};

static int statsd_tcp_handle_payload(noit_check_t *check, char *payload, size_t len) {
  statsd_handle_payload(&check, 1, payload, len);
  return 0;
}
static int statsd_tcp_handler(eventer_t e, int m, void *c, struct timeval *t) {
  return listener_handler(e,m,c,t);
}
static int statsd_tcp_listener(eventer_t e, int m, void *c, struct timeval *t) {
  return listener_listen_handler(e,m,c,t);
}
static int
noit_statsd_tcp_initiate_check(noit_module_t *self,
                               noit_check_t *check,
                               int once, noit_check_t *cause) {
  check->flags |= NP_PASSIVE_COLLECTION;
  if (check->closure == NULL) {
    listener_closure_t *ccl;
    ccl =
      listener_closure_alloc("statsd", self, check,
                             nldeb, nlerr,
                             statsd_tcp_handle_payload,
                             NULL);
    check->closure = ccl;
    unsigned short port = 8126;
    int rows_per_cycle = 100;
    struct sockaddr_in skaddr;
    int sockaddr_len;
    const char *config_val;

    if(mtev_hash_retr_str(check->config, "listen_port", strlen("listen_port"),
                          (const char **)&config_val)) {
      port = atoi(config_val);
    }
    ccl->port = port;

    if(mtev_hash_retr_str(check->config, "rows_per_cycle",
                          strlen("rows_per_cycle"),
                          (const char **)&config_val)) {
      rows_per_cycle = atoi(config_val);
    }
    ccl->rows_per_cycle = rows_per_cycle;

    if(port > 0) ccl->ipv4_listen_fd = socket(AF_INET, NE_SOCK_CLOEXEC|SOCK_STREAM, IPPROTO_TCP);
    if(ccl->ipv4_listen_fd < 0) {
      if(port > 0) {
        mtevL(noit_error, "statsd: socket failed: %s\n", strerror(errno));
        return -1;
      }
    }
    else {
      if(eventer_set_fd_nonblocking(ccl->ipv4_listen_fd)) {
        close(ccl->ipv4_listen_fd);
        ccl->ipv4_listen_fd = -1;
        mtevL(noit_error,
              "statsd: could not set socket (IPv4) non-blocking: %s\n",
              strerror(errno));
        return -1;
      }
      socklen_t reuse = 1;
      if (setsockopt(ccl->ipv4_listen_fd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(reuse)) != 0) {
        mtevL(noit_error, "statsd listener(IPv4) failed(%s) to set REUSEADDR (doing our best)\n", strerror(errno));
      }
#ifdef SO_REUSEPORT
      reuse = 1;
      if (setsockopt(ccl->ipv4_listen_fd, SOL_SOCKET, SO_REUSEPORT, (void *)&reuse, sizeof(reuse)) != 0) {
        mtevL(noit_error, "statsd listener(IPv4) failed(%s) to set REUSEPORT (doing our best)\n", strerror(errno));
      }
#endif
      memset(&skaddr, 0, sizeof(skaddr));
      skaddr.sin_family = AF_INET;
      skaddr.sin_addr.s_addr = htonl(INADDR_ANY);
      skaddr.sin_port = htons(ccl->port);
      sockaddr_len = sizeof(skaddr);
      if(bind(ccl->ipv4_listen_fd, (struct sockaddr *)&skaddr, sockaddr_len) < 0) {
        mtevL(noit_error, "statsd bind(IPv4) failed[%d]: %s\n", ccl->port, strerror(errno));
        close(ccl->ipv4_listen_fd);
        return -1;
      }
      if (listen(ccl->ipv4_listen_fd, 5) != 0) {
        mtevL(noit_error, "statsd listen(IPv4) failed[%d]: %s\n", ccl->port, strerror(errno));
        close(ccl->ipv4_listen_fd);
        return -1;
      }
  
      eventer_t newe = eventer_alloc_fd(statsd_tcp_listener, ccl, ccl->ipv4_listen_fd,
                                        EVENTER_READ | EVENTER_EXCEPTION);
      eventer_add(newe);
    }
    if(port > 0) ccl->ipv6_listen_fd = socket(AF_INET6, NE_SOCK_CLOEXEC|SOCK_STREAM, IPPROTO_TCP);
    if(ccl->ipv6_listen_fd < 0) {
      if(port > 0) {
        mtevL(noit_error, "statsd: IPv6 socket failed: %s\n",
              strerror(errno));
      }
    }
    else {
      if(eventer_set_fd_nonblocking(ccl->ipv6_listen_fd)) {
        close(ccl->ipv6_listen_fd);
        ccl->ipv6_listen_fd = -1;
        mtevL(noit_error,
              "statsd: could not set socket (IPv6) non-blocking: %s\n",
              strerror(errno));
      }
      else {
        socklen_t reuse = 1;
        if (setsockopt(ccl->ipv6_listen_fd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(reuse)) != 0) {
          mtevL(noit_error, "statsd listener(IPv6) failed(%s) to set REUSEADDR (doing our best)\n", strerror(errno));
        }
#ifdef SO_REUSEPORT
        reuse = 1;
        if (setsockopt(ccl->ipv6_listen_fd, SOL_SOCKET, SO_REUSEPORT, (void *)&reuse, sizeof(reuse)) != 0) {
          mtevL(noit_error, "statsd listener(IPv4) failed(%s) to set REUSEPORT (doing our best)\n", strerror(errno));
        }
#endif
        struct sockaddr_in6 skaddr6;
        struct in6_addr in6addr_any;
        sockaddr_len = sizeof(skaddr6);
        memset(&skaddr6, 0, sizeof(skaddr6));
        skaddr6.sin6_family = AF_INET6;
        memset(&in6addr_any, 0, sizeof(in6addr_any));
        skaddr6.sin6_addr = in6addr_any;
        skaddr6.sin6_port = htons(ccl->port);

        if(bind(ccl->ipv6_listen_fd, (struct sockaddr *)&skaddr6, sockaddr_len) < 0) {
          mtevL(noit_error, "statsd bind(IPv6) failed[%d]: %s\n",
                ccl->port, strerror(errno));
          close(ccl->ipv6_listen_fd);
          ccl->ipv6_listen_fd = -1;
        }

        else if (listen(ccl->ipv6_listen_fd, 5) != 0) {
          mtevL(noit_error, "statsd listen(IPv6) failed[%d]: %s\n", ccl->port, strerror(errno));
          close(ccl->ipv6_listen_fd);
          if (ccl->ipv4_listen_fd <= 0) return -1;
        }

      }
    }

    if(ccl->ipv6_listen_fd >= 0) {
      eventer_t newe = eventer_alloc_fd(statsd_tcp_listener, ccl, ccl->ipv6_listen_fd,
                                        EVENTER_READ | EVENTER_EXCEPTION);
      eventer_add(newe);
    }
  }
  INITIATE_CHECK(listener_submit, self, check, cause);
  return 0;
}

static int noit_statsd_tcp_onload(mtev_image_t *self) {
  if(!nlerr) nlerr = mtev_log_stream_find("error/statsd");
  if(!nldeb) nldeb = mtev_log_stream_find("debug/statsd");
  listener_onload();
  return 0;
}

static int noit_statsd_tcp_init(noit_module_t *self) 
{
  eventer_name_callback_ext("statsd_tcp/statsd_tcp_handler", statsd_tcp_handler,
                            listener_describe_callback, self);
  eventer_name_callback_ext("statsd_tcp/statsd_tcp_listener", statsd_tcp_listener,
                            listener_listen_describe_callback, self);

  return 0;
}

#include "statsd_tcp.xmlh"
noit_module_t statsd_tcp = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "statsd_tcp",
    .description = "statsd_tcp collection",
    .xml_description = statsd_tcp_xml_description,
    .onload = noit_statsd_tcp_onload
  },
  noit_listener_config,
  noit_statsd_tcp_init,
  noit_statsd_tcp_initiate_check,
  noit_listener_cleanup
};
