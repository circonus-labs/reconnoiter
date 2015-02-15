/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2015, Circonus, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "noit_defines.h"

#include <stdio.h>
#include <dlfcn.h>
#include <assert.h>

#include <libxml/parser.h>
#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>

#include "noit_dso.h"
#include "noit_conf.h"
#include "utils/noit_hash.h"
#include "utils/noit_log.h"

NOIT_HOOK_IMPL(dso_post_init,
  (),
  void *, closure,
  (void *closure),
  (closure))

static noit_image_t *
noit_load_generic_image(noit_dso_loader_t *loader,
                        char *g_name,
                        noit_conf_section_t section);

noit_dso_loader_t __noit_image_loader = {
  {
    NOIT_LOADER_MAGIC,
    NOIT_LOADER_ABI_VERSION,
    "image",
    "Basic binary image loader",
    NULL
  },
  NULL,
  NULL,
  noit_load_generic_image
};

static noit_hash_table loaders = NOIT_HASH_EMPTY;
static noit_hash_table generics = NOIT_HASH_EMPTY;
static int noit_dso_load_failure_count = 0;

int noit_dso_load_failures() {
  return noit_dso_load_failure_count;
}
noit_dso_loader_t * noit_loader_lookup(const char *name) {
  void *vloader;

  if(noit_hash_retrieve(&loaders, name, strlen(name), &vloader))
    return (noit_dso_loader_t *)vloader;
  return NULL;
}

int
noit_dso_list(noit_hash_table *t, const char ***f) {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *name;
  int klen, i = 0;
  void *vhdr;

  if(noit_hash_size(t) == 0) {
    *f = NULL;
    return 0;
  }

  *f = calloc(noit_hash_size(t), sizeof(**f));
  while(noit_hash_next(t, &iter, (const char **)&name, &klen,
                       &vhdr)) {
    noit_image_t *hdr = (noit_image_t *)vhdr;
    (*f)[i++] = hdr->name;
  }
  return i;
}

static int
noit_dso_list_loaders(const char ***f) {
  return noit_dso_list(&loaders, f);
}

static int
noit_dso_list_generics(const char ***f) {
  return noit_dso_list(&generics, f);
}

noit_dso_generic_t *noit_dso_generic_lookup(const char *name) {
  void *vmodule;

  if(noit_hash_retrieve(&generics, name, strlen(name), &vmodule))
    return (noit_dso_generic_t *)vmodule;
  return NULL;
}

static int noit_dso_generic_validate_magic(noit_image_t *obj) {
  if (NOIT_IMAGE_MAGIC(obj) != NOIT_GENERIC_MAGIC) return -1;
  if (NOIT_IMAGE_VERSION(obj) != NOIT_GENERIC_ABI_VERSION) return -1;
  return 0;
}

static int noit_dso_loader_validate_magic(noit_image_t *obj) {
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

  if(!noit_conf_get_string(NULL, "//modules/@directory", &base))
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
    free(obj->opaque_handle);
    free(obj);
    dlclose(dlhandle);
    return -1;
  }
  if(!noit_hash_store(registry, obj->name, strlen(obj->name), obj)) {
    noitL(noit_error, "Attempted to load module %s more than once.\n", obj->name);
    dlclose(dlhandle);
    return -1;
  }
  ((struct __extended_image_data *)obj->opaque_handle)->dlhandle = dlhandle;
  return 0;
}

static noit_image_t *
noit_load_generic_image(noit_dso_loader_t *loader,
                        char *g_name,
                        noit_conf_section_t section) {
  char g_file[PATH_MAX];

  if(!noit_conf_get_stringbuf(section, "ancestor-or-self::node()/@image",
                              g_file, sizeof(g_file))) {
    noitL(noit_stderr, "No image defined for %s\n", g_name);
    return NULL;
  }
  if(noit_load_image(g_file, g_name, &generics,
                     noit_dso_generic_validate_magic,
                     sizeof(noit_dso_generic_t))) {
    noitL(noit_stderr, "Could not load generic %s:%s\n", g_file, g_name);
    return NULL;
  }
  return (noit_image_t *)noit_dso_generic_lookup(g_name);
}

static noit_image_t *
noit_load_loader_image(noit_dso_loader_t *loader,
                       char *loader_name,
                       noit_conf_section_t section) {
  char loader_file[PATH_MAX];

  if(!noit_conf_get_stringbuf(section, "ancestor-or-self::node()/@image",
                              loader_file, sizeof(loader_file))) {
    noitL(noit_stderr, "No image defined for %s\n", loader_name);
    return NULL;
  }
  if(noit_load_image(loader_file, loader_name, &loaders,
                     noit_dso_loader_validate_magic,
                     sizeof(noit_dso_loader_t))) {
    noitL(noit_stderr, "Could not load loader %s:%s\n", loader_file, loader_name);
    noit_dso_load_failure_count++;
    return NULL;
  }
  return (noit_image_t *)noit_loader_lookup(loader_name);
}

