/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2015-17, Circonus, Inc. All rights reserved.
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
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <mtev_defines.h>
#include "noit_config.h"
#include <mtev_uuid.h>
#include <mtev_json.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#include <eventer/eventer.h>
#include <mtev_conf.h>
#include <mtev_conf_private.h>
#include <mtev_memory.h>
#include <mtev_log.h>
#include <mtev_hash.h>
#include <mtev_skiplist.h>
#include <mtev_conf.h>
#include <mtev_console.h>
#include <mtev_cluster.h>
#include <mtev_str.h>
#include <mtev_watchdog.h>

#include "noit_mtev_bridge.h"
#include "noit_dtrace_probes.h"
#include "noit_check.h"
#include "noit_module.h"
#include "noit_check_tools.h"
#include "noit_check_resolver.h"
#include "noit_check_lmdb.h"
#include "modules/histogram.h"

static int check_recycle_period = 60000;
static mtev_boolean perpetual_metrics = mtev_false;
static bool initialized = false;

static mtev_log_stream_t check_error;
static mtev_log_stream_t check_debug;

static int32_t global_minimum_period = 1000;
static int32_t global_maximum_period = 300000;

#define CHECKS_XPATH_ROOT "/noit"
#define CHECKS_XPATH_PARENT "checks"
#define CHECKS_XPATH_BASE CHECKS_XPATH_ROOT "/" CHECKS_XPATH_PARENT

#define CHECK_DB_HEADER_STRING "X-Check-DB-Type"

MTEV_HOOK_IMPL(check_config_fixup,
  (noit_check_t *check),
  void *, closure,
  (void *closure, noit_check_t *check),
  (closure,check))

MTEV_HOOK_IMPL(check_stats_set_metric,
  (noit_check_t *check, stats_t *stats, metric_t *m),
  void *, closure,
  (void *closure, noit_check_t *check, stats_t *stats, metric_t *m),
  (closure,check,stats,m))

MTEV_HOOK_IMPL(check_stats_set_metric_coerce,
  (noit_check_t *check, stats_t *stats, const char *name,
   metric_type_t type, const char *v, mtev_boolean success),
  void *, closure,
  (void *closure, noit_check_t *check, stats_t *stats, const char *name,
   metric_type_t type, const char *v, mtev_boolean success),
  (closure,check,stats,name,type,v,success))

MTEV_HOOK_IMPL(check_passive_log_stats,
  (noit_check_t *check),
  void *, closure,
  (void *closure, noit_check_t *check),
  (closure,check))

MTEV_HOOK_IMPL(check_set_stats,
  (noit_check_t *check),
  void *, closure,
  (void *closure, noit_check_t *check),
  (closure,check))

MTEV_HOOK_IMPL(check_log_stats,
  (noit_check_t *check),
  void *, closure,
  (void *closure, noit_check_t *check),
  (closure,check))

MTEV_HOOK_IMPL(check_updated,
  (noit_check_t *check),
  void *, closure,
  (void *closure, noit_check_t *check),
  (closure,check))

MTEV_HOOK_IMPL(check_deleted,
  (noit_check_t *check),
  void *, closure,
  (void *closure, noit_check_t *check),
  (closure,check))

MTEV_HOOK_IMPL(check_stats_set_metric_histogram,
  (noit_check_t *check, mtev_boolean cumulative, metric_t *m, uint64_t count),
  void *, closure,
  (void *closure, noit_check_t *check, mtev_boolean cumulative, metric_t *m, uint64_t count),
  (closure, check, cumulative, m, count));

MTEV_HOOK_IMPL(noit_check_stats_populate_json,
  (struct mtev_json_object *doc, noit_check_t *check, stats_t *s, const char *name),
  void *, closure,
  (void *closure, struct mtev_json_object *doc, noit_check_t *check, stats_t *s, const char *name),
  (closure, doc, check, s, name));

MTEV_HOOK_IMPL(noit_check_stats_populate_xml,
  (xmlNodePtr doc, noit_check_t *check, stats_t *s, const char *name),
  void *, closure,
  (void *closure, xmlNodePtr doc, noit_check_t *check, stats_t *s, const char *name),
  (closure, doc, check, s, name));

MTEV_HOOK_IMPL(noit_stats_log_immediate_metric_timed,
  (noit_check_t *check, const char *metric_name, metric_type_t type, const void *value, const struct timeval *whence),
  void *, closure,
  (void *closure, noit_check_t *check, const char *metric_name, metric_type_t type, const void *value, const struct timeval *whence),
  (closure, check, metric_name, type, value, whence));

#define STATS_INPROGRESS 0
#define STATS_CURRENT 1
#define STATS_PREVIOUS 2

mtev_boolean
noit_check_build_tag_extended_name(char *tgt, size_t tgtlen, const char *name, const noit_check_t *check) {
  char tgt_tmp[MAX_METRIC_TAGGED_NAME];
  if(check->tagset) {
    strlcpy(tgt_tmp, name, sizeof(tgt_tmp));
    strlcat(tgt_tmp, check->tagset, sizeof(tgt_tmp));
    name = tgt_tmp;
  } else {
    strlcpy(tgt, name, tgtlen);
  }
  if(noit_metric_canonicalize(name, strlen(name), tgt, tgtlen, mtev_true) <= 0) {
    return mtev_false;
  }
  return mtev_true;
}

void
free_metric(metric_t *m) {
  if(m->metric_name) free(m->metric_name);
  if(m->metric_value.i) free(m->metric_value.i);
}

#define stats_inprogress(c) ((stats_t **)(c->statistics))[STATS_INPROGRESS]
#define stats_current(c) ((stats_t **)(c->statistics))[STATS_CURRENT]
#define stats_previous(c) ((stats_t **)(c->statistics))[STATS_PREVIOUS]

stats_t *
noit_check_get_stats_inprogress(noit_check_t *c) {
  return stats_inprogress(c);
}
stats_t *
noit_check_get_stats_current(noit_check_t *c) {
  return stats_current(c);
}
stats_t *
noit_check_get_stats_previous(noit_check_t *c) {
  return stats_previous(c);
}

struct stats_t {
  struct timeval whence;
  int8_t available;
  int8_t state;
  uint32_t duration;
  mtev_hash_table metrics;
  char status[256];
};

struct timeval *
noit_check_stats_whence(stats_t *s, const struct timeval *n) {
  if(n) memcpy(&s->whence, n, sizeof(*n));
  return &s->whence;
}
int8_t
noit_check_stats_available(stats_t *s, int8_t *n) {
  if(n) s->available = *n;
  return s->available;
}
int8_t
noit_check_stats_state(stats_t *s, int8_t *n) {
  if(n) s->state = *n;
  return s->state;
}
uint32_t
noit_check_stats_duration(stats_t *s, uint32_t *n) {
  if(n) s->duration = *n;
  return s->duration;
}
const char *
noit_check_stats_status(stats_t *s, const char *n) {
  if(n) strlcpy(s->status, n, sizeof(s->status));
  return s->status;
}
mtev_hash_table *
noit_check_stats_metrics(stats_t *s) {
  return &s->metrics;
}
void
noit_stats_set_whence(noit_check_t *c, struct timeval *t) {
  (void)noit_check_stats_whence(noit_check_get_stats_inprogress(c), t);
}
void
noit_stats_set_state(noit_check_t *c, int8_t t) {
  (void)noit_check_stats_state(noit_check_get_stats_inprogress(c), &t);
}
void
noit_stats_set_duration(noit_check_t *c, uint32_t t) {
  (void)noit_check_stats_duration(noit_check_get_stats_inprogress(c), &t);
}
void
noit_stats_set_status(noit_check_t *c, const char *s) {
  (void)noit_check_stats_status(noit_check_get_stats_inprogress(c), s);
}
void
noit_stats_set_available(noit_check_t *c, int8_t t) {
  (void)noit_check_stats_available(noit_check_get_stats_inprogress(c), &t);
}
static void
noit_check_safe_free_metric(void *vs) {
  metric_t *m = vs;
  if (m) {
    free_metric(m);
  }
}
static void
noit_check_safe_free_stats(void *vs) {
  stats_t *s = vs;
  mtev_hash_destroy(&s->metrics, NULL, (void (*)(void *))mtev_memory_safe_free);
}
static stats_t *
noit_check_stats_alloc() {
  stats_t *n;
  n = mtev_memory_safe_malloc_cleanup(sizeof(*n), noit_check_safe_free_stats);
  memset(n, 0, sizeof(*n));
  mtev_hash_init_mtev_memory(&n->metrics, MTEV_HASH_DEFAULT_SIZE, MTEV_HASH_LOCK_MODE_MUTEX);
  return n;
}
static void *
noit_check_stats_set_calloc() {
  int i;
  stats_t **s;
  s = calloc(sizeof(stats_t *), 3);
  for(i=0;i<3;i++) s[i] = noit_check_stats_alloc();
  return s;
}

/* 20 ms slots over 60 second for distribution */
#define SCHEDULE_GRANULARITY 20
#define SLOTS_PER_SECOND (1000/SCHEDULE_GRANULARITY)
#define MAX_MODULE_REGISTRATIONS 64

/* used to manage per-check generic module metadata */
struct vp_w_free {
  void *ptr;
  void (*freefunc)(void *);
};

static mtev_boolean system_needs_causality = mtev_false;
static int32_t text_size_limit = NOIT_DEFAULT_TEXT_METRIC_SIZE_LIMIT;
static int reg_module_id = 0;
static char *reg_module_names[MAX_MODULE_REGISTRATIONS] = { NULL };
static int reg_module_used = -1;
static uint64_t check_completion_count = 0ULL;
static uint64_t check_metrics_seen = 0ULL;
static pthread_mutex_t polls_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t recycling_lock = PTHREAD_MUTEX_INITIALIZER;
static mtev_hash_table polls;
static mtev_hash_table dns_ignore_list;
static mtev_skiplist *watchlist;
static mtev_skiplist *polls_by_name;
static uint32_t __config_load_generation = 0;
static unsigned short check_slots_count[60000 / SCHEDULE_GRANULARITY] = { 0 },
                      check_slots_seconds_count[60] = { 0 };
static mtev_boolean priority_scheduling = mtev_false;
static int priority_dead_zone_seconds = 3;
static noit_lmdb_instance_t *lmdb_instance = NULL;

static void
noit_check_safe_release(void *p) {
  noit_check_t *checker = p;
  if (checker->fire_event) {
    eventer_t removed = eventer_remove(checker->fire_event);

    if (removed) {
      free(eventer_get_closure(removed));
      eventer_free(removed);
      checker->fire_event = NULL;
    }
  }
  if (checker->target) {
    free(checker->target);
  }
  if (checker->module) {
    free(checker->module);
  }
  if (checker->name) {
    free(checker->name);
  }
  if (checker->filterset) {
    free(checker->filterset);
  }
  if (checker->config) {
    mtev_hash_destroy(checker->config, free, free);
    free(checker->config);
    checker->config = NULL;
  }
  if (checker->module_metadata) {
    for (int i = 0; i < reg_module_id; i++) {
      struct vp_w_free *tuple;
      tuple = checker->module_metadata[i];
      if (tuple) {
        if (tuple->freefunc) {
          tuple->freefunc(tuple->ptr);
        }
        free(tuple);
      }
    }
    free(checker->module_metadata);
  }
  if (checker->module_configs) {
    for (int i = 0; i < reg_module_id; i++) {
      if (checker->module_configs[i]) {
        mtev_hash_destroy(checker->module_configs[i], free, free);
        free(checker->module_configs[i]);
      }
    }
    free(checker->module_configs);
  }

  mtev_memory_safe_free(stats_inprogress(checker));
  mtev_memory_safe_free(stats_current(checker));
  mtev_memory_safe_free(stats_previous(checker));

  free(checker->statistics);

  pthread_rwlock_destroy(&checker->feeds_lock);
}
static noit_check_t *
noit_poller_lookup__nolock(uuid_t in) {
  void *vcheck;
  if (mtev_hash_retrieve(&polls, (char *)in, UUID_SIZE, &vcheck)) {
    return noit_check_ref((noit_check_t *)vcheck);
  }
  return NULL;
}
static noit_check_t *
noit_poller_lookup_by_name__nolock(char *target, char *name) {
  noit_check_t tmp_check;
  if(!polls_by_name) return NULL;
  memset(&tmp_check, 0, sizeof(tmp_check));
  tmp_check.target = target;
  tmp_check.name = name;
  noit_check_t *check = mtev_skiplist_find(polls_by_name, &tmp_check, NULL);

  if (check) {
    return noit_check_ref(check);
  }

  return NULL;
}

static int
noit_console_show_timing_slots(mtev_console_closure_t ncct,
                               int argc, char **argv,
                               mtev_console_state_t *dstate,
                               void *closure) {
  int i, j;
  const int upl = (60000 / SCHEDULE_GRANULARITY) / 60;
  char line[upl + 12];
  for(i=0;i<60;i++) {
    int offset = snprintf(line, sizeof(line), "[%02d] %04d: ", i, check_slots_seconds_count[i]);
    if(offset < 0) {
      nc_printf(ncct, "snprintf error\n");
      return 0;
    }
    for(j=i*upl;j<(i+1)*upl;j++) {
      char cp = '!';
      if(check_slots_count[j] < 10) cp = '0' + check_slots_count[j];
      else if(check_slots_count[j] < 36) cp = 'a' + (check_slots_count[j] - 10);
      if(offset < sizeof(line)-2) line[offset++] = cp;
    }
    line[offset] = '\0';
    nc_printf(ncct, "%s\n", line);
  }
  return 0;
}
static int
noit_check_add_to_list(noit_check_t *new_check, const char *newname, const char *newip) {
  int rv = 1;
  char *oldname = NULL;
  if(newname) {
    /* track this stuff outside the lock to avoid allocs */
    oldname = new_check->name;
  }
  pthread_mutex_lock(&polls_lock);
  if(!(new_check->flags & NP_TRANSIENT)) {
    mtevAssert(new_check->name || newname);
    /* This remove could fail -- no big deal */
    if(new_check->name != NULL) {
      mtevAssert(mtev_skiplist_remove(polls_by_name, new_check, NULL));
    }

    /* optional update the name (at the critical point) */
    if(newname) {
      new_check->name = strdup(newname);
    }
    if(newip) {
      new_check->target_ip[0] = '\0';
      strlcpy(new_check->target_ip, newip, sizeof(new_check->target_ip));
    }

    /* This insert could fail.. which means we have a conflict on
     * target`name.  That should result in the check being disabled. */
    rv = 0;
    if(!mtev_skiplist_insert(polls_by_name, new_check)) {
      mtevL(check_error, "Check %s`%s disabled due to naming conflict\n",
            new_check->target, new_check->name);
      new_check->flags |= NP_DISABLED;
      rv = -1;
    }
    if(oldname) free(oldname);
  } else {
    if(newname) {
      free(new_check->name);
      new_check->name = strdup(newname);
    }
    if(newip) {
      new_check->target_ip[0] = '\0';
      strlcpy(new_check->target_ip, newip, sizeof(new_check->target_ip));
    }
  }
  pthread_mutex_unlock(&polls_lock);
  return rv;
}

noit_lmdb_instance_t *noit_check_get_lmdb_instance() {
  return lmdb_instance;
}

uint64_t noit_check_metric_count() {
  return check_metrics_seen;
}
void noit_check_metric_count_add(int add) {
  ck_pr_add_64(&check_metrics_seen, add);
}

uint64_t noit_check_completion_count() {
  return check_completion_count;
}
static void register_console_check_commands();
static int check_recycle_bin_processor(eventer_t, int, void *,
                                       struct timeval *);

