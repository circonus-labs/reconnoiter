/*
 * Copyright (c) 2011-2012, Circonus, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *    * Neither the name Circonus, Inc. nor the names
 *      of its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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

#ifndef UTILS_NOIT_HOOKS
#define UTILS_NOIT_HOOKS

/*#* DOCBOOK
 * <section><title>Abitrary Hooks</title>
 * <para>Building a callout API makes sense for common, structured
 * features in software, but occassionlly there is a need to provide
 * somewhat arbitrary hook points after the software is designed
 * and the noit_hooks system is what satisfies this need.</para>
 * <para>The design goals here are somewhat specific in that we
 * would like to allow for a large number of hook points at low cost
 * when not instrumented.  As such, a hash lookup of registered hooks
 * would be considered to expensive.  Additionally, we want to provide
 * strong, compile-time type safety as it can be all to easy to hook
 * something with a function with a slightly incorrect protoype that
 * could result in disasterous corruption or crashes (or perhaps worse
 * daftly subtle bugs that are punishing to troubleshoot).</para>
 * <para>The hooks system is simple a set of two macros; one allowing
 * for the declaration of function prototypes for registering and invoking
 * a specific programmer-specifiec instrumentation point and the other
 * providing an implementation of the registration and invocation
 * routines.  Due to the nature of C, the macro calling conventions are
 * less than elegant, but ultimately require no complicated implementation
 * by the programmer.</para>
 *   <section><title>Hook Declaration</title>
 *   <para>
 *   Declaring hooks is done by calling the NOIT_HOOK_PROTO macro with
 *   the name of the hook (a term that composes a valid C function name),
 *   the arguments it expects, the type of closure (usually a void *),
 *   and some variations on those themes that provide CPP enough info
 *   to construct an implementation with no programmer "programming."
 *   </para>
 *   <para>The declaration of a hook "foo" will yield in two functions:
 *   foo_hook_invoke and foo_hook_register.</para>
 *   <example>
 *   <title>Declaring a foobarquux hook in a header.</title>
 *   <para>A foobarquux hook prototype that takes a struct timeval * argument.</para>
 *   <programlisting>
 *     NOIT_HOOK_PROTO(foobarquux, (struct timeval *now),
 *                     void *, closure, (void *closure, struct timeval *now));
 *   </programlisting>
 *   </example>
 *   <example>
 *   <title>Implementing a foobarquux hook in source.</title>
 *   <para>A foobarquux hok implementation that takes a struct timeval * argument.</para>
 *   <programlisting>
 *     NOIT_HOOK_IMPL(foobarquux, (struct timeval *now),
 *                    void *, closure, (void *closure, struct timeval *now),
 *                    (closure,now));
 *   </programlisting>
 *   </example>
 *   </section>
 *   <section><title>Hook Usage</title>
 *   <para>Once the hook is implemented, it can be used by the application
 *   and instrumented by code at runtime. In the below example, we'll
 *   invoke the foobarquux instrumentation and assuming no issues arise,
 *   we'll invoke the original foobarquux_work() function.</para>
 *   <example>
 *   <title>Instrumenting a function conditionally.</title>
 *   <para>Before we instrument, suppose we have:</para>
 *   <programlisting>
 *     foobarquux_work();
 *   </programlisting>
 *   <para>Now we wish to allow programmers to add instrumentation
 *   immediately before this code that can conditionally prevent its
 *   executation:</para>
 *   <programlisting><![CDATA[
 *     struct timeval now;
 *     gettimeofday(&now, NULL);
 *     if(NOIT_HOOK_CONTINUE ==
 *        foobarquux_hook_invoke(&now))
 *       foobarquux_work();
 *   ]]></programlisting>
 *   </example>
 *   <para>If the hook should not conditionally cause or prevent code
 *   to run, the invoke function's return value can be ignored.</para>
 *   <para>In order to register a function that allows the above execution
 *   on every other subsequent execution one would provide the following:
 *   </para>
 *   <example>
 *   <title>A sample instrumentation of foobarquux</title>
 *   <programlisting>
 *     static my_sample_hook(void *closure, struct timeval *now) {
 *       static int alt = 0;
 *       return (alt++ % 2) ? NOIT_HOOK_CONTINUE : NOIT_HOOK_DONE;
 *     }
 *
 *     ...
 *       foobarquux_hook_register("sample", my_sample_hook, NULL);
 *     ...
 *   </programlisting>
 *   </example>
 *   </section>
 * </section>
 */

typedef enum {
  NOIT_HOOK_CONTINUE,
  NOIT_HOOK_DONE,
  NOIT_HOOK_ABORT
} noit_hook_return_t;

#define NOIT_HOOK_PROTO(HOOKNAME, HOOKPROTO_NC, CDEF, CNAME, HOOKPROTO) \
API_EXPORT(noit_hook_return_t) HOOKNAME##_hook_invoke HOOKPROTO_NC; \
API_EXPORT(void) HOOKNAME##_hook_register(const char *name, \
    noit_hook_return_t (*func) HOOKPROTO, CDEF CNAME);


#define NOIT_HOOK_IMPL(HOOKNAME, HOOKPROTO_NC, CDEF, CNAME, HOOKPROTO, HOOKPARAMS) \
struct noit_hook_##HOOKNAME##_list { \
  const char *optional_name; \
  const char *proto; \
  noit_hook_return_t (*func) HOOKPROTO; \
  CDEF CNAME; \
  struct noit_hook_##HOOKNAME##_list *next; \
}; \
static volatile struct noit_hook_##HOOKNAME##_list *nh_##HOOKNAME##_list = NULL; \
 \
noit_hook_return_t \
HOOKNAME##_hook_invoke HOOKPROTO_NC { \
  noit_hook_return_t rv = NOIT_HOOK_CONTINUE; \
  struct noit_hook_##HOOKNAME##_list *h; \
  for(h = (struct noit_hook_##HOOKNAME##_list *)nh_##HOOKNAME##_list; \
      h; \
      h = h->next) { \
    if(h->func) { \
      noit_hook_return_t trv; \
      CDEF CNAME = h->CNAME; \
      trv = h->func HOOKPARAMS; \
      if(trv == NOIT_HOOK_ABORT) return NOIT_HOOK_ABORT; \
      if(trv == NOIT_HOOK_DONE) rv = NOIT_HOOK_DONE; \
    } \
  } \
  return rv; \
} \
 \
void HOOKNAME##_hook_register(const char *name, \
                              noit_hook_return_t (*func) HOOKPROTO, \
                              CDEF CNAME) { \
  struct noit_hook_##HOOKNAME##_list *nh; \
  volatile struct noit_hook_##HOOKNAME##_list *last, *expected; \
  nh = calloc(1, sizeof(*nh)); \
  nh->optional_name = name ? strdup(name) : NULL; \
  nh->proto = #HOOKPROTO; \
  nh->func = func; \
  nh->CNAME = CNAME; \
  do { \
    nh->next = (struct noit_hook_##HOOKNAME##_list *)nh_##HOOKNAME##_list; \
    expected = (struct noit_hook_##HOOKNAME##_list *)nh_##HOOKNAME##_list; \
    last = noit_atomic_casptr((volatile void **)&nh_##HOOKNAME##_list, \
                              nh, expected); \
  } while(last != expected); \
}

#endif
