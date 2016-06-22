/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2013-2015, Circonus, Inc. All rights reserved.
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

#ifndef _NOIT_CHECK_H
#define _NOIT_CHECK_H

#include <mtev_defines.h>

#include <uuid/uuid.h>
#include <netinet/in.h>

#include <eventer/eventer.h>
#include <mtev_hash.h>
#include <mtev_skiplist.h>
#include <mtev_hooks.h>
#include <mtev_conf.h>
#include <mtev_console.h>

#include "noit_metric.h"

/*
 * Checks:
 *  attrs:
 *   UUID
 *   host (target)
 *   check (module)
 *   name (identifying the check to the user if
 *         multiple checks of the same module are specified)
 *   config (params for the module)
 *   period (ms)
 *   timeout (ms)
 *  transient:
 *   eventer_t (fire)
 *   stats_t [inprogress, current, previous]
 *   closure
 */

/* The check is currently in-flight (running) */
#define NP_RUNNING               0x00000001
/* The check has been killed by the scheduling system */
#define NP_KILLED                0x00000002
/* The check is disable and should not be run */
#define NP_DISABLED              0x00000004
/* The check is not sufficiently configured to operate */
#define NP_UNCONFIG              0x00000008
/* The check is a transient copy of another check */
#define NP_TRANSIENT             0x00000010
/* The check requires name service resolution */
#define NP_RESOLVE               0x00000020
/* Name service resolution has been compelted for the check */
#define NP_RESOLVED              0x00000040
/* This check should have 'S' lines suppressed from logging */
#define NP_SUPPRESS_STATUS       0x00001000
/* This check should have 'M' lines suppressed from logging */
#define NP_SUPPRESS_METRICS      0x00002000
/* The check should lookup IPv6 records before IPv4 */
#define NP_PREFER_IPV6           0x00004000
/* Do no fallback to IPv6 from IPv4 and vice versa */
#define NP_SINGLE_RESOLVE        0x00008000
/* Indicates that the check is receiving data passively
 * and it does not do anything during invocation to collect metrics
 */
#define NP_PASSIVE_COLLECTION    0x00010000

#define PREFER_IPV4 "prefer-ipv4"
#define PREFER_IPV6 "prefer-ipv6"
#define FORCE_IPV4  "force-ipv4"
#define FORCE_IPV6  "force-ipv6"

#define NP_UNKNOWN '0'             /* stats_t.{available,state} */
#define NP_AVAILABLE 'A'           /* stats_t.available */
#define NP_UNAVAILABLE 'U'         /* stats_t.available */
#define NP_BAD 'B'                 /* stats_t.state */
#define NP_GOOD 'G'                /* stats_t.state */

typedef struct stats_t stats_t;

typedef struct dep_list {
  struct noit_check *check;
  struct dep_list *next;
} dep_list_t;

typedef struct noit_check {
  uuid_t checkid;
  int8_t target_family;
  union {
    struct in_addr addr;
    struct in6_addr addr6;
  } target_addr;
  char *target;
  char *module;
  char *name;
  char *filterset;
  mtev_hash_table *config;
  char *oncheck;               /* target`name of the check that fires us */
  u_int32_t period;            /* period of checks in milliseconds */
  u_int32_t timeout;           /* timeout of check in milliseconds */
  u_int32_t flags;             /* NP_KILLED, NP_RUNNING, NP_TRANSIENT */

  dep_list_t *causal_checks;
  eventer_t fire_event;
  struct timeval last_fire_time;
  u_int32_t generation;        /* This can roll, we don't care */
  void *closure;

  mtev_skiplist *feeds;
  char target_ip[INET6_ADDRSTRLEN];
  void **module_metadata;
  mtev_hash_table **module_configs;
  struct timeval initial_schedule_time;
  int64_t config_seq;          /* If non-zero, must increase */

  void *statistics;
} noit_check_t;

#define NOIT_CHECK_LIVE(a) ((a)->fire_event != NULL)
#define NOIT_CHECK_DISABLED(a) ((a)->flags & NP_DISABLED)
#define NOIT_CHECK_CONFIGURED(a) (((a)->flags & NP_UNCONFIG) == 0)
#define NOIT_CHECK_RUNNING(a) ((a)->flags & NP_RUNNING)
#define NOIT_CHECK_KILLED(a) ((a)->flags & NP_KILLED)
#define NOIT_CHECK_SHOULD_RESOLVE(a) ((a)->flags & NP_RESOLVE)
/* It is resolved if it is resolved or never needed to be resolved */
#define NOIT_CHECK_RESOLVED(a) (((a)->flags & NP_RESOLVED) || (((a)->flags & NP_RESOLVE) == 0))
#define NOIT_CHECK_PREFER_V6(a) ((a)->flags & NP_PREFER_IPV6)
#define NOIT_CHECK_SINGLE_RESOLVE(a) ((a)->flags & NP_SINGLE_RESOLVE)