static int
check_slots_find_smallest(int sec, struct timeval* period, int timeout) {
  int i, j, cyclic, random_offset, jbase = 0, mini = 0, minj = 0;
  unsigned short min_running_i = 0xffff, min_running_j = 0xffff;
  int period_seconds = period->tv_sec;

  /* If we're greater than sixty seconds, we should do our
   * initial scheduling as if the period was sixty seconds. */
  if (period_seconds > 60) {
    period_seconds = 60;
  }

  /* If a check is configured to run at times aligned with sixty seconds
   * and we're configured to use priority scheduling, schedule so that
   * we're guaranteed to finish before the timeout */
  if ((priority_scheduling == mtev_true) &&
      (((period->tv_sec % 60) == 0) && (period->tv_usec == 0))) {
    /* Don't allow a ton of stuff to schedule in the first second in the case
     * of very long timeouts - use the first 10 seconds in this case */
    int allowable_time = MAX(60 - (timeout/1000) - 1, 10);
    int max_seconds = MIN(60-priority_dead_zone_seconds, allowable_time);
    for(i=0;i<max_seconds;i++) {
      int adj_i = (i + sec) % max_seconds;
      if(check_slots_seconds_count[adj_i] < min_running_i) {
        min_running_i = check_slots_seconds_count[adj_i];
        mini = adj_i;
      }
    }
  }
  else {
    /* Just schedule normally*/
    for(i=0;i<period_seconds;i++) {
      int adj_i = (i + sec) % 60;
      if(check_slots_seconds_count[adj_i] < min_running_i) {
        min_running_i = check_slots_seconds_count[adj_i];
        mini = adj_i;
      }
    }
  }
  jbase = mini * (1000/SCHEDULE_GRANULARITY);
  random_offset = drand48() * SLOTS_PER_SECOND;
  for(cyclic=0;cyclic<SLOTS_PER_SECOND;cyclic++) {
    j = jbase + ((random_offset + cyclic) % SLOTS_PER_SECOND);
    if(check_slots_count[j] < min_running_j) {
      min_running_j = check_slots_count[j];
      minj = j;
    }
  }
  return (minj * SCHEDULE_GRANULARITY) + drand48() * SCHEDULE_GRANULARITY;
}
static void
check_slots_adjust_tv(struct timeval *tv, short adj) {
  int offset_ms, idx;
  offset_ms = (tv->tv_sec % 60) * 1000 + (tv->tv_usec / 1000);
  idx = offset_ms / SCHEDULE_GRANULARITY;
  check_slots_count[idx] += adj;
  check_slots_seconds_count[offset_ms / 1000] += adj;
}
void check_slots_inc_tv(struct timeval *tv) {
  check_slots_adjust_tv(tv, 1);
}
void check_slots_dec_tv(struct timeval *tv) {
  check_slots_adjust_tv(tv, -1);
}
static int
noit_check_generic_safe_string(const char *p) {
  if(!p) return 0;
  for(;*p;p++) {
    if(!isprint(*p)) return 0;
  }
  return 1;
}
int
noit_check_validate_target(const char *p) {
  if(!noit_check_generic_safe_string(p)) return 0;
  return 1;
}
int
noit_check_validate_name(const char *p) {
  if(!noit_check_generic_safe_string(p)) return 0;
  return 1;
}
const char *
noit_check_available_string(int16_t available) {
  switch(available) {
    case NP_AVAILABLE:    return "available";
    case NP_UNAVAILABLE:  return "unavailable";
    case NP_UNKNOWN:      return "unknown";
  }
  return NULL;
}
const char *
noit_check_state_string(int16_t state) {
  switch(state) {
    case NP_GOOD:         return "good";
    case NP_BAD:          return "bad";
    case NP_UNKNOWN:      return "unknown";
  }
  return NULL;
}
static int __check_name_compare(const void *a, const void *b) {
  const noit_check_t *ac = a;
  const noit_check_t *bc = b;
  int rv;
  if((rv = strcmp(ac->target, bc->target)) != 0) return rv;
  if((rv = strcmp(ac->name, bc->name)) != 0) return rv;
  return 0;
}
static int __watchlist_compare(const void *a, const void *b) {
  const noit_check_t *ac = a;
  const noit_check_t *bc = b;
  int rv;
  if((rv = memcmp(ac->checkid, bc->checkid, sizeof(ac->checkid))) != 0) return rv;
  if(ac->period < bc->period) return -1;
  if(ac->period == bc->period) return 0;
  return 1;
}
static int __check_target_ip_compare(const void *a, const void *b) {
  const noit_check_t *ac = a;
  const noit_check_t *bc = b;
  int rv;
  if((rv = strcmp(ac->target_ip, bc->target_ip)) != 0) return rv;
  if (ac->name == NULL) return 1;
  if (bc->name == NULL) return -1;
  if((rv = strcmp(ac->name, bc->name)) != 0) return rv;
  return 1;
}
static int __check_target_compare(const void *a, const void *b) {
  const noit_check_t *ac = a;
  const noit_check_t *bc = b;
  int rv;
  if (ac->target == NULL) return 1;
  if (bc->target == NULL) return -1;
  if((rv = strcmp(ac->target, bc->target)) != 0) return rv;
  if (ac->name == NULL) return 1;
  if (bc->name == NULL) return -1;
  if((rv = strcmp(ac->name, bc->name)) != 0) return rv;
  return 1;
}
int
noit_calc_rtype_flag(char *resolve_rtype) {
  int flags = 0;
  if(resolve_rtype) {
    flags |= strcmp(resolve_rtype, PREFER_IPV6) == 0 ||
             strcmp(resolve_rtype, FORCE_IPV6) == 0 ? NP_PREFER_IPV6 : 0;
    flags |= strcmp(resolve_rtype, FORCE_IPV4) == 0 ||
             strcmp(resolve_rtype, FORCE_IPV6) == 0 ? NP_SINGLE_RESOLVE : 0;
  }
  return flags;
}
void
noit_check_fake_last_check(noit_check_t *check,
                           struct timeval *lc, struct timeval *_now) {
  struct timeval now, period, lc_copy;
  int balance_ms;

  if(!_now) {
    mtev_gettimeofday(&now, NULL);
    _now = &now;
  }
  period.tv_sec = check->period / 1000;
  period.tv_usec = (check->period % 1000) * 1000;
  sub_timeval(*_now, period, lc);

  /* We need to set the last check value based on the period, but
   * we also need to store a value that is based around the one-minute
   * time to properly increment the slots; otherwise, the slots will
   * get all messed up */
  if(!(check->flags & NP_TRANSIENT) && check->period) {
    balance_ms = check_slots_find_smallest(_now->tv_sec+1, &period, check->timeout);
    lc->tv_sec = (lc->tv_sec / 60) * 60 + balance_ms / 1000;
    lc->tv_usec = (balance_ms % 1000) * 1000;
    memcpy(&lc_copy, lc, sizeof(lc_copy));
    if(compare_timeval(*_now, *lc) < 0) {
      do {
        sub_timeval(*lc, period, lc);
      } while(compare_timeval(*_now, *lc) < 0);
    }
    else {
      struct timeval test;
      while(1) {
        add_timeval(*lc, period, &test);
        if(compare_timeval(*_now, test) < 0) break;
        memcpy(lc, &test, sizeof(test));
      }
    }
  }
  else {
    memcpy(&lc_copy, lc, sizeof(lc_copy));
  }
  
  /* now, we're going to do an even distribution using the slots */
  if(!(check->flags & NP_TRANSIENT)) check_slots_inc_tv(&lc_copy);
}
static void
noit_poller_process_check_conf(mtev_conf_section_t section) {
  void *vcheck;
  char uuid_str[37];
  char target[256] = "";
  char module[256] = "";
  char name[256] = "";
  char filterset[256] = "";
  char oncheck[1024] = "";
  char resolve_rtype[16] = "";
  char delstr[8] = "";
  int ridx, flags, found;
  int no_period = 0;
  int no_oncheck = 0;
  int minimum_period = 1000, maximum_period = 300000, period = 0, timeout = 0;
  int transient_min_period = 0, transient_period_granularity = 0;
  mtev_boolean disabled = mtev_false, busted = mtev_false, deleted = mtev_false;
  uuid_t uuid, out_uuid;
  int64_t config_seq = 0;
  mtev_hash_table *options;
  mtev_hash_table **moptions = NULL;
  mtev_boolean moptions_used = mtev_false, backdated = mtev_false;

  /* We want to heartbeat here... otherwise, if a lot of checks are 
   * configured or if we're running on a slower system, we could 
   * end up getting watchdog killed before we get a chance to run 
   * any checks */
  mtev_watchdog_child_heartbeat();

  if(reg_module_id > 0) {
    moptions = alloca(reg_module_id * sizeof(mtev_hash_table *));
    memset(moptions, 0, reg_module_id * sizeof(mtev_hash_table *));
    moptions_used = mtev_true;
  }

#define MYATTR(type,a,...) mtev_conf_get_##type(section, "@" #a, __VA_ARGS__)
#define INHERIT(type,a,...) \
  mtev_conf_get_##type(section, "ancestor-or-self::node()/@" #a, __VA_ARGS__)

  if(!MYATTR(stringbuf, uuid, uuid_str, sizeof(uuid_str))) {
    mtevL(noit_stderr, "check has no uuid\n");
    return;
  }
  if(mtev_conf_env_off(section, NULL)) {
    mtevL(noit_stderr, "check %s environmentally disabled.\n", uuid_str);
    return;
  }

  MYATTR(int64, seq, &config_seq);

  if(mtev_uuid_parse(uuid_str, uuid)) {
    mtevL(noit_stderr, "check uuid: '%s' is invalid\n", uuid_str);
    return;
  }

  flags = 0;
  if(MYATTR(stringbuf, deleted, delstr, sizeof(delstr) && !strcmp(delstr, "deleted"))) {
    deleted = mtev_true;
    disabled = mtev_true;
    flags |= NP_DELETED;
  }

  if(!INHERIT(stringbuf, target, target, sizeof(target))) {
    if(!deleted) mtevL(noit_stderr, "check uuid: '%s' has no target\n", uuid_str);
    busted = mtev_true;
  }
  if(!noit_check_validate_target(target)) {
    if(!deleted) mtevL(noit_stderr, "check uuid: '%s' has malformed target\n", uuid_str);
    busted = mtev_true;
  }
  if(!INHERIT(stringbuf, module, module, sizeof(module))) {
    if(!deleted) mtevL(noit_stderr, "check uuid: '%s' has no module\n", uuid_str);
    busted = mtev_true;
  }

  if(!INHERIT(stringbuf, filterset, filterset, sizeof(filterset)))
    filterset[0] = '\0';

  if (!INHERIT(stringbuf, resolve_rtype, resolve_rtype, sizeof(resolve_rtype)))
    strlcpy(resolve_rtype, PREFER_IPV4, sizeof(resolve_rtype));

  if(!MYATTR(stringbuf, name, name, sizeof(name)))
    strlcpy(name, module, sizeof(name));

  if(!noit_check_validate_name(name)) {
    if(!deleted) mtevL(noit_stderr, "check uuid: '%s' has malformed name\n", uuid_str);
    busted = mtev_true;
  }

  INHERIT(int32, minimum_period, &minimum_period);
  INHERIT(int32, maximum_period, &maximum_period);
  if(!INHERIT(int32, period, &period) || period == 0) {
    no_period = 1;
	}
	else {
		if(period < minimum_period) period = minimum_period;
		if(period > maximum_period) period = maximum_period;
	}

  INHERIT(int32, transient_min_period, &transient_min_period);
  if (transient_min_period < 0) {
    transient_min_period = 0;
  }
  INHERIT(int32, transient_period_granularity, &transient_period_granularity);
  if (transient_period_granularity < 0) {
    transient_period_granularity = 0;
  }
  if(!INHERIT(stringbuf, oncheck, oncheck, sizeof(oncheck)) || !oncheck[0]) {
    no_oncheck = 1;
  }

  if(deleted) {
    memcpy(target, "none", 5);
    mtev_uuid_unparse_lower(uuid, name);
  } else {
    if(no_period && no_oncheck) {
      mtevL(noit_stderr, "check uuid: '%s' has neither period nor oncheck\n",
            uuid_str);
      busted = mtev_true;
    }
    if(!(no_period || no_oncheck)) {
      mtevL(noit_stderr, "check uuid: '%s' has oncheck and period.\n",
            uuid_str);
      busted = mtev_true;
    }
    if(!INHERIT(int32, timeout, &timeout)) {
      mtevL(noit_stderr, "check uuid: '%s' has no timeout\n", uuid_str);
      busted = mtev_true;
    }
		if(timeout < 0) timeout = 0;
    if(!no_period && timeout >= period) {
      mtevL(noit_stderr, "check uuid: '%s' timeout > period\n", uuid_str);
      timeout = period/2;
    }
    INHERIT(boolean, disable, &disabled);
  }

  options = mtev_conf_get_hash(section, "config");
  for(ridx=0; ridx<reg_module_id; ridx++) {
    moptions[ridx] = mtev_conf_get_namespaced_hash(section, "config",
                                                   reg_module_names[ridx]);
  }

  if(busted) flags |= (NP_UNCONFIG|NP_DISABLED);
  else if(disabled) flags |= NP_DISABLED;

  flags |= noit_calc_rtype_flag(resolve_rtype);

  vcheck = noit_poller_check_found_and_backdated(uuid, config_seq, &found, &backdated);

  if(found)
    noit_poller_deschedule(uuid, mtev_false, mtev_true);
  if(backdated) {
    mtevL(check_error, "Check config seq backwards, ignored\n");
    if(found) noit_check_log_delete((noit_check_t *)vcheck);
  }
  else {
    noit_poller_schedule(target, module, name, filterset, options,
                         moptions_used ? moptions : NULL,
                         period, timeout,
                         transient_min_period, transient_period_granularity,
                         oncheck[0] ? oncheck : NULL,
                         config_seq, flags, uuid, out_uuid);
    mtevL(check_debug, "loaded uuid: %s\n", uuid_str);
    if(deleted) noit_poller_deschedule(uuid, mtev_false, mtev_false);
  }
  for(ridx=0; ridx<reg_module_id; ridx++) {
    if(moptions[ridx]) {

      mtev_hash_destroy(moptions[ridx], free, free);
      free(moptions[ridx]);
    }
  }
  mtev_hash_destroy(options, free, free);
  free(options);
}
void *
noit_poller_check_found_and_backdated(uuid_t uuid, int64_t config_seq, int *found, mtev_boolean *backdated) {
  void *vcheck = NULL;
  pthread_mutex_lock(&polls_lock);
  *found = mtev_hash_retrieve(&polls, (char *)uuid, UUID_SIZE, &vcheck);
  if(*found) {
    noit_check_t *check = (noit_check_t *)vcheck;
    /* Possibly reset the seq */
    if(config_seq < 0) check->config_seq = 0;

    /* Otherwise note a non-increasing sequence */
    if(check->config_seq > config_seq) *backdated = mtev_true;
  }
  pthread_mutex_unlock(&polls_lock);
  return vcheck;
}
void
noit_poller_process_checks(const char *xpath) {
  int i, cnt = 0;
  mtev_conf_section_t *sec;
  __config_load_generation++;
  mtevL(check_debug, "processing checks\n");
  sec = mtev_conf_get_sections_read(MTEV_CONF_ROOT, xpath, &cnt);
  for(i=0; i<cnt; i++) {
    noit_poller_process_check_conf(sec[i]);
  }
  mtev_conf_release_sections_read(sec, cnt);
  mtevL(check_debug, "processed %d checks\n", cnt);
}

int
noit_check_activate(noit_check_t *check) {
  noit_module_t *mod;
  if(NOIT_CHECK_LIVE(check)) return 0;
  mod = noit_module_lookup(check->module);
  if(mod && mod->initiate_check) {
    if((check->flags & NP_DISABLED) == 0) {
      mod->initiate_check(mod, check, 0, NULL);
      return 1;
    }
    else
      mtevL(check_debug, "Skipping %s`%s, disabled.\n",
            check->target, check->name);
  }
  else {
    if(!mod && !NOIT_CHECK_DISABLED(check)) {
      mtevL(noit_stderr, "Cannot find module '%s'\n", check->module);
      check->flags |= NP_DISABLED;
    }
  }
  return 0;
}

void
noit_poller_initiate() {
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen;
  void *vcheck;
  /* This is only ever called in the beginning, no lock needed */
  while(mtev_hash_next(&polls, &iter, (const char **)key_id, &klen,
                       &vcheck)) {
    noit_check_activate((noit_check_t *)vcheck);
    mtev_watchdog_child_heartbeat();
  }
}

void
noit_poller_flush_epoch(int oldest_allowed) {
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen, i;
  void *vcheck;
#define TOFREE_PER_ITER 1024
  noit_check_t *tofree[TOFREE_PER_ITER];

  /* Cleanup any previous causal map */
  while(1) {
    i = 0;
    pthread_mutex_lock(&polls_lock);
    while(mtev_hash_next(&polls, &iter, (const char **)key_id, &klen,
                         &vcheck) && i < TOFREE_PER_ITER) {
      noit_check_t *check = (noit_check_t *)vcheck;
      if(check->generation < oldest_allowed) {
        tofree[i++] = check;
      }
    }
    pthread_mutex_unlock(&polls_lock);
    if(i==0) break;
    while(i>0) noit_poller_deschedule(tofree[--i]->checkid, mtev_true, mtev_false);
  }
#undef TOFREE_PER_ITER
}

