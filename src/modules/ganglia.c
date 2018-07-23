/*
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
 *     * Neither the name Circonus, Inc. nor the names
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
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <mtev_defines.h>

#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

#include <mtev_str.h>
#include <mtev_rest.h>
#include <mtev_hash.h>
#include <mtev_b64.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_mtev_bridge.h"

#define GANGLIA_DEFAULT_MCAST_ADDR "239.2.11.71"
#define GANGLIA_DEFAULT_MCAST_PORT 8649

typedef struct _mod_config {
  mtev_hash_table *options;
  mtev_boolean asynch_metrics;
  int ipv4_fd;
  int ipv6_fd;
} ganglia_mod_config_t;

typedef struct ganglia_closure_s {
  int stats_count;
  int ntfy_count;
} ganglia_closure_t;

/* based on formats from \\ganglia/monitor-core/lib/gm_protocol.x */
/* taken from version 3.2, 2009-12-13 15:38:58 -0500 */
enum Ganglia_msg_formats {
   gmetadata_full = 128, /* this one refers to metadataref */
   gmetric_ushort,
   gmetric_short,
   gmetric_int,
   gmetric_uint,
   gmetric_string,
   gmetric_float,
   gmetric_double,
   gmetadata_request
};

struct ganglia_dgram {
  noit_module_t *self;
  void *payload;
  int len;
  int type;
  char *name;
};

union ifd { 
  int i;
  float f;
  double d;
};

static mtev_boolean
noit_collects_check_asynch(noit_module_t *self,
                           noit_check_t *check) {
  const char *config_val;
  ganglia_mod_config_t *conf = noit_module_get_userdata(self);
  mtev_boolean is_asynch = conf->asynch_metrics;
  if(mtev_hash_retr_str(check->config,
                        "asynch_metrics", strlen("asynch_metrics"),
                        (const char **)&config_val)) {
    if(!strcasecmp(config_val, "false") || !strcasecmp(config_val, "off"))
      is_asynch = mtev_false;
    else if(!strcasecmp(config_val, "true") || !strcasecmp(config_val, "on"))
      is_asynch = mtev_true;
  }

  if(is_asynch) check->flags |= NP_SUPPRESS_METRICS;
  else check->flags &= ~NP_SUPPRESS_METRICS;
  return is_asynch;
}

static void clear_closure(noit_check_t *check, ganglia_closure_t *gcl) {
  gcl->stats_count = 0;
  gcl->ntfy_count = 0;
}

static int
ganglia_process_dgram(noit_check_t *check, void *closure) {
  struct ganglia_dgram *pkt = closure;
  mtev_boolean immediate;
  ganglia_closure_t *gcl;

  if (!check || strcmp(check->module, "ganglia")) return 0;

  immediate = noit_collects_check_asynch(pkt->self, check);
  if(!check->closure) check->closure = calloc(1, sizeof(ganglia_closure_t));
  gcl = check->closure;

  switch(pkt->type) {
    case gmetadata_full:
      /* nothing of value to get from the metadata */
      break;
    case gmetadata_request:
      break;
    case gmetric_short:
    case gmetric_int:{
      int *val = pkt->payload;
      *val = ntohl(*val);
      noit_stats_set_metric(check, pkt->name, METRIC_INT32, val);
      if(immediate) noit_stats_log_immediate_metric(check, pkt->name, METRIC_INT32, val);
      break;
                     }
    case gmetric_ushort:
    case gmetric_uint:{
      uint32_t *val = pkt->payload;
      *val = ntohl(*val);
      noit_stats_set_metric(check, pkt->name, METRIC_UINT32, val);
      if(immediate) noit_stats_log_immediate_metric(check, pkt->name, METRIC_UINT32, val);
      break;
                      }
    case gmetric_string:{
      uint32_t *len = pkt->payload;
      *len = ntohl(*len);
      pkt->payload += 4;
      char *str = pkt->payload;
      noit_stats_set_metric(check, pkt->name, METRIC_STRING, str);
      if(immediate) noit_stats_log_immediate_metric(check, pkt->name, METRIC_STRING, str);
      break;
                        }
    case gmetric_float:{
      double val = 0;
      union ifd *ptr = pkt->payload;
      ptr->i = ntohl(ptr->i);
      val = (double) ptr->f;
      noit_stats_set_metric(check, pkt->name, METRIC_DOUBLE, &val);
      if(immediate) noit_stats_log_immediate_metric(check, pkt->name, METRIC_DOUBLE, &val);
      break;    
                       }
    case gmetric_double:{
      double val = 0;
      union ifd *ptr = pkt->payload;
      ptr->i = ntohl(ptr->i);
      (ptr+4)->i = ntohl((ptr+4)->i);
      val = ptr->d;
      noit_stats_set_metric(check, pkt->name, METRIC_DOUBLE, &val);
      if(immediate) noit_stats_log_immediate_metric(check, pkt->name, METRIC_DOUBLE, &val);
      break;
                        }
    default:
      mtevL(noit_error, "ganglia: unknown packet type received\n");
      return 0;
  }

  gcl->stats_count++;

  return 1;
}

