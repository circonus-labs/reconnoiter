/*
 * Copyright (c) 2012, OmniTI Computer Consulting, Inc.
 * All rights reserved.
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

#include "noit_defines.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <math.h>
#include <ctype.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"

#define MAX_DEPTH 32

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;

typedef struct _mod_config {
  noit_hash_table *options;
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
  stats_t current;
  int stats_count;
} statsd_closure_t;

static void
clear_closure(noit_check_t *check, statsd_closure_t *ccl) {
  ccl->stats_count = 0;
  noit_check_stats_clear(check, &ccl->current);
}

static int
statsd_submit(noit_module_t *self, noit_check_t *check,
              noit_check_t *cause) {
  statsd_closure_t *ccl;
  struct timeval duration;
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
    memset(&ccl->current, 0, sizeof(ccl->current));
  } else {
    // Don't count the first run
    char human_buffer[256];
    ccl = (statsd_closure_t*)check->closure;
    gettimeofday(&ccl->current.whence, NULL);
    sub_timeval(ccl->current.whence, check->last_fire_time, &duration);
    ccl->current.duration = duration.tv_sec * 1000 + duration.tv_usec / 1000;

    snprintf(human_buffer, sizeof(human_buffer),
             "dur=%d,run=%d,stats=%d", ccl->current.duration,
             check->generation, ccl->stats_count);
    noitL(nldeb, "statsd(%s) [%s]\n", check->target, human_buffer);

    // Not sure what to do here
    ccl->current.available = (ccl->stats_count > 0) ?
        NP_AVAILABLE : NP_UNAVAILABLE;
    ccl->current.state = (ccl->stats_count > 0) ?
        NP_GOOD : NP_BAD;
    ccl->current.status = human_buffer;
    if(check->last_fire_time.tv_sec)
      noit_check_passive_set_stats(check, &ccl->current);

    memcpy(&check->last_fire_time, &ccl->current.whence, sizeof(duration));
  }
  clear_closure(check, ccl);
  return 0;
}

static int
push_payload_at_check(noit_check_t *check, int len, char *payload) {
  statsd_closure_t *ccl;
  char key[256];

  if (check->closure == NULL) return 0;
  ccl = check->closure;

  /* do it here */
  return ccl->stats_count;
}

static int
statsd_handler(eventer_t e, int mask, void *closure,
               struct timeval *now) {
  noit_module_t *self = (noit_module_t *)closure;
  int packets_per_cycle;
  statsd_mod_config_t *conf;

  conf = noit_module_get_userdata(self);
  packets_per_cycle = MAX(conf->packets_per_cycle, 1);
  for( ; packets_per_cycle > 0; packets_per_cycle--) {
    union {
      struct sockaddr_in in;
      struct sockaddr_in6 in6;
    } addr;
    socklen_t addrlen = sizeof(addr);
    ssize_t len;
    noit_check_t *check;
    uuid_t check_id;
    len = recvfrom(e->fd, conf->payload, conf->payload_len, 0,
                   (struct sockaddr *)&addr, &addrlen);
    if(len < 0 && errno == EAGAIN) return -1;
  }
  return 0;
}

static int noit_statsd_initiate_check(noit_module_t *self,
                                        noit_check_t *check,
                                        int once, noit_check_t *cause) {
  if (check->closure == NULL) {
    statsd_closure_t *ccl;
    ccl = check->closure = (void *)calloc(1, sizeof(statsd_closure_t));
    ccl->self = self;
  }
  INITIATE_CHECK(statsd_submit, self, check, cause);
  return 0;
}

static int noit_statsd_config(noit_module_t *self, noit_hash_table *options) {
  statsd_mod_config_t *conf;
  conf = noit_module_get_userdata(self);
  if(conf) {
    if(conf->options) {
      noit_hash_destroy(conf->options, free, free);
      free(conf->options);
    }
  }
  else
    conf = calloc(1, sizeof(*conf));
  conf->options = options;
  noit_module_set_userdata(self, conf);
  return 1;
}

static int noit_statsd_onload(noit_image_t *self) {
  if(!nlerr) nlerr = noit_log_stream_find("error/statsd");
  if(!nldeb) nldeb = noit_log_stream_find("debug/statsd");
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

  if(noit_hash_retr_str(conf->options, "check", strlen("check"),
                        (const char **)&config_val)) {
    if(uuid_parse(config_val, conf->primary) != 0)
      noitL(noit_error, "statsd check isn't a UUID\n");
    conf->primary_active = 1;
    conf->check = NULL;
  }
  if(noit_hash_retr_str(conf->options, "port", strlen("port"),
                        (const char **)&config_val)) {
    port = atoi(config_val);
  }
  conf->port = port;

  if(noit_hash_retr_str(conf->options, "packets_per_cycle",
                        strlen("packets_per_cycle"),
                        (const char **)&config_val)) {
    packets_per_cycle = atoi(config_val);
  }
  conf->packets_per_cycle = packets_per_cycle;

  conf->payload_len = payload_len;
  conf->payload = malloc(conf->payload_len);
  if(!conf->payload) {
    noitL(noit_error, "statsd malloc() failed\n");
    return -1;
  }

  conf->ipv4_fd = socket(PF_INET, SOCK_DGRAM, 0);
  if(conf->ipv4_fd < 0) {
    noitL(noit_error, "statsd: socket failed: %s\n", strerror(errno));
    return -1;
  }
  else {
    if(eventer_set_fd_nonblocking(conf->ipv4_fd)) {
      close(conf->ipv4_fd);
      conf->ipv4_fd = -1;
      noitL(noit_error,
            "collectd: could not set socket non-blocking: %s\n",
            strerror(errno));
      return -1;
    }
  }
  skaddr.sin_family = AF_INET;
  skaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  skaddr.sin_port = htons(conf->port);
  sockaddr_len = sizeof(skaddr);
  if(bind(conf->ipv4_fd, (struct sockaddr *)&skaddr, sockaddr_len) < 0) {
    noitL(noit_error, "bind failed[%d]: %s\n", conf->port, strerror(errno));
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

  conf->ipv6_fd = socket(AF_INET6, SOCK_DGRAM, 0);
  if(conf->ipv6_fd < 0) {
    noitL(noit_error, "statsd: IPv6 socket failed: %s\n",
          strerror(errno));
  }
  else {
    if(eventer_set_fd_nonblocking(conf->ipv6_fd)) {
      close(conf->ipv6_fd);
      conf->ipv6_fd = -1;
      noitL(noit_error,
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
        noitL(noit_error, "bind(IPv6) failed[%d]: %s\n",
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
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "statsd",
    "statsd collection",
    statsd_xml_description,
    noit_statsd_onload
  },
  noit_statsd_config,
  noit_statsd_init,
  noit_statsd_initiate_check,
  NULL
};
