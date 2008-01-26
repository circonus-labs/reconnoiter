/*
 * Copyright (c) 2007, 2008, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_MODULE_H
#define _NOIT_MODULE_H

#include "noit_defines.h"
#include "utils/noit_hash.h"
#include "noit_poller.h"

#define NOIT_MODULE_MAGIC 0x4017DA7A

typedef struct {
  uint32_t magic;
  uint32_t version;
  char *name;
  char *description;
  int (*onload)();
  int (*config)(noit_hash_table *options);
  int (*init)();
  int (*initiate_check)(noit_check_t check);
} noit_module_t;

#define MODULE_MAGIC(a)          ((a)->magic)
#define MODULE_VERSION(a)        ((a)->version)

#define noit_module_validate_magic(a) \
  ((MODULE_MAGIC(a) == NOIT_MODULE_MAGIC)?0:-1)

API_EXPORT(int)
  noit_module_load(const char *file, const char *name);
API_EXPORT(noit_module_t *)
  noit_module_lookup(const char *name);

#endif