static int noit_ganglia_handler(eventer_t e, int mask, void *closure,
                             struct timeval *now) {
  char packet[1500]; /* 1500 is correct; see __GANGLIA_MTU */
  int packet_len = sizeof(packet);
  noit_module_t *self = (noit_module_t *)closure;

  while(1) {
    struct ganglia_dgram pkt;
    int inlen;
    void *payload = &packet;
    char *host, *name;
    int *len;
    uint32_t *type;

    inlen = recvfrom(eventer_get_fd(e), packet, packet_len, 0, NULL, 0);

    if(inlen < 0) {
      if(errno == EAGAIN) break; /* out of data to read, hand it back to eventer
                                    and wait to be scheduled again */
      mtevLT(noit_error, now, "ganglia: recvfrom: %s\n", strerror(errno));
      break;
    }

    type = payload;
    *type = ntohl(*type);
    payload += 4;

    len = payload;
    *len = ntohl(*len);
    if(!*len) {
      mtevL(noit_error, "ganglia: empty host\n");
      return -1;
    }
    payload += 4;
    host = payload;
    payload += (*len + 3) & ~0x03;

    len = payload;
    *len = ntohl(*len);
    if(!*len) {
      mtevL(noit_error, "ganglia: empty name\n");
      return -1;
    }
    payload += 4;
    name = payload;
    payload += (*len + 3) & ~0x03;
    *len = 0; /* add null char to end of host string */

    /* skip the spoof boolean */
    payload += 4;

    /* skip the format string */
    len = payload;
    *len = ntohl(*len);
    payload += 4;
    payload += (*len + 3) & ~0x03;
    *len = 0; /* add null char to end of name string */

    pkt.self = self;
    pkt.payload = payload;
    pkt.len = inlen;
    pkt.type = *type;
    pkt.name = name;

    if(!noit_poller_target_do(host, ganglia_process_dgram, &pkt))
      mtevL(noit_error, "ganglia: no checks from host: %s\n", host);
  }
  return EVENTER_READ | EVENTER_EXCEPTION;
}

