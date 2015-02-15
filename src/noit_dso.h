/*
 * Copyright (c) 2007, 2008, OmniTI Computer Consulting, Inc.
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

#ifndef _NOIT_DSO_H
#define _NOIT_DSO_H

#include "noit_defines.h"
#include "noit_conf.h"
#include "utils/noit_hash.h"
#include "utils/noit_hooks.h"

#define NOIT_LOADER_MAGIC         0xA7AD7104
#define NOIT_LOADER_ABI_VERSION   5

struct __extended_image_data {
  void *userdata;
  void *dlhandle;
};

typedef struct _noit_image {
  uint32_t magic;
  uint32_t version;
  char *name;
  char *description;
  char *xml_description;
  int (*onload)(struct _noit_image *);
  struct __extended_image_data *opaque_handle;
} noit_image_t;

/* onload: is called immediately after the module is loaded and before it
 *         is configured.
 */

typedef struct _noit_dso_generic {
  noit_image_t hdr;
  int (*config)(struct _noit_dso_generic *, noit_hash_table *config);
  int (*init)(struct _noit_dso_generic *);
} noit_dso_generic_t;

#define NOIT_GENERIC_MAGIC         0x3FD892A0
#define NOIT_GENERIC_ABI_VERSION   2

typedef struct _noit_dso_loader {
  noit_image_t hdr;
  int (*config)(struct _noit_dso_loader *, noit_hash_table *config);
  int (*init)(struct _noit_dso_loader *);
  noit_image_t *(*load)(struct _noit_dso_loader *loader,
                        char *module_name,
                        noit_conf_section_t section);
} noit_dso_loader_t;

/* config:  is called once to configure the loader itself
 *   init:  is called once, post config to initialize the module
 *   load:  is called each time the loader is asked to load a module
 */

#define NOIT_IMAGE_MAGIC(a)          ((a)->magic)
#define NOIT_IMAGE_VERSION(a)        ((a)->version)

API_EXPORT(void)
  noit_dso_init();
API_EXPORT(void)
  noit_dso_post_init();
API_EXPORT(int)
  noit_dso_load_failures();
API_EXPORT(int)
  noit_dso_load(const char *file, const char *name);

API_EXPORT(int)
  noit_load_image(const char *file, const char *name,
                  noit_hash_table *registry,
                  int (*validate)(noit_image_t *),
                  size_t obj_size);

API_EXPORT(int)
  noit_dso_list(noit_hash_table *t, const char ***f);

API_EXPORT(noit_dso_loader_t *)
  noit_loader_lookup(const char *name);
API_EXPORT(noit_dso_generic_t *)
  noit_dso_generic_lookup(const char *name);

API_EXPORT(void *)
  noit_image_get_userdata(noit_image_t *mod);
API_EXPORT(void)
  noit_image_set_userdata(noit_image_t *mod, void *newdata);
API_EXPORT(void *)
  noit_dso_loader_get_userdata(noit_dso_loader_t *mod);
API_EXPORT(void)
  noit_dso_loader_set_userdata(noit_dso_loader_t *mod, void *newdata);
API_EXPORT(void *)
  noit_dso_get_userdata(noit_image_t *mod);
API_EXPORT(void)
  noit_dso_set_userdata(noit_image_t *mod, void *newdata);
API_EXPORT(void *)
  noit_dso_alloc_opaque_handle();

struct dso_type {
  const char *name;
  int (*list)(const char ***);
  struct dso_type *next;
};
API_EXPORT(void)
  noit_dso_add_type(const char *name, int (*list)(const char ***));
API_EXPORT(struct dso_type *)
  noit_dso_get_types();

NOIT_HOOK_PROTO(dso_post_init,
                (),
                void *, closure,
                (void *closure))

#endif
