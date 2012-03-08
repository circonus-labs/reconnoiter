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
#include "utils/noit_hooks.h"
#include "noit_check_tools_shared.h"

typedef int (*dispatch_func_t)(noit_module_t *, noit_check_t *,
                               noit_check_t *);

API_EXPORT(void)
  noit_check_tools_init();

API_EXPORT(int)
  noit_check_schedule_next(noit_module_t *self,
                           struct timeval *last_check, noit_check_t *check,
                           struct timeval *now, dispatch_func_t recur,
                           noit_check_t *cause);

API_EXPORT(void)
  noit_check_run_full_asynch_opts(noit_check_t *check, eventer_func_t callback,
                                  int mask);
API_EXPORT(void)
  noit_check_run_full_asynch(noit_check_t *check, eventer_func_t callback);

API_EXPORT(int)
  noit_check_stats_from_json_str(noit_check_t *check, stats_t *s,
                                 const char *json_str, int len);

/*#* DOCBOOK
 * <section><title>Check Hooks</title>
 *   <section><title>check_preflight</title>
 *   <programlisting>
 *     noit_hook_return_t (*f)(void *closure, noit_module_t *self,
 *                             noit_check_t *check, noit_check_t *cause);
 *   </programlisting>
 *   <para>the check_preflight hook is invoked immediately prior to every
 *   check being performed.  The actual invocation of the check can be
 *   avoided by returning NOIT_HOOK_DONE instead of NOIT_HOOK_CONTINUE.
 *   </para>
 *   <para>The arguments to this function are the module of the check
 *   the check itself and the causal check (NULL if this check was not
 *   induced by the completion of another check), respectively.</para>
 *   <para>This instrumentation point can be used to audit intended check
 *   activity or prevent a check from running (such as an ACL).</para>
 *   </section>
 *   <section><title>check_postflight</title>
 *   <programlisting>
 *     noit_hook_return_t (*f)(void *closure, noit_module_t *self,
 *                             noit_check_t *check, noit_check_t *cause);
 *   </programlisting>
 *   <para>The check_postflight hook is invoked immediately subsequent to
 *   a check being commenced.  Note that due to the asynchronous nature
 *   of Reconnoiter, it is highly unlikely that the check will have 
 *   completed by this time.</para>
 *   <para>The arguments to this function are the module of the check,
 *   the check itself and the causal check (NULL if this check was not
 *   induced by the completion of another check), respectively.</para>
 *   <para>Returning NOIT_HOOK_CONTINUE and NOIT_HOOK_DONE have the same
 *   effect for this instrumentation point.</para>
 *   </section>
 * </section>
 */
NOIT_HOOK_PROTO(check_preflight,
                (noit_module_t *self, noit_check_t *check, noit_check_t *cause),
                void *, closure,
                (void *closure, noit_module_t *self, noit_check_t *check, noit_check_t *cause))
NOIT_HOOK_PROTO(check_postflight,
                (noit_module_t *self, noit_check_t *check, noit_check_t *cause),
                void *, closure,
                (void *closure, noit_module_t *self, noit_check_t *check, noit_check_t *cause))

#define INITIATE_CHECK(func, self, check, cause) do { \
  if(once) { \
    if(NOIT_HOOK_CONTINUE == \
       check_preflight_hook_invoke(self, check, cause)) \
      func(self, check, cause); \
    check_postflight_hook_invoke(self, check, cause); \
  } \
  else if(!check->fire_event) { \
    struct timeval epoch = { 0L, 0L }; \
    noit_check_fake_last_check(check, &epoch, NULL); \
    noit_check_schedule_next(self, &epoch, check, NULL, func, cause); \
  } \
} while(0)

#endif

