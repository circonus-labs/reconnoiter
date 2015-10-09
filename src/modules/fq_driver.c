/*
 * Copyright (c) 2011, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2013, Circonus, Inc. All rights reserved.
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
 *     * Neither the name Circonus, Inc. nor the names of its contributors
 *       may be used to endorse or promote products derived from this
 *       software without specific prior written permission.
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

#ifndef _REENTRANT
#define _REENTRANT
#endif

#include <mtev_defines.h>

#include <assert.h>
#include <fq.h>

#include <mtev_dso.h>
#include <eventer/eventer.h>
#include <mtev_log.h>
#include <mtev_conf.h>

#include "stratcon_iep.h"
#include "fq_driver.xmlh"

static mtev_log_stream_t nlerr = NULL;

typedef struct {
  mtev_atomic64_t publications;
  mtev_atomic64_t client_tx_drop;
  mtev_atomic64_t error_messages;
  mtev_atomic64_t no_exchange;
  mtev_atomic64_t no_route;
  mtev_atomic64_t routed;
  mtev_atomic64_t dropped;
  mtev_atomic64_t msgs_in;
  mtev_atomic64_t msgs_out;
} fq_stats_t;

#define MAX_HOSTS 10
struct fq_driver {
  fq_client client[MAX_HOSTS];
  fq_stats_t stats[MAX_HOSTS];
  char hostname[MAX_HOSTS][128];
  int ports[MAX_HOSTS];
  char exchange[128];
  char routingkey[128];
  char username[128];
  char password[128];
  uint64_t allocation_failures;
  uint64_t msg_cnt;
  int nhosts;
  int heartbeat;
  int backlog;
  int port;
  int round_robin;
  int round_robin_target;
  int down_host[MAX_HOSTS];
  time_t last_error[MAX_HOSTS];
};

struct fq_driver global_fq_ctx = { 0 };

#define BUMPSTAT(i,a) mtev_atomic_inc64(&global_fq_ctx.stats[i].a)

/* This is very specific to an internal implementation somewhere...
 * and thus unlikely to be useful unless people name their checks:
 * c_<accountid>_<checknumber>::<rest of name>
 * This code should likley be made generic, perhaps with named
 * pcre captures.  However, I'm worried about performance.
 * For now, leave it and understand it is limited usefulness.
 */
static int extract_uuid_from_jlog(const char *payload, size_t payloadlen,
                                  int *account_id, int *check_id, char *dst) {
  int i = 0;
  const char *atab = payload, *u = NULL;

  if(account_id) *account_id = 0;
  if(check_id) *check_id = 0;

#define advance_past_tab do { \
  atab = memchr(atab, '\t', payloadlen - (atab - payload)); \
  if(!atab) return 0; \
  atab++; \
} while(0)

  /* Tab -> M|S|C */
  advance_past_tab;
  /* Tab -> noit IP */
  advance_past_tab;
  /* Tab -> timestamp */
  advance_past_tab;
  /* Tab -> uuid */
  u = atab;
  advance_past_tab;
  /* Tab -> metric_name */
  atab--;
  if(atab - u < UUID_STR_LEN) return 0;
  if(atab - u > UUID_STR_LEN) {
    const char *f;
    f = memchr(u, '`', payloadlen - (u - payload));
    if(f) {
      f = memchr(f+1, '`', payloadlen - (f + 1 - payload));
      if(f) {
        f++;
        if(memcmp(f, "c_", 2) == 0) {
          f += 2;
          if(account_id) *account_id = atoi(f);
          f = memchr(f, '_', payloadlen - (f - payload));
          if(f) {
            f++;
            if(check_id) *check_id = atoi(f);
          }
        }
      }
    }
  }
  u = atab - UUID_STR_LEN;
  while(i<32 && u < atab) {
    if((*u >= 'a' && *u <= 'f') ||
       (*u >= '0' && *u <= '9')) {
      dst[i*2] = '.';
      dst[i*2 + 1] = *u;
      i++;
    }
    else if(*u != '-') return 0;
    u++;
  }
  dst[i*2] = '\0';
  return 1;
}

