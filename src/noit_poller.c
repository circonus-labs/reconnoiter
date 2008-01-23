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
#include "noit_poller.h"
#include "eventer/eventer.h"

noit_hash_table polls = NOIT_HASH_EMPTY;

void
noit_poller_init() {
  noit_hash_init(&polls);
}

int
noit_poller_schedule(const char *target,
                     const char *module,
                     const char *name,
                     noit_hash_table *config,
                     u_int32_t period,
                     u_int32_t timeout,
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
  uuid_generate(new_check->checkid);

  assert(noit_hash_store(&polls,
                         (char *)new_check->checkid, sizeof(new_check->checkid),
                         new_check));
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

