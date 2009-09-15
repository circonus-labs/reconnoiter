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
#include <netinet/in.h>
#include <arpa/inet.h>

#include "utils/noit_log.h"
#include "utils/noit_hash.h"
#include "utils/noit_skiplist.h"
#include "noit_conf.h"
#include "noit_check.h"
#include "noit_module.h"
#include "noit_console.h"
#include "eventer/eventer.h"

/* 60 seconds of possible stutter */
#define MAX_INITIAL_STUTTER 60000

static noit_hash_table polls = NOIT_HASH_EMPTY;
static noit_skiplist watchlist = { 0 };
static noit_skiplist polls_by_name = { 0 };
static u_int32_t __config_load_generation = 0;
struct uuid_dummy {
  uuid_t foo;
};

static void register_console_check_commands();

#define UUID_SIZE sizeof(struct uuid_dummy)

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
noit_check_max_initial_stutter() {
  int stutter;
  if(!noit_conf_get_int(NULL, "/noit/checks/@max_initial_stutter", &stutter))
    stutter = MAX_INITIAL_STUTTER;
  return stutter;
}
void
noit_check_fake_last_check(noit_check_t *check,
                           struct timeval *lc, struct timeval *_now) {
  struct timeval now, period;
  double r;
  int offset = 0, max;

  if(!(check->flags & NP_TRANSIENT)) {
    r = drand48();
    max = noit_check_max_initial_stutter();
    offset = r * (MIN(max, check->period));
  }
  period.tv_sec = (check->period - offset) / 1000;
  period.tv_usec = ((check->period - offset) % 1000) * 1000;
  if(!_now) {
    gettimeofday(&now, NULL);
    _now = &now;
  }
  sub_timeval(*_now, period, lc);
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
    int no_period = 0;
    int no_oncheck = 0;
    int period = 0, timeout = 0;
    noit_boolean disabled = noit_false, busted = noit_false;
    uuid_t uuid, out_uuid;
    noit_hash_table *options;

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

    INHERIT(boolean, disable, &disabled);
    flags = 0;
    if(busted) flags |= NP_UNCONFIG;
    if(disabled) flags |= NP_DISABLED;

    if(noit_hash_retrieve(&polls, (char *)uuid, UUID_SIZE,
                          &vcheck)) {
      noit_check_t *existing_check = (noit_check_t *)vcheck;
      /* Once set, we can never change it. */
      assert(!existing_check->module || !existing_check->module[0] ||
             !strcmp(existing_check->module, module));
      /* Only set it if it is not yet set */
      if(!existing_check->module || !existing_check->module[0]) {
        if(existing_check->module) free(existing_check->module);
        existing_check->module = strdup(module);
      }
      noit_check_update(existing_check, target, name, filterset, options,
                           period, timeout, oncheck[0] ? oncheck : NULL,
                           flags);
      noitL(noit_debug, "reloaded uuid: %s\n", uuid_str);
    }
    else {
      noit_poller_schedule(target, module, name, filterset, options,
                           period, timeout, oncheck[0] ? oncheck : NULL,
                           flags, uuid, out_uuid);
      noitL(noit_debug, "loaded uuid: %s\n", uuid_str);
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
      char fullcheck[1024];
      char *name = check->oncheck;
      char *target = NULL;

      noitL(noit_debug, "Searching for upstream trigger on %s\n", name);
      if((target = strchr(check->oncheck, '`')) != NULL) {
        strlcpy(fullcheck, check->oncheck, target - check->oncheck);
        name = target + 1;
        target = fullcheck;
      }
      else
       target = check->target;

      parent = noit_poller_lookup_by_name(target, name);
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
  noit_skiplist_init(&polls_by_name);
  noit_skiplist_set_compare(&polls_by_name, __check_name_compare,
                            __check_name_compare);
  noit_skiplist_init(&watchlist);
  noit_skiplist_set_compare(&watchlist, __watchlist_compare,
                            __watchlist_compare);
  register_console_check_commands();
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
  return new_check;
}

noit_check_t *
noit_check_watch(uuid_t in, int period) {
  /* First look for a copy that is being watched */
  noit_check_t n, *f;

  uuid_copy(n.checkid, in);
  n.period = period;

  f = noit_skiplist_find(&watchlist, &n, NULL);
  if(f) return f;
  f = noit_check_clone(in);
  if(!f) return NULL;
  f->period = period;
  f->timeout = period - 10;
  f->flags |= NP_TRANSIENT;
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

int
noit_check_update(noit_check_t *new_check,
                  const char *target,
                  const char *name,
                  const char *filterset,
                  noit_hash_table *config,
                  u_int32_t period,
                  u_int32_t timeout,
                  const char *oncheck,
                  int flags) {
  int8_t family;
  int rv;
  int mask = NP_DISABLED | NP_UNCONFIG;
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
      noitL(noit_stderr, "Cannot translate '%s' to IP\n", target);
      memset(&a, 0, sizeof(a));
      flags |= (NP_UNCONFIG & NP_DISABLED);
    }
  }

  new_check->generation = __config_load_generation;
  new_check->target_family = family;
  memcpy(&new_check->target_addr, &a, sizeof(a));
  if(new_check->target) free(new_check->target);
  new_check->target = strdup(target);
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

  noit_check_update(new_check, target, name, filterset, config,
                    period, timeout, oncheck, flags);
  assert(noit_hash_store(&polls,
                         (char *)new_check->checkid, UUID_SIZE,
                         new_check));
  uuid_copy(out, new_check->checkid);

  return 0;
}