static void fq_logger(fq_client c, const char *err) {
  int i;
  mtevL(nlerr, "fq: %s\n", err);
  for(i=0;i<global_fq_ctx.nhosts;i++) {
    if(c == global_fq_ctx.client[i]) {
      BUMPSTAT(i, error_messages);
      /* We only care about this if we're using round robin processing */
      if (global_fq_ctx.round_robin) {
        if (!strncmp(err, "socket: Connection refused", strlen("socket: Connection refused"))) {
          global_fq_ctx.down_host[i] = true;
          global_fq_ctx.last_error[i] = time(NULL);
        }
      }
      break;
    }
  }
}

static iep_thread_driver_t *noit_fq_allocate(mtev_conf_section_t conf) {
  char *hostname, *cp, *brk, *round_robin;
  int i;

#define GETCONFSTR(w) mtev_conf_get_stringbuf(conf, #w, global_fq_ctx.w, sizeof(global_fq_ctx.w))
  memset(&global_fq_ctx.down_host, 0, sizeof(global_fq_ctx.down_host));
  memset(&global_fq_ctx.last_error, 0, sizeof(global_fq_ctx.last_error));
  snprintf(global_fq_ctx.exchange, sizeof(global_fq_ctx.exchange), "%s",
           "noit.firehose");
  GETCONFSTR(exchange);
  if(!GETCONFSTR(routingkey))
    snprintf(global_fq_ctx.routingkey, sizeof(global_fq_ctx.routingkey), "%s", "check");
  snprintf(global_fq_ctx.username, sizeof(global_fq_ctx.username), "%s", "guest");
  GETCONFSTR(username);
  snprintf(global_fq_ctx.password, sizeof(global_fq_ctx.password), "%s", "guest");
  GETCONFSTR(password);
  if(!mtev_conf_get_int(conf, "heartbeat", &global_fq_ctx.heartbeat))
    global_fq_ctx.heartbeat = 2000;
  if(!mtev_conf_get_int(conf, "backlog", &global_fq_ctx.backlog))
    global_fq_ctx.backlog = 10000;
  if(!mtev_conf_get_int(conf, "port", &global_fq_ctx.port))
    global_fq_ctx.port = 8765;
  (void)mtev_conf_get_string(conf, "round_robin", &round_robin);
  if (!round_robin) {
    global_fq_ctx.round_robin = 0;
  }
  else {
    if (!(strncmp(round_robin, "true", 4))) {
      global_fq_ctx.round_robin = 1;
      global_fq_ctx.round_robin_target = 0;
    }
    else {
      global_fq_ctx.round_robin = 0;
    }
  }
  (void)mtev_conf_get_string(conf, "hostname", &hostname);
  if(!hostname) hostname = strdup("127.0.0.1");
  for(cp = hostname; cp; cp = strchr(cp+1, ',')) global_fq_ctx.nhosts++;
  if(global_fq_ctx.nhosts > MAX_HOSTS) global_fq_ctx.nhosts = MAX_HOSTS;
  for(i = 0, cp = strtok_r(hostname, ",", &brk);
      cp; cp = strtok_r(NULL, ",", &brk), i++) {
    char *pcp;
    fq_client *c = &global_fq_ctx.client[i];

    global_fq_ctx.ports[i] = global_fq_ctx.port;
    strlcpy(global_fq_ctx.hostname[i], cp, sizeof(global_fq_ctx.hostname[i]));
    pcp = strchr(global_fq_ctx.hostname[i], ':');
    if(pcp) {
      *pcp++ = '\0';
      global_fq_ctx.ports[i] = atoi(pcp);
    }
    fq_client_init(c, 0, fq_logger);
    fq_client_creds(*c, global_fq_ctx.hostname[i], global_fq_ctx.ports[i],
                    global_fq_ctx.username, global_fq_ctx.password);
    fq_client_heartbeat(*c, global_fq_ctx.heartbeat);
    fq_client_set_nonblock(*c, 1);
    fq_client_set_backlog(*c, global_fq_ctx.backlog, 0);
    fq_client_connect(*c);
  }
  free(hostname);

  return (iep_thread_driver_t *)&global_fq_ctx;
}

/* connect happens once in allocate, not in connect...
 * so we don't disconnect. */