static int
ganglia_submit(noit_module_t *self, noit_check_t *check,
                         noit_check_t *cause) {
  /* almost entirely pulled from collectd.c:collectd_submit_internal() */
  ganglia_closure_t *gcl;
  struct timeval now, duration, age, *last;
  mtev_boolean immediate;
  /* We are passive, so we don't do anything for transient checks */
  if(check->flags & NP_TRANSIENT) return 0;

  mtev_gettimeofday(&now, NULL);

  /* If we're immediately logging things and we've done so within the
   * check's period... we've no reason to passively log now.
   */
  immediate = noit_collects_check_asynch(self, check);
  last = noit_check_stats_whence(noit_check_get_stats_current(check), NULL);
  sub_timeval(now, *last, &age);
  if(immediate && (age.tv_sec * 1000 + age.tv_usec / 1000) < check->period)
    return 0;

  if(!check->closure) {
    gcl = check->closure = (void *)calloc(1, sizeof(ganglia_closure_t)); 
    memset(gcl, 0, sizeof(ganglia_closure_t));
    noit_stats_set_whence(check, &now);
  } else {
    /*  Don't count the first run */
    char human_buffer[256];
    gcl = (ganglia_closure_t*)check->closure; 
    noit_stats_set_whence(check, &now);
    sub_timeval(now, check->last_fire_time, &duration);
    noit_stats_set_duration(check, duration.tv_sec);

    snprintf(human_buffer, sizeof(human_buffer),
             "dur=%ld,run=%d,stats=%d,ntfy=%d", duration.tv_sec,
             check->generation, gcl->stats_count, gcl->ntfy_count);
    mtevL(noit_debug, "ganglia(%s) [%s]\n", check->target, human_buffer);

    noit_stats_set_available(check, (gcl->ntfy_count > 0 || gcl->stats_count > 0) ? 
        NP_AVAILABLE : NP_UNAVAILABLE);
    noit_stats_set_state(check, (gcl->ntfy_count > 0 || gcl->stats_count > 0) ? 
        NP_GOOD : NP_BAD);
    noit_stats_set_status(check, human_buffer);
    if(check->last_fire_time.tv_sec)
      noit_check_passive_set_stats(check);

    memcpy(&check->last_fire_time, &now, sizeof(duration));
  }
  clear_closure(check, gcl);
  return 0;
}

static int noit_ganglia_initiate_check(noit_module_t *self,
                                        noit_check_t *check,
                                        int once, noit_check_t *cause) {
  check->flags |= NP_PASSIVE_COLLECTION;
  INITIATE_CHECK(ganglia_submit, self, check, cause);
  return 0;
}