int
noit_poller_deschedule(uuid_t in) {
  void *vcheck;
  noit_check_t *checker;
  noit_module_t *mod;
  if(noit_hash_retrieve(&polls,
                        (char *)in, UUID_SIZE,
                        &vcheck) == 0) {
    return -1;
  }
  checker = (noit_check_t *)vcheck;
  checker->flags |= (NP_DISABLED|NP_KILLED);

  if(checker->flags & NP_RUNNING) {
    return 0;
  }

  noit_skiplist_remove(&polls_by_name, checker, NULL);
  noit_hash_delete(&polls, (char *)in, UUID_SIZE, NULL, NULL);

  mod = noit_module_lookup(checker->module);
  if(mod->cleanup) mod->cleanup(mod, checker);
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
  free(checker);
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

void
noit_check_stats_clear(stats_t *s) {
  memset(s, 0, sizeof(*s));
  s->state = NP_UNKNOWN;
  s->available = NP_UNKNOWN;
}
static void
__free_metric(void *vm) {
  metric_t *m = vm;
  free(m->metric_name);
  if(m->metric_value.i) free(m->metric_value.i);
  free(m);
}

void
__stats_add_metric(stats_t *newstate, metric_t *m) {
  noit_hash_replace(&newstate->metrics, m->metric_name, strlen(m->metric_name),
                    m, NULL, __free_metric);
}

static size_t
noit_metric_sizes(metric_type_t type, void *value) {
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
  int negative = 0, bigF = 0;
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
   if(s[0] == 'E') bigF = 1; /* We want the caps variant */
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
void
noit_stats_set_metric(stats_t *newstate, const char *name, metric_type_t type,
                      void *value) {
  metric_t *m;
  void *replacement = NULL;
  if(type == METRIC_GUESS)
    type = noit_metric_guess_type((char *)value, &replacement);
  if(type == METRIC_GUESS) return;

  m = calloc(1, sizeof(*m));
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
  __stats_add_metric(newstate, m);
}

void
noit_check_set_stats(struct _noit_module *module,
                     noit_check_t *check, stats_t *newstate) {
  int report_change = 0;
  dep_list_t *dep;
  if(check->stats.previous.status)
    free(check->stats.previous.status);
  noit_hash_destroy(&check->stats.previous.metrics, NULL, __free_metric);
  memcpy(&check->stats.previous, &check->stats.current, sizeof(stats_t));
  memcpy(&check->stats.current, newstate, sizeof(stats_t));
  if(check->stats.current.status)
    check->stats.current.status = strdup(check->stats.current.status);

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

  /* Write out our status */
  noit_check_log_status(check);
  /* Write out all metrics */
  noit_check_log_metrics(check);

  for(dep = check->causal_checks; dep; dep = dep->next) {
    noit_module_t *mod;
    mod = noit_module_lookup(dep->check->module);
    assert(mod);
    noitL(noit_debug, "Firing %s`%s in response to %s`%s\n",
          dep->check->target, dep->check->name,
          check->target, check->name);
    if((dep->check->flags & NP_DISABLED) == 0)
      if(mod->initiate_check)
        mod->initiate_check(mod, dep->check, 1, check);
  }
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
    NCSCMD("checks", noit_console_show_checks, NULL, NULL, NULL));
}

