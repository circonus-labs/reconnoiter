/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "utils/noit_log.h"
#include "utils/noit_hash.h"
#include "utils/noit_skiplist.h"
#include "noit_conf.h"
#include "noit_poller.h"
#include "noit_module.h"
#include "eventer/eventer.h"

static noit_hash_table polls = NOIT_HASH_EMPTY;
static noit_skiplist polls_by_name = { 0 };
static u_int32_t __config_load_generation = 0;
struct uuid_dummy {
  uuid_t foo;
};
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
  noit_check_t ac = a;
  noit_check_t bc = b;
  int rv;
  if((rv = strcmp(ac->target, bc->target)) != 0) return rv;
  if((rv = strcmp(ac->name, bc->name)) != 0) return rv;
  return 0;
}
void
noit_poller_load_checks() {
  int i, cnt = 0;
  noit_conf_section_t *sec;
  __config_load_generation++;
  sec = noit_conf_get_sections(NULL, "/noit/checks//check", &cnt);
  for(i=0; i<cnt; i++) {
    char uuid_str[37];
    char target[256];
    char module[256];
    char name[256];
    int period = 0, timeout = 0;
    uuid_t uuid, out_uuid;
    noit_hash_table *options;

    if(!noit_conf_get_stringbuf(sec[i], "@uuid",
                                uuid_str, sizeof(uuid_str))) {
      noitL(noit_stderr, "check %d has no uuid\n", i+1);
      continue;
    }
    if(uuid_parse(uuid_str, uuid)) {
      noitL(noit_stderr, "check uuid: '%s' is invalid\n", uuid_str);
      continue;
    }
    if(!noit_conf_get_stringbuf(sec[i], "target", target, sizeof(target))) {
      if(!noit_conf_get_stringbuf(sec[i], "../target", target, sizeof(target))) {
         noitL(noit_stderr, "check uuid: '%s' has no target\n",
                  uuid_str);
         continue;
      }
    }
    if(!noit_conf_get_stringbuf(sec[i], "module", module, sizeof(module))) {
      if(!noit_conf_get_stringbuf(sec[i], "../module", module, sizeof(module))) {
        noitL(noit_stderr, "check uuid: '%s' has no module\n",
                 uuid_str);
        continue;
      }
    }
    if(!noit_conf_get_stringbuf(sec[i], "name", name, sizeof(name))) {
      strcpy(name, module);
    }
    if(!noit_conf_get_int(sec[i], "period", &period)) {
      if(!noit_conf_get_int(sec[i], "../period", &period)) {
        noitL(noit_stderr, "check uuid: '%s' has no period\n", uuid_str);
        continue;
      }
    }
    if(!noit_conf_get_int(sec[i], "timeout", &timeout)) {
      if(!noit_conf_get_int(sec[i], "../timeout", &timeout)) {
        noitL(noit_stderr, "check uuid: '%s' has no timeout\n", uuid_str);
        continue;
      }
    }
    if(timeout >= period) {
      noitL(noit_stderr, "check uuid: '%s' timeout > period\n", uuid_str);
      timeout = period/2;
    }
    options = noit_conf_get_hash(sec[i], "config/*");
    noit_poller_schedule(target, module, name, options,
                         period, timeout, uuid, out_uuid);
    noitL(noit_debug, "loaded uuid: %s\n", uuid_str);
  }
}

void
noit_poller_initiate() {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen;
  noit_check_t check;
  while(noit_hash_next(&polls, &iter, (const char **)key_id, &klen,
                       (void **)&check)) {
    noit_module_t *mod;
    mod = noit_module_lookup(check->module);
    if(mod) {
      mod->initiate_check(mod, check, 0);
    }
    else {
      noitL(noit_stderr, "Cannot find module '%s'\n", check->module);
    }
  }
}

void
noit_poller_init() {
  noit_skiplist_init(&polls_by_name);
  noit_skiplist_set_compare(&polls_by_name, __check_name_compare,
                            __check_name_compare);
  noit_poller_load_checks();
  noit_poller_initiate();
}

