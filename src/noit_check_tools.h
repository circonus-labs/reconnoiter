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

#ifndef _NOIT_CHECK_TOOLS_H
#define _NOIT_CHECK_TOOLS_H

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "noit_module.h"
#include "noit_check.h"
#include "utils/noit_hash.h"

typedef int (*dispatch_func_t)(noit_module_t *, noit_check_t *);

typedef int (*intperpolate_oper_fn)(char *, int len, const char *replacement);

API_EXPORT(void)
  noit_check_tools_init();

API_EXPORT(int)
  noit_check_interpolate_register_oper_fn(const char *name,
                                          intperpolate_oper_fn f);

API_EXPORT(int)
  noit_check_interpolate(char *buff, int len, const char *fmt,
                         noit_hash_table *attrs,
                         noit_hash_table *config);

API_EXPORT(int)
  noit_check_schedule_next(noit_module_t *self,
                           struct timeval *last_check, noit_check_t *check,
                           struct timeval *now, dispatch_func_t recur);

API_EXPORT(void)
  noit_check_run_full_asynch(noit_check_t *check, eventer_func_t callback);

#define INITIATE_CHECK(func, self, check) do { \
  if(once) { \
    func(self, check); \
  } \
  else if(!check->fire_event) { \
    struct timeval epoch = { 0L, 0L }; \
    noit_check_fake_last_check(check, &epoch, NULL); \
    noit_check_schedule_next(self, &epoch, check, NULL, func); \
  } \
} while(0)

API_EXPORT(void)
  noit_check_make_attrs(noit_check_t *check, noit_hash_table *attrs);
API_EXPORT(void)
  noit_check_release_attrs(noit_hash_table *attrs);
API_EXPORT(int)
  noit_check_xpath(char *xpath, int len,
                   const char *base, const char *arg);


#endif