void
noit_poller_make_causal_map() {
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen;
  void *vcheck;

  if(!system_needs_causality) return;

  /* set it to false, we'll set it to true during the scan if we
   * find anything causal.  */
  system_needs_causality = mtev_false;

  /* Cleanup any previous causal map */
  pthread_mutex_lock(&polls_lock);
  while(mtev_hash_next(&polls, &iter, (const char **)key_id, &klen,
                       &vcheck)) {
    noit_check_t *check = (noit_check_t *)vcheck;
    dep_list_t *dep;
    while((dep = check->causal_checks) != NULL) {
      check->causal_checks = dep->next;
      free(dep);
    }
  }

  memset(&iter, 0, sizeof(iter));
  /* Walk all checks and add check dependencies to their parents */
  while(mtev_hash_next(&polls, &iter, (const char **)key_id, &klen,
                       &vcheck)) {
    noit_check_t *check = (noit_check_t *)vcheck, *parent;
    if(check->oncheck) {
      /* This service is causally triggered by another service */
      uuid_t id;
      char fullcheck[1024];
      char *name = check->oncheck;
      char *target = NULL;

      system_needs_causality = mtev_true;
      mtevL(check_debug, "Searching for upstream trigger on %s\n", name);
      parent = NULL;
      if(mtev_uuid_parse(check->oncheck, id) == 0) {
        target = "";
        parent = noit_poller_lookup__nolock(id);
      }
      else if((target = strchr(check->oncheck, '`')) != NULL) {
        strlcpy(fullcheck, check->oncheck, target + 1 - check->oncheck);
        name = target + 1;
        target = fullcheck;
        parent = noit_poller_lookup_by_name__nolock(target, name);
      }
      else {
        target = check->target;
        parent = noit_poller_lookup_by_name__nolock(target, name);
      }

      if(!parent) {
        check->flags |= NP_DISABLED;
        mtevL(noit_stderr, "Disabling check %s`%s, can't find oncheck %s`%s\n",
              check->target, check->name, target, name);
      }
      else {
        dep_list_t *dep;
        dep = malloc(sizeof(*dep));
        dep->check = check;
        dep->next = parent->causal_checks;
        parent->causal_checks = dep;
        mtevL(check_debug, "Causal map %s`%s --> %s`%s\n",
              parent->target, parent->name, check->target, check->name);
        noit_check_deref(parent);
      }
    }
  }
  pthread_mutex_unlock(&polls_lock);
  /* We found some causal checks, so we might need to activate stuff */
  if(system_needs_causality) noit_poller_initiate();
}
void
noit_poller_reload(const char *xpath)
{
  noit_poller_process_checks(xpath ? xpath : "/noit/checks//check");
  if(!xpath) {
    /* Full reload, we need to wipe old checks */
    noit_poller_flush_epoch(__config_load_generation);
  }
  noit_poller_make_causal_map();
}
void
noit_poller_reload_lmdb(uuid_t *checks, int cnt)
{
  noit_check_lmdb_poller_process_checks(checks, cnt);
  if(!checks) {
    /* Full reload, we need to wipe old checks */
    noit_poller_flush_epoch(__config_load_generation);
  }
  noit_poller_make_causal_map();
}
void
noit_check_dns_ignore_tld(const char* extension, const char* ignore) {
  mtev_hash_replace(&dns_ignore_list, strdup(extension), strlen(extension), strdup(ignore), free, free);
}
static void 
noit_check_dns_ignore_list_init() {
  mtev_conf_section_t* dns;
  int cnt;

  noit_check_dns_ignore_tld("onion", "true");
  dns = mtev_conf_get_sections_read(MTEV_CONF_ROOT, "/noit/dns/extension", &cnt);
  if(dns) {
    int i = 0;
    for (i = 0; i < cnt; i++) {
      char* extension = NULL;
      char* ignore = NULL;
      if(mtev_conf_get_string(dns[i], "self::node()/@value", &extension) &&
         mtev_conf_get_string(dns[i], "self::node()/@ignore", &ignore)) {
	if(mtev_conf_env_off(dns[i], NULL)) {
          mtevL(mtev_debug, "dns extension '%s' environmentally ignored.\n", extension);
        }
        else {
          noit_check_dns_ignore_tld(extension, ignore);
        }
      }
      free(extension);
      free(ignore);
    }
  }
  mtev_conf_release_sections_read(dns, cnt);
}
void
noit_poller_init() {
  srand48((getpid() << 16) ^ time(NULL));

  check_error = mtev_log_stream_find("error/noit/check");
  check_debug = mtev_log_stream_find("debug/noit/check");

  mtev_conf_get_int32(MTEV_CONF_ROOT, "//checks/@minimum_period", &global_minimum_period);
  mtev_conf_get_int32(MTEV_CONF_ROOT, "//checks/@maximum_period", &global_maximum_period);

  if (global_minimum_period <= 0) {
    global_minimum_period = 1000;
  }
  if (global_maximum_period <= 0) {
    global_maximum_period = 300000;
  }

  mtev_conf_get_boolean(MTEV_CONF_ROOT, "//checks/@priority_scheduling", &priority_scheduling);
  mtev_conf_get_boolean(MTEV_CONF_ROOT, "//checks/@perpetual_metrics", &perpetual_metrics);

  /* lmdb_path dictates where an LMDB backing store for checks would live.
   * use_lmdb defaults to the existence of an lmdb_path...
   *   explicit true will use one, creating if needed
   *   explicit false will not use it, if even if it exists
   *   otherwise, it will use it only if it already exists
   */
  char *lmdb_path = NULL;
  bool lmdb_path_exists = false;
  (void)mtev_conf_get_string(MTEV_CONF_ROOT, "//checks/@lmdb_path", &lmdb_path);
  if(lmdb_path) {
    struct stat sb;
    int rv = -1;
    while((rv = stat(lmdb_path, &sb)) == -1 && errno == EINTR);
    if(rv == 0 && (S_IFDIR == (sb.st_mode & S_IFMT))) {
      lmdb_path_exists = true;
    }
  }
  mtev_boolean use_lmdb = (lmdb_path && lmdb_path_exists);
  mtev_conf_get_boolean(MTEV_CONF_ROOT, "//checks/@use_lmdb", &use_lmdb);

  if (use_lmdb == mtev_true) {
    if (lmdb_path == NULL) {
      mtevFatal(mtev_error, "noit_check: use_lmdb specified, but no path provided\n");
    }
    lmdb_instance = noit_lmdb_tools_open_instance(lmdb_path);
    if (!lmdb_instance) {
      mtevFatal(mtev_error, "noit_check: couldn't create lmdb instance - %s\n", strerror(errno));
    }
    noit_check_lmdb_migrate_xml_checks_to_lmdb();
  }
  free(lmdb_path);

  noit_check_resolver_init();
  noit_check_tools_init();

  mtev_skiplist *pbn;
  pbn = mtev_skiplist_alloc();
  mtev_skiplist_set_compare(pbn, __check_name_compare,
                            __check_name_compare);
  mtev_skiplist_add_index(pbn, __check_target_ip_compare,
                          __check_target_ip_compare);
  mtev_skiplist_add_index(pbn, __check_target_compare,
                          __check_target_compare);
  ck_pr_barrier();
  /* Now when people see it, it will be complete and functional */
  polls_by_name = pbn;

  watchlist = mtev_skiplist_alloc();
  mtev_skiplist_set_compare(watchlist, __watchlist_compare,
                            __watchlist_compare);
  register_console_check_commands();
  eventer_name_callback("check_recycle_bin_processor",
                        check_recycle_bin_processor);
  mtev_conf_get_int32(MTEV_CONF_ROOT, "/noit/@check_recycle_period", &check_recycle_period);
  eventer_add_in_s_us(check_recycle_bin_processor, NULL, check_recycle_period/1000, 1000*(check_recycle_period%1000));
  mtev_conf_get_int32(MTEV_CONF_ROOT, "/noit/@text_size_limit", &text_size_limit);
  if (text_size_limit <= 0) {
    text_size_limit = NOIT_DEFAULT_TEXT_METRIC_SIZE_LIMIT;
  }
  noit_check_dns_ignore_list_init();
  if (noit_check_get_lmdb_instance()) {
    noit_poller_reload_lmdb(NULL, 0);
  }
  else {
    noit_poller_reload(NULL);
  }
  initialized = true;
}

int
noit_poller_check_count() {
  if(polls_by_name == NULL) return 0;
  return mtev_skiplist_size(polls_by_name);
}

int
noit_poller_transient_check_count() {
  return mtev_skiplist_size(watchlist);
}

noit_check_t *
noit_check_clone(uuid_t in) {
  int i;
  noit_check_t *checker, *new_check;
  void *vcheck;
  if(mtev_hash_retrieve(&polls,
                        (char *)in, UUID_SIZE,
                        &vcheck) == 0) {
    return NULL;
  }
  checker = (noit_check_t *)vcheck;
  if(checker->oncheck) {
    return NULL;
  }
  new_check = calloc(1, sizeof(*new_check));
  new_check->ref_cnt = 1;
  mtevAssert(new_check != NULL);
  memcpy(new_check, checker, sizeof(*new_check));
  pthread_rwlock_init(&new_check->feeds_lock, NULL);
  new_check->target = strdup(new_check->target);
  new_check->module = strdup(new_check->module);
  new_check->name = strdup(new_check->name);
  new_check->filterset = strdup(new_check->filterset);
  new_check->flags = 0;
  new_check->fire_event = NULL;
  memset(&new_check->last_fire_time, 0, sizeof(new_check->last_fire_time));
  new_check->statistics = noit_check_stats_set_calloc();
  new_check->closure = NULL;
  new_check->config = calloc(1, sizeof(*new_check->config));
  mtev_hash_init_locks(new_check->config, MTEV_HASH_DEFAULT_SIZE, MTEV_HASH_LOCK_MODE_MUTEX);
  mtev_hash_merge_as_dict(new_check->config, checker->config);
  if(new_check->tagset) {
    const char *val = NULL;
    (void)mtev_hash_retr_str(new_check->config, "tagset", 6, &val);
    new_check->tagset = val;
  }
  new_check->module_configs = NULL;
  new_check->module_metadata = NULL;

  for(i=0; i<reg_module_id; i++) {
    void *src_metadata;
    mtev_hash_table *src_mconfig;
    src_mconfig = noit_check_get_module_config(checker, i);
    if(src_mconfig) {
      mtev_hash_table *t = calloc(1, sizeof(*new_check->config));
      mtev_hash_init_locks(t, MTEV_HASH_DEFAULT_SIZE, MTEV_HASH_LOCK_MODE_MUTEX);
      mtev_hash_merge_as_dict(t, src_mconfig);
      noit_check_set_module_config(new_check, i, t);
    }
    if(checker->flags & NP_PASSIVE_COLLECTION)
      if(NULL != (src_metadata = noit_check_get_module_metadata(new_check, i)))
        noit_check_set_module_metadata(new_check, i, src_metadata, NULL);
  }
  return new_check;
}

noit_check_t *
noit_check_watch(uuid_t in, int period) {
  /* First look for a copy that is being watched */
  int32_t minimum_pi = 1000, granularity_pi = 500;
  mtev_conf_section_t check_node;
  char uuid_str[UUID_STR_LEN + 1];
  char xpath[1024];
  noit_check_t n, *f;

  mtev_uuid_unparse_lower(in, uuid_str);

  mtevL(check_debug, "noit_check_watch(%s,%d)\n", uuid_str, period);
  if(period == 0) {
    mtev_uuid_copy(n.checkid, in);
    f = noit_poller_lookup(in);
    n.period = period;
    if(mtev_skiplist_find(watchlist, &n, NULL) == NULL) {
      mtevL(check_debug, "Watching %s@%d\n", uuid_str, period);
      noit_check_ref(f);
      mtev_skiplist_insert(watchlist, f);
    }
    return f;
  }

  /* Find the check */
  f = noit_poller_lookup(in);
  if (f) {
    if (f->transient_min_period > 0) {
      minimum_pi = f->transient_min_period;
    }
    if (f->transient_period_granularity < 0) {
      granularity_pi = f->transient_period_granularity;
    }
  }
  else {
    if (noit_check_get_lmdb_instance()) {
      char *transient_min_period_str = noit_check_lmdb_get_specific_field(in, NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "transient_min_period", mtev_false);
      char *transient_period_granularity_str = noit_check_lmdb_get_specific_field(in, NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "transient_period_granularity", mtev_false);
      if (transient_min_period_str) {
        int32_t transient_min_period = atoi(transient_min_period_str);
        if (transient_min_period > 0) {
          minimum_pi = transient_min_period;
        }
      }
      else {
        mtev_conf_get_int32(MTEV_CONF_ROOT, "//checks/@transient_min_period", &minimum_pi);
      }
      if (transient_period_granularity_str) {
        int32_t transient_period_granularity = atoi(transient_period_granularity_str);
        if (transient_period_granularity > 0) {
          granularity_pi = transient_period_granularity;
        }
      }
      else {
        mtev_conf_get_int32(MTEV_CONF_ROOT, "//checks/@transient_period_granularity", &granularity_pi);
      }
      free(transient_min_period_str);
      free(transient_period_granularity_str);
    }
    else {
      snprintf(xpath, sizeof(xpath), "//checks//check[@uuid=\"%s\"]", uuid_str);
      check_node = mtev_conf_get_section_read(MTEV_CONF_ROOT, xpath);
      mtev_conf_get_int32(MTEV_CONF_ROOT, "//checks/@transient_min_period", &minimum_pi);
      mtev_conf_get_int32(MTEV_CONF_ROOT, "//checks/@transient_period_granularity", &granularity_pi);
      if(!mtev_conf_section_is_empty(check_node)) {
        mtev_conf_get_int32(check_node,
                          "ancestor-or-self::node()/@transient_min_period",
                          &minimum_pi);
        mtev_conf_get_int32(check_node,
                          "ancestor-or-self::node()/@transient_period_granularity",
                          &granularity_pi);
      }
      mtev_conf_release_section_read(check_node);
    }
  }

  /* apply the bounds */
  period /= granularity_pi;
  period *= granularity_pi;
  period = MAX(period, minimum_pi);

  mtev_uuid_copy(n.checkid, in);
  n.period = period;

  f = mtev_skiplist_find(watchlist, &n, NULL);
  if(f) return f;
  f = noit_check_clone(in);
  if(!f) return NULL;
  f->period = period;
  f->timeout = period - 10;
  f->flags |= NP_TRANSIENT;
  mtevL(check_debug, "Watching %s@%d\n", uuid_str, period);
  mtev_skiplist_insert(watchlist, noit_check_ref(f));
  return f;
}

noit_check_t *
noit_check_get_watch(uuid_t in, int period) {
  noit_check_t n, *f;

  mtev_uuid_copy(n.checkid, in);
  n.period = period;
  if(period == 0) {
    f = noit_poller_lookup(in);
    if(f) {
      n.period = f->period;
      noit_check_deref(f);
    }
  }

  f = mtev_skiplist_find(watchlist, &n, NULL);
  return noit_check_ref(f);
}

void
noit_check_transient_foreach_feed(noit_check_t *check, void (*cb)(void *, noit_check_t *, const char *),
                                  void *closure) {
  mtev_skiplist_node *curr;
  pthread_rwlock_rdlock(&check->feeds_lock);
  if(check->feeds) {
    for(curr = mtev_skiplist_getlist(check->feeds); curr; mtev_skiplist_next(check->feeds, &curr)) {
      cb(closure, check, (char *)mtev_skiplist_data(curr));
    }
  }
  pthread_rwlock_unlock(&check->feeds_lock);
}
void
noit_check_transient_add_feed(noit_check_t *check, const char *feed) {
  char *feedcopy;
  pthread_rwlock_wrlock(&check->feeds_lock);
  if(!check->feeds) {
    check->feeds = mtev_skiplist_alloc();
    mtev_skiplist_set_compare(check->feeds,
                              (mtev_skiplist_comparator_t)strcmp,
                              (mtev_skiplist_comparator_t)strcmp);
  }
  feedcopy = strdup(feed);
  /* No error on failure -- it's already there */
  if(mtev_skiplist_insert(check->feeds, feedcopy) == NULL) free(feedcopy);
  mtevL(check_debug, "check %s`%s @ %dms has %d feed(s): %s.\n",
        check->target, check->name, check->period, mtev_skiplist_size(check->feeds), feed);
  pthread_rwlock_unlock(&check->feeds_lock);
}
void
noit_check_transient_remove_feed(noit_check_t *check, const char *feed) {
  pthread_rwlock_wrlock(&check->feeds_lock);
  if(!check->feeds) {
    pthread_rwlock_unlock(&check->feeds_lock);
    return;
  }
  if(feed) {
    mtevL(check_debug, "check %s`%s @ %dms removing 1 of %d feeds: %s.\n",
          check->target, check->name, check->period, mtev_skiplist_size(check->feeds), feed);
    mtev_skiplist_remove(check->feeds, feed, free);
  }
  if(mtev_skiplist_size(check->feeds) == 0) {
    char uuid_str[UUID_STR_LEN + 1];
    mtev_uuid_unparse_lower(check->checkid, uuid_str);
    mtevL(check_debug, "Unwatching %s@%d\n", uuid_str, check->period);
    mtev_skiplist_remove(watchlist, check, NULL);
    mtev_skiplist_destroy(check->feeds, free);
    free(check->feeds);
    check->feeds = NULL;
    if(check->flags & NP_TRANSIENT) {
      mtevL(check_debug, "check %s`%s @ %dms has no more listeners.\n",
            check->target, check->name, check->period);
      check->flags |= NP_KILLED;
    }
    noit_check_t *existing = noit_poller_lookup(check->checkid);
    if(existing != check) {
      noit_poller_free_check(check);
    }
    noit_check_deref(existing);
  }
  pthread_rwlock_unlock(&check->feeds_lock);
}

