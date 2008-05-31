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

static noit_module_t *
noit_load_module_image(noit_module_loader_t *loader,
                       char *module_name,
                       noit_conf_section_t section);

noit_module_loader_t __noit_image_loader = {
  {
    NOIT_LOADER_MAGIC,
    NOIT_LOADER_ABI_VERSION,
    "image",
    "Basic binary image loader",
    NULL
  },
  noit_load_module_image
};
struct __extended_image_data {
  void *userdata;
};

static noit_hash_table loaders = NOIT_HASH_EMPTY;
static noit_hash_table modules = NOIT_HASH_EMPTY;

noit_module_loader_t * noit_loader_lookup(const char *name) {
  noit_module_loader_t *loader;

  if(noit_hash_retrieve(&loaders, name, strlen(name), (void **)&loader)) {
    return loader;
  }
  return NULL;
}

noit_module_t * noit_module_lookup(const char *name) {
  noit_module_t *module;

  if(noit_hash_retrieve(&modules, name, strlen(name), (void **)&module)) {
    return module;
  }
  return NULL;
}

static int noit_module_validate_magic(noit_image_t *obj) {
  if (NOIT_IMAGE_MAGIC(obj) != NOIT_MODULE_MAGIC) return -1;
  if (NOIT_IMAGE_VERSION(obj) != NOIT_MODULE_ABI_VERSION) return -1;
  return 0;
}

static int noit_module_loader_validate_magic(noit_image_t *obj) {
  if (NOIT_IMAGE_MAGIC(obj) != NOIT_LOADER_MAGIC) return -1;
  if (NOIT_IMAGE_VERSION(obj) != NOIT_LOADER_ABI_VERSION) return -1;
  return 0;
}

int noit_load_image(const char *file, const char *name,
                    noit_hash_table *registry,
                    int (*validate)(noit_image_t *),
                    size_t obj_size) {
  char module_file[PATH_MAX];
  char *base;
  void *dlhandle;
  void *dlsymbol;
  noit_image_t *obj;

  if(!noit_conf_get_string(NULL, "/noit/modules/@directory", &base))
    base = strdup("");

  if(file[0] == '/')
    strlcpy(module_file, file, sizeof(module_file));
  else
    snprintf(module_file, sizeof(module_file), "%s/%s.%s",
             base, file, MODULEEXT);
  free(base);

  dlhandle = dlopen(module_file, RTLD_LAZY | RTLD_GLOBAL);
  if(!dlhandle) {
    noitL(noit_stderr, "Cannot open image '%s': %s\n",
          module_file, dlerror());
    return -1;
  }

  dlsymbol = dlsym(dlhandle, name);
  if(!dlsymbol) {
    noitL(noit_stderr, "Cannot find '%s' in image '%s': %s\n",
          name, module_file, dlerror());
    dlclose(dlhandle);
    return -1;
  }

  if(validate(dlsymbol) == -1) {
    noitL(noit_stderr, "I can't understand image %s\n", name);
    dlclose(dlhandle);
    return -1;
  }

  obj = calloc(1, obj_size);
  memcpy(obj, dlsymbol, obj_size);
  obj->opaque_handle = calloc(1, sizeof(struct __extended_image_data));

  if(obj->onload && obj->onload(obj)) {
    free(obj);
    return -1;
  }
  noit_hash_store(registry, obj->name, strlen(obj->name), obj);
  return 0;
}

static noit_module_loader_t *
noit_load_loader_image(noit_module_loader_t *loader,
                       char *loader_name,
                       noit_conf_section_t section) {
  char loader_file[PATH_MAX];

  if(!noit_conf_get_stringbuf(section, "ancestor-or-self::node()/@image",
                              loader_file, sizeof(loader_file))) {
    noitL(noit_stderr, "No image defined for %s\n", loader_name);
    return NULL;
  }
  if(noit_load_image(loader_file, loader_name, &loaders,
                     noit_module_loader_validate_magic,
                     sizeof(noit_module_loader_t))) {
    noitL(noit_stderr, "Could not load %s:%s\n", loader_file, loader_name);
    return NULL;
  }
  return noit_loader_lookup(loader_name);
}

