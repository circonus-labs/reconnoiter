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

#include "noit_module.h"
#include "noit_conf.h"
#include "utils/noit_hash.h"
#include "utils/noit_log.h"

static noit_image_t *
noit_load_module_image(noit_dso_loader_t *loader,
                       char *module_name,
                       noit_conf_section_t section);

noit_dso_loader_t __noit_module_loader = {
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

static noit_hash_table modules = NOIT_HASH_EMPTY;
static int noit_module_load_failure_count = 0;

int noit_module_load_failures() {
  return noit_module_load_failure_count;
}

int
noit_module_list_modules(const char ***f) {
  return noit_dso_list(&modules, f);
}

noit_module_t * noit_module_lookup(const char *name) {
  void *vmodule;

  if(noit_hash_retrieve(&modules, name, strlen(name), &vmodule))
    return (noit_module_t *)vmodule;
  return NULL;
}

static int noit_module_validate_magic(noit_image_t *obj) {
  if (NOIT_IMAGE_MAGIC(obj) != NOIT_MODULE_MAGIC) return -1;
  if (NOIT_IMAGE_VERSION(obj) != NOIT_MODULE_ABI_VERSION) return -1;
  return 0;
}

noit_module_t *noit_blank_module() {
  noit_module_t *obj;
  obj = calloc(1, sizeof(*obj));
  obj->hdr.opaque_handle = noit_dso_alloc_opaque_handle();
  return obj;
}

int noit_register_module(noit_module_t *mod) {
  return !noit_hash_store(&modules, mod->hdr.name, strlen(mod->hdr.name), mod);
}

static noit_image_t *
noit_load_module_image(noit_dso_loader_t *loader,
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
    noitL(noit_stderr, "Could not load module %s:%s\n", module_file, module_name);
    return NULL;
  }
  return (noit_image_t *)noit_module_lookup(module_name);
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

    nc_printf(ncct, "= Modules =\n");
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
    if(!mod) mod = (noit_module_t *)noit_dso_generic_lookup(argv[0]);
    noit_module_print_help(ncct, mod, argc == 2); 
    return 0; 
  } 
  nc_printf(ncct, "help module [ <modulename> [ examples ] ]\n");
  return -1;
}

void noit_module_init() {
  noit_conf_section_t *sections;
  int i, cnt = 0;

  noit_dso_add_type("module", noit_module_list_modules);
  noit_console_add_help("module", noit_module_help, noit_module_options);

  /* Load the modules (these *are* specific to the /noit/ root) */
  sections = noit_conf_get_sections(NULL, "/noit/modules//module", &cnt);
  if(!sections) cnt = 0;
  for(i=0; i<cnt; i++) {
    noit_dso_loader_t *loader = &__noit_module_loader;
    noit_hash_table *config;
    noit_module_t *module;
    noit_conf_section_t *include_sections;
    char loader_name[256];
    char module_name[256];
    int section_cnt;

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
        noit_module_load_failure_count++;
        continue;
      }
    } else {
      strlcpy(loader_name, "image", sizeof(loader_name));
    }

    module = (noit_module_t *)loader->load(loader, module_name, sections[i]);
    if(!module) {
      noitL(noit_stderr, "Loader '%s' failed to load '%s'.\n",
            loader_name, module_name);
      noit_module_load_failure_count++;
      continue;
    }
    if(module->config) {
      int rv;
      include_sections = noit_conf_get_sections(sections[i], "include", &section_cnt);
      if ((include_sections) && (section_cnt == 1)) {
        config = noit_conf_get_hash(*include_sections, "config");
      }
      else {
        config = noit_conf_get_hash(sections[i], "config");
      }
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
      if(include_sections) free(include_sections);
    }
    if(module->init && module->init(module)) {
      noitL(noit_stderr,
            "Initialized failed on %s\n", module_name);
      continue;
    }
    noitL(noit_debug, "Module %s successfully loaded.\n", module_name);
  }
  if(cnt) free(sections);
}

#define userdata_accessors(type, field) \
void *noit_##type##_get_userdata(noit_##type##_t *mod) { \
  return mod->field->userdata; \
} \
void noit_##type##_set_userdata(noit_##type##_t *mod, void *newdata) { \
  mod->field->userdata = newdata; \
}

userdata_accessors(module, hdr.opaque_handle)