void noit_dso_init() {
  noit_conf_section_t *sections;
  int i, cnt = 0;

  noit_dso_add_type("loader", noit_dso_list_loaders);
  noit_dso_add_type("generic", noit_dso_list_generics);

  /* Load our generic modules */
  sections = noit_conf_get_sections(NULL, "//modules//generic", &cnt);
  for(i=0; i<cnt; i++) {
    char g_name[256];
    noit_dso_generic_t *gen;

    if(!noit_conf_get_stringbuf(sections[i], "ancestor-or-self::node()/@name",
                                g_name, sizeof(g_name))) {
      noitL(noit_stderr, "No name defined in generic stanza %d\n", i+1);
      continue;
    }
    gen = (noit_dso_generic_t *)
      noit_load_generic_image(&__noit_image_loader, g_name,
                              sections[i]);
    if(!gen) {
      noitL(noit_stderr, "Failed to load generic %s\n", g_name);
      noit_dso_load_failure_count++;
      continue;
    }
    if(gen->config) {
      int rv;
      noit_hash_table *config;
      config = noit_conf_get_hash(sections[i], "config");
      rv = gen->config(gen, config);
      if(rv == 0) {
        noit_hash_destroy(config, free, free);
        free(config);
      }
      else if(rv < 0) {
        noitL(noit_stderr, "Failed to config generic %s\n", g_name);
        continue;
      }
    }
    if(gen->init && gen->init(gen)) {
      noitL(noit_stderr, "Failed to init generic %s\n", g_name);
      noit_dso_load_failure_count++;
    }
    else
      noitL(noit_debug, "Generic module %s successfully loaded.\n", g_name);
  }
  if(sections) free(sections);
  /* Load our module loaders */
  sections = noit_conf_get_sections(NULL, "//modules//loader", &cnt);
  for(i=0; i<cnt; i++) {
    char loader_name[256];
    noit_dso_loader_t *loader;

    if(!noit_conf_get_stringbuf(sections[i], "ancestor-or-self::node()/@name",
                                loader_name, sizeof(loader_name))) {
      noitL(noit_stderr, "No name defined in loader stanza %d\n", i+1);
      continue;
    }
    loader = (noit_dso_loader_t *)
      noit_load_loader_image(&__noit_image_loader, loader_name,
                             sections[i]);
    if(!loader) {
      noitL(noit_stderr, "Failed to load loader %s\n", loader_name);
      noit_dso_load_failure_count++;
      continue;
    }
    if(loader->config) {
      int rv;
      noit_hash_table *config;
      config = noit_conf_get_hash(sections[i], "config");
      rv = loader->config(loader, config);
      if(rv == 0) {
        noit_hash_destroy(config, free, free);
        free(config);
      }
      else if(rv < 0) {
        noitL(noit_stderr, "Failed to config loader %s\n", loader_name);
        noit_dso_load_failure_count++;
        continue;
      }
    }
    if(loader->init && loader->init(loader))
      noitL(noit_stderr, "Failed to init loader %s\n", loader_name);
  }
  if(sections) free(sections);
}

void noit_dso_post_init() {
  if(dso_post_init_hook_invoke() == NOIT_HOOK_ABORT) {
    noitL(noit_stderr, "Module post initialization phase failed.\n");
    noit_dso_load_failure_count++;
  }
}

void *
noit_dso_alloc_opaque_handle() {
  return calloc(1, sizeof(struct __extended_image_data));
}

static struct dso_type *dso_types = NULL;
struct dso_type *noit_dso_get_types() { return dso_types; }

void noit_dso_add_type(const char *name, int (*list)(const char ***)) {
  struct dso_type *node;
  node = calloc(1, sizeof(*node));
  node->name = strdup(name);
  node->list = list;
  node->next = dso_types;
  dso_types = node;
}

#define userdata_accessors(type, field) \
void *noit_##type##_get_userdata(noit_##type##_t *mod) { \
  return mod->field->userdata; \
} \
void noit_##type##_set_userdata(noit_##type##_t *mod, void *newdata) { \
  mod->field->userdata = newdata; \
}

userdata_accessors(image, opaque_handle)
userdata_accessors(dso_loader, hdr.opaque_handle)