static noit_module_t *
noit_load_module_image(noit_module_loader_t *loader,
                       char *module_name,
                       noit_conf_section_t section) {
  char module_file[PATH_MAX];

  if(!noit_conf_get_stringbuf(section, "ancestor-or-self::node()/@image",
                              module_file, sizeof(module_file))) {
    noitL(noit_stderr, "No image defined for %s\n", module_name);
    return NULL;
  }
  if(noit_load_image(module_file, module_name, &modules,
                     noit_module_validate_magic, sizeof(noit_module_t))) {
    noitL(noit_stderr, "Could not load %s:%s\n", module_file, module_name);
    return NULL;
  }
  return noit_module_lookup(module_name);
}

void noit_module_init() {
  noit_conf_section_t *sections;
  int i, cnt = 0;

  /* Load our module loaders */
  sections = noit_conf_get_sections(NULL, "/noit/modules//loader", &cnt);
  for(i=0; i<cnt; i++) {
    char loader_name[256];
    noit_module_loader_t *loader;

    if(!noit_conf_get_stringbuf(sections[i], "ancestor-or-self::node()/@name",
                                loader_name, sizeof(loader_name))) {
      noitL(noit_stderr, "No name defined in loader stanza %d\n", i+1);
      continue;
    }
    loader = noit_load_loader_image(&__noit_image_loader, loader_name,
                                    sections[i]);
    if(!loader) {
      noitL(noit_stderr, "Failed to load loader %s\n", loader_name);
      continue;
    }
  }
  if(sections) free(sections);

  /* Load the modules */
  sections = noit_conf_get_sections(NULL, "/noit/modules//module", &cnt);
  if(!sections) return;
  for(i=0; i<cnt; i++) {
    noit_module_loader_t *loader = &__noit_image_loader;
    noit_hash_table *config;
    noit_module_t *module;
    char loader_name[256];
    char module_name[256];

    /* If no loader is specified, we should use the image loader */
    if(!noit_conf_get_stringbuf(sections[i], "ancestor-or-self::node()/@name",
                                module_name, sizeof(module_name))) {
      noitL(noit_stderr, "No name defined in module stanza %d\n", i+1);
      continue;
    }

    if(noit_conf_get_stringbuf(sections[i], "ancestor-or-self::node()/@loader",
                                loader_name, sizeof(loader_name))) {
      loader = noit_loader_lookup(loader_name);
      if(!loader) {
        noitL(noit_stderr, "No '%s' loader found.\n", loader_name);
        continue;
      }
    } else {
      strlcpy(loader_name, "image", sizeof(loader_name));
    }

    module = loader->load(loader, module_name, sections[i]);
    if(!module) {
      noitL(noit_stderr, "Loader '%s' failed to load '%s'.\n",
            loader_name, module_name);
      continue;
    }
    config = noit_conf_get_hash(sections[i], "config");
    if(module->config) {
      int rv;
      rv = module->config(module, config);
      if(rv == 0) {
        /* Not an error,
         * but the module didn't take responsibility for the config.
         */
        noit_hash_destroy(config, free, free);
        free(config);
      }
      else if(rv < 0) {
        noitL(noit_stderr,
              "Configure failed on %s\n", module_name);
        continue;
      }
    }
    if(module->init && module->init(module)) {
      noitL(noit_stderr,
            "Initialized failed on %s\n", module_name);
      continue;
    }
    noitL(noit_stderr, "Module %s successfully loaded.\n", module_name);
  }
  free(sections);
}

#define userdata_accessors(type, field) \
void *noit_##type##_get_userdata(noit_##type##_t *mod) { \
  return ((struct __extended_image_data *)mod->field)->userdata; \
} \
void noit_##type##_set_userdata(noit_##type##_t *mod, void *newdata) { \
  ((struct __extended_image_data *)mod->field)->userdata = newdata; \
}

userdata_accessors(image, opaque_handle)
userdata_accessors(module_loader, hdr.opaque_handle)
userdata_accessors(module, hdr.opaque_handle)
