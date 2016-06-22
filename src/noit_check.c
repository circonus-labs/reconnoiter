/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2015, Circonus, Inc. All rights reserved.
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

#include "noit_config.h"
#include <mtev_defines.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include <eventer/eventer.h>
#include <mtev_memory.h>
#include <mtev_log.h>
#include <mtev_hash.h>
#include <mtev_skiplist.h>
#include <mtev_watchdog.h>
#include <mtev_conf.h>
#include <mtev_console.h>
#include <mtev_cluster.h>

#include "noit_mtev_bridge.h"
#include "noit_dtrace_probes.h"
#include "noit_check.h"
#include "noit_module.h"
#include "noit_check_tools.h"
#include "noit_check_resolver.h"

#define DEFAULT_TEXT_METRIC_SIZE_LIMIT  512
#define RECYCLE_INTERVAL 60

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

#define STATS_INPROGRESS 0
#define STATS_CURRENT 1
#define STATS_PREVIOUS 2

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
  u_int32_t duration;
  mtev_hash_table metrics;
  char status[256];
};

struct timeval *
noit_check_stats_whence(stats_t *s, struct timeval *n) {
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
u_int32_t
noit_check_stats_duration(stats_t *s, u_int32_t *n) {
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
noit_stats_set_duration(noit_check_t *c, u_int32_t t) {
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
  mtev_hash_init(&n->metrics);
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
static int text_size_limit = DEFAULT_TEXT_METRIC_SIZE_LIMIT;
static int reg_module_id = 0;
static char *reg_module_names[MAX_MODULE_REGISTRATIONS] = { NULL };
static int reg_module_used = -1;
static u_int64_t check_completion_count = 0ULL;
static u_int64_t check_metrics_seen = 0ULL;
static pthread_mutex_t polls_lock = PTHREAD_MUTEX_INITIALIZER;
static mtev_hash_table polls = MTEV_HASH_EMPTY;
static mtev_hash_table dns_ignore_list = MTEV_HASH_EMPTY;
static mtev_skiplist watchlist = { 0 };
static mtev_skiplist polls_by_name = { 0 };
static u_int32_t __config_load_generation = 0;
static unsigned short check_slots_count[60000 / SCHEDULE_GRANULARITY] = { 0 },
                      check_slots_seconds_count[60] = { 0 };
static mtev_boolean priority_scheduling = mtev_false;
static int priority_dead_zone_seconds = 3;

static noit_check_t *
noit_poller_lookup__nolock(uuid_t in) {
  void *vcheck;
  if(mtev_hash_retrieve(&polls, (char *)in, UUID_SIZE, &vcheck))
    return (noit_check_t *)vcheck;
  return NULL;
}
static noit_check_t *
noit_poller_lookup_by_name__nolock(char *target, char *name) {
  noit_check_t tmp_check;
  memset(&tmp_check, 0, sizeof(tmp_check));
  tmp_check.target = target;
  tmp_check.name = name;
  return mtev_skiplist_find(&polls_by_name, &tmp_check, NULL);
}

static int
noit_console_show_timing_slots(mtev_console_closure_t ncct,
                               int argc, char **argv,
                               mtev_console_state_t *dstate,
                               void *closure) {
  int i, j;
  const int upl = (60000 / SCHEDULE_GRANULARITY) / 60;
  for(i=0;i<60;i++) {
    nc_printf(ncct, "[%02d] %04d: ", i, check_slots_seconds_count[i]);
    for(j=i*upl;j<(i+1)*upl;j++) {
      char cp = '!';
      if(check_slots_count[j] < 10) cp = '0' + check_slots_count[j];
      else if(check_slots_count[j] < 36) cp = 'a' + (check_slots_count[j] - 10);
      nc_printf(ncct, "%c", cp);
    }
    nc_printf(ncct, "\n");
  }
  return 0;
}
static int
noit_check_add_to_list(noit_check_t *new_check, const char *newname) {
  char *oldname = NULL, *newnamecopy;
  if(newname) {
    /* track this stuff outside the lock to avoid allocs */
    oldname = new_check->name;
    newnamecopy = strdup(newname);
  }
  pthread_mutex_lock(&polls_lock);
  if(!(new_check->flags & NP_TRANSIENT)) {
    assert(new_check->name || newname);
    /* This remove could fail -- no big deal */
    if(new_check->name != NULL)
      mtev_skiplist_remove(&polls_by_name, new_check, NULL);

    /* optional update the name (at the critical point) */
    if(newname) new_check->name = newnamecopy;

    /* This insert could fail.. which means we have a conflict on
     * target`name.  That should result in the check being disabled. */
    if(!mtev_skiplist_insert(&polls_by_name, new_check)) {
      mtevL(noit_error, "Check %s`%s disabled due to naming conflict\n",
            new_check->target, new_check->name);
      new_check->flags |= NP_DISABLED;
    }
    if(oldname) free(oldname);
  }
  pthread_mutex_unlock(&polls_lock);
  return 1;
}

u_int64_t noit_check_metric_count() {
  return check_metrics_seen;
}
void noit_check_metric_count_add(int add) {
  mtev_atomic64_t *n = (mtev_atomic64_t *)&check_metrics_seen;
  mtev_atomic64_t v = (mtev_atomic64_t)add;
  mtev_atomic_add64(n, v);
}

u_int64_t noit_check_completion_count() {
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
    gettimeofday(&now, NULL);
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
void
noit_poller_process_checks(const char *xpath) {
  int i, flags, cnt = 0, found;
  mtev_conf_section_t *sec;
  __config_load_generation++;
  sec = mtev_conf_get_sections(NULL, xpath, &cnt);
  for(i=0; i<cnt; i++) {
    void *vcheck;
    char uuid_str[37];
    char target[256] = "";
    char module[256] = "";
    char name[256] = "";
    char filterset[256] = "";
    char oncheck[1024] = "";
    char resolve_rtype[16] = "";
    int ridx;
    int no_period = 0;
    int no_oncheck = 0;
    int period = 0, timeout = 0;
    mtev_boolean disabled = mtev_false, busted = mtev_false;
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

#define NEXT(...) mtevL(noit_stderr, __VA_ARGS__); continue
#define MYATTR(type,a,...) mtev_conf_get_##type(sec[i], "@" #a, __VA_ARGS__)
#define INHERIT(type,a,...) \
  mtev_conf_get_##type(sec[i], "ancestor-or-self::node()/@" #a, __VA_ARGS__)

    if(!MYATTR(stringbuf, uuid, uuid_str, sizeof(uuid_str))) {
      mtevL(noit_stderr, "check %d has no uuid\n", i+1);
      continue;
    }

    MYATTR(int64, seq, &config_seq);

    if(uuid_parse(uuid_str, uuid)) {
      mtevL(noit_stderr, "check uuid: '%s' is invalid\n", uuid_str);
      continue;
    }

    if(!INHERIT(stringbuf, target, target, sizeof(target))) {
      mtevL(noit_stderr, "check uuid: '%s' has no target\n", uuid_str);
      busted = mtev_true;
    }
    if(!noit_check_validate_target(target)) {
      mtevL(noit_stderr, "check uuid: '%s' has malformed target\n", uuid_str);
      busted = mtev_true;
    }
    if(!INHERIT(stringbuf, module, module, sizeof(module))) {
      mtevL(noit_stderr, "check uuid: '%s' has no module\n", uuid_str);
      busted = mtev_true;
    }

    if(!INHERIT(stringbuf, filterset, filterset, sizeof(filterset)))
      filterset[0] = '\0';
    
    if (!INHERIT(stringbuf, resolve_rtype, resolve_rtype, sizeof(resolve_rtype)))
      strlcpy(resolve_rtype, PREFER_IPV4, sizeof(resolve_rtype));

    if(!MYATTR(stringbuf, name, name, sizeof(name)))
      strlcpy(name, module, sizeof(name));

    if(!noit_check_validate_name(name)) {
      mtevL(noit_stderr, "check uuid: '%s' has malformed name\n", uuid_str);
      busted = mtev_true;
    }

    if(!INHERIT(int, period, &period) || period == 0)
      no_period = 1;

    if(!INHERIT(stringbuf, oncheck, oncheck, sizeof(oncheck)) || !oncheck[0])
      no_oncheck = 1;

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
    if(!INHERIT(int, timeout, &timeout)) {
      mtevL(noit_stderr, "check uuid: '%s' has no timeout\n", uuid_str);
      busted = mtev_true;
    }
    if(!no_period && timeout >= period) {
      mtevL(noit_stderr, "check uuid: '%s' timeout > period\n", uuid_str);
      timeout = period/2;
    }
    options = mtev_conf_get_hash(sec[i], "config");
    for(ridx=0; ridx<reg_module_id; ridx++) {
      moptions[ridx] = mtev_conf_get_namespaced_hash(sec[i], "config",
                                                     reg_module_names[ridx]);
    }

    INHERIT(boolean, disable, &disabled);
    flags = 0;
    if(busted) flags |= (NP_UNCONFIG|NP_DISABLED);
    else if(disabled) flags |= NP_DISABLED;

    flags |= noit_calc_rtype_flag(resolve_rtype);

    pthread_mutex_lock(&polls_lock);
    found = mtev_hash_retrieve(&polls, (char *)uuid, UUID_SIZE, &vcheck);
    if(found) {
      noit_check_t *check = (noit_check_t *)vcheck;
      /* Possibly reset the seq */
      if(config_seq < 0) check->config_seq = 0;

      /* Otherwise note a non-increasing sequence */
      if(check->config_seq > config_seq) backdated = mtev_true;
    }
    pthread_mutex_unlock(&polls_lock);
    if(found)
      noit_poller_deschedule(uuid, mtev_false);
    if(backdated) {
      mtevL(noit_error, "Check config seq backwards, ignored\n");
      if(found) noit_check_log_delete((noit_check_t *)vcheck);
    }
    else {
      noit_poller_schedule(target, module, name, filterset, options,
                           moptions_used ? moptions : NULL,
                           period, timeout, oncheck[0] ? oncheck : NULL,
                           config_seq, flags, uuid, out_uuid);
      mtevL(noit_debug, "loaded uuid: %s\n", uuid_str);
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
  if(sec) free(sec);
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
      mtevL(noit_debug, "Skipping %s`%s, disabled.\n",
            check->target, check->name);
  }
  else {
    if(!mod) {
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
    while(i>0) noit_poller_deschedule(tofree[--i]->checkid, mtev_true);
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
      mtevL(noit_debug, "Searching for upstream trigger on %s\n", name);
      parent = NULL;
      if(uuid_parse(check->oncheck, id) == 0) {
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
        mtevL(noit_debug, "Causal map %s`%s --> %s`%s\n",
              parent->target, parent->name, check->target, check->name);
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
noit_check_dns_ignore_tld(const char* extension, const char* ignore) {
  mtev_hash_replace(&dns_ignore_list, strdup(extension), strlen(extension), strdup(ignore), NULL, NULL);
}
static void 
noit_check_dns_ignore_list_init() {
  mtev_conf_section_t* dns;
  int cnt;

  dns = mtev_conf_get_sections(NULL, "/noit/dns/extension", &cnt);
  if(dns) {
    int i = 0;
    for (i = 0; i < cnt; i++) {
      char* extension;
      char* ignore;
      if(!mtev_conf_get_string(dns[i], "self::node()/@value", &extension)) {
        continue;
      }
      if(!mtev_conf_get_string(dns[i], "self::node()/@ignore", &ignore)) {
        continue;
      }
      noit_check_dns_ignore_tld(extension, ignore);
    }
  }
}
static void
noit_check_poller_scheduling_init() {
  mtev_conf_get_boolean(NULL, "//checks/@priority_scheduling", &priority_scheduling);
}
void
noit_poller_init() {
  srand48((getpid() << 16) ^ time(NULL));
  noit_check_poller_scheduling_init();
  noit_check_resolver_init();
  noit_check_tools_init();
  mtev_skiplist_init(&polls_by_name);
  mtev_skiplist_set_compare(&polls_by_name, __check_name_compare,
                            __check_name_compare);
  mtev_skiplist_add_index(&polls_by_name, __check_target_ip_compare,
                            __check_target_ip_compare);
  mtev_skiplist_add_index(&polls_by_name, __check_target_compare,
                            __check_target_compare);
  mtev_skiplist_init(&watchlist);
  mtev_skiplist_set_compare(&watchlist, __watchlist_compare,
                            __watchlist_compare);
  register_console_check_commands();
  eventer_name_callback("check_recycle_bin_processor",
                        check_recycle_bin_processor);
  eventer_add_in_s_us(check_recycle_bin_processor, NULL, RECYCLE_INTERVAL, 0);
  mtev_conf_get_int(NULL, "noit/@text_size_limit", &text_size_limit);
  if (text_size_limit <= 0) {
    text_size_limit = DEFAULT_TEXT_METRIC_SIZE_LIMIT;
  }
  noit_check_dns_ignore_list_init();
  noit_poller_reload(NULL);
}

int
noit_poller_check_count() {
  return polls_by_name.size;
}

int
noit_poller_transient_check_count() {
  return watchlist.size;
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
  new_check = mtev_memory_safe_calloc(1, sizeof(*new_check));
  memcpy(new_check, checker, sizeof(*new_check));
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
  mtev_hash_merge_as_dict(new_check->config, checker->config);
  new_check->module_configs = NULL;
  new_check->module_metadata = NULL;

  for(i=0; i<reg_module_id; i++) {
    void *src_metadata;
    mtev_hash_table *src_mconfig;
    src_mconfig = noit_check_get_module_config(checker, i);
    if(src_mconfig) {
      mtev_hash_table *t = calloc(1, sizeof(*new_check->config));
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
  int minimum_pi = 1000, granularity_pi = 500;
  mtev_conf_section_t check_node;
  char uuid_str[UUID_STR_LEN + 1];
  char xpath[1024];
  noit_check_t n, *f;

  uuid_unparse_lower(in, uuid_str);

  mtevL(noit_debug, "noit_check_watch(%s,%d)\n", uuid_str, period);
  if(period == 0) {
    return noit_poller_lookup(in);
  }

  /* Find the check */
  snprintf(xpath, sizeof(xpath), "//checks//check[@uuid=\"%s\"]", uuid_str);
  check_node = mtev_conf_get_section(NULL, xpath);
  mtev_conf_get_int(NULL, "//checks/@transient_min_period", &minimum_pi);
  mtev_conf_get_int(NULL, "//checks/@transient_period_granularity", &granularity_pi);
  if(check_node) {
    mtev_conf_get_int(check_node,
                      "ancestor-or-self::node()/@transient_min_period",
                      &minimum_pi);
    mtev_conf_get_int(check_node,
                      "ancestor-or-self::node()/@transient_period_granularity",
                      &granularity_pi);
  }

  /* apply the bounds */
  period /= granularity_pi;
  period *= granularity_pi;
  period = MAX(period, minimum_pi);

  uuid_copy(n.checkid, in);
  n.period = period;

  f = mtev_skiplist_find(&watchlist, &n, NULL);
  if(f) return f;
  f = noit_check_clone(in);
  if(!f) return NULL;
  f->period = period;
  f->timeout = period - 10;
  f->flags |= NP_TRANSIENT;
  mtevL(noit_debug, "Watching %s@%d\n", uuid_str, period);
  mtev_skiplist_insert(&watchlist, f);
  return f;
}

noit_check_t *
noit_check_get_watch(uuid_t in, int period) {
  noit_check_t n, *f;

  uuid_copy(n.checkid, in);
  n.period = period;

  f = mtev_skiplist_find(&watchlist, &n, NULL);
  return f;
}

void
noit_check_transient_add_feed(noit_check_t *check, const char *feed) {
  char *feedcopy;
  if(!check->feeds) {
    check->feeds = calloc(1, sizeof(*check->feeds));
    mtev_skiplist_init(check->feeds);
    mtev_skiplist_set_compare(check->feeds,
                              (mtev_skiplist_comparator_t)strcmp,
                              (mtev_skiplist_comparator_t)strcmp);
  }
  feedcopy = strdup(feed);
  /* No error on failure -- it's already there */
  if(mtev_skiplist_insert(check->feeds, feedcopy) == NULL) free(feedcopy);
  mtevL(noit_debug, "check %s`%s @ %dms has %d feed(s): %s.\n",
        check->target, check->name, check->period, check->feeds->size, feed);
}
void
noit_check_transient_remove_feed(noit_check_t *check, const char *feed) {
  if(!check->feeds) return;
  if(feed) {
    mtevL(noit_debug, "check %s`%s @ %dms removing 1 of %d feeds: %s.\n",
          check->target, check->name, check->period, check->feeds->size, feed);
    mtev_skiplist_remove(check->feeds, feed, free);
  }
  if(check->feeds->size == 0) {
    char uuid_str[UUID_STR_LEN + 1];
    uuid_unparse_lower(check->checkid, uuid_str);
    mtevL(noit_debug, "Unwatching %s@%d\n", uuid_str, check->period);
    mtev_skiplist_remove(&watchlist, check, NULL);
    mtev_skiplist_destroy(check->feeds, free);
    free(check->feeds);
    check->feeds = NULL;
    if(check->flags & NP_TRANSIENT) {
      mtevL(noit_debug, "check %s`%s @ %dms has no more listeners.\n",
            check->target, check->name, check->period);
      check->flags |= NP_KILLED;
    }
    noit_poller_free_check(check);
  }
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
  char old_target_ip[INET6_ADDRSTRLEN];
  union {
    struct in_addr addr4;
    struct in6_addr addr6;
  } a;

  memset(old_target_ip, 0, INET6_ADDRSTRLEN);
  strlcpy(old_target_ip, new_check->target_ip, sizeof(old_target_ip));

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

  new_check->target_family = family;
  memcpy(&new_check->target_addr, &a, sizeof(a));
  new_check->target_ip[0] = '\0';
  if(failed == 0)
    if(inet_ntop(new_check->target_family,
                 &new_check->target_addr,
                 new_check->target_ip,
                 sizeof(new_check->target_ip)) == NULL) {
      mtevL(noit_error, "inet_ntop failed [%s] -> %d\n", ip_str, errno);
    }
  /*
   * new_check->name could be null if this check is being set for the
   * first time.  add_to_list will set it.
   */
  if (new_check->name == NULL ||
      strcmp(old_target_ip, new_check->target_ip) != 0) {
    noit_check_add_to_list(new_check, newname);
  }

  if(new_check->name == NULL && newname != NULL) {
    assert(new_check->flags & NP_TRANSIENT);
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
int
noit_check_update(noit_check_t *new_check,
                  const char *target,
                  const char *name,
                  const char *filterset,
                  mtev_hash_table *config,
                  mtev_hash_table **mconfigs,
                  u_int32_t period,
                  u_int32_t timeout,
                  const char *oncheck,
                  int64_t seq,
                  int flags) {
  char uuid_str[37];
  int mask = NP_DISABLED | NP_UNCONFIG;

  assert(name);
  uuid_unparse_lower(new_check->checkid, uuid_str);
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
    if(uuid_compare(cluster_id, new_check->checkid)) {
      mtevL(mtev_error, "Setting global cluster identity to '%s'\n", uuid_str);
      mtev_cluster_set_self(new_check->checkid);
    }
  }

  if(NOIT_CHECK_RUNNING(new_check)) {
    char module[256];
    uuid_t id, dummy;
    uuid_copy(id, new_check->checkid);
    strlcpy(module, new_check->module, sizeof(module));
    noit_poller_deschedule(id, mtev_false);
    return noit_poller_schedule(target, module, name, filterset,
                                config, mconfigs, period, timeout, oncheck,
                                seq, flags, id, dummy);
  }

  new_check->generation = __config_load_generation;
  if(new_check->target) free(new_check->target);
  new_check->target = strdup(target);

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

  if(config != NULL) {
    mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
    const char *k;
    int klen;
    void *data;
    if(new_check->config) mtev_hash_delete_all(new_check->config, free, free);
    else new_check->config = calloc(1, sizeof(*new_check->config));
    while(mtev_hash_next(config, &iter, &k, &klen, &data)) {
      mtev_hash_store(new_check->config, strdup(k), klen, strdup((char *)data));
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
  new_check->config_seq = seq;

  /* Unset what could be set.. then set what should be set */
  new_check->flags = (new_check->flags & ~mask) | flags;

  check_config_fixup_hook_invoke(new_check);

  if((new_check->flags & NP_TRANSIENT) == 0)
    noit_check_activate(new_check);

  noit_check_add_to_list(new_check, NULL);
  return 0;
}
int
noit_poller_schedule(const char *target,
                     const char *module,
                     const char *name,
                     const char *filterset,
                     mtev_hash_table *config,
                     mtev_hash_table **mconfigs,
                     u_int32_t period,
                     u_int32_t timeout,
                     const char *oncheck,
                     int64_t seq,
                     int flags,
                     uuid_t in,
                     uuid_t out) {
  noit_check_t *new_check;
  new_check = mtev_memory_safe_calloc(1, sizeof(*new_check));
  if(!new_check) return -1;

  /* The module and the UUID can never be changed */
  new_check->module = strdup(module);
  if(uuid_is_null(in))
    uuid_generate(new_check->checkid);
  else
    uuid_copy(new_check->checkid, in);

  new_check->statistics = noit_check_stats_set_calloc();
  noit_check_update(new_check, target, name, filterset, config, mconfigs,
                    period, timeout, oncheck, seq, flags);
  assert(mtev_hash_store(&polls,
                         (char *)new_check->checkid, UUID_SIZE,
                         new_check));
  uuid_copy(out, new_check->checkid);
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
static void recycle_check(noit_check_t *checker) {
  struct _checker_rcb *n = malloc(sizeof(*n));
  n->checker = checker;
  n->next = checker_rcb;
  checker_rcb = n;
}
void
noit_poller_free_check(noit_check_t *checker) {
  noit_module_t *mod;

  if(checker->flags & NP_RUNNING) {
    recycle_check(checker);
    return;
  }

  mod = noit_module_lookup(checker->module);
  if(mod && mod->cleanup) mod->cleanup(mod, checker);
  if(checker->fire_event) {
     eventer_remove(checker->fire_event);
     free(checker->fire_event->closure);
     eventer_free(checker->fire_event);
     checker->fire_event = NULL;
  }
  if(checker->closure) free(checker->closure);
  if(checker->target) free(checker->target);
  if(checker->module) free(checker->module);
  if(checker->name) free(checker->name);
  if(checker->config) {
    mtev_hash_destroy(checker->config, free, free);
    free(checker->config);
    checker->config = NULL;
  }
  if(checker->module_metadata) {
    int i;
    for(i=0; i<reg_module_id; i++) {
      struct vp_w_free *tuple;
      tuple = checker->module_metadata[i];
      if(tuple) {
        if(tuple->freefunc) tuple->freefunc(tuple->ptr);
        free(tuple);
      }
    }
    free(checker->module_metadata);
  }
  if(checker->module_configs) {
    int i;
    for(i=0; i<reg_module_id; i++) {
      if(checker->module_configs[i]) {
        mtev_hash_destroy(checker->module_configs[i], free, free);
        free(checker->module_configs[i]);
      }
    }
    free(checker->module_configs);
  }

  mtev_memory_safe_free(stats_inprogress(checker));
  mtev_memory_safe_free(stats_current(checker));
  mtev_memory_safe_free(stats_previous(checker));

  mtev_memory_safe_free(checker);
}
static int
check_recycle_bin_processor(eventer_t e, int mask, void *closure,
                            struct timeval *now) {
  static struct timeval one_minute = { RECYCLE_INTERVAL, 0L };
  struct _checker_rcb *prev = NULL, *curr = checker_rcb;
  mtevL(noit_debug, "Scanning check recycle bin\n");
  while(curr) {
    if(!(curr->checker->flags & NP_RUNNING)) {
      mtevL(noit_debug, "Check is ready to free.\n");
      noit_poller_free_check(curr->checker);
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
  add_timeval(*now, one_minute, &e->whence);
  return EVENTER_TIMER;
}

int
noit_poller_deschedule(uuid_t in, mtev_boolean log) {
  void *vcheck;
  noit_check_t *checker;
  if(mtev_hash_retrieve(&polls,
                        (char *)in, UUID_SIZE,
                        &vcheck) == 0) {
    return -1;
  }
  checker = (noit_check_t *)vcheck;
  checker->flags |= (NP_DISABLED|NP_KILLED);

  if(log) noit_check_log_delete(checker);

  assert(mtev_skiplist_remove(&polls_by_name, checker, NULL));
  assert(mtev_hash_delete(&polls, (char *)in, UUID_SIZE, NULL, NULL));

  noit_poller_free_check(checker);
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

  tlist = mtev_skiplist_find(polls_by_name.index,
                             __check_target_ip_compare, NULL);

  pthread_mutex_lock(&polls_lock);
  /* First pass to count */
  memset(&pivot, 0, sizeof(pivot));
  strlcpy(pivot.target_ip, (char*)target_ip, sizeof(pivot.target_ip));
  pivot.name = "";
  pivot.target = "";
  mtev_skiplist_find_neighbors(tlist, &pivot, NULL, NULL, &next);
  while(next && next->data) {
    noit_check_t *check = next->data;
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
  while(next && next->data) {
    noit_check_t *check = next->data;
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

  tlist = mtev_skiplist_find(polls_by_name.index,
                             __check_target_compare, NULL);

  pthread_mutex_lock(&polls_lock);
  memset(&pivot, 0, sizeof(pivot));
  pivot.name = "";
  pivot.target = (char *)target;
  mtev_skiplist_find_neighbors(tlist, &pivot, NULL, NULL, &next);
  while(next && next->data) {
    noit_check_t *check = next->data;
    if(strcmp(check->target, target)) break;
    todo_count++;
    mtev_skiplist_next(tlist, &next);
  }

  if(todo_count > 8192) todo = malloc(todo_count * sizeof(*todo));

  memset(&pivot, 0, sizeof(pivot));
  pivot.name = "";
  pivot.target = (char *)target;
  mtev_skiplist_find_neighbors(tlist, &pivot, NULL, NULL, &next);
  while(next && next->data) {
    noit_check_t *check = next->data;
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

  if(polls_by_name.size == 0) return 0;

  max_count = polls_by_name.size;
  todo = malloc(max_count * sizeof(*todo));

  pthread_mutex_lock(&polls_lock);
  for(iter = mtev_skiplist_getlist(&polls_by_name); iter;
      mtev_skiplist_next(&polls_by_name, &iter)) {
    if(count < max_count) todo[count++] = (noit_check_t *)iter->data;
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
  c->array[c->idx++] = check;
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
noit_check_xpath(char *xpath, int len,
                 const char *base, const char *arg) {
  uuid_t checkid;
  int base_trailing_slash;
  char argcopy[1024], *target, *module, *name;

  base_trailing_slash = (base[strlen(base)-1] == '/');
  xpath[0] = '\0';
  argcopy[0] = '\0';
  if(arg) strlcpy(argcopy, arg, sizeof(argcopy));

  if(uuid_parse(argcopy, checkid) == 0) {
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
    uuid_unparse_lower(check->checkid, uuid_str);
    snprintf(xpath, len, "/noit/checks%s%s/check[@uuid=\"%s\"]",
             base, base_trailing_slash ? "" : "/", uuid_str);
  }
  return strlen(xpath);
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
  assert(!(check->flags & NP_RUNNING));
  check->flags |= NP_RUNNING;
  inp = noit_check_get_stats_inprogress(check);
  gettimeofday(&now, NULL);
  noit_check_stats_whence(inp, &now);
  snprintf(buff, sizeof(buff), "check[%s] implementation offline",
           check->module);
  noit_check_stats_status(inp, buff);
  noit_check_set_stats(check);
  check->flags &= ~NP_RUNNING;
  return 0;
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

static void
__mark_metric_logged(stats_t *newstate, const char *metric_name) {
  void *vm;
  if(mtev_hash_retrieve(&newstate->metrics,
                        metric_name, strlen(metric_name), &vm)) {
    ((metric_t *)vm)->logged = mtev_true;
  }
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
      int len = strlen((char*)value) + 1;
      return ((len >= text_size_limit) ? text_size_limit+1 : len);
    }
    case METRIC_ABSENT:
    case METRIC_NULL:
    case METRIC_GUESS:
      break;
  }
  assert(type != type);
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
     u_int64_t *v;
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

static void
cleanse_metric_name(char *m) {
  char *cp;
  for(cp = m; *cp; cp++)
    if(!isprint(*cp)) *cp=' ';
  for(cp--; *cp == ' ' && cp > m; cp--) /* always leave first char */
    *cp = '\0';
}

int
noit_stats_populate_metric(metric_t *m, const char *name, metric_type_t type,
                           const void *value) {
  void *replacement = NULL;

  /* If we are passed a null name, we want to quit populating the metric...
   * no reason we should ever have a null metric name */
  if (!name) {
    return -1;
  }

  m->metric_name = strdup(name);
  cleanse_metric_name(m->metric_name);

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
  if(newstate == NULL)
    newstate = stats_inprogress(check);
  if(mtev_hash_retrieve(&newstate->metrics, name, strlen(name), &v))
    return (metric_t *)v;
  return NULL;
}

void
noit_stats_set_metric(noit_check_t *check,
                      const char *name, metric_type_t type,
                      const void *value) {
  stats_t *c;
  metric_t *m = mtev_memory_safe_malloc_cleanup(sizeof(*m), noit_check_safe_free_metric);
  memset(m, 0, sizeof(*m));
  if(noit_stats_populate_metric(m, name, type, value)) {
    mtev_memory_safe_free(m);
    return;
  }
  noit_check_metric_count_add(1);
  c = noit_check_get_stats_inprogress(check);
  check_stats_set_metric_hook_invoke(check, c, m);
  __stats_add_metric(c, m);
}
void
noit_stats_set_metric_coerce(noit_check_t *check,
                             const char *name, metric_type_t t,
                             const char *v) {
  char *endptr;
  stats_t *c;
  c = noit_check_get_stats_inprogress(check);
  if(v == NULL) {
   bogus:
    check_stats_set_metric_coerce_hook_invoke(check, c, name, t, v, mtev_false);
    noit_stats_set_metric(check, name, t, NULL);
    return;
  }
  switch(t) {
    case METRIC_STRING:
      noit_stats_set_metric(check, name, t, v);
      break;
    case METRIC_INT32:
    {
      int32_t val;
      val = strtol(v, &endptr, 10);
      if(endptr == v) goto bogus;
      noit_stats_set_metric(check, name, t, &val);
      break;
    }
    case METRIC_UINT32:
    {
      u_int32_t val;
      val = strtoul(v, &endptr, 10);
      if(endptr == v) goto bogus;
      noit_stats_set_metric(check, name, t, &val);
      break;
    }
    case METRIC_INT64:
    {
      int64_t val;
      val = strtoll(v, &endptr, 10);
      if(endptr == v) goto bogus;
      noit_stats_set_metric(check, name, t, &val);
      break;
    }
    case METRIC_UINT64:
    {
      u_int64_t val;
      val = strtoull(v, &endptr, 10);
      if(endptr == v) goto bogus;
      noit_stats_set_metric(check, name, t, &val);
      break;
    }
    case METRIC_DOUBLE:
    {
      double val;
      val = strtod(v, &endptr);
      if(endptr == v) goto bogus;
      noit_stats_set_metric(check, name, t, &val);
      break;
    }
    case METRIC_GUESS:
      noit_stats_set_metric(check, name, t, v);
      break;
    case METRIC_ABSENT:
    case METRIC_NULL:
      assert(0 && "ABSENT and NULL metrics may not be passed to noit_stats_set_metric_coerce");
  }
  check_stats_set_metric_coerce_hook_invoke(check, c, name, t, v, mtev_true);
}
void
noit_stats_log_immediate_metric(noit_check_t *check,
                                const char *name, metric_type_t type,
                                const void *value) {
  struct timeval now;
  stats_t *c;
  metric_t *m = mtev_memory_safe_malloc_cleanup(sizeof(*m), noit_check_safe_free_metric);
  memset(m, 0, sizeof(*m));
  if(noit_stats_populate_metric(m, name, type, value)) {
    mtev_memory_safe_free(m);
    return;
  }
  gettimeofday(&now, NULL);
  noit_check_log_metric(check, &now, m);
  mtev_memory_safe_free(m);
  c = noit_check_get_stats_inprogress(check);
  __mark_metric_logged(c, name);
}

void
noit_check_passive_set_stats(noit_check_t *check) {
  int i, nwatches = 0;
  mtev_skiplist_node *next;
  noit_check_t n;
  noit_check_t *watches[8192];

  uuid_copy(n.checkid, check->checkid);
  n.period = 0;

  noit_check_set_stats(check);

  pthread_mutex_lock(&polls_lock);
  mtev_skiplist_find_neighbors(&watchlist, &n, NULL, NULL, &next);
  while(next && next->data && nwatches < 8192) {
    noit_check_t *wcheck = next->data;
    if(uuid_compare(n.checkid, wcheck->checkid)) break;
    watches[nwatches++] = wcheck;
    mtev_skiplist_next(&watchlist, &next);
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
void
noit_check_set_stats(noit_check_t *check) {
  int report_change = 0;
  char *cp;
  dep_list_t *dep;
  stats_t *old, *prev, *current;

  if(check_set_stats_hook_invoke(check) == MTEV_HOOK_ABORT) return;

  old = stats_previous(check);
  prev = stats_previous(check) = stats_current(check);
  current = stats_current(check) = stats_inprogress(check);
  stats_inprogress(check) = noit_check_stats_alloc();
  
  if(old) {
    mtev_memory_safe_free(old);
  }

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

  mtevL(noit_debug, "%s`%s <- [%s]\n", check->target, check->name,
        current ? current->status : "null");
  if(report_change) {
    mtevL(noit_debug, "%s`%s -> [%s:%s]\n",
          check->target, check->name,
          noit_check_available_string(current ? current->available : NP_UNKNOWN),
          noit_check_state_string(current ? current->state : NP_UNKNOWN));
  }

  if(NOIT_CHECK_STATUS_ENABLED()) {
    char id[UUID_STR_LEN+1];
    uuid_unparse_lower(check->checkid, id);
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
      mtevL(noit_debug, "Firing %s`%s in response to %s`%s\n",
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

  nc_printf(ncct, "%d active watches.\n", watchlist.size);
  pthread_mutex_lock(&polls_lock);
  for(iter = mtev_skiplist_getlist(&watchlist); iter && nwatches < 8192;
      mtev_skiplist_next(&watchlist, &iter)) {
    noit_check_t *check = iter->data;
    watches[nwatches++] = check;
  }
  pthread_mutex_unlock(&polls_lock);

  for(i=0;i<nwatches;i++) {
    noit_check_t *check = watches[i];
    char uuid_str[UUID_STR_LEN + 1];

    uuid_unparse_lower(check->checkid, uuid_str);
    nc_printf(ncct, "%s:\n\t[%s`%s`%s]\n\tPeriod: %dms\n\tFeeds[%d]:\n",
              uuid_str, check->target, check->module, check->name,
              check->period, check->feeds ? check->feeds->size : 0);
    if(check->feeds && check->feeds->size) {
      for(fiter = mtev_skiplist_getlist(check->feeds); fiter;
          mtev_skiplist_next(check->feeds, &fiter)) {
        nc_printf(ncct, "\t\t%s\n", (const char *)fiter->data);
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
  snprintf(out, sizeof(out), "%s`%s (%s [%x])", check->target, check->name,
           check->target_ip, check->flags);
  uuid_unparse_lower(check->checkid, uuid_str);
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
      uuid_unparse_lower(check->checkid, uuid_str);
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
    cmd = mtev_skiplist_find(&dstate->cmds, "attribute", NULL);
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
      uuid_unparse_lower(check->checkid, uuid_str);
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

  max_count = tlist->size;
  if(max_count == 0) return 0;
  todo = malloc(max_count * sizeof(*todo));

  pthread_mutex_lock(&polls_lock);
  for(iter = mtev_skiplist_getlist(tlist); i < max_count && iter;
      mtev_skiplist_next(tlist, &iter)) {
    todo[i++] = iter->data;
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
  return noit_console_short_checks_sl(ncct, &polls_by_name);
}

static int
noit_console_show_checks_target(mtev_console_closure_t ncct,
                                   int argc, char **argv,
                                   mtev_console_state_t *dstate,
                                   void *closure) {
  return noit_console_short_checks_sl(ncct,
           mtev_skiplist_find(polls_by_name.index,
           __check_target_compare, NULL));
}

static int
noit_console_show_checks_target_ip(mtev_console_closure_t ncct,
                                   int argc, char **argv,
                                   mtev_console_state_t *dstate,
                                   void *closure) {
  return noit_console_short_checks_sl(ncct,
           mtev_skiplist_find(polls_by_name.index,
           __check_target_ip_compare, NULL));
}

static void
register_console_check_commands() {
  mtev_console_state_t *tl;
  cmd_info_t *showcmd;

  tl = mtev_console_state_initial();
  showcmd = mtev_console_state_get_cmd(tl, "show");
  assert(showcmd && showcmd->dstate);

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
  mtevL(noit_debug, "Registered module %s as %d\n", name, i);
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
  assert(reg_module_used == reg_module_id);
  if(idx >= reg_module_id || idx < 0) return NULL;
  return reg_module_names[idx];
}

void
noit_check_set_module_metadata(noit_check_t *c, int idx, void *md, void (*freefunc)(void *)) {
  struct vp_w_free *tuple;
  if(reg_module_used < 0) reg_module_used = reg_module_id;
  assert(reg_module_used == reg_module_id);
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
  assert(reg_module_used == reg_module_id);
  if(idx >= reg_module_id || idx < 0) return;
  if(!c->module_configs) c->module_configs = calloc(reg_module_id, sizeof(mtev_hash_table *));
  c->module_configs[idx] = config;
}
void *
noit_check_get_module_metadata(noit_check_t *c, int idx) {
  struct vp_w_free *tuple;
  if(reg_module_used < 0) reg_module_used = reg_module_id;
  assert(reg_module_used == reg_module_id);
  if(idx >= reg_module_id || idx < 0 || !c->module_metadata) return NULL;
  tuple = c->module_metadata[idx];
  return tuple ? tuple->ptr : NULL;
}
mtev_hash_table *
noit_check_get_module_config(noit_check_t *c, int idx) {
  if(reg_module_used < 0) reg_module_used = reg_module_id;
  assert(reg_module_used == reg_module_id);
  if(idx >= reg_module_id || idx < 0 || !c->module_configs) return NULL;
  return c->module_configs[idx];
}
