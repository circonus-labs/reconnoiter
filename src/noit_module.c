/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include <stdio.h>
#include <dlfcn.h>

#include "noit_module.h"
#include "noit_conf.h"
#include "utils/noit_hash.h"
#include "utils/noit_log.h"

static noit_hash_table modules = NOIT_HASH_EMPTY;

int noit_module_load(const char *file, const char *name) {
  char module_file[PATH_MAX];
  char *base;
  void *dlhandle;
  void *dlsymbol;
  noit_module_t *module;

  if(!noit_conf_get_string("/global/modules/directory", &base))
    base = strdup("");

  if(file[0] == '/')
    strlcpy(module_file, file, sizeof(module_file));
  else
    snprintf(module_file, sizeof(module_file), "%s/%s.%s",
             base, name, MODULEEXT);
  free(base);

  dlhandle = dlopen(module_file, RTLD_LAZY | RTLD_GLOBAL);
  if(!dlhandle) {
    noit_log(noit_stderr, NULL, "Cannot open image '%s': %s\n",
             module_file, dlerror());
    return -1;
  }

  dlsymbol = dlsym(dlhandle, name);
  if(!dlsymbol) {
    noit_log(noit_stderr, NULL, "Cannot find '%s' in image '%s': %s\n",
             name, module_file, dlerror());
    dlclose(dlhandle);
    return -1;
  }

  module = (noit_module_t *)dlsymbol;
  if(noit_module_validate_magic(module) == -1) {
    noit_log(noit_stderr, NULL, "I can't understand module %s\n", name);
    dlclose(dlhandle);
    return -1;
  }

  noit_hash_store(&modules, module->name, strlen(module->name), module);
  return 0;
}

noit_module_t * noit_module_lookup(const char *name) {
  noit_module_t *module;

  if(noit_hash_retrieve(&modules, name, strlen(name), (void **)&module)) {
    return module;
  }
  return NULL;
}

