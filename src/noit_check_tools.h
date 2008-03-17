/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_CHECK_TOOLS_H
#define _NOIT_CHECK_TOOLS_H

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "noit_module.h"
#include "noit_check.h"
#include "utils/noit_hash.h"

typedef int (*dispatch_func_t)(noit_module_t *, noit_check_t *);

API_EXPORT(int)
  noit_check_interpolate(char *buff, int len, const char *fmt,
                         noit_hash_table *attrs,
                         noit_hash_table *config);

API_EXPORT(int)
  noit_check_schedule_next(noit_module_t *self,
                           struct timeval *last_check, noit_check_t *check,
                           struct timeval *now, dispatch_func_t recur);

API_EXPORT(void)
  noit_check_run_full_asynch(noit_check_t *check, eventer_func_t callback);

#define INITIATE_CHECK(func, self, check) do { \
  if(once) { \
    func(self, check); \
  } \
  else if(!check->fire_event) { \
    struct timeval epoch = { 0L, 0L }; \
    noit_check_fake_last_check(check, &epoch, NULL); \
    noit_check_schedule_next(self, &epoch, check, NULL, func); \
  } \
} while(0)

API_EXPORT(void)
  noit_check_make_attrs(noit_check_t *check, noit_hash_table *attrs);
API_EXPORT(void)
  noit_check_release_attrs(noit_hash_table *attrs);


#endif