API_EXPORT(void) noit_poller_init();
API_EXPORT(u_int64_t) noit_check_completion_count();
API_EXPORT(u_int64_t) noit_check_metric_count();
API_EXPORT(void) noit_check_metric_count_add(int);
API_EXPORT(int) noit_poller_check_count();
API_EXPORT(int) noit_poller_transient_check_count();
API_EXPORT(void) noit_poller_reload(const char *xpath); /* NULL for all */
API_EXPORT(void) noit_poller_process_checks(const char *xpath);
API_EXPORT(void) noit_poller_make_causal_map();
API_EXPORT(void) noit_check_dns_ignore_tld(const char* extension, const char* ignore);

API_EXPORT(void)
  noit_check_fake_last_check(noit_check_t *check,
                             struct timeval *lc, struct timeval *_now);

API_EXPORT(int)
  noit_poller_schedule(const char *target,
                       const char *module,
                       const char *name,
                       const char *filterset,
                       mtev_hash_table *config,
                       mtev_hash_table **mconfig,
                       u_int32_t period,
                       u_int32_t timeout,
                       const char *oncheck,
                       int64_t seq,
                       int flags,
                       uuid_t in,
                       uuid_t out);

API_EXPORT(int)
  noit_check_resolve(noit_check_t *check);

API_EXPORT(int)
  noit_check_update(noit_check_t *new_check,
                    const char *target,
                    const char *name,
                    const char *filterset,
                    mtev_hash_table *config,
                    mtev_hash_table **mconfig,
                    u_int32_t period,
                    u_int32_t timeout,
                    const char *oncheck,
                    int64_t seq,
                    int flags);

API_EXPORT(mtev_boolean)
  noit_check_is_valid_target(const char *str);

API_EXPORT(int)
  noit_check_activate(noit_check_t *check);

API_EXPORT(int)
  noit_poller_deschedule(uuid_t in, mtev_boolean log);

API_EXPORT(noit_check_t *)
  noit_poller_lookup(uuid_t in);

API_EXPORT(noit_check_t *)
  noit_poller_lookup_by_name(char *target, char *name);

API_EXPORT(int)
  noit_poller_lookup_by_ip_module(const char *ip, const char *mod,
                                  noit_check_t **checks, int nchecks);

API_EXPORT(int)
   noit_poller_target_ip_do(const char *target,
                            int (*f)(noit_check_t *, void *),
                            void *closure);

API_EXPORT(int)
   noit_poller_target_do(const char *target,
                         int (*f)(noit_check_t *, void *),
                         void *closure);

API_EXPORT(int)
   noit_poller_do(int (*f)(noit_check_t *, void *),
                  void *closure);

API_EXPORT(int)
  noit_check_xpath(char *xpath, int len,
                   const char *base, const char *arg);

struct _noit_module;

stats_t *
noit_check_get_stats_inprogress(noit_check_t *c);
stats_t *
noit_check_get_stats_current(noit_check_t *c);
stats_t *
noit_check_get_stats_previous(noit_check_t *c);

/* This if for modules (passive) than cannot be watched... */
API_EXPORT(void)
  noit_check_passive_set_stats(noit_check_t *check);

/* This is for normal (active) modules... */
API_EXPORT(void)
  noit_check_set_stats(noit_check_t *check);

API_EXPORT(void)
  noit_stats_set_whence(noit_check_t *check, struct timeval *t);
API_EXPORT(void)
  noit_stats_set_duration(noit_check_t *check, u_int32_t t);
API_EXPORT(void)
  noit_stats_set_available(noit_check_t *check, int8_t t);
API_EXPORT(void)
  noit_stats_set_state(noit_check_t *check, int8_t t);
API_EXPORT(void)
  noit_stats_set_status(noit_check_t *check, const char *t);

API_EXPORT(metric_t *)
  noit_stats_get_metric(noit_check_t *check, stats_t *, const char *);

API_EXPORT(void)
  noit_stats_set_metric(noit_check_t *check,
                        const char *, metric_type_t, const void *);

API_EXPORT(void)
  noit_stats_set_metric_coerce(noit_check_t *check,
                               const char *, metric_type_t,
                               const char *);

API_EXPORT(void)
  noit_stats_log_immediate_metric(noit_check_t *check,
                                  const char *name, metric_type_t type,
                                  const void *value);

API_EXPORT(const char *)
  noit_check_available_string(int16_t available);
API_EXPORT(const char *)
  noit_check_state_string(int16_t state);
API_EXPORT(int)
  noit_calc_rtype_flag(char *resolve_rtype);
API_EXPORT(int)
  noit_stats_snprint_metric_value(char *b, int l, metric_t *m);
API_EXPORT(int)
  noit_stats_snprint_metric(char *b, int l, metric_t *m);

