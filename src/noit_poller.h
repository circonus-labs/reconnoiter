/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_POLLER_H
#define _NOIT_POLLER_H

#include "noit_defines.h"

#include <uuid/uuid.h>
#include <netinet/in.h>

#include "eventer/eventer.h"
#include "utils/noit_hash.h"

/*
 * Checks:
 *  attrs:
 *   UUID
 *   host (target)
 *   check (module)
 *   name (identifying the check to the user if
 *         multiple checks of the same module are specified)
 *   config (params for the module)
 *   period (ms)
 *   timeout (ms)
 *  transient:
 *   eventer_t (fire)
 *   stats_t [inprogress, last]
 *   closure
 */

#define NP_RUNNING  0x00000001
#define NP_KILLED   0x00000002
#define NP_DISABLED 0x00000004

typedef struct {
  struct timeval whence;
  u_int32_t duration;
  char *status;
} stats_t;

typedef struct {
  uuid_t checkid;
  int8_t target_family;
  union {
    struct in_addr addr;
    struct in6_addr addr6;
  } target_addr;
  char *target;
  char *module;
  char *name;
  noit_hash_table *config;
  u_int32_t period;
  u_int32_t timeout;
  u_int32_t flags;             /* NP_KILLED, NP_RUNNING */

  eventer_t fire_event;
  struct {
    stats_t in_progress;
    stats_t last;
  } stats;
  void *closure;
} * noit_check_t;

API_EXPORT(int)
  noit_poller_schedule(const char *target,
                       const char *module,
                       const char *name,
                       noit_hash_table *config,
                       u_int32_t period,
                       u_int32_t timeout,
                       uuid_t out);

API_EXPORT(int)
  noit_poller_deschedule(uuid_t in);

#endif
