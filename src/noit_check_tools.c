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

#include <mtev_defines.h>
#include <mtev_str.h>
#include <mtev_json.h>
#include <eventer/eventer.h>

#include "noit_mtev_bridge.h"
#include "noit_dtrace_probes.h"
#include "noit_check_tools.h"
#include "noit_check_tools_shared.h"

MTEV_HOOK_IMPL(check_preflight,
  (noit_module_t *self, noit_check_t *check, noit_check_t *cause),
  void *, closure,
  (void *closure, noit_module_t *self, noit_check_t *check, noit_check_t *cause),
  (closure,self,check,cause))
MTEV_HOOK_IMPL(check_postflight,
  (noit_module_t *self, noit_check_t *check, noit_check_t *cause),
  void *, closure,
  (void *closure, noit_module_t *self, noit_check_t *check, noit_check_t *cause),
  (closure,self,check,cause))

typedef struct {
  noit_module_t *self;
  noit_check_t *check;
  noit_check_t *cause;
  dispatch_func_t dispatch;
} recur_closure_t;

static void
noit_check_recur_name_details(char *buf, int buflen,
                              eventer_t e, void *closure) {
  char id_str[UUID_STR_LEN+1];
  recur_closure_t *rcl = e ? eventer_get_closure(e) : NULL;
  if(!e || !rcl) {
    snprintf(buf, buflen, "noit_check_recur_handler");
    return;
  }
  mtev_uuid_unparse_lower(rcl->check->checkid, id_str);
  snprintf(buf, buflen, "fire(%s)", id_str);
  return;
}
static int
noit_check_recur_handler(eventer_t e, int mask, void *closure,
                              struct timeval *now) {
  recur_closure_t *rcl = closure;
  int ms;

  if(e != rcl->check->fire_event) return 0;

  eventer_set_closure(e, NULL);
  noit_check_resolve(rcl->check);
  ms = noit_check_schedule_next(rcl->self, NULL, rcl->check, now,
                                rcl->dispatch, NULL);
  if(ms == 0)
    rcl->check->fire_event = NULL; /* This is us, we get free post-return */
  if(NOIT_CHECK_RESOLVED(rcl->check)) {
    if(MTEV_HOOK_CONTINUE ==
       check_preflight_hook_invoke(rcl->self, rcl->check, rcl->cause)) {
      if(NOIT_CHECK_DISPATCH_ENABLED()) {
        char id[UUID_STR_LEN+1];
        mtev_uuid_unparse_lower(rcl->check->checkid, id);
        NOIT_CHECK_DISPATCH(id, rcl->check->module, rcl->check->name,
                            rcl->check->target);
      }
      if(ms < rcl->check->timeout && !(rcl->check->flags & NP_TRANSIENT))
        mtevL(((ms == 0) ? noit_debug : noit_error), 
              "%s might not finish in %dms (timeout %dms)\n",
              rcl->check->name, ms, rcl->check->timeout);
      rcl->dispatch(rcl->self, rcl->check, rcl->cause);
    }
    check_postflight_hook_invoke(rcl->self, rcl->check, rcl->cause);
  }
  else
    mtevL(noit_debug, "skipping %s`%s`%s, unresolved\n",
          rcl->check->target, rcl->check->module, rcl->check->name);
  free(rcl);
  return 0;
}