static int noit_fq_connect(iep_thread_driver_t *dr) {
  return 1;
}
static int noit_fq_disconnect(iep_thread_driver_t *d) {
  return 0;
}

static int
noit_fq_submit(iep_thread_driver_t *dr,
               const char *payload, size_t payloadlen) {
  int i;
  struct fq_driver *driver = (struct fq_driver *)dr;
  const char *routingkey = driver->routingkey;
  fq_msg *msg;

  if(*payload == 'M' ||
     *payload == 'S' ||
     *payload == 'C' ||
     (*payload == 'H' && payload[1] == '1') ||
     (*payload == 'F' && payload[1] == '1') ||
     (*payload == 'B' && (payload[1] == '1' || payload[1] == '2'))) {
    char uuid_str[32 * 2 + 1];
    int account_id, check_id;
    if(extract_uuid_from_jlog(payload, payloadlen,
                              &account_id, &check_id, uuid_str)) {
      if(*routingkey) {
        char *replace;
        int newlen = strlen(driver->routingkey) + 1 + sizeof(uuid_str) + 2 * 32;
        replace = alloca(newlen);
        snprintf(replace, newlen, "%s.%x.%x.%d.%d%s", driver->routingkey,
                 account_id%16, (account_id/16)%16, account_id,
                 check_id, uuid_str);
        routingkey = replace;
      }
    }
  }

  /* Setup our message */
  msg = fq_msg_alloc(payload, payloadlen);
  if(msg == NULL) {
    driver->allocation_failures++;
    return -1;
  }
  driver->msg_cnt++;
  fq_msg_exchange(msg, driver->exchange, strlen(driver->exchange));
  mtevL(mtev_debug, "route[%s] -> %s\n", driver->exchange, routingkey);
  fq_msg_route(msg, routingkey, strlen(routingkey));
  fq_msg_id(msg, NULL);

  if (global_fq_ctx.round_robin) {
    int checked = 0, good = 0;
    time_t cur_time;
    while (1) {
      if (!global_fq_ctx.down_host[global_fq_ctx.round_robin_target]) {
        good = 1;
        break;
      }
      cur_time = time(NULL);
      if (cur_time - global_fq_ctx.last_error[global_fq_ctx.round_robin_target] >= 10) {
        global_fq_ctx.down_host[global_fq_ctx.round_robin_target] = false;
        good = 1;
        break;
      } 
      global_fq_ctx.round_robin_target = (global_fq_ctx.round_robin_target+1) % driver->nhosts;
      checked++;
      if (checked == driver->nhosts) {
        /* This means everybody is down.... just try to send to whatever fq
           we're pointing at */
        break;
      }
    }
    if (good) {
      if(fq_client_publish(driver->client[global_fq_ctx.round_robin_target], msg) == 1) {
        BUMPSTAT(global_fq_ctx.round_robin_target, publications);
      }
      else {
        BUMPSTAT(global_fq_ctx.round_robin_target, client_tx_drop);
      }
    }
    /* Go ahead and try to publish to the hosts that are down, just in
       case they've come back up. This should help minimize lost messages */
    for (i=0; i<driver->nhosts; i++) {
      if (global_fq_ctx.down_host[i]) {
        if(fq_client_publish(driver->client[i], msg) == 1) {
          BUMPSTAT(i, publications);
        }
        else {
          BUMPSTAT(i, client_tx_drop);
        }
      }
    }
    global_fq_ctx.round_robin_target = (global_fq_ctx.round_robin_target+1) % driver->nhosts;
  }
  else {
    for(i=0; i<driver->nhosts; i++) {
      if(fq_client_publish(driver->client[i], msg) == 1) {
        BUMPSTAT(i, publications);
      }
      else {
        BUMPSTAT(i, client_tx_drop);
      }
    }
  }
  fq_msg_deref(msg);
  return 0;
}

static void noit_fq_deallocate(iep_thread_driver_t *d) {
  /* No allocations are actually done in allocate...
   * We just use on single global context, so nothing to free here.
   */
}

mq_driver_t mq_driver_fq = {
  noit_fq_allocate,
  noit_fq_connect,
  noit_fq_submit,
  noit_fq_disconnect,
  noit_fq_deallocate
};

