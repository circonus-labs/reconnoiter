/*
 * Copyright (c) 2012, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2013-2015, Circonus, Inc. All rights reserved.
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

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <math.h>
#include <ctype.h>
#include <arpa/inet.h>

#include <mtev_hash.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_mtev_bridge.h"

#define MAX_CHECKS 3

static mtev_log_stream_t nlerr = NULL;
static mtev_log_stream_t nldeb = NULL;
static const char *COUNTER_STRING = "c";

typedef struct _mod_config {
  mtev_hash_table *options;
  int packets_per_cycle;
  unsigned short port;
  uuid_t primary;
  int primary_active;
  noit_check_t *check;
  char *payload;
  int payload_len;
  int ipv4_fd;
  int ipv6_fd;
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
    gettimeofday(&now, NULL);
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
  u_int32_t one = 1, cnt = 1;
  char buff[256];
  statsd_closure_t *ccl;
  stats_t *inprogress;
  metric_t *m;

  if (sample == 0.0) return; /* would be a div-by-zero */
  if (check->closure == NULL) return;
  ccl = check->closure;

  inprogress = noit_check_get_stats_inprogress(check);
  /* First key counts */
  snprintf(buff, sizeof(buff), "%s`count", key);
  m = noit_stats_get_metric(check, inprogress, buff);
  if(!m) ccl->stats_count++;
  if(m && m->metric_type == METRIC_UINT32 && m->metric_value.I != NULL) {
    (*m->metric_value.I)++;
    cnt = *m->metric_value.I;
    check_stats_set_metric_hook_invoke(check, inprogress, m);
  }
  else
    noit_stats_set_metric(check, buff, METRIC_UINT32, &one);

  /* Next the actual data */
  if(type == 'c') {
    double v = diff * (1.0 / sample) / (check->period / 1000.0);
    snprintf(buff, sizeof(buff), "%s`rate", key);
    m = noit_stats_get_metric(check, inprogress, buff);
    if(m && m->metric_type == METRIC_DOUBLE && m->metric_value.n != NULL) {
      (*m->metric_value.n) += v;
      check_stats_set_metric_hook_invoke(check, inprogress, m);
    }
    else
      noit_stats_set_metric(check, buff, METRIC_DOUBLE, &v);
  }

  snprintf(buff, sizeof(buff), "%s`%s", key,
           (type == 'c') ? "counter" : (type == 'g') ? "gauge" : "timing");
  m = noit_stats_get_metric(check, inprogress, buff);
  if(m && m->metric_type == METRIC_DOUBLE && m->metric_value.n != NULL) {
    if(type == 'c') (*m->metric_value.n) += (diff * (1.0/sample));
    else {
      double new_avg = ((double)(cnt - 1) * (*m->metric_value.n) + diff) / (double)cnt;
      (*m->metric_value.n) = new_avg;
    }
    check_stats_set_metric_hook_invoke(check, inprogress, m);
  }
  else
    noit_stats_set_metric(check, buff, METRIC_DOUBLE, &diff);
}

static void
statsd_handle_payload(noit_check_t **checks, int nchecks,
                      char *payload, int len) {
  char *cp, *ecp, *endptr;
  cp = ecp = payload;
  endptr = payload + len - 1;
  while(ecp != NULL && ecp < endptr) {
    int i, idx = 0, last_space = 0;
    char key[256], *value;
    const char *type = NULL;
    ecp = memchr(ecp, '\n', len - (ecp - payload));
    if(ecp) *ecp++ = '\0';
    while(idx < sizeof(key) - 2 && *cp != '\0' && *cp != ':') {
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
              *cp == '`' || *cp == '.' || *cp == '_' || *cp == '-') {
        key[idx++] = *cp;
      }
      last_space = 0;
      cp++;
    }
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
          for(i=0;i<nchecks;i++)
            update_check(checks[i], key, *type, diff, sampleRate);
          break;
        default:
          break;
      }
      cp = value;
    }

    cp = ecp;
  }
}