static int noit_ganglia_config(noit_module_t *self, mtev_hash_table *options) {
  ganglia_mod_config_t *conf = noit_module_get_userdata(self);
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

static int noit_ganglia_onload(mtev_image_t *self) {
  eventer_name_callback("noit_ganglia/handler", noit_ganglia_handler);
  return 0;
}

static int noit_ganglia_init(noit_module_t *self) {
  const char *config_val;
  ganglia_mod_config_t *conf;
  conf = noit_module_get_userdata(self);
  struct ip_mreq mreq;
  struct ipv6_mreq mreqv6;
  struct sockaddr_in skaddr;
  struct sockaddr_in6 skaddr6;
  const char *multiaddr, *multiaddr6;
  int portint=0;
  unsigned short port;

  conf->asynch_metrics = mtev_true;
  if(mtev_hash_retr_str(conf->options,
                        "asynch_metrics", strlen("asynch_metrics"),
                        (const char **)&config_val)) {
    if(!strcasecmp(config_val, "false") || !strcasecmp(config_val, "off"))
      conf->asynch_metrics = mtev_false;
  }

  /* Default Collectd port */
  portint = GANGLIA_DEFAULT_MCAST_PORT;
  if(mtev_hash_retr_str(conf->options,
                         "port", strlen("port"),
                         (const char**)&config_val))
    portint = atoi(config_val);
  port = (unsigned short) portint;

  if(!mtev_hash_retr_str(conf->options,
                         "multiaddr", strlen("multiaddr"),
                         (const char**)&multiaddr))
    multiaddr = GANGLIA_DEFAULT_MCAST_ADDR;


  conf->ipv4_fd = conf->ipv6_fd = -1;

  /* ipv4 socket and binding */
  conf->ipv4_fd = socket(PF_INET, NE_SOCK_CLOEXEC|SOCK_DGRAM, IPPROTO_UDP);
  if(conf->ipv4_fd < 0) {
    mtevL(noit_error, "ganglia: ipv4 socket failed: %s\n", strerror(errno));
    return -1;
  }

  if(eventer_set_fd_nonblocking(conf->ipv4_fd)) {
    close(conf->ipv4_fd);
    mtevL(noit_error, "ganglia: could not set ipv4 socket non-blocking: %s\n", strerror(errno));
    return -1;
  }

  /* ipv4 binding */
  memset(&skaddr, 0, sizeof(skaddr));
  skaddr.sin_family = AF_INET;
  skaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  skaddr.sin_port = htons(port);

  if(bind(conf->ipv4_fd, (struct sockaddr *)&skaddr, sizeof(skaddr))) {
    close(conf->ipv4_fd);
    mtevL(noit_error, "ganglia: ipv4 binding failed: %s\n", strerror(errno));
    return -1;
  }

  /* join ipv4 multicast */
  memset(&mreq, 0, sizeof(mreq));

  (void)inet_pton(AF_INET, multiaddr, &mreq.imr_multiaddr.s_addr);

  mreq.imr_interface.s_addr = htonl(INADDR_ANY);

  if(setsockopt(conf->ipv4_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq))) {
    close(conf->ipv4_fd);
    mtevL(noit_error, "ganglia: ipv4 multicast join failed: %s\n", strerror(errno));
    return -1;
  }

  eventer_t newe;
  newe = eventer_alloc_fd(noit_ganglia_handler, self, conf->ipv4_fd,
                          EVENTER_READ | EVENTER_EXCEPTION);
  eventer_add(newe);
  mtevL(noit_debug, "ganglia: Added ipv4 handler!\n");

  portint = GANGLIA_DEFAULT_MCAST_PORT;
  if(mtev_hash_retr_str(conf->options,
                         "port6", strlen("port6"),
                         (const char**)&config_val))
    portint = atoi(config_val);
  port = (unsigned short) portint;

  if(!mtev_hash_retr_str(conf->options,
                         "multiaddr6", strlen("multiaddr6"),
                         (const char**)&multiaddr6))
    /* ganglia doesn't have a default ipv6 multicast */
    multiaddr6 = NULL;

  /* ipv6 socket, nonblocking */
  if(multiaddr6) { 
    conf->ipv6_fd = socket(AF_INET6, NE_SOCK_CLOEXEC|SOCK_DGRAM, IPPROTO_UDP);
    if(conf->ipv6_fd < 0) {
      mtevL(noit_error, "ganglia: IPv6 socket creation failed: %s\n", strerror(errno));
    }
    else if(eventer_set_fd_nonblocking(conf->ipv6_fd)) {
      mtevL(noit_error, "ganglia: could not set socket non-blocking: %s\n", strerror(errno));
      close(conf->ipv6_fd);
      conf->ipv6_fd =  -1;
    }
  }

  /* ipv6 binding */
  if(conf->ipv6_fd > 0) {
    memset(&skaddr6, 0, sizeof(skaddr6));
    skaddr6.sin6_family = AF_INET6;
    skaddr6.sin6_addr = in6addr_any;
    skaddr6.sin6_port = htons(port);
    if(bind(conf->ipv6_fd, (struct sockaddr *)&skaddr6, sizeof(skaddr6))) {
      mtevL(noit_error, "ganglia: ipv6 binding failed: %s\n", strerror(errno));
      close(conf->ipv6_fd);
      conf->ipv6_fd = -1;
    }
  }

  /* join ipv6 multicast */
  if(conf->ipv6_fd > 0) {
    memset(&mreqv6, 0, sizeof(mreqv6));

    (void)inet_pton(AF_INET6, multiaddr6, &mreqv6.ipv6mr_multiaddr.s6_addr);

    mreqv6.ipv6mr_interface = 0;

    if(setsockopt(conf->ipv6_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreqv6, sizeof(mreqv6))) {
      mtevL(noit_error, "ganglia: ipv6 multicast join failed: %s\n", strerror(errno));
      close(conf->ipv6_fd);
      conf->ipv6_fd = -1;
    }
  }

  if(conf->ipv6_fd > 0) {
    eventer_t newe;
    newe = eventer_alloc_fd(noit_ganglia_handler, self, conf->ipv6_fd,
                            EVENTER_READ | EVENTER_EXCEPTION);
    eventer_add(newe);
    mtevL(noit_debug, "ganglia: Added ipv6 handler!\n");
  }

  noit_module_set_userdata(self, conf);

  return 0;
}

#include "ganglia.xmlh"
noit_module_t ganglia = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "ganglia",
    .description = "ganglia collection",
    .xml_description = ganglia_xml_description,
    .onload = noit_ganglia_onload
  },
  noit_ganglia_config,
  noit_ganglia_init,
  noit_ganglia_initiate_check,
  NULL
};