int
noit_poller_schedule(const char *target,
                     const char *module,
                     const char *name,
                     noit_hash_table *config,
                     u_int32_t period,
                     u_int32_t timeout,
                     uuid_t in,
                     uuid_t out) {
  int8_t family;
  int rv;
  union {
    struct in_addr addr4;
    struct in6_addr addr6;
  } a;
  noit_check_t new_check;


  family = AF_INET;
  rv = inet_pton(family, target, &a);
  if(rv != 1) {
    family = AF_INET6;
    rv = inet_pton(family, target, &a);
    if(rv != 1) {
      noitL(noit_stderr, "Cannot translate '%s' to IP\n", target);
      return -1;
    }
  }

  new_check = calloc(1, sizeof(*new_check));
  if(!new_check) return -1;
  new_check->generation = __config_load_generation;
  new_check->target_family = family;
  memcpy(&new_check->target_addr, &a, sizeof(a));
  new_check->target = strdup(target);
  new_check->module = strdup(module);
  new_check->name = name?strdup(name):NULL;

  if(config != NULL) {
    noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
    const char *k;
    int klen;
    void *data;
    new_check->config = calloc(1, sizeof(*new_check->config));
    while(noit_hash_next(config, &iter, &k, &klen, &data)) {
      noit_hash_store(new_check->config, strdup(k), klen, strdup((char *)data));
    }
  }
  new_check->period = period;
  new_check->timeout = timeout;
  new_check->flags = 0;
  if(uuid_is_null(in))
    uuid_generate(new_check->checkid);
  else
    uuid_copy(new_check->checkid, in);

  assert(noit_hash_store(&polls,
                         (char *)new_check->checkid, UUID_SIZE,
                         new_check));
  noit_skiplist_insert(&polls_by_name, new_check);
  uuid_copy(out, new_check->checkid);
  return 0;
}

int
noit_poller_deschedule(uuid_t in) {
  noit_check_t checker;
  if(noit_hash_retrieve(&polls,
                        (char *)in, UUID_SIZE,
                        (void **)&checker) == 0) {
    return -1;
  }
  if(checker->flags & NP_RUNNING) {
    checker->flags |= NP_KILLED;
    return 0;
  }
  if(checker->fire_event) {
     eventer_remove(checker->fire_event);
     eventer_free(checker->fire_event);
     checker->fire_event = NULL;
  }
  noit_hash_delete(&polls, (char *)in, UUID_SIZE, free, free);

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

noit_check_t
noit_poller_lookup(uuid_t in) {
  noit_check_t check;
  if(noit_hash_retrieve(&polls,
                        (char *)in, UUID_SIZE,
                        (void **)&check)) {
    return check;
  }
  return NULL;
}
noit_check_t
noit_poller_lookup_by_name(char *target, char *name) {
  noit_check_t check, tmp_check;
  tmp_check = calloc(1, sizeof(*tmp_check));
  tmp_check->target = target;
  tmp_check->name = name;
  check = noit_skiplist_find(&polls_by_name, &tmp_check, NULL);
  free(tmp_check);
  return check;
}


void
noit_poller_set_state(noit_check_t check, stats_t *newstate) {
  int report_change = 0;
  if(check->stats.previous.status)
    free(check->stats.previous.status);
  memcpy(&check->stats.previous, &check->stats.current, sizeof(stats_t));
  memcpy(&check->stats.current, newstate, sizeof(stats_t));
  if(check->stats.current.status)
    check->stats.current.status = strdup(check->stats.current.status);

  /* check for state changes */
  if(check->stats.current.available != 0 &&
     check->stats.previous.available != 0 &&
     check->stats.current.available != check->stats.previous.available)
    report_change = 1;
  if(check->stats.current.state != 0 &&
     check->stats.previous.state != 0 &&
     check->stats.current.state != check->stats.previous.state)
    report_change = 1;

  noitL(noit_error, "%s/%s <- [%s]\n", check->target, check->module,
        check->stats.current.status);
  if(report_change) {
    noitL(noit_error, "%s/%s -> [%s/%s]\n",
          check->target, check->module,
          __noit_check_available_string(check->stats.current.available),
          __noit_check_state_string(check->stats.current.state));
  }
}