int
noit_check_schedule_next(noit_module_t *self,
                         struct timeval *last_check, noit_check_t *check,
                         struct timeval *now, dispatch_func_t dispatch,
                         noit_check_t *cause) {
  eventer_t newe;
  struct timeval period, earliest, diff, tgt;
  int64_t diffms, periodms, offsetms;
  recur_closure_t *rcl;
  int initial = last_check ? 1 : 0;

  mtevAssert(cause == NULL);
  if(check->period == 0) return 0;

  /* if last_check is not passed, we use the initial_schedule_time
   * otherwise, we set the initial_schedule_time
   */
  if(!last_check) last_check = &check->initial_schedule_time;
  else memcpy(&check->initial_schedule_time, last_check, sizeof(*last_check));

  if(NOIT_CHECK_DISABLED(check) || NOIT_CHECK_KILLED(check)) {
    if(!(check->flags & NP_TRANSIENT)) check_slots_dec_tv(last_check);
    memset(&check->initial_schedule_time, 0, sizeof(struct timeval));
    return 0;
  }

  /* If we have an event, we know when we intended it to fire.  This means
   * we should schedule that point + period.
   */
  if(now)
    memcpy(&earliest, now, sizeof(earliest));
  else
    mtev_gettimeofday(&earliest, NULL);

  /* If the check is unconfigured and needs resolving, we'll set the
   * period down a bit lower so we can pick up the resolution quickly.
   * The one exception is if this is the initial run.
   */
  if(!initial &&
     !NOIT_CHECK_RESOLVED(check) && NOIT_CHECK_SHOULD_RESOLVE(check) &&
     check->period > 1000) {
    period.tv_sec = 1;
    period.tv_usec = 0;
  }
  else {
    period.tv_sec = check->period / 1000;
    period.tv_usec = (check->period % 1000) * 1000;
  }
  periodms = period.tv_sec * 1000 + period.tv_usec / 1000;

  /* calculate the differnet between the initial schedule time and "now" */
  if(compare_timeval(earliest, *last_check) >= 0) {
    sub_timeval(earliest, *last_check, &diff);
    diffms = (int64_t)diff.tv_sec * 1000 + diff.tv_usec / 1000;
  }
  else {
    mtevFatal(noit_error, "time is going backwards. abort.\n");
  }
  /* determine the offset from initial schedule time that would place
   * us at the next period-aligned point past "now" */
  offsetms = ((diffms / periodms) + 1) * periodms;
  diff.tv_sec = offsetms / 1000;
  diff.tv_usec = (offsetms % 1000) * 1000;

  tgt = *last_check;
  add_timeval(tgt, diff, &tgt);

  sub_timeval(tgt, earliest, &diff);
  diffms = (int64_t)diff.tv_sec * 1000 + (int)diff.tv_usec / 1000;
  mtevAssert(compare_timeval(tgt, earliest) > 0);

  rcl = calloc(1, sizeof(*rcl));
  rcl->self = self;
  rcl->check = check;
  rcl->cause = cause;
  rcl->dispatch = dispatch;
  newe = eventer_alloc_timer(noit_check_recur_handler, rcl, &tgt);

  if(self->thread_unsafe) {
    eventer_set_owner(newe, eventer_choose_owner(0));
  } else {
    eventer_set_owner(newe, CHOOSE_EVENTER_THREAD_FOR_CHECK(check));
  }
  check->fire_event = newe;
  eventer_add(newe);
  return diffms;
}

void
noit_check_run_full_asynch_opts(noit_check_t *check, eventer_func_t callback,
                                int mask) {
  struct timeval __now, p_int, tgt;
  eventer_t e;
  mtev_gettimeofday(&__now, NULL);
  memcpy(&tgt, &__now, sizeof(__now));
  p_int.tv_sec = check->timeout / 1000;
  p_int.tv_usec = (check->timeout % 1000) * 1000;
  add_timeval(tgt, p_int, &tgt);
  e = eventer_alloc_asynch_timeout(callback, check->closure, &tgt);
  eventer_set_mask(e, EVENTER_ASYNCH | mask);
  eventer_add(e);
}
void
noit_check_run_full_asynch(noit_check_t *check, eventer_func_t callback) {
  noit_check_run_full_asynch_opts(check, callback,
                                  EVENTER_DEFAULT_ASYNCH_ABORT);
}

void
noit_check_tools_init() {
  eventer_name_callback_ext("noit_check_recur_handler",
                            noit_check_recur_handler,
                            noit_check_recur_name_details, NULL);
}

