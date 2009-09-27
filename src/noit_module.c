/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
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

#include <libxml/parser.h>
#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>

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
  NULL,
  NULL,
  noit_load_module_image
};
struct __extended_image_data {
  void *userdata;
};

static noit_hash_table loaders = NOIT_HASH_EMPTY;
static noit_hash_table modules = NOIT_HASH_EMPTY;
static noit_hash_table generics = NOIT_HASH_EMPTY;

noit_module_loader_t * noit_loader_lookup(const char *name) {
  void *vloader;

  if(noit_hash_retrieve(&loaders, name, strlen(name), &vloader))
    return (noit_module_loader_t *)vloader;
  return NULL;
}

noit_module_t * noit_module_lookup(const char *name) {
  void *vmodule;

  if(noit_hash_retrieve(&modules, name, strlen(name), &vmodule))
    return (noit_module_t *)vmodule;
  return NULL;
}

noit_module_generic_t * noit_module_generic_lookup(const char *name) {
  void *vmodule;

  if(noit_hash_retrieve(&generics, name, strlen(name), &vmodule))
    return (noit_module_generic_t *)vmodule;
  return NULL;
}

static int noit_module_validate_magic(noit_image_t *obj) {
  if (NOIT_IMAGE_MAGIC(obj) != NOIT_MODULE_MAGIC) return -1;
  if (NOIT_IMAGE_VERSION(obj) != NOIT_MODULE_ABI_VERSION) return -1;
  return 0;
}

static int noit_module_generic_validate_magic(noit_image_t *obj) {
  if (NOIT_IMAGE_MAGIC(obj) != NOIT_GENERIC_MAGIC) return -1;
  if (NOIT_IMAGE_VERSION(obj) != NOIT_GENERIC_ABI_VERSION) return -1;
  return 0;
}

static int noit_module_loader_validate_magic(noit_image_t *obj) {
  if (NOIT_IMAGE_MAGIC(obj) != NOIT_LOADER_MAGIC) return -1;
  if (NOIT_IMAGE_VERSION(obj) != NOIT_LOADER_ABI_VERSION) return -1;
  return 0;
}

noit_module_t *noit_blank_module() {
  noit_module_t *obj;
  obj = calloc(1, sizeof(*obj));
  obj->hdr.opaque_handle = calloc(1, sizeof(struct __extended_image_data));
  return obj;
}

