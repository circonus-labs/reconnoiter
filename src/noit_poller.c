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
#include "noit_conf.h"
#include "noit_poller.h"
#include "eventer/eventer.h"

static noit_hash_table polls = NOIT_HASH_EMPTY;

void
noit_poller_load_checks() {
  int i, cnt = 0;
  noit_conf_section_t *sec;
  sec = noit_conf_get_sections(NULL, "/noit/checks//check", &cnt);
  for(i=0; i<cnt; i++) {
    char uuid_str[37];
    char target[256];
    char module[256];
    char name[256];
    int period, timeout;
    uuid_t uuid, out_uuid;
    noit_hash_table *options;

    if(!noit_conf_get_stringbuf(sec[i], "@uuid",
                                uuid_str, sizeof(uuid_str))) {
      noit_log(noit_stderr, NULL, "check %d has no uuid\n", i+1);
      continue;
    }
    if(uuid_parse(uuid_str, uuid)) {
      noit_log(noit_stderr, NULL, "check uuid: '%s' is invalid\n", uuid_str);
      continue;
    }
    if(!noit_conf_get_stringbuf(sec[i], "target", target, sizeof(target))) {
      if(!noit_conf_get_stringbuf(sec[i], "../target", target, sizeof(target))) {
         noit_log(noit_stderr, NULL, "check uuid: '%s' has no target\n",
                  uuid_str);
         continue;
      }
    }
    if(!noit_conf_get_stringbuf(sec[i], "module", module, sizeof(module))) {
      if(!noit_conf_get_stringbuf(sec[i], "../module", module, sizeof(module))) {
        noit_log(noit_stderr, NULL, "check uuid: '%s' has no module\n",
                 uuid_str);
        continue;
      }
    }
    if(!noit_conf_get_stringbuf(sec[i], "name", name, sizeof(name))) {
      strcpy(name, module);
    }
    if(!noit_conf_get_int(sec[i], "period", &period)) {
      if(!noit_conf_get_int(sec[i], "../period", &period)) {
        noit_log(noit_stderr, NULL, "check uuid: '%s' has no period\n", uuid_str);
        continue;
      }
    }
    if(!noit_conf_get_int(sec[i], "timeout", &timeout)) {
      if(!noit_conf_get_int(sec[i], "../timeout", &timeout)) {
        noit_log(noit_stderr, NULL, "check uuid: '%s' has no timeout\n", uuid_str);
        continue;
      }
    }
    options = noit_conf_get_hash(sec[i], "config");
    noit_poller_schedule(target, module, name, options,
                         period, timeout, uuid, out_uuid);
    noit_log(noit_debug, NULL, "loaded uuid: %s\n", uuid_str);
  }
}

void
noit_poller_init() {
  noit_poller_load_checks();
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
      noit_log(noit_stderr, NULL, "Cannot translate '%s' to IP\n", target);
      return -1;
    }
  }

  new_check = calloc(1, sizeof(*new_check));
  if(!new_check) return -1;
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
                         (char *)new_check->checkid, sizeof(new_check->checkid),
                         new_check));
  uuid_copy(out, new_check->checkid);
  return 0;
}

int
noit_poller_deschedule(uuid_t in) {
  noit_check_t checker;
  if(noit_hash_retrieve(&polls,
                        (char *)in, sizeof(in),
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
  noit_hash_delete(&polls, (char *)in, sizeof(in), free, free);

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
