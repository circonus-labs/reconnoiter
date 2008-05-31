/*
 * Copyright (c) 2007, 2008, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_MODULE_H
#define _NOIT_MODULE_H

#include "noit_defines.h"
#include "utils/noit_hash.h"
#include "noit_check.h"

#define NOIT_LOADER_MAGIC         0xA7AD7104
#define NOIT_LOADER_ABI_VERSION   2

typedef struct _noit_image {
  uint32_t magic;
  uint32_t version;
  char *name;
  char *description;
  int (*onload)(struct _noit_image *);
  void *opaque_handle;
} noit_image_t;

typedef struct _noit_module_loader {
  noit_image_t hdr;
  struct _noit_module *(*load)(struct _noit_module_loader *loader,
                               char *module_name,
                               noit_conf_section_t section);
} noit_module_loader_t;

#define NOIT_MODULE_MAGIC         0x4017DA7A
#define NOIT_MODULE_ABI_VERSION   2

typedef struct _noit_module {
  noit_image_t hdr;
  int (*config)(struct _noit_module *, noit_hash_table *options);
  int (*init)(struct _noit_module *);
  int (*initiate_check)(struct _noit_module *, noit_check_t *check,
                        int once, noit_check_t *cause);
  void (*cleanup)(struct _noit_module *, noit_check_t *);
} noit_module_t;

#define NOIT_IMAGE_MAGIC(a)          ((a)->magic)
#define NOIT_IMAGE_VERSION(a)        ((a)->version)

API_EXPORT(void)
  noit_module_init();
API_EXPORT(int)
  noit_module_load(const char *file, const char *name);
API_EXPORT(noit_module_t *)
  noit_module_lookup(const char *name);

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


#endif
