/* Copyright 2019, Circonus Inc.
 * All Rights Reserved.
 */

#ifndef CHECK_TEST_H
#define CHECK_TEST_H

#include <mtev_hooks.h>

MTEV_RUNTIME_AVAIL(noit_testcheck_lookup,
                   noit_testcheck_lookup_dyn)
MTEV_RUNTIME_RESOLVE(noit_testcheck_lookup,
                     noit_testcheck_lookup_dyn,
                     noit_check_t *,
                     (uuid_t in),
                     (in))

#define NOIT_TESTCHECK_LOOKUP(noit_check_ptr,uuid) do { \
  if(noit_testcheck_lookup_available()) { \
    noit_check_ptr = noit_testcheck_lookup(uuid); \
  } \
  else { \
    noit_check_ptr = NULL; \
  } \
} while(0)

#endif
