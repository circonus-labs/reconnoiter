/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "noit_module.h"
#include "utils/noit_hash.h"

static noit_hash_table modules = NOIT_HASH_EMPTY;

int noit_module_load(const char *file, const char *name) {
}
noit_module_t * noit_module_lookup(const char *name) {
  noit_module_t *module;

  if(noit_hash_retrieve(&modules, name, strlen(name), (void **)&module)) {
    return module;
  }
  return NULL;
}