mtev_boolean
noit_check_is_valid_target(const char *target) {
  int8_t family;
  int rv;
  union {
    struct in_addr addr4;
    struct in6_addr addr6;
  } a;

  family = AF_INET;
  rv = inet_pton(family, target, &a);
  if(rv != 1) {
    family = AF_INET6;
    rv = inet_pton(family, target, &a);
    if(rv != 1) {
      return mtev_false;
    }
  }
  return mtev_true;
}
int
noit_check_set_ip(noit_check_t *new_check,
                  const char *ip_str, const char *newname) {
  int8_t family;
  int rv, failed = 0;
  char new_target_ip[INET6_ADDRSTRLEN];
  union {
    struct in_addr addr4;
    struct in6_addr addr6;
  } a;

  family = NOIT_CHECK_PREFER_V6(new_check) ? AF_INET6 : AF_INET;
  rv = inet_pton(family, ip_str, &a);
  if(rv != 1) {
    if (!NOIT_CHECK_SINGLE_RESOLVE(new_check)) {
      family = family == AF_INET ? AF_INET6 : AF_INET;
      rv = inet_pton(family, ip_str, &a);
      if(rv != 1) {
        family = AF_INET;
        memset(&a, 0, sizeof(a));
        failed = -1;
      }
    } else {
      failed = -1;
    }
  }

  new_target_ip[0] = '\0';
  /* target_family and target_addr are not indexes, we can change them
   * whilst in list */
  new_check->target_family = family;
  memcpy(&new_check->target_addr, &a, sizeof(a));
  if(failed == 0) {
    if(inet_ntop(new_check->target_family,
                 &new_check->target_addr,
                 new_target_ip,
                 sizeof(new_target_ip)) == NULL) {
      failed = -1;
      mtevL(check_error, "inet_ntop failed [%s] -> %d\n", ip_str, errno);
    }
  }
  /*
   * new_check->name could be null if this check is being set for the
   * first time.  add_to_list will set it.
   */
  if (new_check->name == NULL ||
      strcmp(new_check->target_ip, new_target_ip) != 0) {
    noit_check_add_to_list(new_check, newname,
                           (failed == 0 && strcmp(new_check->target_ip, new_target_ip)) ? new_target_ip : NULL);
  }

  if(new_check->name == NULL && newname != NULL) {
    mtevAssert(new_check->flags & NP_TRANSIENT);
    new_check->name = strdup(newname);
  }

  return failed;
}
int
noit_check_resolve(noit_check_t *check) {
  uint8_t family_pref = NOIT_CHECK_PREFER_V6(check) ? AF_INET6 : AF_INET;
  char ipaddr[INET6_ADDRSTRLEN];
  if(!NOIT_CHECK_SHOULD_RESOLVE(check)) return 1; /* success, not required */
  noit_check_resolver_remind(check->target);
  if(noit_check_resolver_fetch(check->target, ipaddr, sizeof(ipaddr),
                               family_pref) >= 0) {
    check->flags |= NP_RESOLVED;
    noit_check_set_ip(check, ipaddr, NULL);
    return 0;
  }
  check->flags &= ~NP_RESOLVED;
  return -1;
}

static int
delayed_cluster_set(eventer_t e, int mask, void *closure, struct timeval *now) {
  uuid_t me;
  memcpy(me, closure, UUID_SIZE);
  char uuid_str[37];
  mtev_uuid_unparse_lower(me, uuid_str);
  mtevL(mtev_notice, "Setting global cluster identity to '%s'\n", uuid_str);
  mtev_cluster_set_self(me);
  free(closure);
  return 0;
}

int
noit_check_update(noit_check_t *new_check,
                  const char *target,
                  const char *name,
                  const char *filterset,
                  mtev_hash_table *config,
                  mtev_hash_table **mconfigs,
                  uint32_t period,
                  uint32_t timeout,
                  int32_t transient_min_period,
                  int32_t transient_period_granularity,
                  const char *oncheck,
                  int64_t seq,
                  int flags) {
  int rv = 0;
  char uuid_str[37];
  int mask = NP_DISABLED | NP_UNCONFIG;

  mtevAssert(name);
  mtev_uuid_unparse_lower(new_check->checkid, uuid_str);
  if(!new_check->statistics) new_check->statistics = noit_check_stats_set_calloc();
  if(seq < 0) new_check->config_seq = seq = 0;
  if(new_check->config_seq > seq) {
    mtevL(mtev_error, "noit_check_update[%s] skipped: seq backwards\n", uuid_str);
    return -1;
  }

  /* selfcheck will identify this node in a cluster */
  if(mtev_cluster_enabled() && !strcmp(new_check->module, "selfcheck")) {
    uuid_t cluster_id;
    mtev_cluster_get_self(cluster_id);
    if(mtev_uuid_compare(cluster_id, new_check->checkid)) {
      void *uuid_copy = malloc(UUID_SIZE);
      memcpy(uuid_copy, new_check->checkid, UUID_SIZE);
      eventer_add_in_s_us(delayed_cluster_set, uuid_copy, 0, 0);
    }
  }

  if(NOIT_CHECK_RUNNING(new_check)) {
    char module[256];
    uuid_t id, dummy;
    mtev_uuid_copy(id, new_check->checkid);
    strlcpy(module, new_check->module, sizeof(module));
    noit_poller_deschedule(id, mtev_false, mtev_true);
    return noit_poller_schedule(target, module, name, filterset,
                                config, mconfigs, period, timeout,
                                transient_min_period, transient_period_granularity,
                                oncheck, seq, flags, id, dummy);
  }

  new_check->generation = __config_load_generation;

  /* The polls_by_name is indexed off target/name, we can't change the
   * target whilst in list... if it is there and it is us, bracket our
   * change with a remove and re-add. */
  pthread_mutex_lock(&polls_lock);
  noit_check_t *existing = NULL;
  if(new_check->target && new_check->name) {
    existing = noit_poller_lookup_by_name__nolock(new_check->target, new_check->name);
  }
  if(existing == new_check) {
    mtev_skiplist_remove(polls_by_name, existing, NULL);
  }
  if(new_check->target) free(new_check->target);
  new_check->target = strdup(target);
  if(existing == new_check) {
    mtev_skiplist_insert(polls_by_name, existing);
  }
  pthread_mutex_unlock(&polls_lock);

  // apply resolution flags to check.
  if (flags & NP_PREFER_IPV6)
    new_check->flags |= NP_PREFER_IPV6;
  else
    new_check->flags &= ~NP_PREFER_IPV6;
  if (flags & NP_SINGLE_RESOLVE)
    new_check->flags |= NP_SINGLE_RESOLVE;
  else
    new_check->flags &= ~NP_SINGLE_RESOLVE;
  if (flags & NP_RESOLVE)
    new_check->flags |= NP_RESOLVE;
  else
    new_check->flags &= ~NP_RESOLVE;

  /* This sets both the name and the target_addr */
  if(noit_check_set_ip(new_check, target, name)) {
    mtev_boolean should_resolve;
    mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
    const char *key, *value;
    int klen;
    char* extension = strrchr(target, '.');
    new_check->flags |= NP_RESOLVE;
    new_check->flags &= ~NP_RESOLVED;
    /* If we match any of the extensions we're supposed to ignore,
     * don't resolve */
    if (extension && (strlen(extension) > 1)) {
      while(mtev_hash_next(&dns_ignore_list, &iter, &key, &klen, (void**)&value)) {
        if ((!strcmp("true", value)) && (!strcmp(extension+1, key))) {
            new_check->flags &= ~NP_RESOLVE;
            break;
        }
      }
    }
    if(noit_check_should_resolve_targets(&should_resolve) && !should_resolve)
      flags |= NP_DISABLED | NP_UNCONFIG;
    noit_check_resolve(new_check);
  }

  if(new_check->filterset) free(new_check->filterset);
  new_check->filterset = filterset ? strdup(filterset): NULL;

  new_check->tagset = NULL;
  if(config != NULL) {
    mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
    const char *k;
    int klen;
    void *data;
    if(new_check->config) mtev_hash_delete_all(new_check->config, free, free);
    else {
      new_check->config = calloc(1, sizeof(*new_check->config));
      mtev_hash_init_locks(new_check->config, MTEV_HASH_DEFAULT_SIZE, MTEV_HASH_LOCK_MODE_MUTEX);
    }
    while(mtev_hash_next(config, &iter, &k, &klen, &data)) {
      char *value = strdup((char *)data);
      mtev_hash_store(new_check->config, strdup(k), klen, value);
      if(!strcmp(k, "tagset")) {
        new_check->tagset = value;
      }
    }
  }
  if(mconfigs != NULL) {
    int i;
    for(i=0; i<reg_module_id; i++) {
      mtev_hash_table *t;
      if(NULL != (t = noit_check_get_module_config(new_check, i))) {
        noit_check_set_module_config(new_check, i, NULL);
        mtev_hash_destroy(t, free, free);
        free(t);
      }
      if(mconfigs[i]) {
        mtev_hash_table *t = calloc(1, sizeof(*new_check->config));
        mtev_hash_init_locks(t, MTEV_HASH_DEFAULT_SIZE, MTEV_HASH_LOCK_MODE_MUTEX);
        mtev_hash_merge_as_dict(t, mconfigs[i]);
        noit_check_set_module_config(new_check, i, t);
      }
    }
  }
  if(new_check->oncheck) free(new_check->oncheck);
  new_check->oncheck = oncheck ? strdup(oncheck) : NULL;
  if(new_check->oncheck) system_needs_causality = mtev_true;
  new_check->period = period;
  new_check->timeout = timeout;
  new_check->transient_min_period = transient_min_period;
  new_check->transient_period_granularity = transient_period_granularity;
  new_check->config_seq = seq;
  noit_cluster_mark_check_changed(new_check, NULL);

  /* Unset what could be set.. then set what should be set */
  new_check->flags = (new_check->flags & ~mask) | flags;

  check_config_fixup_hook_invoke(new_check);

  if((new_check->flags & NP_TRANSIENT) == 0)
    noit_check_activate(new_check);

  rv = noit_check_add_to_list(new_check, NULL, NULL);
  if(rv == 0) {
    check_updated_hook_invoke(new_check);
  }
  return rv;
}
int
noit_poller_schedule(const char *target,
                     const char *module,
                     const char *name,
                     const char *filterset,
                     mtev_hash_table *config,
                     mtev_hash_table **mconfigs,
                     uint32_t period,
                     uint32_t timeout,
                     int32_t transient_min_period,
                     int32_t transient_period_granularity,
                     const char *oncheck,
                     int64_t seq,
                     int flags,
                     uuid_t in,
                     uuid_t out) {
  noit_check_t *new_check = calloc(1, sizeof(*new_check));
  new_check->ref_cnt = 1;

  pthread_rwlock_init(&new_check->feeds_lock, NULL);

  /* The module and the UUID can never be changed */
  new_check->module = strdup(module);
  if(mtev_uuid_is_null(in))
    mtev_uuid_generate(new_check->checkid);
  else
    mtev_uuid_copy(new_check->checkid, in);

  new_check->statistics = noit_check_stats_set_calloc();
  noit_check_update(new_check, target, name, filterset, config, mconfigs,
                    period, timeout, transient_min_period,
                    transient_period_granularity, oncheck, seq, flags);
  mtev_hash_replace(&polls,
                    (char *)new_check->checkid, UUID_SIZE,
                    new_check, NULL, (NoitHashFreeFunc)noit_poller_free_check);
  mtev_uuid_copy(out, new_check->checkid);
  noit_check_log_check(new_check);

  return 0;
}

/* A quick little list of recycleable checks.  This list never really
 * grows large, so no sense in thinking too hard about the algorithmic
 * complexity.
 */
struct _checker_rcb {
  noit_check_t *checker;
  struct _checker_rcb *next;
};
static struct _checker_rcb *checker_rcb = NULL;
static void recycle_check(noit_check_t *checker, mtev_boolean has_lock) {
  struct _checker_rcb *n = malloc(sizeof(*n));
  if(!has_lock) pthread_mutex_lock(&recycling_lock);
  n->checker = checker;
  n->next = checker_rcb;
  checker_rcb = n;
  if(!has_lock) pthread_mutex_unlock(&recycling_lock);
}
void
noit_poller_free_check_internal(noit_check_t *checker, mtev_boolean has_lock) {
  noit_module_t *mod = noit_module_lookup(checker->module);
  if(mod && mod->cleanup) mod->cleanup(mod, checker);
  else if(checker->closure) {
    free(checker->closure);
    checker->closure = NULL;
  }

  if (checker->flags & NP_PASSIVE_COLLECTION) {
    struct timeval current_time;
    mtev_gettimeofday(&current_time, NULL);
    if (checker->last_fire_time.tv_sec == 0) {
      memcpy(&checker->last_fire_time, &current_time, sizeof(struct timeval));
    }
    /* If NP_RUNNING is set for some reason or we've fired recently, recycle
     * the check.... we don't want to free it */
    if ((checker->flags & NP_RUNNING) ||
        (sub_timeval_ms(current_time,checker->last_fire_time) < (checker->period*2))) {
      recycle_check(checker, has_lock);
      return;
    }
  }
  else if(checker->flags & NP_RUNNING) {
    /* If the check is running, don't free it - will clean it up later */
    recycle_check(checker, has_lock);
    return;
  }

  noit_check_t *existing = noit_poller_lookup_by_name__nolock(checker->target, checker->name);
  mtevAssert(existing != checker);
  noit_check_deref(checker);
  noit_check_deref(existing);
}
void
noit_poller_free_check(noit_check_t *checker) {
  checker->flags |= NP_KILLED;
  noit_poller_free_check_internal(checker, mtev_false);
}

struct check_remove_todo {
  uuid_t id;
  struct check_remove_todo *next;
};
static void
check_recycle_bin_processor_internal_cleanup_xml(struct check_remove_todo *head) {
  while(head) {
    char idstr[UUID_STR_LEN+1], xpath[1024];
    mtev_uuid_unparse_lower(head->id, idstr);
    mtev_conf_section_t lock = mtev_conf_get_section_write(MTEV_CONF_ROOT, "/noit/checks");
    if(noit_check_xpath(xpath, sizeof(xpath), "/", idstr) > 0) {
      xmlXPathContextPtr xpath_ctxt = NULL;
      xmlXPathObjectPtr pobj = NULL;
      mtev_conf_xml_xpath(NULL, &xpath_ctxt);
      pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
      if(pobj && pobj->type == XPATH_NODESET && !xmlXPathNodeSetIsEmpty(pobj->nodesetval) &&
         xmlXPathNodeSetGetLength(pobj->nodesetval) >= 1) {
        xmlNodePtr node = xmlXPathNodeSetItem(pobj->nodesetval, 0);
        mtev_conf_section_t section = mtev_conf_section_from_xmlnodeptr(node);
        char *delstring = NULL;
        mtev_boolean remove = mtev_false;
        if(mtev_conf_get_string(section, "self::node()/@deleted", &delstring)) {
          if ((!strcmp(delstring, "deleted")) || (!strcmp(delstring, "true"))) {
            remove = mtev_true;
          }
          free(delstring);
        }
        if (remove == mtev_true) {
          CONF_REMOVE(section);
          xmlUnlinkNode(node);
          mtev_conf_mark_changed();
        }
        else {
          mtevL(mtev_error, "check_recycle_bin_processor_internal_cleanup_xml: check %s has been restored, not deleting\n", idstr);
        }
      }
      if(pobj) xmlXPathFreeObject(pobj);
    }
    mtev_conf_release_section_write(lock);
    struct check_remove_todo *tofree = head;
    head = head->next;
    free(tofree);
  }
  (void)mtev_conf_write_file(NULL);
}
static void
check_recycle_bin_processor_internal_cleanup_lmdb(struct check_remove_todo *head) {
  while(head) {
    if (noit_check_lmdb_remove_check_from_db(head->id, mtev_false)) {
      char id_str[UUID_STR_LEN + 1];
      mtev_uuid_unparse_lower(head->id, id_str);
      mtevL(mtev_error, "check_recycle_bin_processor_internal_cleanup_lmdb: check %s has been restored, not deleting\n", id_str);
    }
    struct check_remove_todo *tofree = head;
    head = head->next;
    free(tofree);
  }
}
static void
check_recycle_bin_processor_internal() {
  struct check_remove_todo *head = NULL;
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  struct _checker_rcb *prev = NULL, *curr = NULL;
  mtevL(check_debug, "Scanning checks for cluster sync\n");
  pthread_mutex_lock(&polls_lock);
  while(mtev_hash_adv(&polls, &iter)) {
    noit_check_t *check = (noit_check_t *)iter.value.ptr;
    if(NOIT_CHECK_DELETED(check) && !noit_cluster_checkid_replication_pending(check->checkid)) {
      char idstr[UUID_STR_LEN+1];
      mtev_uuid_unparse_lower(check->checkid, idstr);

      struct check_remove_todo *todo = calloc(1, sizeof(*todo));
      mtev_uuid_copy(todo->id, check->checkid);
      todo->next = head;
      head = todo;
      mtevL(check_debug, "cluster delete of %s complete.\n", idstr);
      /* Set the config_seq to zero so it can be truly descheduled */
      check->config_seq = 0;
      noit_poller_deschedule(check->checkid, mtev_true, mtev_false);
    }
  }
  pthread_mutex_unlock(&polls_lock);

  if (noit_check_get_lmdb_instance()) {
    check_recycle_bin_processor_internal_cleanup_lmdb(head);
  }
  else {
    check_recycle_bin_processor_internal_cleanup_xml(head);
  }

  mtevL(check_debug, "Scanning check recycle bin\n");
  pthread_mutex_lock(&recycling_lock);
  curr = checker_rcb;
  while(curr) {
    noit_check_t *check = curr->checker;
    mtev_boolean free_check = mtev_false;
    if (check->flags & NP_PASSIVE_COLLECTION) {
      struct timeval current_time;
      mtev_gettimeofday(&current_time, NULL);
      if ((!(check->flags & NP_RUNNING)) &&
          (sub_timeval_ms(current_time,check->last_fire_time) >= (check->period*2))) {
        free_check = mtev_true;
      }
    }
    else if(!(curr->checker->flags & NP_RUNNING)) {
      free_check = mtev_true;
    }

    if (free_check == mtev_true) {
      mtevL(check_debug, "0x%p: Check is ready to free.\n", check);
      noit_poller_free_check_internal(curr->checker, mtev_true);
      if(prev) prev->next = curr->next;
      else checker_rcb = curr->next;
      free(curr);
      curr = prev ? prev->next : checker_rcb;
    }
    else {
      prev = curr;
      curr = curr->next;
    }
  }
  pthread_mutex_unlock(&recycling_lock);
}

