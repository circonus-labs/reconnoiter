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

#ifndef _NOIT_MODULE_H
#define _NOIT_MODULE_H

#include "noit_defines.h"
#include "noit_dso.h"
#include "noit_check.h"

#define NOIT_MODULE_MAGIC         0x4017DA7A
#define NOIT_MODULE_ABI_VERSION   5

typedef struct _noit_module {
  noit_image_t hdr;
  int (*config)(struct _noit_module *, noit_hash_table *options);
  int (*init)(struct _noit_module *);
  int (*initiate_check)(struct _noit_module *, noit_check_t *check,
                        int once, noit_check_t *cause);
  void (*cleanup)(struct _noit_module *, noit_check_t *);
  unsigned thread_unsafe :1;
} noit_module_t;

/*         config: is called to pass the config into the module.
 *           init: is called once to initialize the module
 * initiate_check: is called so start the module against checks
 *        cleanup: is called if a particular check is stopped
 */

API_EXPORT(void)
  noit_module_init();
API_EXPORT(int)
  noit_module_load_failures();
API_EXPORT(int)
  noit_module_list_modules(const char ***f);
API_EXPORT(noit_module_t *)
  noit_module_lookup(const char *name);
API_EXPORT(noit_module_t *)
  noit_blank_module();
API_EXPORT(int)
  noit_register_module(noit_module_t *mod);

API_EXPORT(void *)
  noit_module_get_userdata(noit_module_t *mod);
API_EXPORT(void)
  noit_module_set_userdata(noit_module_t *mod, void *);

#endif
