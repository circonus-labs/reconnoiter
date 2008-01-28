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

struct __extended_module_data {
  void *userdata;
};

static noit_hash_table modules = NOIT_HASH_EMPTY;

int noit_module_load(const char *file, const char *name) {
  char module_file[PATH_MAX];
  char *base;
  void *dlhandle;
  void *dlsymbol;
  noit_module_t *module;

  if(!noit_conf_get_string(NULL, "/noit/modules/directory", &base))
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

  if(noit_module_validate_magic((noit_module_t *)dlsymbol) == -1) {
    noit_log(noit_stderr, NULL, "I can't understand module %s\n", name);
    dlclose(dlhandle);
    return -1;
  }

  module = calloc(1, sizeof(*module));
  memcpy(module, dlsymbol, sizeof(*module));
  module->opaque_handle = calloc(1, sizeof(struct __extended_module_data));

  if(module->onload(module)) {
    free(module);
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

void noit_module_init() {
  noit_conf_section_t *sections;
  int i, cnt = 0;

  sections = noit_conf_get_sections(NULL, "/noit/modules/module", &cnt);
  if(!sections) return;
  for(i=0; i<cnt; i++) {
    noit_hash_table *config;
    noit_module_t *module;
    char module_file[PATH_MAX];
    char module_name[256];
    if(!noit_conf_get_stringbuf(sections[i], "image",
                                module_file, sizeof(module_file))) {
      noit_log(noit_stderr, NULL,
               "No image defined in module stanza %d\n", i+1);
      continue;
    }
    if(!noit_conf_get_stringbuf(sections[i], "name",
                                module_name, sizeof(module_name))) {
      noit_log(noit_stderr, NULL,
               "No name defined in module stanza %d\n", i+1);
      continue;
    }
    if(noit_module_load(module_file, module_name)) {
      noit_log(noit_stderr, NULL,
               "Could not load %s:%s\n", module_file, module_name);
      continue;
    }
    config = noit_conf_get_hash(sections[i], "config/*");
    module = noit_module_lookup(module_name);
    if(module->config(module, config)) {
      noit_log(noit_stderr, NULL,
               "Configure failed on %s:%s\n", module_file, module_name);
      continue;
    }
    if(module->init(module)) {
      noit_log(noit_stderr, NULL,
               "Initialized failed on %s:%s\n", module_file, module_name);
      continue;
    }
    noit_log(noit_stderr, NULL, "Module %s:%s successfully loaded.\n",
             module_file, module_name);
  }
}

void *noit_module_get_userdata(noit_module_t *mod) {
  struct __extended_module_data *emd;
  emd = (struct __extended_module_data *)mod->opaque_handle;
  return emd->userdata;
}
void noit_module_set_userdata(noit_module_t *mod, void *newdata) {
  struct __extended_module_data *emd;
  emd = (struct __extended_module_data *)mod->opaque_handle;
  emd->userdata = newdata;
}