static int noit_fq_driver_config(mtev_dso_generic_t *self, mtev_hash_table *o) {
  return 0;
}
static int noit_fq_driver_onload(mtev_image_t *self) {
  return 0;
}

static int
noit_console_show_fq(mtev_console_closure_t ncct,
                     int argc, char **argv,
                     mtev_console_state_t *dstate,
                     void *closure) {
  int i;
  nc_printf(ncct, " == FQ ==\n");
  nc_printf(ncct, " Allocation Failures:   %llu\n", global_fq_ctx.allocation_failures);
  nc_printf(ncct, " Messages:              %llu\n", global_fq_ctx.msg_cnt);
  for(i=0; i<global_fq_ctx.nhosts; i++) {
    fq_stats_t *s = &global_fq_ctx.stats[i];
    nc_printf(ncct, " === %s:%d ===\n", global_fq_ctx.hostname[i],
              global_fq_ctx.ports[i]);
    nc_printf(ncct, "  publications:         %llu\n", s->publications);
    nc_printf(ncct, "  client_tx_drop:       %llu\n", s->client_tx_drop);
    nc_printf(ncct, "  error_messages:       %llu\n", s->error_messages);
    nc_printf(ncct, "  no_exchange:          %llu\n", s->no_exchange);
    nc_printf(ncct, "  no_route:             %llu\n", s->no_route);
    nc_printf(ncct, "  routed:               %llu\n", s->routed);
    nc_printf(ncct, "  dropped:              %llu\n", s->dropped);
    nc_printf(ncct, "  msgs_in:              %llu\n", s->msgs_in);
    nc_printf(ncct, "  msgs_out:             %llu\n", s->msgs_out);
    nc_printf(ncct, "  client_tx_backlog:    %llu\n",
              fq_client_data_backlog(global_fq_ctx.client[i]));
  }
  return 1;
}

static void
register_console_fq_commands() {
  mtev_console_state_t *tl;
  cmd_info_t *showcmd;

  tl = mtev_console_state_initial();
  showcmd = mtev_console_state_get_cmd(tl, "show");
  assert(showcmd && showcmd->dstate);
  mtev_console_state_add_cmd(showcmd->dstate,
    NCSCMD("fq", noit_console_show_fq, NULL, NULL, NULL));
}

static void process_fq_status(char *key, uint32_t value, void *cl) {
  fq_stats_t *s = (fq_stats_t *)cl;
  switch(*key) {
    case 'n':
      if(!strcmp(key, "no_exchange")) s->no_exchange = value;
      else if(!strcmp(key, "no_route")) s->no_route = value;
      break;
    case 'r':
      if(!strcmp(key, "routed")) s->routed = value;
      break;
    case 'd':
      if(!strcmp(key, "dropped")) s->dropped = value;
      break;
    case 'm':
      if(!strcmp(key, "msgs_in")) s->msgs_in = value;
      else if(!strcmp(key, "msgs_out")) s->msgs_out = value;
      break;
    default:
      break;
  }
}
static int
fq_status_checker(eventer_t e, int mask, void *closure, struct timeval *now) {
  int i;
  for(i=0; i<global_fq_ctx.nhosts; i++) {
    fq_client_status(global_fq_ctx.client[i], process_fq_status,
                     (void *)&global_fq_ctx.stats[i]);
  }
  eventer_add_in_s_us(fq_status_checker, NULL, 5, 0);
  return 0;
}

static int noit_fq_driver_init(mtev_dso_generic_t *self) {
  if(!nlerr) nlerr = mtev_log_stream_find("error/fq_driver");
  if(!nlerr) nlerr = mtev_error;
  stratcon_iep_mq_driver_register("fq", &mq_driver_fq);
  register_console_fq_commands();
  eventer_add_in_s_us(fq_status_checker, NULL, 0, 0);
  return 0;
}

mtev_dso_generic_t fq_driver = {
  {
    .magic = MTEV_GENERIC_MAGIC,
    .version = MTEV_GENERIC_ABI_VERSION,
    .name = "fq_driver",
    .description = "FQ driver for IEP MQ submission",
    .xml_description = fq_driver_xml_description,
    .onload = noit_fq_driver_onload
  },
  noit_fq_driver_config,
  noit_fq_driver_init
};