int noit_register_module(noit_module_t *mod) {
  noit_hash_store(&modules, mod->hdr.name, strlen(mod->hdr.name), mod);
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

static noit_module_generic_t *
noit_load_generic_image(noit_module_loader_t *loader,
                        char *g_name,
                        noit_conf_section_t section) {
  char g_file[PATH_MAX];

  if(!noit_conf_get_stringbuf(section, "ancestor-or-self::node()/@image",
                              g_file, sizeof(g_file))) {
    noitL(noit_stderr, "No image defined for %s\n", g_name);
    return NULL;
  }
  if(noit_load_image(g_file, g_name, &generics,
                     noit_module_generic_validate_magic,
                     sizeof(noit_module_generic_t))) {
    noitL(noit_stderr, "Could not load %s:%s\n", g_file, g_name);
    return NULL;
  }
  return noit_module_generic_lookup(g_name);
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

#include "module-online.h"

void noit_module_print_help(noit_console_closure_t ncct,
                            noit_module_t *module, int examples) {
  const char *params[3] = { NULL };
  xmlDocPtr help = NULL, output = NULL;
  xmlOutputBufferPtr out;
  xmlCharEncodingHandlerPtr enc;
  static xmlDocPtr helpStyleDoc = NULL;
  static xsltStylesheetPtr helpStyle = NULL;
  if(!helpStyle) {
    if(!helpStyleDoc)
      helpStyleDoc = xmlParseMemory(helpStyleXML, strlen(helpStyleXML));
    if(!helpStyleDoc) {
      nc_printf(ncct, "Invalid XML for style XML\n");
      return;
    }
    helpStyle = xsltParseStylesheetDoc(helpStyleDoc);
  }
  if(!helpStyle) {
    nc_printf(ncct, "no available stylesheet.\n");
    return;
  }
  if(!module) {
    nc_printf(ncct, "no module\n");
    return;
  }
  if(!module->hdr.xml_description) {
    nc_printf(ncct, "%s is undocumented, complain to the vendor.\n",
              module->hdr.name);
    return;
  }
  help = xmlParseMemory(module->hdr.xml_description,
                        strlen(module->hdr.xml_description));
  if(!help) {
    nc_printf(ncct, "%s module has invalid XML documentation.\n",
              module->hdr.name);
    return;
  }

  if(examples) {
    params[0] = "example";
    params[1] = "'1'";
    params[2] = NULL;
  }
  output = xsltApplyStylesheet(helpStyle, help, params);
  if(!output) {
    nc_printf(ncct, "formatting failed.\n");
    goto out;
  }

  enc = xmlGetCharEncodingHandler(XML_CHAR_ENCODING_UTF8);
  out = xmlOutputBufferCreateIO(noit_console_write_xml,
                                noit_console_close_xml,
                                ncct, enc);
  xmlSaveFormatFileTo(out, output, "utf8", 1);

 out:
  if(help) xmlFreeDoc(help);
  if(output) xmlFreeDoc(output);
}

char *
noit_module_options(noit_console_closure_t ncct,
                    noit_console_state_stack_t *stack,
                    noit_console_state_t *state,
                    int argc, char **argv, int idx) {
  if(argc == 1) {
    /* List modules */ 
    noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
    const char *name;
    int klen, i = 0;
    void *vhdr;

    while(noit_hash_next(&loaders, &iter, (const char **)&name, &klen,
                         &vhdr)) {
      noit_image_t *hdr = (noit_image_t *)vhdr;
      if(!strncmp(hdr->name, argv[0], strlen(argv[0]))) {
        if(idx == i) return strdup(hdr->name);
        i++;
      }
    }
    memset(&iter, 0, sizeof(iter));
    while(noit_hash_next(&modules, &iter, (const char **)&name, &klen,
                         &vhdr)) {
      noit_image_t *hdr = (noit_image_t *)vhdr;
      if(!strncmp(hdr->name, argv[0], strlen(argv[0]))) {
        if(idx == i) return strdup(hdr->name);
        i++;
      }
    }
    return NULL;
  }
  if(argc == 2) {
    if(!strncmp("examples", argv[1], strlen(argv[1])))
      if(idx == 0) return strdup("examples");
  }
  return NULL;
}
int
noit_module_help(noit_console_closure_t ncct,
                 int argc, char **argv,
                 noit_console_state_t *state, void *closure) {
  if(argc == 0) { 
    /* List modules */ 
    noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
    const char *name;
    int klen;
    void *vhdr;

    nc_printf(ncct, "= Loaders and Modules =\n");
    while(noit_hash_next(&loaders, &iter, (const char **)&name, &klen,
                         &vhdr)) {
      noit_image_t *hdr = (noit_image_t *)vhdr;;
      nc_printf(ncct, "  %s\n", hdr->name);
    }
    memset(&iter, 0, sizeof(iter));
    while(noit_hash_next(&modules, &iter, (const char **)&name, &klen,
                         &vhdr)) {
      noit_image_t *hdr = (noit_image_t *)vhdr;;
      nc_printf(ncct, "  %s\n", hdr->name);
    }
    return 0;
  } 
  else if(argc == 1 || 
          (argc == 2 && !strcmp(argv[1], "examples"))) { 
    /* help for a specific module */ 
    noit_module_t *mod; 
    mod = noit_module_lookup(argv[0]); 
    noit_module_print_help(ncct, mod, argc == 2); 
    return 0; 
  } 
  nc_printf(ncct, "help module [ <modulename> [ examples ] ]\n");
  return -1;
}

void noit_module_init() {
  noit_conf_section_t *sections;
  int i, cnt = 0;

  noit_console_add_help("module", noit_module_help, noit_module_options);

  /* Load our generic modules */
  sections = noit_conf_get_sections(NULL, "/noit/modules//generic", &cnt);
  for(i=0; i<cnt; i++) {
    char g_name[256];
    noit_module_generic_t *gen;

    if(!noit_conf_get_stringbuf(sections[i], "ancestor-or-self::node()/@name",
                                g_name, sizeof(g_name))) {
      noitL(noit_stderr, "No name defined in generic stanza %d\n", i+1);
      continue;
    }
    gen = noit_load_generic_image(&__noit_image_loader, g_name,
                                  sections[i]);
    if(!gen) {
      noitL(noit_stderr, "Failed to load generic %s\n", g_name);
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
    if(gen->init && gen->init(gen))
      noitL(noit_stderr, "Failed to init generic %s\n", g_name);
    else
      noitL(noit_stderr, "Generic module %s successfully loaded.\n", g_name);
  }
  if(sections) free(sections);
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
        continue;
      }
    }
    if(loader->init && loader->init(loader))
      noitL(noit_stderr, "Failed to init loader %s\n", loader_name);
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
