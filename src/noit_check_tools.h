/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2015-2017, Circonus, Inc. All rights reserved.
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

#include <mtev_defines.h>
#include <eventer/eventer.h>
#include <mtev_hash.h>
#include <mtev_hooks.h>
#include <mtev_rest.h>
#include "noit_module.h"
#include "noit_check.h"
#include "noit_clustering.h"
#include "noit_check_tools_shared.h"

typedef int (*dispatch_func_t)(noit_module_t *, noit_check_t *,
                               noit_check_t *);

API_EXPORT(void)
  noit_check_tools_init();

API_EXPORT(int)
  noit_rest_show_config(mtev_http_rest_closure_t *restc,
                        int npats, char **pats);

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
  noit_check_stats_from_json_str(noit_check_t *check,
                                 const char *json_str, int len);

API_EXPORT(void)
  noit_check_make_attrs(noit_check_t *check, mtev_hash_table *attrs);

static inline unsigned int
noit_check_uuid_to_integer(uuid_t uuid)
{
  const int *x = (const int *)uuid;
  return x[0] ^ x[1] ^ x[2] ^ x[3];
}

API_EXPORT(eventer_pool_t *)
  noit_check_choose_pool_by_module(const char *mod);
API_EXPORT(eventer_pool_t *)
  noit_check_choose_pool(noit_check_t *check);
API_EXPORT(pthread_t)
  noit_check_choose_eventer_thread(noit_check_t *check);

#define CHOOSE_EVENTER_THREAD_FOR_CHECK(check) noit_check_choose_eventer_thread(check)

/*#* DOCBOOK
 * <section><title>Check Hooks</title>
 *   <section><title>check_stats_set_metric</title>
 *   <programlisting>
 *     noit_hook_return_t (*f)(void *closure, noit_check_t *check, metric_t *m);
 *   </programlisting>
 *   <para>the check_stats_set_metric hook is invoked each time a check is
 *   run and an individual metric is identified and inserted into
 *   the running status.
 *   </para>
 *   </section>
 *   <section><title>check_preflight</title>
 *   <programlisting>
 *     noit_hook_return_t (*f)(void *closure, noit_module_t *self,
 *                             noit_check_t *check, noit_check_t *cause);
 *   </programlisting>
 *   <para>the check_preflight hook is invoked immediately prior to every
 *   check being performed.  The actual invocation of the check can be
 *   avoided by returning MTEV_HOOK_DONE instead of MTEV_HOOK_CONTINUE.
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
 *   <para>Returning MTEV_HOOK_CONTINUE and MTEV_HOOK_DONE have the same
 *   effect for this instrumentation point.</para>
 *   </section>
 *   <section><title>check_log_stats</title>
 *   <programlisting>
 *     noit_hook_return_t (*f)(void *closure, noit_check_t *check);
 *   </programlisting>
 *   <para>The check_log_stats is called when a check logs a metrics
 *   bundle.</para>
 *   <para>If MTEV_HOOK_DONE is returned, normal logging is averted.</para>
 *   </section>
 *   <section><title>check_passive_log_stats</title>
 *   <programlisting>
 *     noit_hook_return_t (*f)(void *closure, noit_check_t *check);
 *   </programlisting>
 *   <para>The check_passive_log_stats is called when a check logs a
 *   metric immediately.</para>
 *   <para>If MTEV_HOOK_DONE is returned, normal logging is averted.</para>
 *   </section>
 * </section>
 */
MTEV_HOOK_PROTO(check_preflight,
                (noit_module_t *self, noit_check_t *check, noit_check_t *cause),
                void *, closure,
                (void *closure, noit_module_t *self, noit_check_t *check, noit_check_t *cause))
MTEV_HOOK_PROTO(check_postflight,
                (noit_module_t *self, noit_check_t *check, noit_check_t *cause),
                void *, closure,
                (void *closure, noit_module_t *self, noit_check_t *check, noit_check_t *cause))

#define BAIL_ON_RUNNING_CHECK(check) do { \
  if(!noit_should_run_check(check, NULL)) { \
    static mtev_log_stream_t noit_cluster_debug = NULL; \
    if(!noit_cluster_debug) noit_cluster_debug = mtev_log_stream_find("debug/cluster"); \
    mtevL(noit_cluster_debug, "Check %s is running on another node\n", check->name); \
    return -1; \
  } \
  if(check->flags & NP_RUNNING) { \
    mtevL(mtev_error, "Check %s is still running!\n", check->name); \
    return -1; \
  } \
} while(0)

#define INITIATE_CHECK_EX(func, oncefunc, self, check, cause) do { \
  if(once) { \
    if(MTEV_HOOK_CONTINUE == \
       check_preflight_hook_invoke(self, check, cause)) \
      oncefunc(self, check, cause); \
    check_postflight_hook_invoke(self, check, cause); \
  } \
  else if(!check->fire_event) { \
    struct timeval epoch = { 0L, 0L }; \
    noit_check_fake_last_check(check, &epoch, NULL); \
    noit_check_schedule_next(self, &epoch, check, NULL, func, cause); \
  } \
} while(0)

#define INITIATE_CHECK(func, self, check, cause) do { \
  INITIATE_CHECK_EX(func, func, self, check, cause); \
} while(0)

#endif

