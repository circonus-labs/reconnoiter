/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
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
#define MAX_INITIAL_STUTTER 60

static noit_hash_table polls = NOIT_HASH_EMPTY;
static noit_skiplist polls_by_name = { 0 };
static u_int32_t __config_load_generation = 0;
struct uuid_dummy {
  uuid_t foo;
};

static void register_console_check_commands();

#define UUID_SIZE sizeof(struct uuid_dummy)

static const char *
__noit_check_available_string(int16_t available) {
  switch(available) {
    case NP_AVAILABLE:    return "available";
    case NP_UNAVAILABLE:  return "unavailable";
    case NP_UNKNOWN:      return "unknown";
  }
  return "???";
}
static const char *
__noit_check_state_string(int16_t state) {
  switch(state) {
    case NP_GOOD:         return "good";
    case NP_BAD:          return "bad";
    case NP_UNKNOWN:      return "unknown";
  }
  return "???";
}
static int __check_name_compare(void *a, void *b) {
  noit_check_t *ac = a;
  noit_check_t *bc = b;
  int rv;
  if((rv = strcmp(ac->target, bc->target)) != 0) return rv;
  if((rv = strcmp(ac->name, bc->name)) != 0) return rv;
  return 0;
}
int
noit_check_max_initial_stutter() {
  int stutter;
  if(!noit_conf_get_int(NULL, "/noit/checks/@max_initial_stutter", &stutter))
    stutter = MAX_INITIAL_STUTTER;
  return stutter * 1000;
}
void
noit_check_fake_last_check(noit_check_t *check,
                           struct timeval *lc, struct timeval *_now) {
  struct timeval now, period;
  double r;
  int offset;

  r = drand48();
  offset = r * (MIN(MAX_INITIAL_STUTTER, check->period));
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
    noit_check_t *existing_check;
    char uuid_str[37];
    char target[256];
    char module[256];
    char name[256];
    char oncheck[1024] = "";
    int no_period = 0;
    int no_oncheck = 0;
    int period = 0, timeout = 0;
    noit_conf_boolean disabled = noit_false, busted = noit_false;
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
                          (void **)&existing_check)) {
      /* Once set, we can never change it. */
      assert(!existing_check->module || !existing_check->module[0] ||
             !strcmp(existing_check->module, module));
      /* Only set it if it is not yet set */
      if(!existing_check->module || !existing_check->module[0]) {
        if(existing_check->module) free(existing_check->module);
        existing_check->module = strdup(module);
      }
      noit_check_update(existing_check, target, name, options,
                           period, timeout, oncheck[0] ? oncheck : NULL,
                           flags);
      noitL(noit_debug, "reloaded uuid: %s\n", uuid_str);
    }
    else {
      noit_poller_schedule(target, module, name, options,
                           period, timeout, oncheck[0] ? oncheck : NULL,
                           flags, uuid, out_uuid);
      noitL(noit_debug, "loaded uuid: %s\n", uuid_str);
    }
  }
}

void
noit_poller_initiate() {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen;
  noit_check_t *check;
  while(noit_hash_next(&polls, &iter, (const char **)key_id, &klen,
                       (void **)&check)) {
    noit_module_t *mod;
    mod = noit_module_lookup(check->module);
    if(mod) {
      if(NOIT_CHECK_LIVE(check))
        continue;
      if((check->flags & NP_DISABLED) == 0) {
        if(mod->initiate_check)
          mod->initiate_check(mod, check, 0, NULL);
      }
      else
        noitL(noit_debug, "Skipping %s`%s, disabled.\n",
              check->target, check->name);
    }
    else {
      noitL(noit_stderr, "Cannot find module '%s'\n", check->module);
      check->flags |= NP_DISABLED;
    }
  }
}

void
noit_poller_flush_epoch(int oldest_allowed) {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen;
  noit_check_t *check, *tofree = NULL;

  /* Cleanup any previous causal map */
  while(noit_hash_next(&polls, &iter, (const char **)key_id, &klen,
                       (void **)&check)) {
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
  noit_check_t *check, *parent;

  /* Cleanup any previous causal map */
  while(noit_hash_next(&polls, &iter, (const char **)key_id, &klen,
                       (void **)&check)) {
    dep_list_t *dep;
    while((dep = check->causal_checks) != NULL) {
      check->causal_checks = dep->next;
      free(dep);
    }
  }

  memset(&iter, 0, sizeof(iter));
  /* Walk all checks and add check dependencies to their parents */
  while(noit_hash_next(&polls, &iter, (const char **)key_id, &klen,
                       (void **)&check)) {
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
  register_console_check_commands();
  noit_poller_reload(NULL);
}

int
noit_check_update(noit_check_t *new_check,
                  const char *target,
                  const char *name,
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

  /* This remove could fail -- no big deal */
  noit_skiplist_remove(&polls_by_name, new_check, NULL);

  /* This insert could fail.. which means we have a conflict on
   * target`name.  That should result in the check being disabled. */
  if(!noit_skiplist_insert(&polls_by_name, new_check)) {
    noitL(noit_stderr, "Check %s`%s disabled due to naming conflict\n",
          new_check->target, new_check->name);
    new_check->flags |= NP_DISABLED;
  }
  noit_check_log_check(new_check);
  return 0;
}
int
noit_poller_schedule(const char *target,
                     const char *module,
                     const char *name,
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

  noit_check_update(new_check, target, name, config,
                    period, timeout, oncheck, flags);
  assert(noit_hash_store(&polls,
                         (char *)new_check->checkid, UUID_SIZE,
                         new_check));
  uuid_copy(out, new_check->checkid);

  return 0;
}

int
noit_poller_deschedule(uuid_t in) {
  noit_check_t *checker;
  noit_module_t *mod;
  if(noit_hash_retrieve(&polls,
                        (char *)in, UUID_SIZE,
                        (void **)&checker) == 0) {
    return -1;
  }
  if(checker->flags & NP_RUNNING) {
    checker->flags |= NP_KILLED;
    return 0;
  }
  checker->flags |= NP_KILLED;

  noit_skiplist_remove(&polls_by_name, checker, NULL);
  noit_hash_delete(&polls, (char *)in, UUID_SIZE, NULL, NULL);

  mod = noit_module_lookup(checker->module);
  mod->cleanup(mod, checker);
  if(checker->fire_event) {
     eventer_remove(checker->fire_event);
     eventer_free(checker->fire_event);
     checker->fire_event = NULL;
  }

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
  noit_check_t *check;
  if(noit_hash_retrieve(&polls,
                        (char *)in, UUID_SIZE,
                        (void **)&check)) {
    return check;
  }
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

  while(*trailer && isspace(*trailer)) *trailer++; /* rtrim */

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
noit_stats_set_metric(stats_t *newstate, char *name, metric_type_t type,
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

  noitL(noit_error, "%s`%s <- [%s]\n", check->target, check->name,
        check->stats.current.status);
  if(report_change) {
    noitL(noit_error, "%s`%s -> [%s:%s]\n",
          check->target, check->name,
          __noit_check_available_string(check->stats.current.available),
          __noit_check_state_string(check->stats.current.state));
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

static int
noit_console_show_checks(noit_console_closure_t ncct,
                         int argc, char **argv,
                         noit_console_state_t *dstate,
                         void *closure) {
  struct timeval _now;
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen;
  noit_check_t *check;

  gettimeofday(&_now, NULL);
  while(noit_hash_next(&polls, &iter, (const char **)key_id, &klen,
                       (void **)&check)) {
    nc_printf_check_brief(ncct, check);
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
    NCSCMD("checks", noit_console_show_checks, NULL, NULL));
}

