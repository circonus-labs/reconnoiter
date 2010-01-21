/*
 * Copyright (c) 2007, 2008, OmniTI Computer Consulting, Inc.
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

#ifndef _NOIT_MODULE_H
#define _NOIT_MODULE_H

#include "noit_defines.h"
#include "utils/noit_hash.h"
#include "noit_check.h"

#define NOIT_LOADER_MAGIC         0xA7AD7104
#define NOIT_LOADER_ABI_VERSION   4

typedef struct _noit_image {
  uint32_t magic;
  uint32_t version;
  char *name;
  char *description;
  char *xml_description;
  int (*onload)(struct _noit_image *);
  void *opaque_handle;
} noit_image_t;

/* onload: is called immediately after the module is loaded and before it
 *         is configured.
 */

typedef struct _noit_module_generic {
  noit_image_t hdr;
  int (*config)(struct _noit_module_generic *, noit_hash_table *config);
  int (*init)(struct _noit_module_generic *);
} noit_module_generic_t;

#define NOIT_GENERIC_MAGIC         0x3FD892A0
#define NOIT_GENERIC_ABI_VERSION   1

typedef struct _noit_module_loader {
  noit_image_t hdr;
  int (*config)(struct _noit_module_loader *, noit_hash_table *config);
  int (*init)(struct _noit_module_loader *);
  struct _noit_module *(*load)(struct _noit_module_loader *loader,
                               char *module_name,
                               noit_conf_section_t section);
} noit_module_loader_t;

/* config:  is called once to configure the loader itself
 *   init:  is called once, post config to initialize the module
 *   load:  is called each time the loader is asked to load a module
 */

#define NOIT_MODULE_MAGIC         0x4017DA7A
#define NOIT_MODULE_ABI_VERSION   3

typedef struct _noit_module {
  noit_image_t hdr;
  int (*config)(struct _noit_module *, noit_hash_table *options);
  int (*init)(struct _noit_module *);
  int (*initiate_check)(struct _noit_module *, noit_check_t *check,
                        int once, noit_check_t *cause);
  void (*cleanup)(struct _noit_module *, noit_check_t *);
} noit_module_t;

/*         config: is called to pass the config into the module.
 *           init: is called once to initialize the module
 * initiate_check: is called so start the module against checks
 *        cleanup: is called if a particular check is stopped
 */

#define NOIT_IMAGE_MAGIC(a)          ((a)->magic)
#define NOIT_IMAGE_VERSION(a)        ((a)->version)

API_EXPORT(void)
  noit_module_init();
API_EXPORT(int)
  noit_module_load_failures();
API_EXPORT(int)
  noit_module_load(const char *file, const char *name);
API_EXPORT(noit_module_t *)
  noit_module_lookup(const char *name);
API_EXPORT(noit_module_t *)
  noit_blank_module();
API_EXPORT(int)
  noit_register_module(noit_module_t *mod);

API_EXPORT(void *)
  noit_image_get_userdata(noit_image_t *mod);
API_EXPORT(void)
  noit_image_set_userdata(noit_image_t *mod, void *newdata);
API_EXPORT(void *)
  noit_module_loader_get_userdata(noit_module_loader_t *mod);
API_EXPORT(void)
  noit_module_loader_set_userdata(noit_module_loader_t *mod, void *newdata);
API_EXPORT(void *)
  noit_module_get_userdata(noit_module_t *mod);
API_EXPORT(void)
  noit_module_set_userdata(noit_module_t *mod, void *newdata);
API_EXPORT(void)
  noit_module_print_help(noit_console_closure_t ncct,
                         noit_module_t *module, int examples);

#endif
