/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include "noit_module.h"
#include "noit_poller.h"

static int ping_icmp_onload(noit_module_t *self) {
  return 0;
}
static int ping_icmp_config(noit_module_t *self, noit_hash_table *options) {
  return 0;
}
static int ping_icmp_init(noit_module_t *self) {
  return 0;
}
static int ping_icmp_initiate_check(noit_module_t *self, noit_check_t check) {
  return 0;
}

noit_module_t ping_icmp = {
  NOIT_MODULE_MAGIC,
  NOIT_MODULE_ABI_VERSION,
  "ping_icmp",
  "ICMP based host availability detection",
  ping_icmp_onload,
  ping_icmp_config,
  ping_icmp_init,
  ping_icmp_initiate_check
};