static int
populate_stats_from_resmon_formatted_json(noit_check_t *check,
                                          struct json_object *o,
                                          const char *prefix) {
  int count = 0;
  char keybuff[MAX_METRIC_TAGGED_NAME];
#define MKKEY(fmt, arg) do { \
  if(prefix) snprintf(keybuff, sizeof(keybuff), "%s`" fmt, prefix, arg); \
  else snprintf(keybuff, sizeof(keybuff), fmt, arg); \
} while(0)
  if(o == NULL) {
    if(prefix) {
      noit_stats_set_metric(check, prefix, METRIC_STRING, NULL);
      count++;
    }
    return count;
  }
  switch(json_object_get_type(o)) {
    /* sub callers */
    case json_type_array:
    {
      int i, alen = json_object_array_length(o);
      for(i=0;i<alen;i++) {
        struct json_object *item = json_object_array_get_idx(o, i);
        MKKEY("%d", i);
        count += populate_stats_from_resmon_formatted_json(check, item, keybuff);
      }
    }
    break;
    case json_type_object:
    {
      struct json_object *has_type = NULL, *has_value = NULL;
      mtev_json_object_object_foreach(o,key,value) {
        if(!strcmp(key, "_type")) has_type = (struct json_object *)value;
        else if(!strcmp(key, "_value")) has_value = (struct json_object *)value;
        else {
          struct json_object *item = (struct json_object *)value;
          MKKEY("%s", key);
          count += populate_stats_from_resmon_formatted_json(check, item, keybuff);
        }
      }
      if(prefix && has_type &&
         json_object_is_type(has_type, json_type_string)) {
        const char *type_str = json_object_get_string(has_type);

#define COERCE_JSON_OBJECT(type, item) do { \
  const char *value_str = NULL; \
  if(json_object_is_type(item, json_type_string)) \
    value_str = json_object_get_string(item); \
  else if(!json_object_is_type(item, json_type_null)) \
    value_str = json_object_to_json_string(item); \
  switch(type) { \
    case METRIC_INT32: case METRIC_UINT32: case METRIC_INT64: \
    case METRIC_UINT64: case METRIC_DOUBLE: case METRIC_STRING: \
      noit_stats_set_metric_coerce(check, prefix, \
                                   (metric_type_t)type, value_str); \
      count++; \
    default: \
      break; \
  } \
} while(0)


        if (has_value == NULL) {
          if(*type_str != 'h' && *type_str != 'H') {
            noit_stats_set_metric_coerce(check, prefix, (metric_type_t)*type_str, NULL);
            count++;
          }
        }
        else if(json_object_is_type(has_value, json_type_array)) {
          int i, alen = json_object_array_length(has_value);
          for(i=0;i<alen;i++) {
            struct json_object *item = json_object_array_get_idx(has_value, i);
            if (item) {
              if(*type_str == 'h' || *type_str == 'H') {
                const char *value_str = NULL;
                if(json_object_is_type(item, json_type_string))
                  value_str = json_object_get_string(item);
                else if(!json_object_is_type(item, json_type_null))
                  value_str = json_object_to_json_string(item);
                if(value_str) noit_stats_set_metric_histogram(check, prefix, (*type_str == 'H'), METRIC_GUESS, (void *)value_str, 1);
              }
              else {
                COERCE_JSON_OBJECT(*type_str, item);
              }
            }
            else if(*type_str != 'h') {
              noit_stats_set_metric_coerce(check, prefix, (metric_type_t)*type_str, NULL);
              count++;
            }
          }
        }
        else {
          if(*type_str == 'h' || *type_str == 'H') {
            const char *value_str = NULL;
            if(json_object_is_type(has_value, json_type_string))
              value_str = json_object_get_string(has_value);
            else if(!json_object_is_type(has_value, json_type_null))
              value_str = json_object_to_json_string(has_value);
            if(value_str) noit_stats_set_metric_histogram(check, prefix, (*type_str == 'H'), METRIC_GUESS, (void *)value_str, 1);
          }
          else {
            COERCE_JSON_OBJECT(*type_str, has_value);
          }
        }
      }
      break;
    }

    /* directs */
    case json_type_string:
      if(prefix) {
        noit_stats_set_metric(check, prefix, METRIC_GUESS,
                              (char *)json_object_get_string(o));
        count++;
      }
      break;
    case json_type_boolean:
      if(prefix) {
        int val = json_object_get_boolean(o) ? 1 : 0;
        noit_stats_set_metric(check, prefix, METRIC_INT32, &val);
        count++;
      }
      break;
    case json_type_null:
      if(prefix) {
        noit_stats_set_metric(check, prefix, METRIC_STRING, NULL);
        count++;
      }
      break;
    case json_type_double:
      if(prefix) {
        double val = json_object_get_double(o);
        noit_stats_set_metric(check, prefix, METRIC_DOUBLE, &val);
        count++;
      }
      break;
    case json_type_int:
      if(prefix) {
        int64_t i64;
        uint64_t u64;
        switch(json_object_get_int_overflow(o)) {
          case json_overflow_int:
            i64 = json_object_get_int(o);
            noit_stats_set_metric(check, prefix, METRIC_INT64, &i64);
            count++;
            break;
          case json_overflow_int64:
            i64 = json_object_get_int64(o);
            noit_stats_set_metric(check, prefix, METRIC_INT64, &i64);
            count++;
            break;
          case json_overflow_uint64:
            u64 = json_object_get_uint64(o);
            noit_stats_set_metric(check, prefix, METRIC_UINT64, &u64);
            count++;
            break;
        }
      }
  }
  return count;
}
int
noit_check_stats_from_json_str(noit_check_t *check,
                               const char *json_str, int len) {
  int rv = -1;
  struct json_tokener *tok = NULL;
  struct json_object *root = NULL;
  tok = json_tokener_new();
  root = json_tokener_parse_ex(tok, json_str, len);
  if(root) rv = populate_stats_from_resmon_formatted_json(check, root, NULL);
  if(tok) json_tokener_free(tok);
  if(root) json_object_put(root);
  return rv;
}