static int
check_recycle_bin_processor(eventer_t e, int mask, void *closure,
                            struct timeval *now) {
  check_recycle_bin_processor_internal();
  eventer_add_in_s_us(check_recycle_bin_processor, NULL, check_recycle_period/1000, 1000*(check_recycle_period%1000));
  return 0;
}

int
noit_poller_deschedule(uuid_t in, mtev_boolean log, mtev_boolean readding) {
  void *vcheck;
  noit_check_t *checker;
  if(mtev_hash_retrieve(&polls,
                        (char *)in, UUID_SIZE,
                        &vcheck) == 0) {
    return -1;
  }
  checker = (noit_check_t *)vcheck;
  checker->flags |= (NP_DISABLED|NP_KILLED|NP_DELETED);

  if(log) noit_check_log_delete(checker);

  if(checker->config_seq == 0 || readding) {
    mtevAssert(mtev_skiplist_remove(polls_by_name, checker, NULL));
    mtevAssert(mtev_hash_delete(&polls, (char *)in, UUID_SIZE, NULL, NULL));
  }

  check_deleted_hook_invoke(checker);

  if(checker->config_seq == 0 || readding) {
    noit_poller_free_check(checker);
    return 1;
  }

  return 0;
}

noit_check_t *
noit_poller_lookup(uuid_t in) {
  noit_check_t *check;
  pthread_mutex_lock(&polls_lock);
  check = noit_poller_lookup__nolock(in);
  pthread_mutex_unlock(&polls_lock);
  return check;
}
noit_check_t *
noit_poller_lookup_by_name(char *target, char *name) {
  noit_check_t *check;
  pthread_mutex_lock(&polls_lock);
  check = noit_poller_lookup_by_name__nolock(target,name);
  pthread_mutex_unlock(&polls_lock);
  return check;
}
int
noit_poller_target_ip_do(const char *target_ip,
                         int (*f)(noit_check_t *, void *),
                         void *closure) {
  int i, count = 0, todo_count = 0;
  noit_check_t pivot;
  mtev_skiplist *tlist;
  mtev_skiplist_node *next;
  noit_check_t *todo_onstack[8192];
  noit_check_t **todo = todo_onstack;

  if(!polls_by_name) return 0;

  tlist = mtev_skiplist_find(mtev_skiplist_indexes(polls_by_name),
                             __check_target_ip_compare, NULL);

  pthread_mutex_lock(&polls_lock);
  /* First pass to count */
  memset(&pivot, 0, sizeof(pivot));
  strlcpy(pivot.target_ip, (char*)target_ip, sizeof(pivot.target_ip));
  pivot.name = "";
  pivot.target = "";
  mtev_skiplist_find_neighbors(tlist, &pivot, NULL, NULL, &next);
  while(next && mtev_skiplist_data(next)) {
    noit_check_t *check = mtev_skiplist_data(next);
    if(strcmp(check->target_ip, target_ip)) break;
    todo_count++;
    mtev_skiplist_next(tlist, &next);
  }

  if(todo_count > 8192) todo = malloc(todo_count * sizeof(*todo));

  memset(&pivot, 0, sizeof(pivot));
  strlcpy(pivot.target_ip, (char*)target_ip, sizeof(pivot.target_ip));
  pivot.name = "";
  pivot.target = "";
  mtev_skiplist_find_neighbors(tlist, &pivot, NULL, NULL, &next);
  while(next && mtev_skiplist_data(next)) {
    noit_check_t *check = mtev_skiplist_data(next);
    if(strcmp(check->target_ip, target_ip)) break;
    if(count < todo_count) todo[count++] = check;
    mtev_skiplist_next(tlist, &next);
  }
  pthread_mutex_unlock(&polls_lock);

  todo_count = count;
  count = 0;
  for(i=0;i<todo_count;i++)
    count += f(todo[i],closure);

  if(todo != todo_onstack) free(todo);
  return count;
}
int
noit_poller_target_do(const char *target, int (*f)(noit_check_t *, void *),
                      void *closure) {
  int i, todo_count = 0, count = 0;
  noit_check_t pivot;
  mtev_skiplist *tlist;
  mtev_skiplist_node *next;
  noit_check_t *todo_onstack[8192];
  noit_check_t **todo = todo_onstack;

  if(!polls_by_name) return 0;

  tlist = mtev_skiplist_find(mtev_skiplist_indexes(polls_by_name),
                             __check_target_compare, NULL);

  pthread_mutex_lock(&polls_lock);
  memset(&pivot, 0, sizeof(pivot));
  pivot.name = "";
  pivot.target = (char *)target;
  mtev_skiplist_find_neighbors(tlist, &pivot, NULL, NULL, &next);
  while(next && mtev_skiplist_data(next)) {
    noit_check_t *check = mtev_skiplist_data(next);
    if(strcmp(check->target, target)) break;
    todo_count++;
    mtev_skiplist_next(tlist, &next);
  }

  if(todo_count > 8192) todo = malloc(todo_count * sizeof(*todo));

  memset(&pivot, 0, sizeof(pivot));
  pivot.name = "";
  pivot.target = (char *)target;
  mtev_skiplist_find_neighbors(tlist, &pivot, NULL, NULL, &next);
  while(next && mtev_skiplist_data(next)) {
    noit_check_t *check = mtev_skiplist_data(next);
    if(strcmp(check->target, target)) break;
    if(count < todo_count) todo[count++] = check;
    mtev_skiplist_next(tlist, &next);
  }
  pthread_mutex_unlock(&polls_lock);

  todo_count = count;
  count = 0;
  for(i=0;i<todo_count;i++)
    count += f(todo[i],closure);

  if(todo != todo_onstack) free(todo);
  return count;
}

int
noit_poller_do(int (*f)(noit_check_t *, void *),
               void *closure) {
  mtev_skiplist_node *iter;
  int i, count = 0, max_count = 0;
  noit_check_t **todo;

  if(!polls_by_name) return 0;

  max_count = mtev_skiplist_size(polls_by_name);
  if(max_count == 0) return 0;

  todo = malloc(max_count * sizeof(*todo));

  pthread_mutex_lock(&polls_lock);
  for(iter = mtev_skiplist_getlist(polls_by_name); iter;
      mtev_skiplist_next(polls_by_name, &iter)) {
    if(count < max_count) todo[count++] = (noit_check_t *)mtev_skiplist_data(iter);
  }
  pthread_mutex_unlock(&polls_lock);

  max_count = count;
  count = 0;
  for(i=0;i<max_count;i++)
    count += f(todo[i], closure);
  free(todo);
  return count;
}

struct ip_module_collector_crutch {
  noit_check_t **array;
  const char *module;
  int idx;
  int allocd;
};
static int ip_module_collector(noit_check_t *check, void *cl) {
  struct ip_module_collector_crutch *c = cl;
  if(c->idx >= c->allocd) return 0;
  if(strcmp(check->module, c->module)) return 0;
  c->array[c->idx++] = noit_check_ref(check);
  return 1;
}
int
noit_poller_lookup_by_ip_module(const char *ip, const char *mod,
                                noit_check_t **checks, int nchecks) {
  struct ip_module_collector_crutch crutch;
  crutch.array = checks;
  crutch.allocd = nchecks;
  crutch.idx = 0;
  crutch.module = mod;
  return noit_poller_target_ip_do(ip, ip_module_collector, &crutch);
}
int
noit_poller_lookup_by_module(const char *ip, const char *mod,
                             noit_check_t **checks, int nchecks) {
  struct ip_module_collector_crutch crutch;
  crutch.array = checks;
  crutch.allocd = nchecks;
  crutch.idx = 0;
  crutch.module = mod;
  return noit_poller_target_do(ip, ip_module_collector, &crutch);
}

int
noit_check_xpath_check(char *xpath, int len,
                  noit_check_t *check) {
  char uuid_str[UUID_PRINTABLE_STRING_LENGTH];
  mtev_uuid_unparse_lower(check->checkid, uuid_str);
  return noit_check_xpath(xpath, len, "/", uuid_str);
}

int
noit_check_xpath(char *xpath, int len,
                 const char *base, const char *arg) {
  uuid_t checkid;
  int base_trailing_slash;
  char argcopy[1024], *target, *module, *name;

  base_trailing_slash = (base[strlen(base)-1] == '/');
  xpath[0] = '\0';
  argcopy[0] = '\0';
  if(arg) strlcpy(argcopy, arg, sizeof(argcopy));

  if(mtev_uuid_parse(argcopy, checkid) == 0) {
    /* If they kill by uuid, we'll seek and destroy -- find it anywhere */
    snprintf(xpath, len, "/noit/checks%s%s/check[@uuid=\"%s\"]",
             base, base_trailing_slash ? "" : "/", argcopy);
  }
  else if((module = strchr(argcopy, '`')) != NULL) {
    noit_check_t *check;
    char uuid_str[37];
    target = argcopy;
    *module++ = '\0';
    if((name = strchr(module+1, '`')) == NULL)
      name = module;
    else
      name++;
    check = noit_poller_lookup_by_name(target, name);
    if(!check) {
      return -1;
    }
    mtev_uuid_unparse_lower(check->checkid, uuid_str);
    noit_check_deref(check);
    snprintf(xpath, len, "/noit/checks%s%s/check[@uuid=\"%s\"]",
             base, base_trailing_slash ? "" : "/", uuid_str);
  }
  return strlen(xpath);
}

char*
noit_check_path(noit_check_t *check) {
  char xpath[1024];
  mtev_conf_section_t section;
  xmlNodePtr node, parent;
  mtev_prependable_str_buff_t *path;
  char *path_str;
  int path_str_len;

  noit_check_xpath_check(xpath, sizeof(xpath), check);
  section = mtev_conf_get_section_read(MTEV_CONF_ROOT, xpath);

  if(mtev_conf_section_is_empty(section)) {
    mtev_conf_release_section_read(section);
    return NULL;
  }

  node = mtev_conf_section_to_xmlnodeptr(section);
  path = mtev_prepend_str_alloc();
  mtev_prepend_str(path, "/", 1);
  parent = node->parent;
  while(parent && strcmp((char*)parent->name, CHECKS_XPATH_PARENT)) {
    mtev_prepend_str(path, (char*)parent->name, strlen((char*)parent->name));
    mtev_prepend_str(path, "/", 1);
    parent = parent->parent;
  }

  path_str_len = mtev_prepend_strlen(path);
  path_str = malloc(path_str_len+1);
  memcpy(path_str, path->string, path_str_len);
  mtev_prepend_str_free(path);
  path_str[path_str_len] = '\0';
  mtev_conf_release_section_read(section);
  return path_str;
}

static int
bad_check_initiate(noit_module_t *self, noit_check_t *check,
                   int once, noit_check_t *cause) {
  /* self is likely null here -- why it is bad, in fact */
  /* this is only suitable to call in one-offs */
  struct timeval now;
  stats_t *inp;
  char buff[256];
  if(!once) return -1;
  if(!check) return -1;
  mtevAssert(!(check->flags & NP_RUNNING));
  noit_check_begin(check);
  inp = noit_check_get_stats_inprogress(check);
  mtev_gettimeofday(&now, NULL);
  noit_check_stats_whence(inp, &now);
  snprintf(buff, sizeof(buff), "check[%s] implementation offline",
           check->module);
  noit_check_stats_status(inp, buff);
  noit_check_set_stats(check);
  noit_check_end(check);
  return 0;
}
noit_check_t *noit_check_ref(noit_check_t *check) {
  ck_pr_add_int(&check->ref_cnt, 1);
  return check;
}
void noit_check_deref(noit_check_t *check) {
  if (check) {
    bool done;

    ck_pr_dec_int_zero(&check->ref_cnt, &done);

    if (done) {
      noit_check_safe_release(check);
    }
  }
}
void noit_check_begin(noit_check_t *check) {
  const char *force_str = NULL;
  bool force = false;
  mtevAssert(!NOIT_CHECK_RUNNING(check));
  check->flags |= NP_RUNNING;
  Zipkin_Span *active = mtev_zipkin_active_span(NULL);
  int64_t trace_id, parent_id,
          *trace_id_ptr = NULL, *parent_id_ptr = NULL;

  if(active) {
    bool has_parent = mtev_zipkin_span_get_ids(active, &trace_id, &parent_id, NULL);
    trace_id_ptr = &trace_id;
    if(has_parent)
      parent_id_ptr = &parent_id;
  }
  (void)mtev_hash_retr_str(check->config, "_zipkin_trace", strlen("_zipkin_trace"), &force_str);
  if(force_str) {
    if(strcmp(force_str, "false") && strcmp(force_str, "off")) force = true;
  }
  check->span = mtev_zipkin_span_new(trace_id_ptr, parent_id_ptr, NULL,
                                     check->name, true, false, force);
  if(check->span) {
    mtev_zipkin_span_attach_logs(check->span, true);
    if(!active) {
      eventer_t e = eventer_get_this_event();
      mtev_zipkin_event_trace_level_t lvl = ZIPKIN_TRACE_EVENT_CALLBACKS;
      mtev_zipkin_attach_to_eventer(e, check->span, false, &lvl);
    }
    mtev_zipkin_span_annotate(check->span, NULL, "check_begin", false);
    char id_str[UUID_STR_LEN+1];
    mtev_uuid_unparse_lower(check->checkid, id_str);
    mtev_zipkin_span_bannotate_str(check->span, "check.uuid", false, id_str, true);
    mtev_zipkin_span_bannotate_str(check->span, "check.module", false, check->module, true);
    mtev_zipkin_span_bannotate_str(check->span, "check.target", false, check->target, true);
    mtev_zipkin_span_bannotate_i32(check->span, "check.period", false, check->period);
  }
}
void noit_check_end(noit_check_t *check) {
  mtevAssert(NOIT_CHECK_RUNNING(check));
  check->flags &= ~NP_RUNNING;
  if(check->span) {
    mtev_zipkin_span_annotate(check->span, NULL, "check_end", false);
    mtev_zipkin_span_publish(check->span);
    check->span = NULL;
  }
}
void
noit_check_stats_clear(noit_check_t *check, stats_t *s) {
  memset(s, 0, sizeof(*s));
  s->state = NP_UNKNOWN;
  s->available = NP_UNKNOWN;
}

static void
__stats_add_metric(stats_t *newstate, metric_t *m) {
  mtev_hash_replace(&newstate->metrics, m->metric_name, strlen(m->metric_name),
                    m, NULL, (void (*)(void *))mtev_memory_safe_free);
}

mtev_boolean
noit_stats_mark_metric_logged(stats_t *newstate, metric_t *m, mtev_boolean create) {
  void *vm;
  if(mtev_hash_retrieve(&newstate->metrics,
      m->metric_name, strlen(m->metric_name), &vm)) {
    ((metric_t *)vm)->logged = mtev_true;
    return mtev_false;
  } else if(create) {
    m->logged = mtev_true;
    mtev_hash_replace(&newstate->metrics, m->metric_name, strlen(m->metric_name),
                        m, NULL, (void (*)(void *))mtev_memory_safe_free);
    return mtev_true;
  }
  return mtev_false;
}

