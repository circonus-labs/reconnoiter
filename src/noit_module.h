/*
 * Copyright (c) 2007, 2008, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_MODULE_H
#define _NOIT_MODULE_H

#include "noit_defines.h"
#include "utils/noit_hash.h"
#include "noit_check.h"

#define NOIT_MODULE_MAGIC         0x4017DA7A
#define NOIT_MODULE_ABI_VERSION   1

typedef struct _noit_module {
  uint32_t magic;
  uint32_t version;
  char *name;
  char *description;
  int (*onload)(struct _noit_module *);
  int (*config)(struct _noit_module *, noit_hash_table *options);
  int (*init)(struct _noit_module *);
  int (*initiate_check)(struct _noit_module *, noit_check_t *check,
                        int once, noit_check_t *cause);
  void (*cleanup)(struct _noit_module *, noit_check_t *);
  void *opaque_handle;
} noit_module_t;

#define MODULE_MAGIC(a)          ((a)->magic)
#define MODULE_VERSION(a)        ((a)->version)

#define noit_module_validate_magic(a) \
  ((MODULE_MAGIC(a) == NOIT_MODULE_MAGIC)?0:-1)

API_EXPORT(void)
  noit_module_init();
API_EXPORT(int)
  noit_module_load(const char *file, const char *name);
API_EXPORT(noit_module_t *)
  noit_module_lookup(const char *name);

API_EXPORT(void *)
  noit_module_get_userdata(noit_module_t *mod);
API_EXPORT(void)
  noit_module_set_userdata(noit_module_t *mod, void *newdata);

#endif
