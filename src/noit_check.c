/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
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
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "noit_defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "dtrace_probes.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"
#include "utils/noit_skiplist.h"
#include "noit_conf.h"
#include "noit_check.h"
#include "noit_module.h"
#include "noit_console.h"
#include "noit_check_tools.h"
#include "noit_check_resolver.h"
#include "eventer/eventer.h"

NOIT_HOOK_IMPL(check_stats_set_metric,
  (noit_check_t *check, stats_t *stats, metric_t *m),
  void *, closure,
  (void *closure, noit_check_t *check, stats_t *stats, metric_t *m),
  (closure,check,stats,m))

NOIT_HOOK_IMPL(check_passive_log_stats,
  (noit_check_t *check),
  void *, closure,
  (void *closure, noit_check_t *check),
  (closure,check))

NOIT_HOOK_IMPL(check_log_stats,
  (noit_check_t *check),
  void *, closure,
  (void *closure, noit_check_t *check),
  (closure,check))

/* 20 ms slots over 60 second for distribution */
#define SCHEDULE_GRANULARITY 20
#define SLOTS_PER_SECOND (1000/SCHEDULE_GRANULARITY)
#define MAX_MODULE_REGISTRATIONS 64

/* used to manage per-check generic module metadata */
struct vp_w_free {
  void *ptr;
  void (*freefunc)(void *);
};

static int reg_module_id = 0;
static char *reg_module_names[MAX_MODULE_REGISTRATIONS] = { NULL };
static int reg_module_used = -1;
static u_int64_t check_completion_count = 0;
static noit_hash_table polls = NOIT_HASH_EMPTY;
static noit_skiplist watchlist = { 0 };
static noit_skiplist polls_by_name = { 0 };
static u_int32_t __config_load_generation = 0;
static unsigned short check_slots_count[60000 / SCHEDULE_GRANULARITY] = { 0 },
                      check_slots_seconds_count[60] = { 0 };