static size_t
noit_metric_sizes(metric_type_t type, const void *value) {
  switch(type) {
    case METRIC_INT32:
    case METRIC_UINT32:
      return sizeof(int32_t);
    case METRIC_INT64:
    case METRIC_UINT64:
      return sizeof(int64_t);
    case METRIC_DOUBLE:
      return sizeof(double);
    case METRIC_STRING: {
      const char *lf = strchr(value, '\n');
      int len = lf ? lf - (const char *)value + 1 : strlen((char*)value) + 1;
      return ((len >= text_size_limit) ? text_size_limit+1 : len);
    }
    case METRIC_HISTOGRAM:
    case METRIC_HISTOGRAM_CUMULATIVE:
    case METRIC_ABSENT:
    case METRIC_GUESS:
      break;
  }
  mtevAssert(type != type);
  return 0;
}
static metric_type_t
noit_metric_guess_type(const char *s, void **replacement) {
  char *copy, *cp, *trailer, *rpl;
  int negative = 0;
  metric_type_t type = METRIC_STRING;

  if(!s) return METRIC_GUESS;
  copy = cp = strdup(s);

  /* TRIM the string */
  while(*cp && isspace(*cp)) cp++; /* ltrim */
  s = cp; /* found a good starting point */
  while(*cp) cp++; /* advance to \0 */
  cp--; /* back up one */
  while(cp > s && isspace(*cp)) *cp-- = '\0'; /* rtrim */

  /* Find the first space */
  cp = (char *)s;
  while(*cp && !isspace(*cp)) cp++;
  trailer = cp;
  cp--; /* backup one */
  if(cp > s && *cp == '%') *cp-- = '\0'; /* chop a last % is there is one */

  while(*trailer && isspace(*trailer)) *trailer++ = '\0'; /* rtrim */

  /* string was       '  -1.23e-01%  inodes used  ' */
  /* copy is (~ = \0) '  -1.23e-01~  inodes used~~' */
  /*                     ^           ^              */
  /*                     s           trailer        */

  /* So, the trailer must not contain numbers */
  while(*trailer) { if(isdigit(*trailer)) goto notanumber; trailer++; }

  /* And the 's' must be of the form:
   *  0) may start with a sign [-+]?
   *  1) [1-9][0-9]*
   *  2) [0]?.[0-9]+
   *  3) 0
   *  4) [1-9][0-9]*.[0-9]+
   *  5) all of the above ending with e[+-][0-9]+
   */
   rpl = (char *)s;
   /* CASE 0 */
   if(s[0] == '-' || s[0] == '+') {
     if(s[0] == '-') negative = 1;
     s++;
   }

   if(s[0] == '.') goto decimal; /* CASE 2 */
   if(s[0] == '0') { /* CASE 2 & 3 */
     s++;
     if(!s[0]) goto scanint; /* CASE 3 */
     if(s[0] == '.') goto decimal; /* CASE 2 */
     goto notanumber;
   }
   if(s[0] >= '1' && s[0] <= '9') { /* CASE 1 & 4 */
     s++;
     while(isdigit(s[0])) s++; /* CASE 1 & 4 */
     if(!s[0]) goto scanint; /* CASE 1 */
     if(s[0] == '.') goto decimal; /* CASE 4 */
     goto notanumber;
   }
   /* Not case 1,2,3,4 */
   goto notanumber;

  decimal:
   s++;
   if(!isdigit(s[0])) goto notanumber;
   s++;
   while(isdigit(s[0])) s++;
   if(!s[0]) goto scandouble;
   if(s[0] == 'e' || s[0] == 'E') goto exponent; /* CASE 5 */
   goto notanumber;

  exponent:
   s++;
   if(s[0] != '-' && s[0] != '+') goto notanumber;
   s++;
   if(!isdigit(s[0])) goto notanumber;
   s++;
   while(isdigit(s[0])) s++;
   if(!s[0]) goto scandouble;
   goto notanumber;

 scanint:
   if(negative) {
     int64_t *v;
     v = malloc(sizeof(*v));
     *v = strtoll(rpl, NULL, 10);
     *replacement = v;
     type = METRIC_INT64;
     goto alldone;
   }
   else {
     uint64_t *v;
     v = malloc(sizeof(*v));
     *v = strtoull(rpl, NULL, 10);
     *replacement = v;
     type = METRIC_UINT64;
     goto alldone;
   }
 scandouble:
   {
     double *v;
     v = malloc(sizeof(*v));
     *v = strtod(rpl, NULL);
     *replacement = v;
     type = METRIC_DOUBLE;
     goto alldone;
   }

 alldone:
 notanumber:
  free(copy);
  return type;
}

static int
noit_stats_populate_metric_with_tagset(metric_t *m, const char *name, metric_type_t type,
                           const void *value) {
  void *replacement = NULL;

  /* If we are passed a null name, we want to quit populating the metric...
   * no reason we should ever have a null metric name */
  if (!name) {
    return -1;
  }

  m->metric_name = strdup(name);

  if(type == METRIC_GUESS)
    type = noit_metric_guess_type((char *)value, &replacement);
  if(type == METRIC_GUESS) return -1;

  m->metric_type = type;

  if(replacement)
    m->metric_value.vp = replacement;
  else if(value) {
    size_t len;
    len = noit_metric_sizes(type, value);
    m->metric_value.vp = malloc(len);
    memcpy(m->metric_value.vp, value, len);
    if (type == METRIC_STRING) {
      m->metric_value.s[len-1] = 0;
    }
  }
  else m->metric_value.vp = NULL;
  return 0;
}

metric_t *
noit_stats_get_metric(noit_check_t *check,
                      stats_t *newstate, const char *name) {
  void *v;
  char name_copy[MAX_METRIC_TAGGED_NAME];
  if(strlen(name) > sizeof(name_copy)-1) return NULL;
  if(noit_metric_canonicalize(name, strlen(name),
                              name_copy, sizeof(name_copy), mtev_true) <= 0)
    return NULL;
  if(newstate == NULL)
    newstate = stats_inprogress(check);
  if(mtev_hash_retrieve(&newstate->metrics, name_copy, strlen(name_copy), &v))
    return (metric_t *)v;
  return NULL;
}

metric_t *
noit_stats_get_last_metric(noit_check_t *check, const char *name) {
  metric_t *m;
  m = noit_stats_get_metric(check, NULL, name);
  if(m) return m;
  m = noit_stats_get_metric(check, noit_check_get_stats_current(check), name);
  if(m) return m;
  m = noit_stats_get_metric(check, noit_check_get_stats_previous(check), name);
  return m;
}

/* TODO: Actually use timestamp, does nothing yet - timestamp
 * is included here to ease future transition to using it */
void
noit_stats_set_metric_with_timestamp(noit_check_t *check,
                      const char *name, metric_type_t type,
                      const void *value,
                      struct timeval *timestamp) {
  stats_t *c;
  char tagged_name[MAX_METRIC_TAGGED_NAME];
  if(noit_check_build_tag_extended_name(tagged_name, sizeof(tagged_name), name, check) <= 0) {
    return;
  }

  metric_t *m = mtev_memory_safe_malloc_cleanup(sizeof(*m), noit_check_safe_free_metric);
  memset(m, 0, sizeof(*m));

  if(noit_stats_populate_metric_with_tagset(m, tagged_name, type, value)) {
    mtev_memory_safe_free(m);
    return;
  }
  noit_check_metric_count_add(1);
  c = noit_check_get_stats_inprogress(check);
  if(check_stats_set_metric_hook_invoke(check, c, m) == MTEV_HOOK_CONTINUE) {
    __stats_add_metric(c, m);
  } else {
    mtev_memory_safe_free(m);
  }
}

static void
noit_stats_set_metric_with_timestamp_ex_f(void *vcheck,
                      const char *name, metric_type_t type,
                      const void *value,
                      struct timeval *timestamp) {
  noit_stats_set_metric_with_timestamp((noit_check_t *)vcheck, name, type, value, timestamp);
}

void
noit_stats_set_metric(noit_check_t *check,
                      const char *name, metric_type_t type,
                      const void *value) {
  noit_stats_set_metric_with_timestamp(check, name, type, value, NULL);
}

void
noit_stats_set_metric_histogram(noit_check_t *check,
                                const char *name_raw, mtev_boolean cumulative,
                                metric_type_t type, void *value, uint64_t count) {
  if(count == 0) return;
  if(!check_stats_set_metric_histogram_hook_exists()) return;

  void *replacement = NULL;
  char tagged_name[MAX_METRIC_TAGGED_NAME];
  if(noit_check_build_tag_extended_name(tagged_name, sizeof(tagged_name), name_raw, check) <= 0)
    return;
  metric_type_t gtype = type;
  if(gtype == METRIC_GUESS) gtype = noit_metric_guess_type((char *)value, &replacement);
  if(gtype == METRIC_GUESS) return;

  metric_t m_onstack = { .metric_name = tagged_name,
                         .metric_type = gtype,
                         .metric_value = { .vp = replacement ? replacement : value } };
  check_stats_set_metric_histogram_hook_invoke(check, cumulative, &m_onstack, count);
  free(replacement);
}

static char *ltrim(char *s)
{
  const char *seps = "\t\n\r \"";
  size_t totrim = strspn(s, seps);
  if (totrim > 0) {
    size_t len = strlen(s);
    if (totrim == len) {
      s[0] = '\0';
    }
    else {
      memmove(s, s + totrim, len + 1 - totrim);
    }
  }
  return s;
}

static char *rtrim(char *s)
{
  int i;
  const char *seps = "\t\n\r \"";
  i = strlen(s) - 1;
  while (i >= 0 && strchr(seps, s[i]) != NULL) {
    s[i] = '\0';
    i--;
  }
  return s;
}

static char *trim(char *s)
{
  return ltrim(rtrim(s));
}

void
noit_metric_coerce_ex_with_timestamp(noit_check_t *check,
                             const char *name_raw, metric_type_t t,
                             const char *v,
                             struct timeval *timestamp,
                             void (*f)(void *, const char *, metric_type_t, const void *v, struct timeval *),
                             void *closure, stats_t *stats) {
  void *replacement = NULL;
  if(strlen(name_raw) > MAX_METRIC_TAGGED_NAME-1) return;

  char tagged_name[MAX_METRIC_TAGGED_NAME];
  if(noit_check_build_tag_extended_name(tagged_name, sizeof(tagged_name), name_raw, check) <= 0)
    return;

  char *endptr;
  if(v == NULL) {
   bogus:
    if(stats) check_stats_set_metric_coerce_hook_invoke(check, stats, tagged_name, t, v, mtev_false);
    f(closure, tagged_name, t, NULL, timestamp);
    return;
  }

  /* It's possible that the incoming value is enclosed in quotation marks.
   * This is sent commonly over SNMP where the dumb ass switches send back 
   * a STRING type but then go the extra mile and enclose the string
   * in quotation marks.  So instead of getting something like: "0.56"
   * you get something like: "\"0.56\"" which is beyond stupid, but alas.
   * 
   * This little snippet trims any enclosing quotation marks and spaces in the incoming 
   * `v` before coercing it
   */
  char copy_v[256] = {0}; // intentionally larger than anything we could coerce anyway
  strlcpy(copy_v, v, 256);
  const char *safe_v = trim(copy_v);

  switch(t) {
    case METRIC_STRING:
      f(closure, tagged_name, t, safe_v, timestamp);
      break;
    case METRIC_INT32:
    {
      int32_t val;
      val = strtol(safe_v, &endptr, 10);
      if(endptr == safe_v) goto bogus;
      f(closure, tagged_name, t, &val, timestamp);
      break;
    }
    case METRIC_UINT32:
    {
      uint32_t val;
      val = strtoul(safe_v, &endptr, 10);
      if(endptr == safe_v) goto bogus;
      f(closure, tagged_name, t, &val, timestamp);
      break;
    }
    case METRIC_INT64:
    {
      int64_t val;
      val = strtoll(safe_v, &endptr, 10);
      if(endptr == safe_v) goto bogus;
      f(closure, tagged_name, t, &val, timestamp);
      break;
    }
    case METRIC_UINT64:
    {
      uint64_t val;
      val = strtoull(safe_v, &endptr, 10);
      if(endptr == safe_v) goto bogus;
      f(closure, tagged_name, t, &val, timestamp);
      break;
    }
    case METRIC_DOUBLE:
    {
      double val;
      val = strtod(safe_v, &endptr);
      if(endptr == safe_v) goto bogus;
      f(closure, tagged_name, t, &val, timestamp);
      break;
    }
    case METRIC_GUESS:
      t = noit_metric_guess_type((char *)safe_v, &replacement);
      f(closure, tagged_name, t, (t != METRIC_GUESS) ? replacement : safe_v, timestamp);
      free(replacement);
      break;
    case METRIC_HISTOGRAM:
    case METRIC_HISTOGRAM_CUMULATIVE:
    case METRIC_ABSENT:
      mtevAssert(0 && "bad metric type passed to noit_stats_set_metric_coerce");
  }
  if(stats) check_stats_set_metric_coerce_hook_invoke(check, stats, tagged_name, t, safe_v, mtev_true);
}

void
noit_stats_set_metric_coerce_with_timestamp(noit_check_t *check,
                             const char *name_raw, metric_type_t t,
                             const char *v,
                             struct timeval *timestamp) {
  stats_t *stats = noit_check_get_stats_inprogress(check);
  noit_metric_coerce_ex_with_timestamp(check, name_raw, t, v, timestamp,
      noit_stats_set_metric_with_timestamp_ex_f, check, stats);
}

void
noit_stats_set_metric_coerce(noit_check_t *check,
                             const char *name, metric_type_t t,
                             const char *v) {
  stats_t *stats = noit_check_get_stats_inprogress(check);
  noit_metric_coerce_ex_with_timestamp(check, name, t, v, NULL,
      noit_stats_set_metric_with_timestamp_ex_f, check, stats);
}

static void
record_immediate_metric_with_tagset(noit_check_t *check,
                                const char *name, metric_type_t type,
                                const void *value, mtev_boolean do_log, const struct timeval *time) {
  struct timeval now;
  stats_t *c;
  metric_t *m = mtev_memory_safe_malloc_cleanup(sizeof(*m), noit_check_safe_free_metric);
  memset(m, 0, sizeof(*m));

  if(noit_stats_populate_metric_with_tagset(m, name, type, value)) {
    mtev_memory_safe_free(m);
    return;
  }
  if(time == NULL) {
    gettimeofday(&now, NULL);
    time = &now;
  }
  if(do_log == mtev_true) {
    noit_check_log_metric(check, time, m);
  }
  c = noit_check_get_stats_inprogress(check);
  if(noit_stats_mark_metric_logged(c, m, mtev_true) == mtev_false) {
    mtev_memory_safe_free(m);
  }
}
void
noit_stats_log_immediate_metric_timed(noit_check_t *check,
                                const char *name, metric_type_t type,
                                const void *value, const struct timeval *whence) {
  char tagged_name[MAX_METRIC_TAGGED_NAME];
  if(noit_check_build_tag_extended_name(tagged_name, sizeof(tagged_name), name, check) <= 0) {
    return;
  }

  if(noit_stats_log_immediate_metric_timed_hook_invoke(check, tagged_name, type,
                                                       value, whence) != MTEV_HOOK_CONTINUE) {
    return;
  }

  record_immediate_metric_with_tagset(check, tagged_name, type, value, mtev_true, whence);
}
void
noit_stats_log_immediate_metric(noit_check_t *check,
                                const char *name, metric_type_t type,
                                const void *value) {
  noit_stats_log_immediate_metric_timed(check, name, type, value, NULL);
}


mtev_boolean
noit_stats_log_immediate_histo_tv(noit_check_t *check,
                                const char *name, const char *hist_encoded, size_t hist_encoded_len,
                                mtev_boolean cumulative, struct timeval whence) {
  char tagged_name[MAX_METRIC_TAGGED_NAME];
  if(noit_check_build_tag_extended_name(tagged_name, sizeof(tagged_name), name, check) <= 0) {
    return mtev_false;
  }

  if (noit_log_histo_encoded_available()) {
    noit_log_histo_encoded(check, &whence, mtev_true, tagged_name, hist_encoded, hist_encoded_len, cumulative, mtev_false);
  } else {
    return mtev_false;
  }

  /* This sets up a dummy numeric entry so that other people will know the name exists. */
  /* This should likely be handled in the histogram module, but that plumbing is far from here */
  record_immediate_metric_with_tagset(check, name, METRIC_INT32, NULL, mtev_false, &whence);
  return mtev_true;
}

mtev_boolean
noit_stats_log_immediate_histo(noit_check_t *check,
                                const char *name, const char *hist_encoded, size_t hist_encoded_len,
                                mtev_boolean cumulative, uint64_t whence_s) {
  struct timeval whence;
  whence.tv_sec = whence_s;
  whence.tv_usec = 0;
  return noit_stats_log_immediate_histo_tv(check,name,hist_encoded,hist_encoded_len,cumulative,whence);
}