API_EXPORT(noit_check_t *)
  noit_check_clone(uuid_t in);
API_EXPORT(void)
  noit_poller_free_check(noit_check_t *checker);
API_EXPORT(noit_check_t *)
  noit_check_watch(uuid_t in, int period);
API_EXPORT(noit_check_t *)
  noit_check_get_watch(uuid_t in, int period);
API_EXPORT(void)
  noit_check_transient_add_feed(noit_check_t *check, const char *feed);
API_EXPORT(void)
  noit_check_transient_remove_feed(noit_check_t *check, const char *feed);

/* Register your module */
API_EXPORT(int)
  noit_check_register_module(const char *);
/* Find a registered module's id */
API_EXPORT(int)
  noit_check_registered_module_by_name(const char *name);
/* Get the count of registered modules */
API_EXPORT(int)
  noit_check_registered_module_cnt();
/* Get a registered module name by id */
API_EXPORT(const char *)
  noit_check_registered_module(int);

API_EXPORT(int)
  noit_check_validate_target(const char *);
API_EXPORT(int)
  noit_check_validate_name(const char *);

API_EXPORT(void)
  noit_check_set_module_metadata(noit_check_t *, int, void *, void (*freefunc)(void *));
API_EXPORT(void)
  noit_check_set_module_config(noit_check_t *, int, mtev_hash_table *);
API_EXPORT(void *)
  noit_check_get_module_metadata(noit_check_t *, int);
API_EXPORT(mtev_hash_table *)
  noit_check_get_module_config(noit_check_t *, int);

/* These are from noit_check_log.c */
API_EXPORT(void) noit_check_log_check(noit_check_t *check);
API_EXPORT(void) noit_filterset_log_auto_add(char *filter, noit_check_t *check,
                                             metric_t *m, mtev_boolean allow);
API_EXPORT(void) noit_check_log_status(noit_check_t *check);
API_EXPORT(void) noit_check_log_delete(noit_check_t *check);
API_EXPORT(void) noit_check_log_bundle(noit_check_t *check);
API_EXPORT(void) noit_check_log_metrics(noit_check_t *check);
API_EXPORT(void) noit_check_log_metric(noit_check_t *check,
                                       struct timeval *whence, metric_t *m);
API_EXPORT(void) noit_check_extended_id_split(const char *in, int len,
                                              char *target, int target_len,
                                              char *module, int module_len,
                                              char *name, int name_len,
                                              char *uuid, int uuid_len);


API_EXPORT(char *)
  noit_console_check_opts(mtev_console_closure_t ncct,
                          mtev_console_state_stack_t *stack,
                          mtev_console_state_t *dstate,
                          int argc, char **argv, int idx);
API_EXPORT(char *)
  noit_console_conf_check_opts(mtev_console_closure_t ncct,
                               mtev_console_state_stack_t *stack,
                               mtev_console_state_t *dstate,
                               int argc, char **argv, int idx);

API_EXPORT(void) check_slots_inc_tv(struct timeval *tv);
API_EXPORT(void) check_slots_dec_tv(struct timeval *tv);

API_EXPORT(struct timeval *)
  noit_check_stats_whence(stats_t *s, struct timeval *n);
API_EXPORT(int8_t)
  noit_check_stats_available(stats_t *s, int8_t *n);
API_EXPORT(int8_t)
  noit_check_stats_state(stats_t *s, int8_t *n);
API_EXPORT(u_int32_t)
  noit_check_stats_duration(stats_t *s, u_int32_t *n);
API_EXPORT(const char *)
  noit_check_stats_status(stats_t *s, const char *n);
API_EXPORT(mtev_hash_table *)
  noit_check_stats_metrics(stats_t *s);

MTEV_HOOK_PROTO(check_config_fixup,
                (noit_check_t *check),
                void *, closure,
                (void *closure, noit_check_t *check))

MTEV_HOOK_PROTO(check_stats_set_metric,
                (noit_check_t *check, stats_t *stats, metric_t *m),
                void *, closure,
                (void *closure, noit_check_t *check, stats_t *stats, metric_t *m))

MTEV_HOOK_PROTO(check_stats_set_metric_coerce,
                (noit_check_t *check, stats_t *stats, const char *name,
                 metric_type_t type, const char *v, mtev_boolean success),
                void *, closure,
                (void *closure, noit_check_t *check, stats_t *stats, const char *name,
                 metric_type_t type, const char *v, mtev_boolean success))

MTEV_HOOK_PROTO(check_passive_log_stats,
                (noit_check_t *check),
                void *, closure,
                (void *closure, noit_check_t *check))

MTEV_HOOK_PROTO(check_set_stats,
                (noit_check_t *check),
                void *, closure,
                (void *closure, noit_check_t *check))

MTEV_HOOK_PROTO(check_log_stats,
                (noit_check_t *check),
                void *, closure,
                (void *closure, noit_check_t *check))

#endif