void
noit_check_make_attrs(noit_check_t *check, mtev_hash_table *attrs) {
#define CA_STORE(a,b) mtev_hash_store(attrs, a, strlen(a), b)
  mtev_hash_init(attrs);
  CA_STORE("target", check->target);
  CA_STORE("target_ip", check->target_ip);
  CA_STORE("name", check->name);
  CA_STORE("module", check->module);
}

/* eventer pool selection works by exploring the pool name space
 * looking for the first available pool by name as follows (using
 * graphite_pickle as an example module):
 *
 *    noit_check_ (if check based)
 *    noit_module_graphite_pickle
 *    noit_module_graphite
 *    noit_module
 *    noit
 *    -> default (NULL)
 */
eventer_pool_t *noit_check_choose_pool_by_module(const char *mod) {
  char poolname[256];
  eventer_pool_t *dedicated_pool = NULL;
  if(!mod) return NULL;
  snprintf(poolname, sizeof(poolname), "noit_module_%s", mod);
  do {
    dedicated_pool = eventer_pool(poolname);
    char *spot_ = strrchr(poolname, '_');
    if(!spot_) break;
    *spot_ = '\0';
  } while(dedicated_pool == NULL);
  return dedicated_pool;
}

eventer_pool_t *noit_check_choose_pool(noit_check_t *check) {
  char poolname[256];
  eventer_pool_t *dedicated_pool = NULL;
  strlcpy(poolname, "noit_check_", sizeof(poolname));
  mtev_uuid_unparse_lower(check->checkid, poolname + strlen("noit_check_"));
  dedicated_pool = eventer_pool(poolname);
  if(!dedicated_pool) dedicated_pool = noit_check_choose_pool_by_module(check->module);
  return dedicated_pool;
}

pthread_t noit_check_choose_eventer_thread(noit_check_t *check) {
  eventer_pool_t *dedicated_pool = noit_check_choose_pool(check);
  int rnd = noit_check_uuid_to_integer(check->checkid) / sizeof(*(check)) * 2654435761;
  if(dedicated_pool) return eventer_choose_owner_pool(dedicated_pool, rnd);
  return eventer_choose_owner(rnd);
}