void
noit_check_passive_set_stats(noit_check_t *check) {
  int i, nwatches = 0;
  mtev_skiplist_node *next;
  noit_check_t n;
  noit_check_t *watches[8192];

  mtev_uuid_copy(n.checkid, check->checkid);
  n.period = 0;

  noit_check_set_stats(check);

  pthread_mutex_lock(&polls_lock);
  mtev_skiplist_find_neighbors(watchlist, &n, NULL, NULL, &next);
  while(next && mtev_skiplist_data(next) && nwatches < 8192) {
    noit_check_t *wcheck = mtev_skiplist_data(next);
    if(mtev_uuid_compare(n.checkid, wcheck->checkid)) break;
    watches[nwatches++] = wcheck;
    mtev_skiplist_next(watchlist, &next);
  }
  pthread_mutex_unlock(&polls_lock);

  for(i=0;i<nwatches;i++) {
    void *backup;
    noit_check_t *wcheck = watches[i];
    /* Swap the real check's stats into place */
    backup = wcheck->statistics;
    wcheck->statistics = check->statistics;

    if(check_passive_log_stats_hook_invoke(check) == MTEV_HOOK_CONTINUE) {
      /* Write out our status */
      noit_check_log_status(wcheck);
      /* Write out all metrics */
      noit_check_log_metrics(wcheck);
    }
    /* Swap them back out */
    wcheck->statistics = backup;
  }
}
static void
stats_merge(stats_t *tgt, stats_t *src) {
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  const char *name;
  int namelen;
  void *vm;

  if(!src) return;

  memcpy(&tgt->whence, &src->whence, sizeof(struct timeval));
  memcpy(tgt->status, src->status, sizeof(tgt->status));
  tgt->available = src->available;
  tgt->state = src->state;
  tgt->duration = src->duration;

  while(mtev_hash_next(&src->metrics, &iter, &name, &namelen, &vm)) {
    __stats_add_metric(tgt, (metric_t *)vm);
  }
  mtev_hash_delete_all(&src->metrics, NULL, NULL);
}
void
noit_check_set_stats(noit_check_t *check) {
  int report_change = 0;
  char *cp;
  dep_list_t *dep;
  stats_t *prev = NULL, *current = NULL;

  if(check_set_stats_hook_invoke(check) == MTEV_HOOK_ABORT) return;

  if(perpetual_metrics) {
    if(!stats_previous(check)) {
      stats_previous(check) = noit_check_stats_alloc();
    }
    stats_t *all = stats_previous(check);
    prev = stats_current(check);
    if(prev) {
      stats_merge(all,prev);
      mtev_memory_safe_free(prev);
    }
  }
  else {
    mtev_memory_safe_free(stats_previous(check));
    stats_previous(check) = stats_current(check);
  }
  current = stats_current(check) = stats_inprogress(check);
  stats_inprogress(check) = noit_check_stats_alloc();
  
  if(current) {
    for(cp = current->status; cp && *cp; cp++)
      if(*cp == '\r' || *cp == '\n') *cp = ' ';
  }

  /* check for state changes */
  if((!current || (current->available != NP_UNKNOWN)) &&
     (!prev || (prev->available != NP_UNKNOWN)) &&
     (!current || !prev || (current->available != prev->available)))
    report_change = 1;
  if((!current || (current->state != NP_UNKNOWN)) &&
     (!prev || (prev->state != NP_UNKNOWN)) &&
     (!current || !prev || (current->state != prev->state)))
    report_change = 1;

  mtevL(check_debug, "%s`%s <- [%s]\n", check->target, check->name,
        current ? current->status : "null");
  if(report_change) {
    mtevL(check_debug, "%s`%s -> [%s:%s]\n",
          check->target, check->name,
          noit_check_available_string(current ? current->available : NP_UNKNOWN),
          noit_check_state_string(current ? current->state : NP_UNKNOWN));
  }

  if(NOIT_CHECK_STATUS_ENABLED()) {
    char id[UUID_STR_LEN+1];
    mtev_uuid_unparse_lower(check->checkid, id);
    NOIT_CHECK_STATUS(id, check->module, check->name, check->target,
                      current ? current->available : NP_UNKNOWN,
                      current ? current->state : NP_UNKNOWN,
                      current ? current->status : "null");
  }

  if(check_log_stats_hook_invoke(check) == MTEV_HOOK_CONTINUE) {
    /* Write out the bundled information */
    noit_check_log_bundle(check);
  }
  /* count the check as complete */
  check_completion_count++;

  for(dep = check->causal_checks; dep; dep = dep->next) {
    noit_module_t *mod;
    mod = noit_module_lookup(dep->check->module);
    if(!mod) {
      bad_check_initiate(mod, dep->check, 1, check);
    }
    else {
      mtevL(check_debug, "Firing %s`%s in response to %s`%s\n",
            dep->check->target, dep->check->name,
            check->target, check->name);
      if((dep->check->flags & NP_DISABLED) == 0)
        if(mod->initiate_check)
          mod->initiate_check(mod, dep->check, 1, check);
    }
  }
}

static int
noit_console_show_watchlist(mtev_console_closure_t ncct,
                            int argc, char **argv,
                            mtev_console_state_t *dstate,
                            void *closure) {
  mtev_skiplist_node *iter, *fiter;
  int nwatches = 0, i;
  noit_check_t *watches[8192];

  nc_printf(ncct, "%d active watches.\n", mtev_skiplist_size(watchlist));
  pthread_mutex_lock(&polls_lock);
  for(iter = mtev_skiplist_getlist(watchlist); iter && nwatches < 8192;
      mtev_skiplist_next(watchlist, &iter)) {
    noit_check_t *check = mtev_skiplist_data(iter);
    watches[nwatches++] = check;
  }
  pthread_mutex_unlock(&polls_lock);

  for(i=0;i<nwatches;i++) {
    noit_check_t *check = watches[i];
    char uuid_str[UUID_STR_LEN + 1];

    mtev_uuid_unparse_lower(check->checkid, uuid_str);
    nc_printf(ncct, "%s:\n\t[%s`%s`%s]\n\tPeriod: %dms\n\tFeeds[%d]:\n",
              uuid_str, check->target, check->module, check->name,
              check->period, check->feeds ? mtev_skiplist_size(check->feeds) : 0);
    if(check->feeds && mtev_skiplist_size(check->feeds)) {
      for(fiter = mtev_skiplist_getlist(check->feeds); fiter;
          mtev_skiplist_next(check->feeds, &fiter)) {
        nc_printf(ncct, "\t\t%s\n", (const char *)mtev_skiplist_data(fiter));
      }
    }
  }
  return 0;
}

static void
nc_printf_check_brief(mtev_console_closure_t ncct,
                      noit_check_t *check) {
  stats_t *current;
  char out[512];
  char uuid_str[37];
  if(NOIT_CHECK_DELETED(check))
    snprintf(out, sizeof(out), "-- deleted pending cluster synchronization --");
  else
    snprintf(out, sizeof(out), "%s`%s (%s [%x])", check->target, check->name,
             check->target_ip, check->flags);
  mtev_uuid_unparse_lower(check->checkid, uuid_str);
  nc_printf(ncct, "%s %s\n", uuid_str, out);
  current = stats_current(check);
  if(current)
    nc_printf(ncct, "\t%s\n", current->status);
}

char *
noit_console_conf_check_opts(mtev_console_closure_t ncct,
                             mtev_console_state_stack_t *stack,
                             mtev_console_state_t *dstate,
                             int argc, char **argv, int idx) {
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen, i = 0;
  void *vcheck;

  if(argc == 1) {
    if(!strncmp("new", argv[0], strlen(argv[0]))) {
      if(idx == i) return strdup("new");
      i++;
    }
    pthread_mutex_lock(&polls_lock);
    while(mtev_hash_next(&polls, &iter, (const char **)key_id, &klen,
                         &vcheck)) {
      noit_check_t *check = (noit_check_t *)vcheck;
      char out[512];
      char uuid_str[37];
      snprintf(out, sizeof(out), "%s`%s", check->target, check->name);
      mtev_uuid_unparse_lower(check->checkid, uuid_str);
      if(!strncmp(out, argv[0], strlen(argv[0]))) {
        if(idx == i) {
          pthread_mutex_unlock(&polls_lock);
          return strdup(out);
        }
        i++;
      }
      if(!strncmp(uuid_str, argv[0], strlen(argv[0]))) {
        if(idx == i) {
          pthread_mutex_unlock(&polls_lock);
          return strdup(uuid_str);
        }
        i++;
      }
    }
    pthread_mutex_unlock(&polls_lock);
  }
  if(argc == 2) {
    cmd_info_t *cmd;
    if(!strcmp("new", argv[0])) return NULL;
    cmd = mtev_skiplist_find(dstate->cmds, "attribute", NULL);
    if(!cmd) return NULL;
    return mtev_console_opt_delegate(ncct, stack, cmd->dstate, argc-1, argv+1, idx);
  }
  return NULL;
}

char *
noit_console_check_opts(mtev_console_closure_t ncct,
                        mtev_console_state_stack_t *stack,
                        mtev_console_state_t *dstate,
                        int argc, char **argv, int idx) {
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen, i = 0;

  if(argc == 1) {
    void *vcheck;
    pthread_mutex_lock(&polls_lock);
    while(mtev_hash_next(&polls, &iter, (const char **)key_id, &klen,
                         &vcheck)) {
      char out[512];
      char uuid_str[37];
      noit_check_t *check = (noit_check_t *)vcheck;
      snprintf(out, sizeof(out), "%s`%s", check->target, check->name);
      mtev_uuid_unparse_lower(check->checkid, uuid_str);
      if(!strncmp(out, argv[0], strlen(argv[0]))) {
        if(idx == i) {
          pthread_mutex_unlock(&polls_lock);
          return strdup(out);
        }
        i++;
      }
      if(!strncmp(uuid_str, argv[0], strlen(argv[0]))) {
        if(idx == i) {
          pthread_mutex_unlock(&polls_lock);
          return strdup(uuid_str);
        }
        i++;
      }
    }
    pthread_mutex_unlock(&polls_lock);
  }
  if(argc == 2) {
    return mtev_console_opt_delegate(ncct, stack, dstate, argc-1, argv+1, idx);
  }
  return NULL;
}

static int
noit_console_show_checks(mtev_console_closure_t ncct,
                         int argc, char **argv,
                         mtev_console_state_t *dstate,
                         void *closure) {
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen, i = 0, nchecks;
  void *vcheck;
  noit_check_t **checks;

  nchecks = mtev_hash_size(&polls);
  if(nchecks == 0) return 0;
  checks = malloc(nchecks * sizeof(*checks));

  pthread_mutex_lock(&polls_lock);
  while(mtev_hash_next(&polls, &iter, (const char **)key_id, &klen,
                       &vcheck)) {
    if(i<nchecks) checks[i++] = vcheck;
  }
  pthread_mutex_unlock(&polls_lock);

  nchecks = i;
  for(i=0;i<nchecks;i++)
    nc_printf_check_brief(ncct,checks[i]);

  free(checks);
  return 0;
}

static int
noit_console_short_checks_sl(mtev_console_closure_t ncct,
                             mtev_skiplist *tlist) {
  int max_count, i = 0;
  noit_check_t **todo;
  mtev_skiplist_node *iter;

  max_count = mtev_skiplist_size(tlist);
  if(max_count == 0) return 0;
  todo = malloc(max_count * sizeof(*todo));

  pthread_mutex_lock(&polls_lock);
  for(iter = mtev_skiplist_getlist(tlist); i < max_count && iter;
      mtev_skiplist_next(tlist, &iter)) {
    todo[i++] = mtev_skiplist_data(iter);
  }
  pthread_mutex_unlock(&polls_lock);

  max_count = i;
  for(i=0;i<max_count;i++)
    nc_printf_check_brief(ncct, todo[i]);

  free(todo);
  return 0;
}
static int
noit_console_show_checks_name(mtev_console_closure_t ncct,
                              int argc, char **argv,
                              mtev_console_state_t *dstate,
                              void *closure) {
  if(!polls_by_name) return 0;
  return noit_console_short_checks_sl(ncct, polls_by_name);
}

static int
noit_console_show_checks_target(mtev_console_closure_t ncct,
                                   int argc, char **argv,
                                   mtev_console_state_t *dstate,
                                   void *closure) {
  if(!polls_by_name) return 0;
  return noit_console_short_checks_sl(ncct,
           mtev_skiplist_find(mtev_skiplist_indexes(polls_by_name),
           __check_target_compare, NULL));
}

static int
noit_console_show_checks_target_ip(mtev_console_closure_t ncct,
                                   int argc, char **argv,
                                   mtev_console_state_t *dstate,
                                   void *closure) {
  if(!polls_by_name) return 0;
  return noit_console_short_checks_sl(ncct,
           mtev_skiplist_find(mtev_skiplist_indexes(polls_by_name),
           __check_target_ip_compare, NULL));
}

static void
register_console_check_commands() {
  mtev_console_state_t *tl;
  cmd_info_t *showcmd;

  tl = mtev_console_state_initial();
  showcmd = mtev_console_state_get_cmd(tl, "show");
  mtevAssert(showcmd && showcmd->dstate);

  mtev_console_state_add_cmd(showcmd->dstate,
    NCSCMD("timing_slots", noit_console_show_timing_slots, NULL, NULL, NULL));

  mtev_console_state_add_cmd(showcmd->dstate,
    NCSCMD("checks", noit_console_show_checks, NULL, NULL, NULL));

  mtev_console_state_add_cmd(showcmd->dstate,
    NCSCMD("checks:name", noit_console_show_checks_name, NULL,
           NULL, NULL));

  mtev_console_state_add_cmd(showcmd->dstate,
    NCSCMD("checks:target", noit_console_show_checks_target, NULL,
           NULL, NULL));

  mtev_console_state_add_cmd(showcmd->dstate,
    NCSCMD("checks:target_ip", noit_console_show_checks_target_ip, NULL,
           NULL, NULL));

  mtev_console_state_add_cmd(showcmd->dstate,
    NCSCMD("watches", noit_console_show_watchlist, NULL, NULL, NULL));
}

int
noit_check_register_module(const char *name) {
  int i;
  for(i=0; i<reg_module_id; i++)
    if(!strcmp(reg_module_names[i], name)) return i;
  if(reg_module_id >= MAX_MODULE_REGISTRATIONS) return -1;
  mtevL(check_debug, "Registered module %s as %d\n", name, i);
  i = reg_module_id++;
  reg_module_names[i] = strdup(name);
  mtev_conf_set_namespace(reg_module_names[i]);
  return i;
}
int
noit_check_registered_module_by_name(const char *name) {
  int i;
  for(i=0; i<reg_module_id; i++)
    if(!strcmp(reg_module_names[i], name)) return i;
  return -1;
}
int
noit_check_registered_module_cnt() {
  return reg_module_id;
}
const char *
noit_check_registered_module(int idx) {
  if(reg_module_used < 0) reg_module_used = reg_module_id;
  mtevAssert(reg_module_used == reg_module_id);
  if(idx >= reg_module_id || idx < 0) return NULL;
  return reg_module_names[idx];
}

void
noit_check_set_module_metadata(noit_check_t *c, int idx, void *md, void (*freefunc)(void *)) {
  struct vp_w_free *tuple;
  if(reg_module_used < 0) reg_module_used = reg_module_id;
  mtevAssert(reg_module_used == reg_module_id);
  if(idx >= reg_module_id || idx < 0) return;
  if(!c->module_metadata) c->module_metadata = calloc(reg_module_id, sizeof(void *));
  c->module_metadata[idx] = calloc(1, sizeof(struct vp_w_free));
  tuple = c->module_metadata[idx];
  tuple->ptr = md;
  tuple->freefunc = freefunc;
}
void
noit_check_set_module_config(noit_check_t *c, int idx, mtev_hash_table *config) {
  if(reg_module_used < 0) reg_module_used = reg_module_id;
  mtevAssert(reg_module_used == reg_module_id);
  if(idx >= reg_module_id || idx < 0) return;
  if(!c->module_configs) c->module_configs = calloc(reg_module_id, sizeof(mtev_hash_table *));
  c->module_configs[idx] = config;
}
void *
noit_check_get_module_metadata(noit_check_t *c, int idx) {
  struct vp_w_free *tuple;
  if(reg_module_used < 0) reg_module_used = reg_module_id;
  mtevAssert(reg_module_used == reg_module_id);
  if(idx >= reg_module_id || idx < 0 || !c->module_metadata) return NULL;
  tuple = c->module_metadata[idx];
  return tuple ? tuple->ptr : NULL;
}
mtev_hash_table *
noit_check_get_module_config(noit_check_t *c, int idx) {
  if(reg_module_used < 0) reg_module_used = reg_module_id;
  mtevAssert(reg_module_used == reg_module_id);
  if(idx >= reg_module_id || idx < 0 || !c->module_configs) return NULL;
  return c->module_configs[idx];
}
void
noit_check_init_globals(void) {
  mtev_hash_init(&polls);
  mtev_hash_init(&dns_ignore_list);
}