static int
statsd_handler(eventer_t e, int mask, void *closure,
               struct timeval *now) {
  noit_module_t *self = (noit_module_t *)closure;
  int packets_per_cycle;
  statsd_mod_config_t *conf;
  noit_check_t *parent = NULL;

  conf = noit_module_get_userdata(self);
  if(conf->primary_active) parent = noit_poller_lookup(conf->primary);

  packets_per_cycle = MAX(conf->packets_per_cycle, 1);
  for( ; packets_per_cycle > 0; packets_per_cycle--) {
    noit_check_t *checks[MAX_CHECKS];
    int nchecks = 0;
    char ip[INET6_ADDRSTRLEN];
    union {
      struct sockaddr_in in;
      struct sockaddr_in6 in6;
    } addr;
    socklen_t addrlen = sizeof(addr);
    ssize_t len;
    uuid_t check_id;
    len = recvfrom(e->fd, conf->payload, conf->payload_len-1, 0,
                   (struct sockaddr *)&addr, &addrlen);
    if(len < 0) {
      if(errno != EAGAIN)
        mtevL(nlerr, "statsd: recvfrom() -> %s\n", strerror(errno));
      break;
    }
    switch(addr.in.sin_family) {
      case AF_INET:
        addrlen = sizeof(struct sockaddr_in);
        inet_ntop(AF_INET, &((struct sockaddr_in *)&addr)->sin_addr, ip, addrlen);
        break;
      case AF_INET6:
        addrlen = sizeof(struct sockaddr_in6);
        inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&addr)->sin6_addr, ip, addrlen);
        break;
      default:
        ip[0] = '\0';
    }
    conf->payload[len] = '\0';
    nchecks = 0;
    if(*ip)
      nchecks = noit_poller_lookup_by_ip_module(ip, self->hdr.name,
                                                checks, MAX_CHECKS-1);
    mtevL(nldeb, "statsd(%d bytes) from '%s' -> %d checks%s\n", (int)len,
          ip, (int)nchecks, parent ? " + a parent" : "");
    if(parent) checks[nchecks++] = parent;
    if(nchecks)
      statsd_handle_payload(checks, nchecks, conf->payload, len);
  }
  return EVENTER_READ | EVENTER_EXCEPTION;
}

static int noit_statsd_initiate_check(noit_module_t *self,
                                        noit_check_t *check,
                                        int once, noit_check_t *cause) {
  check->flags |= NP_PASSIVE_COLLECTION;
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
  if(!nlerr) nlerr = mtev_log_stream_find("error/statsd");
  if(!nldeb) nldeb = mtev_log_stream_find("debug/statsd");
  if(!nlerr) nlerr = noit_error;
  if(!nldeb) nldeb = noit_debug;
  return 0;
}

static int noit_statsd_init(noit_module_t *self) {
  unsigned short port = 8125;
  int packets_per_cycle = 100;
  int payload_len = 256*1024;
  struct sockaddr_in skaddr;
  int sockaddr_len;
  const char *config_val;
  statsd_mod_config_t *conf;
  conf = noit_module_get_userdata(self);

  eventer_name_callback("statsd/statsd_handler", statsd_handler);

  if(mtev_hash_retr_str(conf->options, "check", strlen("check"),
                        (const char **)&config_val)) {
    if(uuid_parse((char *)config_val, conf->primary) != 0)
      mtevL(noit_error, "statsd check isn't a UUID\n");
    conf->primary_active = 1;
    conf->check = NULL;
  }
  if(mtev_hash_retr_str(conf->options, "port", strlen("port"),
                        (const char **)&config_val)) {
    port = atoi(config_val);
  }
  conf->port = port;

  if(mtev_hash_retr_str(conf->options, "packets_per_cycle",
                        strlen("packets_per_cycle"),
                        (const char **)&config_val)) {
    packets_per_cycle = atoi(config_val);
  }
  conf->packets_per_cycle = packets_per_cycle;

  conf->payload_len = payload_len;
  conf->payload = malloc(conf->payload_len);
  if(!conf->payload) {
    mtevL(noit_error, "statsd malloc() failed\n");
    return -1;
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
    newe = eventer_alloc();
    newe->fd = conf->ipv4_fd;
    newe->mask = EVENTER_READ | EVENTER_EXCEPTION;
    newe->callback = statsd_handler;
    newe->closure = self;
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
    newe = eventer_alloc();
    newe->fd = conf->ipv6_fd;
    newe->mask = EVENTER_READ | EVENTER_EXCEPTION;
    newe->callback = statsd_handler;
    newe->closure = self;
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