static int
noit_console_show_timing_slots(noit_console_closure_t ncct,
                               int argc, char **argv,
                               noit_console_state_t *dstate,
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

u_int64_t noit_check_completion_count() {
  return check_completion_count;
}
static void register_console_check_commands();
static int check_recycle_bin_processor(eventer_t, int, void *,
                                       struct timeval *);

static int
check_slots_find_smallest(int sec) {
  int i, j, cyclic, random_offset, jbase = 0, mini = 0, minj = 0;
  unsigned short min_running_i = 0xffff, min_running_j = 0xffff;
  for(i=0;i<60;i++) {
    int adj_i = (i + sec) % 60;
    if(check_slots_seconds_count[adj_i] < min_running_i) {
      min_running_i = check_slots_seconds_count[adj_i];
      mini = adj_i;
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
  struct timeval now, period;
  int balance_ms;

  if(!_now) {
    gettimeofday(&now, NULL);
    _now = &now;
  }
  period.tv_sec = check->period / 1000;
  period.tv_usec = (check->period % 1000) * 1000;
  sub_timeval(*_now, period, lc);

  if(!(check->flags & NP_TRANSIENT) && check->period) {
    balance_ms = check_slots_find_smallest(_now->tv_sec+1);
    lc->tv_sec = (lc->tv_sec / 60) * 60 + balance_ms / 1000;
    lc->tv_usec = (balance_ms % 1000) * 1000;
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
  /* now, we're going to do an even distribution using the slots */
  if(!(check->flags & NP_TRANSIENT)) check_slots_inc_tv(lc);
}
void
noit_poller_process_checks(const char *xpath) {
  int i, flags, cnt = 0;
  noit_conf_section_t *sec;
  __config_load_generation++;
  sec = noit_conf_get_sections(NULL, xpath, &cnt);
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
    noit_boolean disabled = noit_false, busted = noit_false;
    uuid_t uuid, out_uuid;
    noit_hash_table *options;
    noit_hash_table **moptions = NULL;
    noit_boolean moptions_used = noit_false;

    if(reg_module_id > 0) {
      moptions = alloca(reg_module_id * sizeof(noit_hash_table *));
      memset(moptions, 0, reg_module_id * sizeof(noit_hash_table *));
    }

#define NEXT(...) noitL(noit_stderr, __VA_ARGS__); continue
#define MYATTR(type,a,...) noit_conf_get_##type(sec[i], "@" #a, __VA_ARGS__)
#define INHERIT(type,a,...) \
  noit_conf_get_##type(sec[i], "ancestor-or-self::node()/@" #a, __VA_ARGS__)

    if(!MYATTR(stringbuf, uuid, uuid_str, sizeof(uuid_str))) {
      noitL(noit_stderr, "check %d has no uuid\n", i+1);
      continue;
    }

    if(uuid_parse(uuid_str, uuid)) {
      noitL(noit_stderr, "check uuid: '%s' is invalid\n", uuid_str);
      continue;
    }

    if(!INHERIT(stringbuf, target, target, sizeof(target))) {
      noitL(noit_stderr, "check uuid: '%s' has no target\n", uuid_str);
      busted = noit_true;
    }
    if(!INHERIT(stringbuf, module, module, sizeof(module))) {
      noitL(noit_stderr, "check uuid: '%s' has no module\n", uuid_str);
      busted = noit_true;
    }

    if(!INHERIT(stringbuf, filterset, filterset, sizeof(filterset)))
      filterset[0] = '\0';
    
    if (!INHERIT(stringbuf, resolve_rtype, resolve_rtype, sizeof(resolve_rtype)))
      strlcpy(resolve_rtype, PREFER_IPV4, sizeof(resolve_rtype));

    if(!MYATTR(stringbuf, name, name, sizeof(name)))
      strlcpy(name, module, sizeof(name));

    if(!INHERIT(int, period, &period) || period == 0)
      no_period = 1;

    if(!INHERIT(stringbuf, oncheck, oncheck, sizeof(oncheck)) || !oncheck[0])
      no_oncheck = 1;

    if(no_period && no_oncheck) {
      noitL(noit_stderr, "check uuid: '%s' has neither period nor oncheck\n",
            uuid_str);
      busted = noit_true;
    }
    if(!(no_period || no_oncheck)) {
      noitL(noit_stderr, "check uuid: '%s' has oncheck and period.\n",
            uuid_str);
      busted = noit_true;
    }
    if(!INHERIT(int, timeout, &timeout)) {
      noitL(noit_stderr, "check uuid: '%s' has no timeout\n", uuid_str);
      busted = noit_true;
    }
    if(!no_period && timeout >= period) {
      noitL(noit_stderr, "check uuid: '%s' timeout > period\n", uuid_str);
      timeout = period/2;
    }
    options = noit_conf_get_hash(sec[i], "config");
    for(ridx=0; ridx<reg_module_id; ridx++) {
      moptions[ridx] = noit_conf_get_namespaced_hash(sec[i], "config",
                                                     reg_module_names[ridx]);
      if(moptions[ridx]) moptions_used = noit_true;
    }

    INHERIT(boolean, disable, &disabled);
    flags = 0;
    if(busted) flags |= (NP_UNCONFIG|NP_DISABLED);
    else if(disabled) flags |= NP_DISABLED;

    flags |= noit_calc_rtype_flag(resolve_rtype);

    if(noit_hash_retrieve(&polls, (char *)uuid, UUID_SIZE,
                          &vcheck)) {
      noit_check_t *existing_check = (noit_check_t *)vcheck;
      /* Once set, it cannot be checked if the check is live */
      assert(!existing_check->module || !existing_check->module[0] ||
             !strcmp(existing_check->module, module) ||
             !NOIT_CHECK_LIVE(existing_check));
      /* Set it if it is unset or being changed */
      if(!existing_check->module || !existing_check->module[0] ||
         strcmp(existing_check->module, module)) {
        if(existing_check->module) free(existing_check->module);
        existing_check->module = strdup(module);
      }
      noit_check_update(existing_check, target, name, filterset, options,
                           moptions_used ? moptions : NULL,
                           period, timeout, oncheck[0] ? oncheck : NULL,
                           flags);
      noitL(noit_debug, "reloaded uuid: %s\n", uuid_str);
    }
    else {
      noit_poller_schedule(target, module, name, filterset, options,
                           moptions_used ? moptions : NULL,
                           period, timeout, oncheck[0] ? oncheck : NULL,
                           flags, uuid, out_uuid);
      noitL(noit_debug, "loaded uuid: %s\n", uuid_str);
    }

    for(ridx=0; ridx<reg_module_id; ridx++) {
      if(moptions[ridx]) {
        noit_hash_destroy(moptions[ridx], free, free);
        free(moptions[ridx]);
      }
    }
    noit_hash_destroy(options, free, free);
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
      noitL(noit_debug, "Skipping %s`%s, disabled.\n",
            check->target, check->name);
  }
  else {
    if(!mod) {
      noitL(noit_stderr, "Cannot find module '%s'\n", check->module);
      check->flags |= NP_DISABLED;
    }
  }
  return 0;
}

void
noit_poller_initiate() {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen;
  void *vcheck;
  while(noit_hash_next(&polls, &iter, (const char **)key_id, &klen,
                       &vcheck)) {
    noit_check_activate((noit_check_t *)vcheck);
  }
}

void
noit_poller_flush_epoch(int oldest_allowed) {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen;
  noit_check_t *tofree = NULL;
  void *vcheck;

  /* Cleanup any previous causal map */
  while(noit_hash_next(&polls, &iter, (const char **)key_id, &klen,
                       &vcheck)) {
    noit_check_t *check = (noit_check_t *)vcheck;
    /* We don't free the one we're looking at... we free it on the next
     * pass.  This leaves out iterator in good shape.  We just need to
     * remember to free it one last time outside the while loop, down...
     */
    if(tofree) {
      noit_poller_deschedule(tofree->checkid);
      tofree = NULL;
    }
    if(check->generation < oldest_allowed) {
      tofree = check;
    }
  }
  /* ... here */
  if(tofree) noit_poller_deschedule(tofree->checkid);
}

void
noit_poller_make_causal_map() {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen;
  void *vcheck;

  /* Cleanup any previous causal map */
  while(noit_hash_next(&polls, &iter, (const char **)key_id, &klen,
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
  while(noit_hash_next(&polls, &iter, (const char **)key_id, &klen,
                       &vcheck)) {
    noit_check_t *check = (noit_check_t *)vcheck, *parent;
    if(check->oncheck) {
      /* This service is causally triggered by another service */
      uuid_t id;
      char fullcheck[1024];
      char *name = check->oncheck;
      char *target = NULL;

      noitL(noit_debug, "Searching for upstream trigger on %s\n", name);
      parent = NULL;
      if(uuid_parse(check->oncheck, id) == 0) {
        target = "";
        parent = noit_poller_lookup(id);
      }
      if((target = strchr(check->oncheck, '`')) != NULL) {
        strlcpy(fullcheck, check->oncheck, target + 1 - check->oncheck);
        name = target + 1;
        target = fullcheck;
        parent = noit_poller_lookup_by_name(target, name);
      }
      else {
        target = check->target;
        parent = noit_poller_lookup_by_name(target, name);
      }

      if(!parent) {
        check->flags |= NP_DISABLED;
        noitL(noit_stderr, "Disabling check %s`%s, can't find oncheck %s`%s\n",
              check->target, check->name, target, name);
      }
      else {
        dep_list_t *dep;
        dep = malloc(sizeof(*dep));
        dep->check = check;
        dep->next = parent->causal_checks;
        parent->causal_checks = dep;
        noitL(noit_debug, "Causal map %s`%s --> %s`%s\n",
              parent->target, parent->name, check->target, check->name);
      }
    }
  }
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
  noit_poller_initiate();
}
void
noit_poller_init() {
  srand48((getpid() << 16) & time(NULL));
  noit_check_resolver_init();
  noit_check_tools_init();
  noit_skiplist_init(&polls_by_name);
  noit_skiplist_set_compare(&polls_by_name, __check_name_compare,
                            __check_name_compare);
  noit_skiplist_init(&watchlist);
  noit_skiplist_set_compare(&watchlist, __watchlist_compare,
                            __watchlist_compare);
  register_console_check_commands();
  eventer_name_callback("check_recycle_bin_processor",
                        check_recycle_bin_processor);
  eventer_add_in_s_us(check_recycle_bin_processor, NULL, 60, 0);
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
  if(noit_hash_retrieve(&polls,
                        (char *)in, UUID_SIZE,
                        &vcheck) == 0) {
    return NULL;
  }
  checker = (noit_check_t *)vcheck;
  if(checker->oncheck) {
    return NULL;
  }
  new_check = calloc(1, sizeof(*new_check));
  memcpy(new_check, checker, sizeof(*new_check));
  new_check->target = strdup(new_check->target);
  new_check->module = strdup(new_check->module);
  new_check->name = strdup(new_check->name);
  new_check->filterset = strdup(new_check->filterset);
  new_check->flags = 0;
  new_check->fire_event = NULL;
  memset(&new_check->last_fire_time, 0, sizeof(new_check->last_fire_time));
  memset(&new_check->stats, 0, sizeof(new_check->stats));
  new_check->closure = NULL;
  new_check->config = calloc(1, sizeof(*new_check->config));
  noit_hash_merge_as_dict(new_check->config, checker->config);
  for(i=0; i<reg_module_id; i++) {
    noit_hash_table *src_mconfig;
    src_mconfig = noit_check_get_module_config(checker, i);
    if(src_mconfig) {
      noit_hash_table *t = calloc(1, sizeof(*new_check->config));
      noit_hash_merge_as_dict(t, src_mconfig);
      noit_check_set_module_config(new_check, i, t);
    }
  }
  return new_check;
}

noit_check_t *
noit_check_watch(uuid_t in, int period) {
  /* First look for a copy that is being watched */
  int minimum_pi = 1000, granularity_pi = 500;
  noit_conf_section_t check_node;
  char uuid_str[UUID_STR_LEN + 1];
  char xpath[1024];
  noit_check_t n, *f;

  uuid_unparse_lower(in, uuid_str);
  /* Find the check */
  snprintf(xpath, sizeof(xpath), "//checks//check[@uuid=\"%s\"]", uuid_str);
  check_node = noit_conf_get_section(NULL, xpath);
  noit_conf_get_int(NULL, "//checks/@transient_min_period", &minimum_pi);
  noit_conf_get_int(NULL, "//checks/@transient_period_granularity", &granularity_pi);
  if(check_node) {
    noit_conf_get_int(check_node,
                      "ancestor-or-self::node()/@transient_min_period",
                      &minimum_pi);
    noit_conf_get_int(check_node,
                      "ancestor-or-self::node()/@transient_period_granularity",
                      &granularity_pi);
  }

  /* apply the bounds */
  period /= granularity_pi;
  period *= granularity_pi;
  period = MAX(period, minimum_pi);

  uuid_copy(n.checkid, in);
  n.period = period;

  f = noit_skiplist_find(&watchlist, &n, NULL);
  if(f) return f;
  f = noit_check_clone(in);
  if(!f) return NULL;
  f->period = period;
  f->timeout = period - 10;
  f->flags |= NP_TRANSIENT;
  noitL(noit_debug, "Watching %s@%d\n", uuid_str, period);
  noit_skiplist_insert(&watchlist, f);
  return f;
}

noit_check_t *
noit_check_get_watch(uuid_t in, int period) {
  noit_check_t n, *f;

  uuid_copy(n.checkid, in);
  n.period = period;

  f = noit_skiplist_find(&watchlist, &n, NULL);
  return f;
}

void
noit_check_transient_add_feed(noit_check_t *check, const char *feed) {
  char *feedcopy;
  if(!check->feeds) {
    check->feeds = calloc(1, sizeof(*check->feeds));
    noit_skiplist_init(check->feeds);
    noit_skiplist_set_compare(check->feeds,
                              (noit_skiplist_comparator_t)strcmp,
                              (noit_skiplist_comparator_t)strcmp);
  }
  feedcopy = strdup(feed);
  /* No error on failure -- it's already there */
  if(noit_skiplist_insert(check->feeds, feedcopy) == NULL) free(feedcopy);
  noitL(noit_debug, "check %s`%s @ %dms has %d feed(s): %s.\n",
        check->target, check->name, check->period, check->feeds->size, feed);
}
void
noit_check_transient_remove_feed(noit_check_t *check, const char *feed) {
  if(!check->feeds) return;
  if(feed) {
    noitL(noit_debug, "check %s`%s @ %dms removing 1 of %d feeds: %s.\n",
          check->target, check->name, check->period, check->feeds->size, feed);
    noit_skiplist_remove(check->feeds, feed, free);
  }
  if(check->feeds->size == 0) {
    char uuid_str[UUID_STR_LEN + 1];
    uuid_unparse_lower(check->checkid, uuid_str);
    noitL(noit_debug, "Unwatching %s@%d\n", uuid_str, check->period);
    noit_skiplist_remove(&watchlist, check, NULL);
    noit_skiplist_destroy(check->feeds, free);
    free(check->feeds);
    check->feeds = NULL;
    if(check->flags & NP_TRANSIENT) {
      noitL(noit_debug, "check %s`%s @ %dms has no more listeners.\n",
            check->target, check->name, check->period);
      check->flags |= NP_KILLED;
    }
  }
}

noit_boolean
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
      return noit_false;
    }
  }
  return noit_true;
}
int
noit_check_set_ip(noit_check_t *new_check,
                  const char *ip_str) {
  int8_t family;
  int rv, failed = 0;
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

  new_check->target_family = family;
  memcpy(&new_check->target_addr, &a, sizeof(a));
  new_check->target_ip[0] = '\0';
  if(failed == 0)
    if(inet_ntop(new_check->target_family,
                 &new_check->target_addr,
                 new_check->target_ip,
                 sizeof(new_check->target_ip)) == NULL) {
      noitL(noit_error, "inet_ntop failed [%s] -> %d\n", ip_str, errno);
    }
  return failed;
}
int
noit_check_resolve(noit_check_t *check) {
  uint8_t family_pref = AF_INET;
  char ipaddr[INET6_ADDRSTRLEN];
  if(!NOIT_CHECK_SHOULD_RESOLVE(check)) return 1; /* success, not required */
  noit_check_resolver_remind(check->target);
  if(noit_check_resolver_fetch(check->target, ipaddr, sizeof(ipaddr),
                               family_pref) >= 0) {
    check->flags |= NP_RESOLVED;
    noit_check_set_ip(check, ipaddr);
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
                  noit_hash_table *config,
                  noit_hash_table **mconfigs,
                  u_int32_t period,
                  u_int32_t timeout,
                  const char *oncheck,
                  int flags) {
  int mask = NP_DISABLED | NP_UNCONFIG;

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

  if(noit_check_set_ip(new_check, target)) {
    noit_boolean should_resolve;
    new_check->flags |= NP_RESOLVE;
    new_check->flags &= ~NP_RESOLVED;
    if(noit_conf_get_boolean(NULL, "//checks/@resolve_targets",
                             &should_resolve) && should_resolve == noit_false)
      
      flags |= NP_DISABLED | NP_UNCONFIG;
    noit_check_resolve(new_check);
  }

  if(new_check->name) free(new_check->name);
  new_check->name = name ? strdup(name): NULL;
  if(new_check->filterset) free(new_check->filterset);
  new_check->filterset = filterset ? strdup(filterset): NULL;

  if(config != NULL) {
    noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
    const char *k;
    int klen;
    void *data;
    if(new_check->config) noit_hash_delete_all(new_check->config, free, free);
    else new_check->config = calloc(1, sizeof(*new_check->config));
    while(noit_hash_next(config, &iter, &k, &klen, &data)) {
      noit_hash_store(new_check->config, strdup(k), klen, strdup((char *)data));
    }
  }
  if(mconfigs != NULL) {
    int i;
    for(i=0; i<reg_module_id; i++) {
      if(mconfigs[i]) {
        noit_hash_table *t = calloc(1, sizeof(*new_check->config));
        noit_hash_merge_as_dict(t, mconfigs[i]);
        noit_check_set_module_config(new_check, i, t);
      }
    }
  }
  if(new_check->oncheck) free(new_check->oncheck);
  new_check->oncheck = oncheck ? strdup(oncheck) : NULL;
  new_check->period = period;
  new_check->timeout = timeout;

  /* Unset what could be set.. then set what should be set */
  new_check->flags = (new_check->flags & ~mask) | flags;

  if(!(new_check->flags & NP_TRANSIENT)) {
    /* This remove could fail -- no big deal */
    noit_skiplist_remove(&polls_by_name, new_check, NULL);

    /* This insert could fail.. which means we have a conflict on
     * target`name.  That should result in the check being disabled. */
    if(!noit_skiplist_insert(&polls_by_name, new_check)) {
      noitL(noit_stderr, "Check %s`%s disabled due to naming conflict\n",
            new_check->target, new_check->name);
      new_check->flags |= NP_DISABLED;
    }
  }
  noit_check_log_check(new_check);
  return 0;
}
int
noit_poller_schedule(const char *target,
                     const char *module,
                     const char *name,
                     const char *filterset,
                     noit_hash_table *config,
                     noit_hash_table **mconfigs,
                     u_int32_t period,
                     u_int32_t timeout,
                     const char *oncheck,
                     int flags,
                     uuid_t in,
                     uuid_t out) {
  noit_check_t *new_check;
  new_check = calloc(1, sizeof(*new_check));
  if(!new_check) return -1;

  /* The module and the UUID can never be changed */
  new_check->module = strdup(module);
  if(uuid_is_null(in))
    uuid_generate(new_check->checkid);
  else
    uuid_copy(new_check->checkid, in);

  noit_check_update(new_check, target, name, filterset, config, mconfigs,
                    period, timeout, oncheck, flags);
  assert(noit_hash_store(&polls,
                         (char *)new_check->checkid, UUID_SIZE,
                         new_check));
  uuid_copy(out, new_check->checkid);

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
    noit_hash_destroy(checker->config, free, free);
    free(checker->config);
    checker->config = NULL;
  }
  if(checker->module_metadata) {
    int i;
    for(i=0; i<reg_module_id; i++) {
      struct vp_w_free *tuple;
      tuple = checker->module_metadata[i];
      if(tuple && tuple->freefunc) tuple->freefunc(tuple->ptr);
      free(tuple);
    }
    free(checker->module_metadata);
  }
  if(checker->module_configs) {
    int i;
    for(i=0; i<reg_module_id; i++) {
      if(checker->module_configs[i]) {
        noit_hash_destroy(checker->module_configs[i], free, free);
        free(checker->module_configs[i]);
      }
    }
    free(checker->module_configs);
  }
  free(checker);
}
static int
check_recycle_bin_processor(eventer_t e, int mask, void *closure,
                            struct timeval *now) {
  static struct timeval one_minute = { 60L, 0L };
  struct _checker_rcb *prev = NULL, *curr = checker_rcb;
  noitL(noit_debug, "Scanning check recycle bin\n");
  while(curr) {
    if(!(curr->checker->flags & NP_RUNNING)) {
      noitL(noit_debug, "Check is ready to free.\n");
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
noit_poller_deschedule(uuid_t in) {
  void *vcheck;
  noit_check_t *checker;
  if(noit_hash_retrieve(&polls,
                        (char *)in, UUID_SIZE,
                        &vcheck) == 0) {
    return -1;
  }
  checker = (noit_check_t *)vcheck;
  checker->flags |= (NP_DISABLED|NP_KILLED);

  noit_check_log_delete(checker);

  noit_skiplist_remove(&polls_by_name, checker, NULL);
  noit_hash_delete(&polls, (char *)in, UUID_SIZE, NULL, NULL);

  noit_poller_free_check(checker);
  return 0;
}

noit_check_t *
noit_poller_lookup(uuid_t in) {
  void *vcheck;
  if(noit_hash_retrieve(&polls, (char *)in, UUID_SIZE, &vcheck))
    return (noit_check_t *)vcheck;
  return NULL;
}
noit_check_t *
noit_poller_lookup_by_name(char *target, char *name) {
  noit_check_t *check, *tmp_check;
  tmp_check = calloc(1, sizeof(*tmp_check));
  tmp_check->target = target;
  tmp_check->name = name;
  check = noit_skiplist_find(&polls_by_name, tmp_check, NULL);
  free(tmp_check);
  return check;
}
int
noit_poller_target_do(char *target, int (*f)(noit_check_t *, void *),
                      void *closure) {
  int count = 0;
  noit_check_t pivot;
  noit_skiplist_node *next;

  memset(&pivot, 0, sizeof(pivot));
  pivot.target = target;
  pivot.name = "";
  noit_skiplist_find_neighbors(&polls_by_name, &pivot, NULL, NULL, &next);
  while(next && next->data) {
    noit_check_t *check = next->data;
    if(strcmp(check->target, target)) break;
    count += f(check,closure);
    noit_skiplist_next(&polls_by_name, &next);
  }
  return count;
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
  stats_t current;
  char buff[256];
  if(!once) return -1;
  if(!check) return -1;
  assert(!(check->flags & NP_RUNNING));
  check->flags |= NP_RUNNING;
  noit_check_stats_clear(check, &current);
  gettimeofday(&current.whence, NULL);
  current.duration = 0;
  current.available = NP_UNKNOWN;
  current.state = NP_UNKNOWN;
  snprintf(buff, sizeof(buff), "check[%s] implementation offline",
           check->module);
  current.status = buff;
  noit_check_set_stats(check, &current);
  check->flags &= ~NP_RUNNING;
  return 0;
}
void
noit_check_stats_clear(noit_check_t *check, stats_t *s) {
  memset(s, 0, sizeof(*s));
  s->state = NP_UNKNOWN;
  s->available = NP_UNKNOWN;
}
void
free_metric(metric_t *m) {
  if(!m) return;
  if(m->metric_name) free(m->metric_name);
  if(m->metric_value.i) free(m->metric_value.i);
  free(m);
}

void
__stats_add_metric(stats_t *newstate, metric_t *m) {
  noit_hash_replace(&newstate->metrics, m->metric_name, strlen(m->metric_name),
                    m, NULL, (void (*)(void *))free_metric);
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
    case METRIC_STRING:
      return strlen((char *)value) + 1;
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
     v = calloc(1, sizeof(*v));
     *v = strtoll(rpl, NULL, 10);
     *replacement = v;
     type = METRIC_INT64;
     goto alldone;
   }
   else {
     u_int64_t *v;
     v = calloc(1, sizeof(*v));
     *v = strtoull(rpl, NULL, 10);
     *replacement = v;
     type = METRIC_UINT64;
     goto alldone;
   }
 scandouble:
   {
     double *v;
     v = calloc(1, sizeof(*v));
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
int
noit_stats_populate_metric(metric_t *m, const char *name, metric_type_t type,
                           const void *value) {
  void *replacement = NULL;
  if(type == METRIC_GUESS)
    type = noit_metric_guess_type((char *)value, &replacement);
  if(type == METRIC_GUESS) return -1;

  m->metric_name = strdup(name);
  m->metric_type = type;
  if(replacement)
    m->metric_value.vp = replacement;
  else if(value) {
    size_t len;
    len = noit_metric_sizes(type, value);
    m->metric_value.vp = calloc(1, len);
    memcpy(m->metric_value.vp, value, len);
  }
  return 0;
}

metric_t *
noit_stats_get_metric(noit_check_t *check,
                      stats_t *newstate, const char *name) {
  void *v;
  if(noit_hash_retrieve(&newstate->metrics, name, strlen(name), &v))
    return (metric_t *)v;
  return NULL;
}

void
noit_stats_set_metric(noit_check_t *check,
                      stats_t *newstate, const char *name, metric_type_t type,
                      const void *value) {
  metric_t *m = calloc(1, sizeof(*m));
  if(noit_stats_populate_metric(m, name, type, value)) {
    free_metric(m);
    return;
  }
  check_stats_set_metric_hook_invoke(check, newstate, m);
  __stats_add_metric(newstate, m);
}
void
noit_stats_set_metric_coerce(noit_check_t *check,
                             stats_t *stat, const char *name, metric_type_t t,
                             const char *v) {
  char *endptr;
  if(v == NULL) {
   bogus:
    noit_stats_set_metric(check, stat, name, t, NULL);
    return;
  }
  switch(t) {
    case METRIC_STRING:
      noit_stats_set_metric(check, stat, name, t, v);
      break;
    case METRIC_INT32:
    {
      int32_t val;
      val = strtol(v, &endptr, 10);
      if(endptr == v) goto bogus;
      noit_stats_set_metric(check, stat, name, t, &val);
      break;
    }
    case METRIC_UINT32:
    {
      u_int32_t val;
      val = strtoul(v, &endptr, 10);
      if(endptr == v) goto bogus;
      noit_stats_set_metric(check, stat, name, t, &val);
      break;
    }
    case METRIC_INT64:
    {
      int64_t val;
      val = strtoll(v, &endptr, 10);
      if(endptr == v) goto bogus;
      noit_stats_set_metric(check, stat, name, t, &val);
      break;
    }
    case METRIC_UINT64:
    {
      u_int64_t val;
      val = strtoull(v, &endptr, 10);
      if(endptr == v) goto bogus;
      noit_stats_set_metric(check, stat, name, t, &val);
      break;
    }
    case METRIC_DOUBLE:
    {
      double val;
      val = strtod(v, &endptr);
      if(endptr == v) goto bogus;
      noit_stats_set_metric(check, stat, name, t, &val);
      break;
    }
    case METRIC_GUESS:
      noit_stats_set_metric(check, stat, name, t, v);
      break;
  }
}
void
noit_stats_log_immediate_metric(noit_check_t *check,
                                const char *name, metric_type_t type,
                                void *value) {
  struct timeval now;
  metric_t *m = calloc(1, sizeof(*m));
  if(noit_stats_populate_metric(m, name, type, value)) {
    free_metric(m);
    return;
  }
  gettimeofday(&now, NULL);
  noit_check_log_metric(check, &now, m);
  free_metric(m);
}

void
noit_check_passive_set_stats(noit_check_t *check, stats_t *newstate) {
  noit_skiplist_node *next;
  noit_check_t n;

  uuid_copy(n.checkid, check->checkid);
  n.period = 0;

  noit_check_set_stats(check,newstate);
  noit_skiplist_find_neighbors(&watchlist, &n, NULL, NULL, &next);
  while(next && next->data) {
    stats_t backup;
    noit_check_t *wcheck = next->data;
    if(uuid_compare(n.checkid, wcheck->checkid)) break;

    /* Swap the real check's stats into place */
    memcpy(&backup, &wcheck->stats.current, sizeof(stats_t));
    memcpy(&wcheck->stats.current, newstate, sizeof(stats_t));

    if(check_passive_log_stats_hook_invoke(check) == NOIT_HOOK_CONTINUE) {
      /* Write out our status */
      noit_check_log_status(wcheck);
      /* Write out all metrics */
      noit_check_log_metrics(wcheck);
    }
    /* Swap them back out */
    memcpy(&wcheck->stats.current, &backup, sizeof(stats_t));

    noit_skiplist_next(&watchlist, &next);
  }
}
void
noit_check_set_stats(noit_check_t *check, stats_t *newstate) {
  int report_change = 0;
  char *cp;
  dep_list_t *dep;
  if(check->stats.previous.status)
    free(check->stats.previous.status);
  noit_hash_destroy(&check->stats.previous.metrics, NULL,
                    (void (*)(void *))free_metric);
  memcpy(&check->stats.previous, &check->stats.current, sizeof(stats_t));
  memcpy(&check->stats.current, newstate, sizeof(stats_t));
  if(check->stats.current.status)
    check->stats.current.status = strdup(check->stats.current.status);
  for(cp = check->stats.current.status; cp && *cp; cp++)
    if(*cp == '\r' || *cp == '\n') *cp = ' ';

  /* check for state changes */
  if(check->stats.current.available != NP_UNKNOWN &&
     check->stats.previous.available != NP_UNKNOWN &&
     check->stats.current.available != check->stats.previous.available)
    report_change = 1;
  if(check->stats.current.state != NP_UNKNOWN &&
     check->stats.previous.state != NP_UNKNOWN &&
     check->stats.current.state != check->stats.previous.state)
    report_change = 1;

  noitL(noit_debug, "%s`%s <- [%s]\n", check->target, check->name,
        check->stats.current.status);
  if(report_change) {
    noitL(noit_debug, "%s`%s -> [%s:%s]\n",
          check->target, check->name,
          noit_check_available_string(check->stats.current.available),
          noit_check_state_string(check->stats.current.state));
  }

  if(NOIT_CHECK_STATUS_ENABLED()) {
    char id[UUID_STR_LEN+1];
    uuid_unparse_lower(check->checkid, id);
    NOIT_CHECK_STATUS(id, check->module, check->name, check->target,
                      check->stats.current.available,
                      check->stats.current.state,
                      check->stats.current.status);
  }

  if(check_log_stats_hook_invoke(check) == NOIT_HOOK_CONTINUE) {
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
      noitL(noit_debug, "Firing %s`%s in response to %s`%s\n",
            dep->check->target, dep->check->name,
            check->target, check->name);
      if((dep->check->flags & NP_DISABLED) == 0)
        if(mod->initiate_check)
          mod->initiate_check(mod, dep->check, 1, check);
    }
  }
}

static int
noit_console_show_watchlist(noit_console_closure_t ncct,
                            int argc, char **argv,
                            noit_console_state_t *dstate,
                            void *closure) {
  noit_skiplist_node *iter, *fiter;
  nc_printf(ncct, "%d active watches.\n", watchlist.size);
  for(iter = noit_skiplist_getlist(&watchlist); iter;
      noit_skiplist_next(&watchlist, &iter)) {
    char uuid_str[UUID_STR_LEN + 1];
    noit_check_t *check = iter->data;

    uuid_unparse_lower(check->checkid, uuid_str);
    nc_printf(ncct, "%s:\n\t[%s`%s`%s]\n\tPeriod: %dms\n\tFeeds[%d]:\n",
              uuid_str, check->target, check->module, check->name,
              check->period, check->feeds ? check->feeds->size : 0);
    if(check->feeds && check->feeds->size) {
      for(fiter = noit_skiplist_getlist(check->feeds); fiter;
          noit_skiplist_next(check->feeds, &fiter)) {
        nc_printf(ncct, "\t\t%s\n", (const char *)fiter->data);
      }
    }
  }
  return 0;
}

static void
nc_printf_check_brief(noit_console_closure_t ncct,
                      noit_check_t *check) {
  char out[512];
  char uuid_str[37];
  snprintf(out, sizeof(out), "%s`%s", check->target, check->name);
  uuid_unparse_lower(check->checkid, uuid_str);
  nc_printf(ncct, "%s %s\n", uuid_str, out);
  if(check->stats.current.status)
    nc_printf(ncct, "\t%s\n", check->stats.current.status);
}

char *
noit_console_conf_check_opts(noit_console_closure_t ncct,
                             noit_console_state_stack_t *stack,
                             noit_console_state_t *dstate,
                             int argc, char **argv, int idx) {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen, i = 0;
  void *vcheck;

  if(argc == 1) {
    if(!strncmp("new", argv[0], strlen(argv[0]))) {
      if(idx == i) return strdup("new");
      i++;
    }
    while(noit_hash_next(&polls, &iter, (const char **)key_id, &klen,
                         &vcheck)) {
      noit_check_t *check = (noit_check_t *)vcheck;
      char out[512];
      char uuid_str[37];
      snprintf(out, sizeof(out), "%s`%s", check->target, check->name);
      uuid_unparse_lower(check->checkid, uuid_str);
      if(!strncmp(out, argv[0], strlen(argv[0]))) {
        if(idx == i) return strdup(out);
        i++;
      }
      if(!strncmp(uuid_str, argv[0], strlen(argv[0]))) {
        if(idx == i) return strdup(uuid_str);
        i++;
      }
    }
  }
  if(argc == 2) {
    cmd_info_t *cmd;
    if(!strcmp("new", argv[0])) return NULL;
    cmd = noit_skiplist_find(&dstate->cmds, "attribute", NULL);
    if(!cmd) return NULL;
    return noit_console_opt_delegate(ncct, stack, cmd->dstate, argc-1, argv+1, idx);
  }
  return NULL;
}

char *
noit_console_check_opts(noit_console_closure_t ncct,
                        noit_console_state_stack_t *stack,
                        noit_console_state_t *dstate,
                        int argc, char **argv, int idx) {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen, i = 0;

  if(argc == 1) {
    void *vcheck;
    while(noit_hash_next(&polls, &iter, (const char **)key_id, &klen,
                         &vcheck)) {
      char out[512];
      char uuid_str[37];
      noit_check_t *check = (noit_check_t *)vcheck;
      snprintf(out, sizeof(out), "%s`%s", check->target, check->name);
      uuid_unparse_lower(check->checkid, uuid_str);
      if(!strncmp(out, argv[0], strlen(argv[0]))) {
        if(idx == i) return strdup(out);
        i++;
      }
      if(!strncmp(uuid_str, argv[0], strlen(argv[0]))) {
        if(idx == i) return strdup(uuid_str);
        i++;
      }
    }
  }
  if(argc == 2) {
    return noit_console_opt_delegate(ncct, stack, dstate, argc-1, argv+1, idx);
  }
  return NULL;
}

static int
noit_console_show_checks(noit_console_closure_t ncct,
                         int argc, char **argv,
                         noit_console_state_t *dstate,
                         void *closure) {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen;
  void *vcheck;

  while(noit_hash_next(&polls, &iter, (const char **)key_id, &klen,
                       &vcheck)) {
    nc_printf_check_brief(ncct, (noit_check_t *)vcheck);
  }
  return 0;
}

static void
register_console_check_commands() {
  noit_console_state_t *tl;
  cmd_info_t *showcmd;

  tl = noit_console_state_initial();
  showcmd = noit_console_state_get_cmd(tl, "show");
  assert(showcmd && showcmd->dstate);

  noit_console_state_add_cmd(showcmd->dstate,
    NCSCMD("timing_slots", noit_console_show_timing_slots, NULL, NULL, NULL));

  noit_console_state_add_cmd(showcmd->dstate,
    NCSCMD("checks", noit_console_show_checks, NULL, NULL, NULL));

  noit_console_state_add_cmd(showcmd->dstate,
    NCSCMD("watches", noit_console_show_watchlist, NULL, NULL, NULL));
}

int
noit_check_register_module(const char *name) {
  int i;
  for(i=0; i<reg_module_id; i++)
    if(!strcmp(reg_module_names[i], name)) return i;
  if(reg_module_id >= MAX_MODULE_REGISTRATIONS) return -1;
  noitL(noit_debug, "Registered module %s as %d\n", name, i);
  i = reg_module_id++;
  reg_module_names[i] = strdup(name);
  noit_conf_set_namespace(reg_module_names[i]);
  return i;
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
noit_check_set_module_config(noit_check_t *c, int idx, noit_hash_table *config) {
  if(reg_module_used < 0) reg_module_used = reg_module_id;
  assert(reg_module_used == reg_module_id);
  if(idx >= reg_module_id || idx < 0) return;
  if(!c->module_configs) c->module_configs = calloc(reg_module_id, sizeof(noit_hash_table *));
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
noit_hash_table *
noit_check_get_module_config(noit_check_t *c, int idx) {
  if(reg_module_used < 0) reg_module_used = reg_module_id;
  assert(reg_module_used == reg_module_id);
  if(idx >= reg_module_id || idx < 0 || !c->module_configs) return NULL;
  return c->module_configs[idx];
}