#define XMLSETPROP(node, name, part, fmt) do { \
  char buff[256]; \
  snprintf(buff, sizeof(buff), fmt, part); \
  xmlSetProp(node, (xmlChar *)name, (xmlChar *)buff); \
} while(0)

xmlNodePtr
noit_check_to_xml(noit_check_t *check, xmlDocPtr doc, xmlNodePtr parent) {
  int mod, mod_cnt;
  mtev_hash_iter iter;
  char uuid_str[UUID_STR_LEN+1];
  xmlNodePtr node, confnode;
  node = xmlNewNode(NULL, (xmlChar *)"check");

  /* Normal attributes */
  mtev_uuid_unparse_lower(check->checkid, uuid_str);
  xmlSetProp(node, (xmlChar *)"uuid", (xmlChar *)uuid_str);
  XMLSETPROP(node, "seq", check->config_seq, "%"PRId64);
  if(NOIT_CHECK_DELETED(check)) {
    xmlSetProp(node, (xmlChar *)"deleted", (xmlChar *)"deleted");
    return node;
  }
  XMLSETPROP(node, "target", check->target, "%s");
  XMLSETPROP(node, "module", check->module, "%s");
  XMLSETPROP(node, "name", check->name, "%s");
  XMLSETPROP(node, "filterset", check->filterset, "%s");
  XMLSETPROP(node, "period", check->period, "%u");
  XMLSETPROP(node, "timeout", check->timeout, "%u");
  if(check->oncheck) {
    XMLSETPROP(node, "oncheck", check->oncheck, "%s");
  }

  /* Config stuff */
  confnode = xmlNewNode(NULL, (xmlChar *)"config");
  xmlAddChild(node, confnode);
  memset(&iter, 0, sizeof(iter));
  while(mtev_hash_adv(check->config, &iter)) {
    xmlNewTextChild(confnode, NULL, (xmlChar *)iter.key.str, (xmlChar *)iter.value.str);
  }

  mod_cnt = noit_check_registered_module_cnt();
  for(mod=0; mod<mod_cnt; mod++) {
    xmlNsPtr ns;
    const char *nsname;
    char buff[256];
    mtev_hash_table *config;
    config = noit_check_get_module_config(check, mod);
    if(!config) continue;

    nsname = noit_check_registered_module(mod);

    snprintf(buff, sizeof(buff), "noit://module/%s", nsname);
    ns = xmlSearchNs(doc, parent ? parent : node, (xmlChar *)nsname);
    if(!ns) ns = xmlNewNs(parent ? parent : node, (xmlChar *)buff, (xmlChar *)nsname);
    memset(&iter, 0, sizeof(iter));
    while(mtev_hash_adv(config, &iter)) {
      xmlNodePtr confnodechild;
      confnodechild = xmlNewTextChild(confnode, ns, (xmlChar*) "value", (xmlChar *)iter.value.str);
      XMLSETPROP(confnodechild, "name", iter.key.str, "%s");
    }
  }
  return node;
}

void
noit_check_build_cluster_changelog(void *vpeer) {
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  while(mtev_hash_adv(&polls, &iter)) {
    noit_check_t *check = iter.value.ptr;
    if(check->config_seq >= 0) noit_cluster_mark_check_changed(check, vpeer);
  }
}

int
noit_check_process_repl(xmlDocPtr doc) {
  int i = 0;
  xmlNodePtr root, child, next = NULL, node;
  if(!initialized) return -1;
  if (noit_check_get_lmdb_instance()) {
    return noit_check_lmdb_process_repl(doc);
  }
  root = xmlDocGetRootElement(doc);
  mtev_conf_section_t section;
  mtev_conf_section_t checks = mtev_conf_get_section_write(MTEV_CONF_ROOT, "/noit/checks");
  mtevAssert(!mtev_conf_section_is_empty(checks));
  for(child = xmlFirstElementChild(root); child; child = next) {
    next = xmlNextElementSibling(child);

    uuid_t checkid;
    int64_t seq;
    char uuid_str[UUID_STR_LEN+1], seq_str[32];
    section = mtev_conf_section_from_xmlnodeptr(child);
    mtevAssert(mtev_conf_get_stringbuf(section, "@uuid",
                                       uuid_str, sizeof(uuid_str)));
    mtevAssert(mtev_uuid_parse(uuid_str, checkid) == 0);
    mtevAssert(mtev_conf_get_stringbuf(section, "@seq",
                                       seq_str, sizeof(seq_str)));
    seq = strtoll(seq_str, NULL, 10);

    noit_check_t *check = noit_poller_lookup(checkid);

    /* too old, don't bother */
    if(check && check->config_seq >= seq) {
      i++;
      noit_check_deref(check);
      continue;
    }

    if(check) {
      char xpath[1024];

      snprintf(xpath, sizeof(xpath), "/noit/checks//check[@uuid=\"%s\"]",
               uuid_str);
      mtev_conf_section_t oldsection = mtev_conf_get_section_write(MTEV_CONF_ROOT, xpath);
      if(!mtev_conf_section_is_empty(oldsection)) {
        CONF_REMOVE(oldsection);
        node = mtev_conf_section_to_xmlnodeptr(oldsection);
        xmlUnlinkNode(node);
        xmlFreeNode(node);
      }
      mtev_conf_release_section_write(oldsection);
      noit_check_deref(check);
    }

    xmlNodePtr checks_node = mtev_conf_section_to_xmlnodeptr(checks);
    child = xmlDocCopyNode(child, checks_node->doc, 1);
    xmlAddChild(mtev_conf_section_to_xmlnodeptr(checks), child);
    CONF_DIRTY(section);
    noit_poller_process_check_conf(section);
    i++;
  }
  mtev_conf_release_section_write(checks);
  mtev_conf_mark_changed();
  if(mtev_conf_write_file(NULL) != 0)
    mtevL(check_error, "local config write failed\n");
  return i;
}

/* This function assumes that the cursor is pointing at the first element for a uuid
 * It will iterate the cursor until it hits a new uuid or the end of the database, 
 * then reeturn the return code fro, the lmdb call
 * It is the responsibility of the caller to handle this */
int
noit_poller_lmdb_create_check_from_database_locked(MDB_cursor *cursor, uuid_t checkid) {
  void *vcheck = NULL;
  int rc = 0;
  char uuid_str[37];
  char target[256] = "";
  char module[256] = "";
  char name[256] = "";
  char filterset[256] = "";
  char oncheck[1024] = "";
  char resolve_rtype[16] = "";
  char delstr[16] = "";
  char seq_str[256] = "";
  char period_str[256] = "";
  char timeout_str[256] = "";
  char transient_min_period_str[256] = "";
  char transient_period_granularity_str[256] = "";
  uuid_t out_uuid;
  int64_t config_seq = 0;
  int ridx, flags = 0, found = 0;
  int no_oncheck = 1;
  int no_period = 1;
  int no_timeout = 1;
  int32_t period = 0, timeout = 0, transient_min_period = 0, transient_period_granularity = 0;
  mtev_boolean disabled = mtev_false, busted = mtev_false, deleted = mtev_false;
  mtev_hash_table options;
  mtev_hash_table **moptions = NULL;
  mtev_boolean moptions_used = mtev_false, moptions_malloced = mtev_false, backdated = mtev_false;
  MDB_val mdb_key, mdb_data;

  /* We want to heartbeat here... otherwise, if a lot of checks are 
   * configured or if we're running on a slower system, we could 
   * end up getting watchdog killed before we get a chance to run 
   * any checks */
  mtev_watchdog_child_heartbeat();

  mtev_uuid_unparse_lower(checkid, uuid_str);
  mtev_hash_init(&options);

  if(reg_module_id > 0) {
    if (reg_module_id > 10) {
      moptions = malloc(reg_module_id * sizeof(mtev_hash_table *));
      moptions_malloced = mtev_true;
    }
    else {
      moptions = alloca(reg_module_id * sizeof(mtev_hash_table *));
    }
    memset(moptions, 0, reg_module_id * sizeof(mtev_hash_table *));
    moptions_used = mtev_true;
  }

  rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_GET_CURRENT);
  while (rc == 0) {
    noit_lmdb_check_data_t *data = noit_lmdb_check_data_from_key(mdb_key.mv_data);
    mtevAssert(data);
    /* If the uuid doesn't match, we're done */
    if (mtev_uuid_compare(checkid, data->id) != 0) {
      noit_lmdb_free_check_data(data);
      break;
    }
    int copySize = 0;
    if (data->type == NOIT_LMDB_CHECK_ATTRIBUTE_TYPE) {
#define COPYSTRING(val) do { \
  copySize = (mdb_data.mv_data == NULL) ? 0 : MIN(mdb_data.mv_size, sizeof(val) - 1); \
  memcpy(val, mdb_data.mv_data, copySize); \
  val[copySize] = 0; \
} while(0);
      if (strcmp(data->key, "target") == 0) {
        COPYSTRING(target);
      }
      else if (strcmp(data->key, "module") == 0) {
        COPYSTRING(module);
      }
      else if (strcmp(data->key, "name") == 0) {
        COPYSTRING(name);
      }
      else if (strcmp(data->key, "filterset") == 0) {
        COPYSTRING(filterset);
      }
      else if (strcmp(data->key, "seq") == 0) {
        COPYSTRING(seq_str);
        config_seq = strtoll(seq_str, NULL, 10);
      }
      else if (strcmp(data->key, "period") == 0) {
        COPYSTRING(period_str);
        period = atoi(period_str);
        no_period = 0;
        if (period < global_minimum_period) {
          period = global_minimum_period;
        }
	else if (period > global_maximum_period) {
          period = global_maximum_period;
        }
      }
      else if (strcmp(data->key, "timeout") == 0) {
        COPYSTRING(timeout_str);
        no_timeout = 0;
        timeout = atoi(timeout_str);
      }
      else if (strcmp(data->key, "oncheck") == 0) {
        COPYSTRING(oncheck);
        no_oncheck = 0;
      }
      else if (strcmp(data->key, "deleted") == 0) {
        COPYSTRING(delstr);
        if (strcmp(delstr, "deleted") == 0) {
          deleted = mtev_true;
          disabled = mtev_true;
          flags |= NP_DELETED;
        }
      }
      else if (strcmp(data->key, "resolve_rtype") == 0) {
        COPYSTRING(resolve_rtype);
      }
      else if (strcmp(data->key, "transient_min_period") == 0) {
        COPYSTRING(transient_min_period_str);
        transient_min_period = atoi(transient_min_period_str);
        if (transient_min_period < 0) {
          transient_min_period = 0;
        }
      }
      else if (strcmp(data->key, "transient_period_granularity") == 0) {
        COPYSTRING(transient_period_granularity_str);
        transient_period_granularity = atoi(transient_period_granularity_str);
        if (transient_period_granularity < 0) {
          transient_period_granularity = 0;
        }
      }
      else {
        mtevL(mtev_error, "unknown attribute in check: %s\n", data->key);
      }
    }
    else if (data->type == NOIT_LMDB_CHECK_CONFIG_TYPE) {
      mtev_hash_table *insertTable = NULL;
      if (data->ns == NULL) {
        insertTable = &options;
      }
      else {
        for(ridx=0; ridx<reg_module_id; ridx++) {
          if (strcmp(reg_module_names[ridx], data->ns) == 0) {
            if (!moptions[ridx]) {
              moptions[ridx] = calloc(1, sizeof(mtev_hash_table));
              mtev_hash_init(moptions[ridx]);
            }
            insertTable = moptions[ridx];
            break;
          }
        }
      }
      if (insertTable) {
        char *key = strdup(data->key);
        char *value = NULL;
        if (mdb_data.mv_data == NULL) {
          value = (char *)malloc(1);
          value[0] = 0;
        }
        else {
          value = (char *)malloc(mdb_data.mv_size + 1);
          memcpy(value, mdb_data.mv_data, mdb_data.mv_size);
          value[mdb_data.mv_size] = 0;
        }
        mtev_hash_store(insertTable, key, strlen(key), value);
      }
    }
    else {
      mtevFatal(mtev_error, "unknown type\n");
    }
    noit_lmdb_free_check_data(data);
    rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_NEXT);
  }

  /* These *may* be defined in the check stanza and not in the db - if this is the case,
   * we need to set these based on the inheritable values */
#define CHECK_FROM_LMDB_INHERIT(type,a,...) \
  mtev_conf_get_##type(checks, "ancestor-or-self::node()/@" #a, __VA_ARGS__)
  mtev_conf_section_t checks = mtev_conf_get_section_read(MTEV_CONF_ROOT, "/noit/checks");
  if (!strlen(target)) {
    CHECK_FROM_LMDB_INHERIT(stringbuf, target, target, sizeof(target));
  }
  if (!strlen(module)) {
    CHECK_FROM_LMDB_INHERIT(stringbuf, module, module, sizeof(module));
  }
  if (!strlen(filterset)) {
    CHECK_FROM_LMDB_INHERIT(stringbuf, filterset, filterset, sizeof(filterset));
  }
  if (!strlen(oncheck)) {
    CHECK_FROM_LMDB_INHERIT(stringbuf, oncheck, oncheck, sizeof(oncheck));
  }
  if (!strlen(resolve_rtype)) {
    if (!CHECK_FROM_LMDB_INHERIT(stringbuf, resolve_rtype, resolve_rtype, sizeof(resolve_rtype))) {
      strlcpy(resolve_rtype, PREFER_IPV4, sizeof(resolve_rtype));
    }
  }
  if (no_period) {
    period = 0;
    if (CHECK_FROM_LMDB_INHERIT(int32, period, &period)) {
      no_period = 0;
      if(period < global_minimum_period) {
        period = global_minimum_period;
      }
      if(period > global_maximum_period) {
        period = global_maximum_period;
      }
    }
  }
  if (no_timeout) {
    timeout = 0;
    if (CHECK_FROM_LMDB_INHERIT(int32, timeout, &timeout)) {
      no_timeout = 0;
    }
  }
  mtev_conf_release_section_read(checks);

  if(deleted) {
    memcpy(target, "none", 5);
    mtev_uuid_unparse_lower(checkid, name);
  } else {
    if(no_period && no_oncheck) {
      mtevL(mtev_error, "check uuid: '%s' has neither period nor oncheck\n",
        uuid_str);
      busted = mtev_true;
    }
    if(!(no_period || no_oncheck)) {
      mtevL(mtev_error, "check uuid: '%s' has oncheck and period.\n",
        uuid_str);
      busted = mtev_true;
    }
    if (no_timeout) {
      mtevL(noit_stderr, "check uuid: '%s' has no timeout\n", uuid_str);
      busted = mtev_true;
    }
    if(timeout < 0) timeout = 0;
    if(!no_period && timeout >= period) {
      mtevL(mtev_error, "check uuid: '%s' timeout > period\n", uuid_str);
      timeout = period/2;
    }
  }

  if(busted) flags |= (NP_UNCONFIG|NP_DISABLED);
  else if(disabled) flags |= NP_DISABLED;

  flags |= noit_calc_rtype_flag(resolve_rtype);

  vcheck = noit_poller_check_found_and_backdated(checkid, config_seq, &found, &backdated);

  if(found) {
    noit_poller_deschedule(checkid, mtev_false, mtev_true);
  }
  if(backdated) {
    mtevL(mtev_error, "Check config seq backwards, ignored\n");
    if(found) {
      noit_check_log_delete((noit_check_t *)vcheck);
    }
  }
  else {
    noit_poller_schedule(target, module, name, filterset, &options,
                         moptions_used ? moptions : NULL,
                         period, timeout,
                         transient_min_period, transient_period_granularity,
                         oncheck[0] ? oncheck : NULL,
                         config_seq, flags, checkid, out_uuid);
    mtevL(mtev_debug, "loaded uuid: %s\n", uuid_str);
    if(deleted) {
      noit_poller_deschedule(checkid, mtev_false, mtev_false);
    }
  }
  for(ridx=0; ridx<reg_module_id; ridx++) {
    if(moptions[ridx]) {
      mtev_hash_destroy(moptions[ridx], free, free);
      free(moptions[ridx]);
    }
  }
  if (moptions_malloced == mtev_true) {
    free(moptions);
  }
  mtev_hash_destroy(&options, free, free);

  return rc;
}

char **noit_check_get_namespaces(int *cnt) {
  char **toRet = NULL;
  int i = 0;
  mtevAssert(cnt);
  *cnt = reg_module_id;

  if (reg_module_id == 0) {
    return toRet;
  }
  toRet = (char **)calloc(reg_module_id, sizeof(char *));
  for (i = 0; i < reg_module_id; i++) {
    toRet[i] = strdup(reg_module_names[i]);
  }
  return toRet;
}

void
noit_check_set_db_source_header(mtev_http_session_ctx *ctx) {
  if (!ctx) {
    return;
  }
  mtev_http_response_header_set(ctx, CHECK_DB_HEADER_STRING,
    (noit_check_get_lmdb_instance()) ? "LMDB" : "XML");
}
