/*
 * Copyright (c) 2011, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2011-2017, Circonus, Inc. All rights reserved.
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
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include <inttypes.h>
#include <mtev_defines.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>
#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>

#include <mtev_rand.h>
#include <mtev_rest.h>
#include <mtev_hash.h>
#include <mtev_json.h>
#include <mtev_memory.h>
#include <mtev_uuid.h>

#include "noit_metric.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_filters.h"
#include "noit_mtev_bridge.h"

extern void
yajl_string_encode(const yajl_print_t print, void * ctx,
                   const unsigned char * str, size_t len,
                   int escape_solidus);

#define DEFAULT_HTTPTRAP_DELIMITER '`'
#define MAX_DEPTH 32

#define HT_EX_TYPE 0x1
#define HT_EX_VALUE 0x2
#define HT_EX_TS 0x4
#define HT_EX_TAGS 0x8
#define HT_EX_FLAGS 0x10

static mtev_log_stream_t nlerr = NULL;
static mtev_log_stream_t nldeb = NULL;
static mtev_log_stream_t nlyajl = NULL;
static noit_module_t *global_self = NULL; /* used for cross traps */
static uint64_t httptrap_count = 0;

#define _YD(fmt...) mtevL(nlyajl, fmt)

static mtev_boolean httptrap_surrogate;
static const char *TRUNCATE_ERROR = "at least one metric exceeded max name length";

typedef struct _mod_config {
  mtev_hash_table *options;
  mtev_boolean asynch_metrics;
  mtev_boolean fanout;
} httptrap_mod_config_t;

typedef struct httptrap_closure_s {
  noit_module_t *self;
} httptrap_closure_t;

typedef enum {
  HTTPTRAP_VOP_REPLACE,
  HTTPTRAP_VOP_AVERAGE,
  HTTPTRAP_VOP_ACCUMULATE
} httptrap_vop_t;

static void metric_local_free(void *vm) {
  if(vm) {
    metric_t *m = vm;
    free(m->metric_name);
    free(m->metric_value.vp);
  }
  free(vm);
}

struct value_list {
  char *v;
  struct value_list *next;
};
struct rest_json_payload {
  noit_check_t *check;
  uuid_t check_id;
  yajl_handle parser;
  struct timeval start_time;
  int len;
  int complete;
  char delimiter;
  char *error;
  char *supp_err;
  int depth;
  char *keys[MAX_DEPTH];
  int array_depth[MAX_DEPTH];
  unsigned char last_special_key;
  unsigned char saw_complex_type;
  mtev_boolean got_timestamp;
  struct timeval last_timestamp;
  httptrap_vop_t vop_flag;

  metric_type_t last_type;
  struct value_list *last_value;
  int cnt;
  uint32_t filtered_cnt;
  mtev_boolean immediate;
  uint64_t current_counter;
  mtev_hash_table *immediate_metrics;
};

static void
track_filtered(struct rest_json_payload *json, const char *name) {
  metric_t m = { .metric_name = (char *)name };
  if(!noit_apply_filterset(json->check->filterset, json->check, &m)) {
    json->filtered_cnt++;
  }
}

static void
rest_json_flush_immediate(struct rest_json_payload *rxc) {
  noit_check_log_bundle_metrics(rxc->check, &rxc->start_time, rxc->immediate_metrics);
  mtev_hash_delete_all(rxc->immediate_metrics, NULL, metric_local_free);
}

static void 
metric_local_track_or_log(void *vrxc, const char *name, 
                          metric_type_t t, const void *vp, struct timeval *w) {
  struct rest_json_payload *rxc = vrxc;
  if(t == METRIC_GUESS) return;
  void *vm;
  if(rxc->immediate_metrics == NULL) {
    rxc->immediate_metrics = calloc(1, sizeof(*rxc->immediate_metrics));
    mtev_hash_init(rxc->immediate_metrics);
  }
  if(mtev_hash_retrieve(rxc->immediate_metrics, name, strlen(name), &vm)) {
    /* collision, just log it out */
    rest_json_flush_immediate(rxc);
  }
  metric_t *m = malloc(sizeof(*m));
  memset(m, 0, sizeof(*m));
  m->metric_name = strdup(name);
  m->metric_type = t;
  if(rxc->got_timestamp) {
    memcpy(&m->whence, &rxc->last_timestamp, sizeof(struct timeval));
  }
  if(vp) {
    if(t == METRIC_STRING) m->metric_value.s = strdup((const char *)vp);
    else {
      size_t vsize = 0;
      switch(m->metric_type) {
        case METRIC_INT32:
          vsize = sizeof(int32_t);
          break;
        case METRIC_UINT32:
          vsize = sizeof(uint32_t);
          break;
        case METRIC_INT64:
          vsize = sizeof(int64_t);
          break;
        case METRIC_UINT64:
          vsize = sizeof(uint64_t);
          break;
        case METRIC_DOUBLE:
          vsize = sizeof(double);
          break;
        default:
          break;
      }
      if(vsize) {
        m->metric_value.vp = malloc(vsize);
        memcpy(m->metric_value.vp, vp, vsize);
      }
    }
  }
  noit_stats_mark_metric_logged(noit_check_get_stats_inprogress(rxc->check), m, mtev_false);
  mtev_hash_store(rxc->immediate_metrics, m->metric_name, strlen(m->metric_name), m);
}

static void
metric_local_accrue(struct rest_json_payload *rxc, const char *name, metric_type_t t, void *vp) {
  noit_metric_coerce_ex_with_timestamp(rxc->check, name, t, vp, &rxc->start_time,
    metric_local_track_or_log, rxc, NULL);
}

#define NEW_LV(json,a) do { \
  struct value_list *nlv = malloc(sizeof(*nlv)); \
  nlv->v = a; \
  nlv->next = json->last_value; \
  json->last_value = nlv; \
} while(0)

static mtev_boolean
noit_httptrap_check_asynch(noit_module_t *self,
                           noit_check_t *check) {
  const char *config_val;
  httptrap_mod_config_t *conf;
  if(!self) return mtev_true;
  conf = noit_module_get_userdata(self);
  if(!conf) return mtev_true;
  mtev_boolean is_asynch = conf->asynch_metrics;
  if(mtev_hash_retr_str(check->config,
                        "asynch_metrics", strlen("asynch_metrics"),
                        (const char **)&config_val)) {
    if(!strcasecmp(config_val, "false") || !strcasecmp(config_val, "off"))
      is_asynch = mtev_false;
    else if(!strcasecmp(config_val, "true") || !strcasecmp(config_val, "on"))
      is_asynch = mtev_true;
  }

  if(is_asynch) check->flags |= NP_SUPPRESS_METRICS;
  else check->flags &= ~NP_SUPPRESS_METRICS;
  return is_asynch;
}

static mtev_boolean
noit_httptrap_check_fanout(noit_module_t *self,
                           noit_check_t *check) {
  const char *config_val;
  httptrap_mod_config_t *conf;
  if(!self) return mtev_true;
  conf = noit_module_get_userdata(self);
  if(!conf) return mtev_true;
  mtev_boolean is_fanout = conf->fanout;
  if(mtev_hash_retr_str(check->config,
                        "fanout", strlen("fanout"),
                        (const char **)&config_val)) {
    if(!strcasecmp(config_val, "false") || !strcasecmp(config_val, "off"))
      is_fanout = mtev_false;
    else if(!strcasecmp(config_val, "true") || !strcasecmp(config_val, "on"))
      is_fanout = mtev_true;
  }

  return is_fanout;
}

static int
set_array_key(struct rest_json_payload *json) {
  if(json->array_depth[json->depth] > 0) {
    char str[256];
    int strLen;
    snprintf(str, sizeof(str), "%d", json->array_depth[json->depth] - 1);
    json->array_depth[json->depth]++;
    strLen = strlen(str);
    if(json->keys[json->depth]) free(json->keys[json->depth]);
    json->keys[json->depth] = NULL;
    if(json->depth == 0) {
      json->keys[json->depth] = malloc(strLen+1);
      memcpy(json->keys[json->depth], str, strLen);
      json->keys[json->depth][strLen] = '\0';
    }
    else {
      int uplen = strlen(json->keys[json->depth-1]);
      /* This is too large.... return an error */
      if(uplen + 1 + strLen > MAX_METRIC_TAGGED_NAME) {
	   strLen = MAX_METRIC_TAGGED_NAME - uplen - 1;
	   if(strLen < 0) strLen = 0;
        if(!json->supp_err)
	     json->supp_err = strdup(TRUNCATE_ERROR);
      }
      json->keys[json->depth] = malloc(uplen + 1 + strLen + 1);
      memcpy(json->keys[json->depth], json->keys[json->depth-1], uplen);
      json->keys[json->depth][uplen] = json->delimiter;
      memcpy(json->keys[json->depth] + uplen + 1, str, strLen);
      json->keys[json->depth][uplen + 1 + strLen] = '\0';
    }
  }
  return 0;
}
static int
httptrap_yajl_cb_null(void *ctx) {
  struct rest_json_payload *json = ctx;
  int rv;
  if(json->depth<0) {
    _YD("[%3d] cb_null [BAD]\n", json->depth);
    return 0;
  }
  rv = set_array_key(json);
  if(json->last_special_key == HT_EX_VALUE) {
    _YD("[%3d]*cb_null\n", json->depth);
    NEW_LV(json, NULL);
    return 1;
  }
  if(json->last_special_key) return 0;
  if(rv) return 1;
  if(json->keys[json->depth]) {
    _YD("[%3d] cb_null\n", json->depth);
    track_filtered(json, json->keys[json->depth]);
    noit_stats_set_metric(json->check,
        json->keys[json->depth], METRIC_INT32, NULL);
    if(json->immediate || json->got_timestamp)
      metric_local_accrue(json, json->keys[json->depth], METRIC_INT32, NULL);
    json->cnt++;
  }
  return 1;
}
static int
httptrap_yajl_cb_boolean(void *ctx, int boolVal) {
  int ival, rv;
  struct rest_json_payload *json = ctx;
  if(json->depth<0) {
    _YD("[%3d] cb_boolean [BAD]\n", json->depth);
    return 0;
  }
  rv = set_array_key(json);
  if(json->last_special_key == HT_EX_VALUE) {
    NEW_LV(json, strdup(boolVal ? "1" : "0"));
    _YD("[%3d]*cb_boolean -> %s\n", json->depth, boolVal ? "true" : "false");
    return 1;
  }
  if(json->last_special_key) return 0;
  if(rv) return 1;
  if(json->keys[json->depth]) {
    ival = boolVal ? 1 : 0;
    _YD("[%3d] cb_boolean -> %s\n", json->depth, boolVal ? "true" : "false");
    track_filtered(json, json->keys[json->depth]);
    noit_stats_set_metric(json->check,
        json->keys[json->depth], METRIC_INT32, &ival);
    if(json->immediate || json->got_timestamp)
      metric_local_accrue(json, json->keys[json->depth], METRIC_INT32, &ival);
    json->cnt++;
  }
  return 1;
}
static int
httptrap_yajl_cb_number(void *ctx, const char * numberVal,
                        size_t numberLen) {
  char val[128];
  struct rest_json_payload *json = ctx;
  int rv;
  if(json->depth<0) {
    _YD("[%3d] cb_number [BAD]\n", json->depth);
    return 0;
  }
  rv = set_array_key(json);
  if(json->last_special_key == HT_EX_VALUE) {
    char *str;
    str = malloc(numberLen+1);
    memcpy(str, numberVal, numberLen);
    str[numberLen] = '\0';
    NEW_LV(json, str);
    _YD("[%3d] cb_number %s\n", json->depth, str);
    return 1;
  }
  if(json->last_special_key == HT_EX_TS) {
    uint64_t ts = strtoull(numberVal, NULL, 10);
    json->last_timestamp.tv_sec = (ts / 1000);
    json->last_timestamp.tv_usec = (ts % 1000) * 1000;
    json->saw_complex_type |= HT_EX_TS;
    json->got_timestamp = mtev_true;
    _YD("[%3d] cb_number { _ts: %zu }\n", json->depth, ts);
    return 1;
  }
  if(rv) return 1;
  /* If we get a number for _flags, it simply doesn't
   * match any flags, so... no op
   */
  if(json->last_special_key == HT_EX_FLAGS) return 1;
  if(json->last_special_key) {
    _YD("[%3d] cb_number [BAD]\n", json->depth);
    return 0;
  }
  if(json->keys[json->depth]) {
    if(numberLen > sizeof(val)-1) numberLen = sizeof(val)-1;
    memcpy(val, numberVal, numberLen);
    val[numberLen] = '\0';
    _YD("[%3d] cb_number %s\n", json->depth, val);
    track_filtered(json, json->keys[json->depth]);
    noit_stats_set_metric(json->check,
        json->keys[json->depth], METRIC_GUESS, val);
    if(json->immediate || json->got_timestamp)
      metric_local_accrue(json, json->keys[json->depth], METRIC_GUESS, val);
    json->cnt++;
  }
  return 1;
}
static int
httptrap_yajl_cb_string(void *ctx, const unsigned char * stringVal,
                        size_t stringLen) {
  struct rest_json_payload *json = ctx;
  char val[4096];
  int rv;
  if(json->depth<0) {
    _YD("[%3d] cb_string [BAD]\n", json->depth);
    return 0;
  }
  if(json->last_special_key == HT_EX_TAGS) /* handle tag */
    return 1;
  rv = set_array_key(json);
  if(json->last_special_key == HT_EX_TYPE) {
    if(stringLen != 1) return 0;
    if(*stringVal == 'L' || *stringVal == 'l' ||
        *stringVal == 'I' || *stringVal == 'i' ||
        *stringVal == 'n' || *stringVal == 's' ||
        *stringVal == 'h' || *stringVal == 'H') {
      json->last_type = *stringVal;
      json->saw_complex_type |= HT_EX_TYPE;
      _YD("[%3d] cb_string { _type: %c }\n", json->depth, *stringVal);
      return 1;
    }
    _YD("[%3d] cb_string { bad _type: %.*s }\n", json->depth,
        (int)stringLen, stringVal);
    return 0;
  }
  else if(json->last_special_key == HT_EX_VALUE) {
    char *str;
    str = malloc(stringLen+1);
    memcpy(str, stringVal, stringLen);
    str[stringLen] = '\0';
    NEW_LV(json, str);
    _YD("[%3d] cb_string { _value: %s }\n", json->depth, str);
    json->saw_complex_type |= HT_EX_VALUE;
    return 1;
  }
  else if(json->last_special_key == HT_EX_TS) {
    uint64_t ts = strtoull((const char *)stringVal, NULL, 10);
    json->last_timestamp.tv_sec = (ts / 1000);
    json->last_timestamp.tv_usec = (ts % 1000) * 1000;
    json->saw_complex_type |= HT_EX_TS;
    json->got_timestamp = mtev_true;
    _YD("[%3d] cb_string { _ts: %zu }\n", json->depth, ts);
    return 1;
  }
  else if(json->last_special_key == HT_EX_FLAGS) {
    int i;
    for(i=0;i<stringLen;i++) {
      switch(stringVal[i]) {
        case '+':
          json->vop_flag = HTTPTRAP_VOP_ACCUMULATE; break;
        case '~':
          json->vop_flag = HTTPTRAP_VOP_AVERAGE; break;
	default: break;
      }
    }
    _YD("[%3d] cb_string { _fl: %.*s }\n", json->depth, (int)stringLen, stringVal);
    return 1;
  }
  else if(json->last_special_key == HT_EX_TS) return 1;
  else if(json->last_special_key == HT_EX_TAGS) return 1;
  if(rv) return 1;
  if(json->keys[json->depth]) {
    if(stringLen > sizeof(val)-1) stringLen = sizeof(val)-1;
    memcpy(val, stringVal, stringLen);
    val[stringLen] = '\0';
    _YD("[%3d] cb_string %s\n", json->depth, val);
    track_filtered(json, json->keys[json->depth]);
    noit_stats_set_metric(json->check,
        json->keys[json->depth], METRIC_GUESS, val);
    if(json->immediate || json->got_timestamp)
      metric_local_accrue(json, json->keys[json->depth], METRIC_GUESS, val);
    json->cnt++;
  }
  return 1;
}
static int
httptrap_yajl_cb_start_map(void *ctx) {
  struct rest_json_payload *json = ctx;
  _YD("[%3d] cb_start_map\n", json->depth);
  if(set_array_key(json)) return 1;
  json->depth++;
  if(json->depth >= MAX_DEPTH) return 0;
  return 1;
}
static int
httptrap_yajl_cb_end_map(void *ctx) {
  struct value_list *p, *last_p = NULL;
  struct rest_json_payload *json = ctx;
  const char *metric_name;

  _YD("[%3d]%-.*s cb_end_map\n", json->depth, json->depth, "");
  json->depth--;
  metric_name = json->keys[json->depth];
  if((json->saw_complex_type & HT_EX_VALUE) &&
     (
       (json->saw_complex_type & HT_EX_TYPE) ||
       (json->saw_complex_type & HT_EX_TS)
     )) {
    long double total = 0, cnt = 0, accum = 1;
    double newval;
    mtev_boolean use_computed_value = mtev_false;
    metric_t *m;

    /* Purpose statement...
     * for extended types, users can request that values be accumulated
     * or averaged.. or replaced..
     * note that replacement will still average a single submission.
     *
     * Here we make an attempt to find the last known metric value
     * and if we're in avging mode, the last inprogress metric value,
     * but we also need a cnt to make the weight right for the avg.
     *
     * If we are immediate mode, averaging over the "period" makes
     * no sense whatsoever... so if the user has requested averaging
     * and we're in immediate mode, we revert to replacement.
     */
    if(json->immediate && json->vop_flag == HTTPTRAP_VOP_AVERAGE)
      json->vop_flag = HTTPTRAP_VOP_REPLACE;

    switch(json->vop_flag) {
    case HTTPTRAP_VOP_REPLACE: break;
    case HTTPTRAP_VOP_AVERAGE:
      /* We are asked to compute an average, presumably only within
       * out time window (check period) so we restrict our pull to
       * in progress metrics only (get_metric) not (get_last_metric)
       * and we also much fetch a count...
       */
      track_filtered(json, metric_name);
      m = noit_stats_get_metric(json->check, NULL, metric_name);
      double old_value;
      if(noit_metric_as_double(m, &old_value)) {
        cnt = m->accumulator;
        total = (long double)old_value * (long double)cnt;
      }
      break;
    case HTTPTRAP_VOP_ACCUMULATE:
      /* We have one value, if not found that value is zero.
       * we also want the count to remain zero as we add, so
       * we ultimately divide by one and not the set size, so
       * set accum = 0 so we will not accumulate a divisor.
       */
      cnt = 1;
      accum = 0;
      track_filtered(json, metric_name);
      m = noit_stats_get_last_metric(json->check, metric_name);
      double old_total = 0.0;
      noit_metric_as_double(m, &old_total);
      total = old_total;
      break;
    }

    for(p=json->last_value;p;p=p->next) {
      if(json->last_type == 'h' || json->last_type == 'H') {
        metric_type_t hist_type = json->last_type == 'H' ? METRIC_HISTOGRAM_CUMULATIVE : METRIC_HISTOGRAM;
        if(json->saw_complex_type & HT_EX_TS) {
          if(p == json->last_value && p->next == NULL) {
            /* There can be exactly one, it should be base64 encoded */
            track_filtered(json, metric_name);
            noit_stats_log_immediate_histo_tv(json->check, metric_name, p->v, strlen(p->v),
                                              hist_type == METRIC_HISTOGRAM_CUMULATIVE, json->last_timestamp);
          }
        } else {
          track_filtered(json, metric_name);
          noit_stats_set_metric_histogram(json->check, metric_name,
                                          hist_type == METRIC_HISTOGRAM_CUMULATIVE, METRIC_GUESS, p->v, 1);
        }
      } else {
        if(json->got_timestamp) {
          track_filtered(json, metric_name);
          noit_stats_set_metric_coerce_with_timestamp(json->check,
              metric_name,
              (json->saw_complex_type & HT_EX_TYPE) ? json->last_type : METRIC_GUESS,
              p->v,
              &json->last_timestamp);
        } else {
          track_filtered(json, metric_name);
          noit_stats_set_metric_coerce_with_timestamp(json->check,
              metric_name,
              (json->saw_complex_type & HT_EX_TYPE) ? json->last_type : METRIC_GUESS,
              p->v, NULL);
        }
      }
      last_p = p;
      if((p->v != NULL) && (json->saw_complex_type & HT_EX_TYPE) && (IS_METRIC_TYPE_NUMERIC(json->last_type))) {
        total += strtold(p->v, NULL);
        cnt = cnt + accum;
        use_computed_value = mtev_true;
      }
      json->cnt++;
    }
    if(use_computed_value) {
      newval = (double)(total / (long double)cnt);
      /* Perform and in-place update of the metric value correcting it */
      track_filtered(json, metric_name);
      m = noit_stats_get_metric(json->check, NULL, metric_name);
      if(m && IS_METRIC_TYPE_NUMERIC(m->metric_type)) {
        if(m->metric_value.vp == NULL) {
          double *dp = malloc(sizeof(double));
          *dp = newval;
          m->metric_value.vp = (void *)dp;
        }
        else {
          *(m->metric_value.n) = newval;
        }
        m->metric_type = METRIC_DOUBLE;
        m->accumulator = cnt;
      }
    }
    if((json->immediate || json->got_timestamp) && last_p != NULL && json->last_type != 'h' && json->last_type != 'H') {
      metric_type_t t = use_computed_value ?
                          'n' :
                          ((json->saw_complex_type & HT_EX_TYPE) ?
                            json->last_type :
                            METRIC_GUESS);
      char double_str[32];
      void *value = last_p->v;
      if(use_computed_value) {
        snprintf(double_str, sizeof(double_str), "%g", newval);
        value = double_str;
      }
      /* metric_local_accrue will ultimately set the timestamp if one is set in `json` */
      metric_local_accrue(json, metric_name, t, value);
    }
  }
  json->saw_complex_type = 0;
  json->got_timestamp = mtev_false;
  json->vop_flag = HTTPTRAP_VOP_REPLACE;
  for(p=json->last_value;p;) {
    struct value_list *savenext;
    savenext = p->next;
    if(p->v) free(p->v);
    savenext = p->next;
    free(p);
    p = savenext;
  }
  json->last_value = NULL;
  return 1;
}
static int
httptrap_yajl_cb_start_array(void *ctx) {
  struct rest_json_payload *json = ctx;
  if(set_array_key(json)) return 1;
  json->depth++;
  json->array_depth[json->depth]++;
  return 1;
}
static int
httptrap_yajl_cb_end_array(void *ctx) {
  struct rest_json_payload *json = ctx;
  json->array_depth[json->depth] = 0;
  json->depth--;
  return 1;
}
static int
httptrap_yajl_cb_map_key(void *ctx, const unsigned char * key,
                         size_t stringLen) {
  struct rest_json_payload *json = ctx;
  if(stringLen > MAX_METRIC_TAGGED_NAME) {
    if(!json->supp_err)
      json->supp_err = strdup(TRUNCATE_ERROR);
    stringLen = MAX_METRIC_TAGGED_NAME;
  }
  if(json->keys[json->depth]) free(json->keys[json->depth]);
  json->keys[json->depth] = NULL;
  if(stringLen == 5 && memcmp(key, "_type", 5) == 0) {
    json->last_special_key = HT_EX_TYPE;
    if(json->depth > 0) json->keys[json->depth] = strdup(json->keys[json->depth-1]);
    return 1;
  }
  if(stringLen == 6 && memcmp(key, "_value", 6) == 0) {
    if(json->depth > 0) json->keys[json->depth] = strdup(json->keys[json->depth-1]);
    json->last_special_key = HT_EX_VALUE;
    json->saw_complex_type |= HT_EX_VALUE;
    return 1;
  }
  if(stringLen == 3 && memcmp(key, "_ts", 3) == 0) {
    json->last_special_key = HT_EX_TS;
    return 1;
  }
  if(stringLen == 3 && memcmp(key, "_fl", 3) == 0) {
    json->last_special_key = HT_EX_FLAGS;
    return 1;
  }
  if(stringLen == 5 && memcmp(key, "_tags", 5) == 0) {
    json->last_special_key = HT_EX_TAGS;
    return 1;
  }
  json->last_special_key = 0;
  if(json->depth == 0) {
    json->keys[json->depth] = malloc(stringLen+1);
    memcpy(json->keys[json->depth], key, stringLen);
    json->keys[json->depth][stringLen] = '\0';
  }
  else {
    int uplen = strlen(json->keys[json->depth-1]);
    if(uplen + 1 + stringLen > MAX_METRIC_TAGGED_NAME) {
      if(MAX_METRIC_TAGGED_NAME - uplen - 1 < 0) stringLen = 0;
      else stringLen = MAX_METRIC_TAGGED_NAME - uplen - 1;
      if(!json->supp_err)
	   json->supp_err = strdup(TRUNCATE_ERROR);
    }
    json->keys[json->depth] = malloc(uplen + 1 + stringLen + 1);
    memcpy(json->keys[json->depth], json->keys[json->depth-1], uplen);
    json->keys[json->depth][uplen] = json->delimiter;
    memcpy(json->keys[json->depth] + uplen + 1, key, stringLen);
    json->keys[json->depth][uplen + 1 + stringLen] = '\0';
  }
  return 1;
}
static yajl_callbacks httptrap_yajl_callbacks = {
  .yajl_null = httptrap_yajl_cb_null,
  .yajl_boolean = httptrap_yajl_cb_boolean,
  .yajl_number = httptrap_yajl_cb_number,
  .yajl_string = httptrap_yajl_cb_string,
  .yajl_start_map = httptrap_yajl_cb_start_map,
  .yajl_map_key = httptrap_yajl_cb_map_key,
  .yajl_end_map = httptrap_yajl_cb_end_map,
  .yajl_start_array = httptrap_yajl_cb_start_array,
  .yajl_end_array = httptrap_yajl_cb_end_array
};

static void
rest_json_payload_free(void *f) {
  int i;
  struct rest_json_payload *json = f;
  if(json->immediate_metrics) {
    rest_json_flush_immediate(json);
  }
  mtev_hash_destroy(json->immediate_metrics, NULL, metric_local_free);
  free(json->immediate_metrics);
  if(json->parser) yajl_free(json->parser);
  if(json->error) free(json->error);
  if(json->supp_err) free(json->supp_err);
  for(i=0;i<MAX_DEPTH;i++)
    if(json->keys[i]) free(json->keys[i]);
  if(json->last_value) free(json->last_value);
  free(json);
}

static struct rest_json_payload *
rest_get_json_upload(mtev_http_rest_closure_t *restc,
                    int *mask, int *complete) {
  struct rest_json_payload *rxc;
  mtev_http_request *req = mtev_http_session_request(restc->http_ctx);
  httptrap_closure_t *ccl = NULL;
  char buffer[32768];

  rxc = restc->call_closure;
  if (!rxc->check) {
    rxc->check = noit_poller_lookup(rxc->check_id);
  }
  if (!rxc->check) {
    rxc->error = strdup("Unable to retrieve check");
    *complete = 1;
    return NULL;
  }

  if(!strcmp(rxc->check->module, "httptrap")) ccl = rxc->check->closure;
  rxc->immediate = noit_httptrap_check_asynch(ccl ? ccl->self : global_self, rxc->check);
  if(rxc->immediate && !rxc->immediate_metrics) {
    rxc->immediate_metrics = calloc(1, sizeof(*rxc->immediate_metrics));
    mtev_hash_init(rxc->immediate_metrics);
  }
  while(!rxc->complete) {
    int len;
    len = mtev_http_session_req_consume(restc->http_ctx, buffer,
                                        sizeof(buffer), sizeof(buffer), mask);
    if(len > 0) {
      yajl_status status;
      _YD("inbound payload chunk (%d bytes) continuing YAJL parse\n", len);
      status = yajl_parse(rxc->parser, (unsigned char *)buffer, len);
      if(status != yajl_status_ok) {
        unsigned char *err;
        *complete = 1;
        err = yajl_get_error(rxc->parser, 1, (unsigned char *)buffer, len);
        rxc->error = strdup((char *)err);
        yajl_free_error(rxc->parser, err);
        return rxc;
      }
      rxc->len += len;
    }
    if(len < 0 && errno == EAGAIN) return NULL;
    else if(len < 0) {
      rxc->error = strdup("Unable to read incoming payload");
      *complete = 1;
      return NULL;
    }
    if(len == 0 && mtev_http_request_payload_complete(req)) {
      rxc->complete = 1;
      _YD("no more data, finishing YAJL parse\n");
      yajl_complete_parse(rxc->parser);
    }
  }

  *complete = 1;
  return rxc;
}

static int httptrap_submit(noit_module_t *self, noit_check_t *check,
                           noit_check_t *cause) {
  httptrap_closure_t *ccl;
  struct timeval duration;
  /* We are passive, so we don't do anything for transient checks */
  if(check->flags & NP_TRANSIENT) return 0;

  noit_httptrap_check_asynch(self, check);
  if(!check->closure) {
    ccl = check->closure = (void *)calloc(1, sizeof(httptrap_closure_t));
    memset(ccl, 0, sizeof(httptrap_closure_t));
    ccl->self = self;
  } else {
    // Don't count the first run
    struct timeval now;
    char human_buffer[256];
    int stats_count = 0;
    stats_t *s = noit_check_get_stats_inprogress(check);

    mtev_gettimeofday(&now, NULL);
    sub_timeval(now, check->last_fire_time, &duration);
    noit_stats_set_whence(check, &now);
    noit_stats_set_duration(check, duration.tv_sec * 1000 + duration.tv_usec / 1000);

    /* We just want to set the number of metrics here to the number
     * of metrics in the stats_t struct */
    if (s) {
      mtev_hash_table *metrics = noit_check_stats_metrics(s);
      if (metrics) {
        stats_count = mtev_hash_size(metrics);
      }
    }

    char uuid_str[37];
    mtev_uuid_unparse_lower(check->checkid, uuid_str);
    snprintf(human_buffer, sizeof(human_buffer),
             "dur=%ld,run=%d,stats=%d", duration.tv_sec * 1000 + duration.tv_usec / 1000,
             check->generation, stats_count);
    mtevL(nldeb, "httptrap for %s (%s) [%s]\n", uuid_str, check->target, human_buffer);

    // Not sure what to do here
    noit_stats_set_available(check, (stats_count > 0) ?
        NP_AVAILABLE : NP_UNAVAILABLE);
    noit_stats_set_state(check, (stats_count > 0) ?
        NP_GOOD : NP_BAD);
    noit_stats_set_status(check, human_buffer);
    if(check->last_fire_time.tv_sec)
      noit_check_passive_set_stats(check);

    memcpy(&check->last_fire_time, &now, sizeof(now));
  }
  return 0;
}

static mtev_boolean
cross_module_reverse_allowed(noit_check_t *check, const char *secret) {
  void *vstr;
  mtev_hash_table *config;
  static int reverse_check_module_id = -1;
  if(reverse_check_module_id < 0) {
    reverse_check_module_id = noit_check_registered_module_by_name("reverse");
    if(reverse_check_module_id < 0) return mtev_false;
  }
  config = noit_check_get_module_config(check, reverse_check_module_id);
  if(!config) return mtev_false;
  if(mtev_hash_retrieve(config, "secret_key", strlen("secret_key"), &vstr)) {
    if(!strcmp((const char *)vstr, secret)) return mtev_true;
  }
  return mtev_false;
}

static void
http_write_encoded(void *vctx, const char* str, size_t len) {
  mtev_http_session_ctx *ctx = vctx;
  mtev_http_response_append(ctx, str, len);
}

static bool
origin_allowed(mtev_http_session_ctx *ctx, noit_check_t *check, const char **origin) {
  mtev_http_request *req;
  mtev_hash_table *hdrs;
  req = mtev_http_session_request(ctx);
  hdrs = mtev_http_request_headers_table(req);
  *origin = mtev_hash_dict_get(hdrs, "origin");
  bool pass_origin = false;
  if(*origin) {
    const char *origin_match = mtev_hash_dict_get(check->config, "cors_origin");
    pass_origin = origin_match == NULL;
    if(origin_match) {
      const char *err;
      int erroff;
      int localovector[30];
      pcre *re = pcre_compile(origin_match, 0, &err, &erroff, NULL);
      if(re) {
        pass_origin = pcre_exec(re, NULL, *origin, strlen(*origin), 0, 0,
                                localovector, sizeof(localovector)/sizeof(*localovector)) > 0;
        pcre_free(re);
      }
    }
  }
  return pass_origin;
}

static int
rest_httptrap_options_handler(mtev_http_rest_closure_t *restc,
                              int npats, char **pats) {
  int error_code = 500;
  const char *error = "internal error", *secret = NULL;
  mtev_http_session_ctx *ctx = restc->http_ctx;
  noit_check_t *check = NULL;
  uuid_t check_id;

  if(npats != 2) {
    error = "bad uri";
    error_code = 404;
    goto error;
  }
  if(mtev_uuid_parse(pats[0], check_id)) {
    error = "uuid parse error";
    error_code = 404;
    goto error;
  }

  mtev_boolean allowed = mtev_false;
  check = noit_poller_lookup(check_id);
  if(!check) {
    error = "no such check";
    error_code = 404;
    goto error;
  }
  if(!httptrap_surrogate && strcmp(check->module, "httptrap")) {
    error = "no such httptrap check";
    error_code = 404;
    goto error;
  }

  /* check "secret" then "httptrap_secret" as a fallback */
  (void)mtev_hash_retr_str(check->config, "secret", strlen("secret"), &secret);
  if(!secret) (void)mtev_hash_retr_str(check->config, "httptrap_secret", strlen("httptrap_secret"), &secret);
  if(secret && !strcmp(pats[1], secret)) allowed = mtev_true;
  if(!allowed && cross_module_reverse_allowed(check, pats[1])) allowed = mtev_true;

  if(!allowed) {
    error = "secret mismatch";
    error_code = 403;
    goto error;
  }

  const char *origin;
  mtev_http_response_standard(ctx, 204, "No Content", "application/json");
  if(origin_allowed(ctx, check, &origin)) {
    mtev_http_response_header_set(ctx, "Access-Control-Allow-Origin", origin);
    mtev_http_response_header_set(ctx, "Access-Control-Allow-Methods", "POST, PUT, OPTIONS");
    mtev_http_response_header_set(ctx, "Access-Control-Allow-Headers", "Content-Type");
  }
  mtev_http_response_end(ctx);
  noit_check_defer(check);
  return 0;

 error:
  mtev_http_response_standard(ctx, error_code, "ERROR", "application/json");
  mtev_http_response_append(ctx, "{ \"error\": \"", 12);
  yajl_string_encode((yajl_print_t)http_write_encoded, ctx, (const unsigned char*)error, strlen(error), 0);
  mtev_http_response_append(ctx, "\" }", 3);
  mtev_http_response_end(ctx);
  noit_check_defer(check);
  return 0;
}

static int
rest_httptrap_handler(mtev_http_rest_closure_t *restc,
                      int npats, char **pats) {
  int mask = EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
  int complete = 0, cnt;
  int error_code = 500;
  struct rest_json_payload *rxc = NULL;
  const char *error = "internal error", *secret = NULL;
  mtev_http_session_ctx *ctx = restc->http_ctx;
  const unsigned int DEBUGDATA_OUT_SIZE=4096;
  char debugdata_out[DEBUGDATA_OUT_SIZE];
  int debugflag=0;
  const char *debugchkflag;
  noit_check_t *check = NULL;
  uuid_t check_id;
  mtev_http_request *req;
  mtev_hash_table *hdrs;
  uint64_t current_counter = ck_pr_faa_64(&httptrap_count, 1);

  mtevL(nldeb, "httptrap handler initiated for %s (%" PRIu64 ")\n", npats ? pats[0] : "?", current_counter);

  if(npats != 2) {
    error = "bad uri";
    error_code = 404;
    goto error;
  }
  if(mtev_uuid_parse(pats[0], check_id)) {
    error = "uuid parse error";
    error_code = 404;
    goto error;
  }

  if(restc->call_closure == NULL) {
    mtev_boolean allowed = mtev_false;
    const char *delimiter = NULL;
    check = noit_poller_lookup(check_id);
    if(!check) {
      error = "no such check";
      error_code = 404;
      goto error;
    }
    if(!httptrap_surrogate && strcmp(check->module, "httptrap")) {
      error = "no such httptrap check";
      error_code = 404;
      noit_check_deref(check);
      goto error;
    }

    /* check "secret" then "httptrap_secret" as a fallback */
    (void)mtev_hash_retr_str(check->config, "secret", strlen("secret"), &secret);
    if(!secret) (void)mtev_hash_retr_str(check->config, "httptrap_secret", strlen("httptrap_secret"), &secret);
    if(secret && !strcmp(pats[1], secret)) allowed = mtev_true;
    if(!allowed && cross_module_reverse_allowed(check, pats[1])) allowed = mtev_true;

    if(!allowed) {
      error = "secret mismatch";
      error_code = 403;
      noit_check_deref(check);
      goto error;
    }

    rxc = restc->call_closure = calloc(1, sizeof(*rxc));
    mtev_gettimeofday(&rxc->start_time, NULL);
    rxc->delimiter = DEFAULT_HTTPTRAP_DELIMITER;
    rxc->current_counter = current_counter;

    /* check "delimiter" then "httptrap_delimiter" as a fallback */
    (void)mtev_hash_retr_str(check->config, "delimiter", strlen("delimiter"), &delimiter);
    if(!delimiter) (void)mtev_hash_retr_str(check->config, "httptrap_delimiter", strlen("httptrap_delimiter"), &delimiter);
    if(delimiter && *delimiter) rxc->delimiter = *delimiter;
    rxc->check = check;
    mtev_uuid_copy(rxc->check_id, check_id);
    rxc->parser = yajl_alloc(&httptrap_yajl_callbacks, NULL, rxc);
    rxc->depth = -1;
    yajl_config(rxc->parser, yajl_allow_comments, 1);
    yajl_config(rxc->parser, yajl_dont_validate_strings, 1);
    yajl_config(rxc->parser, yajl_allow_trailing_garbage, 1);
    yajl_config(rxc->parser, yajl_allow_multiple_values, 1);
    yajl_config(rxc->parser, yajl_allow_partial_values, 1);
    restc->call_closure_free = rest_json_payload_free;

    /* flip threads */
    mtev_http_connection *conn = mtev_http_session_connection(ctx);
    eventer_t e = mtev_http_connection_event(conn);
    if(e) {
      pthread_t tgt;
      if(noit_httptrap_check_fanout(global_self, rxc->check)) {
        eventer_pool_t *tgtpool = noit_check_choose_pool(rxc->check);
        unsigned long rnd = mtev_rand();
        if(tgtpool) tgt = eventer_choose_owner_pool(tgtpool, rnd);
        else tgt = eventer_choose_owner(rnd);
      } else {
        tgt = CHOOSE_EVENTER_THREAD_FOR_CHECK(rxc->check);
      }
      if(!pthread_equal(eventer_get_owner(e), tgt)) {
        eventer_set_owner(e, tgt);
        return EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
      }
    }
  }
  else rxc = restc->call_closure;

  /*Examine headers for x-circonus-httptrap-debug flag*/
  req = mtev_http_session_request(ctx);
  hdrs = mtev_http_request_headers_table(req);

  /*Check if debug header passed in. If present and set to true, set debugflag value to one.*/
  if(mtev_hash_retr_str(hdrs, "x-circonus-httptrap-debug", strlen("x-circonus-httptrap-debug"), &debugchkflag))
  {
    if (strcmp(debugchkflag,"true")==0)
    {
      debugflag=1;
    }
  }

  mtevL(nldeb, "Processing JSON upload for %s (%" PRIu64 ")\n", pats[0], current_counter);

  rxc = rest_get_json_upload(restc, &mask, &complete);
  if(rxc == NULL && !complete)
  {
    mtevL(nldeb, "Payload read abort with mask %d for %s (%" PRIu64 ")\n", mask, pats[0], current_counter);
    return mask;
  }

  if(!rxc) {
    rxc = restc->call_closure;
    mtevL(nldeb, "Payload read error: %s\n", rxc->error);
    error_code = 406;
    goto error;
  }
  if(rxc->error) {
    mtevL(nldeb, "Payload read parse error: %s\n", rxc->error);
    error_code = 406;
    goto error;
  }

  cnt = rxc->cnt;
  mtevL(nldeb, "Processed %d records for %s (%" PRIu64 ")\n", cnt, pats[0], current_counter);

  mtev_http_response_status_set(ctx, 200, "OK");
  const char *origin;
  if(rxc->check && origin_allowed(ctx, rxc->check, &origin)) {
    mtev_http_response_header_set(ctx, "Access-Control-Allow-Origin", origin);
  }
  mtev_http_response_header_set(ctx, "Content-Type", "application/json");
  mtev_http_response_option_set(ctx, MTEV_HTTP_CLOSE);

  json_object *obj =  NULL;
  obj = json_object_new_object();
  /*If debugflag remains zero, simply output the number of metrics.*/
  if (debugflag==0)
  {
    json_object_object_add(obj, "stats", json_object_new_int(cnt));
    json_object_object_add(obj, "filtered", json_object_new_int(rxc->filtered_cnt));
    if (rxc->supp_err)
      json_object_object_add(obj, "error", json_object_new_string(rxc->supp_err));
  }

  /*Otherwise, if set to one, output current metrics in addition to number of current metrics.*/
  else if (debugflag==1)
  {
      stats_t *c;
      mtev_hash_table *metrics;
      json_object *metrics_obj;
      metrics_obj = json_object_new_object();

      /*Retrieve check information.*/
      check = rxc->check;
      c = noit_check_get_stats_inprogress(check);
      metrics = noit_check_stats_metrics(c);
      mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
      const char *k;
      int klen;
      void *data;
      memset(debugdata_out,'\0',sizeof(debugdata_out));

      /*Extract metrics*/
      while(mtev_hash_next(metrics, &iter, &k, &klen, &data))
      {
        char buff[NOIT_DEFAULT_TEXT_METRIC_SIZE_LIMIT], type_str[2];
        metric_t *tmp=(metric_t *)data;
        char *metric_name=tmp->metric_name;
        metric_type_t metric_type=tmp->metric_type;
        noit_stats_snprint_metric_value(buff, sizeof(buff), tmp);
        json_object *value_obj = json_object_new_object();
        snprintf(type_str, sizeof(type_str), "%c", metric_type);
        json_object_object_add(value_obj, "_type", json_object_new_string(type_str));
        json_object_object_add(value_obj, "_value", json_object_new_string(buff));
        json_object_object_add(metrics_obj, metric_name, value_obj);
      }

      /*Output stats and metrics.*/
      json_object_object_add(obj, "stats", json_object_new_int(cnt));
      json_object_object_add(obj, "metrics", metrics_obj);
  }

  const char *json_out = json_object_to_json_string(obj);
  mtev_http_response_append(ctx, json_out, strlen(json_out));
  json_object_put(obj);
  mtev_http_response_end(ctx);
  if (rxc) {
    noit_check_deref(rxc->check);
  }
  return 0;

 error:
  if (rxc) {
    noit_check_deref(rxc->check);
  }
  mtev_http_response_standard(ctx, error_code, "ERROR", "application/json");
  mtev_http_response_append(ctx, "{ \"error\": \"", 12);
  if (rxc && rxc->error)
    error = rxc->error;
  mtevL(nldeb, "Error %s for %s (%" PRIu64 ")\n", error, npats ? pats[0] : "?", current_counter);
  yajl_string_encode((yajl_print_t)http_write_encoded, ctx, (const unsigned char*)error, strlen(error), 0);
  mtev_http_response_append(ctx, "\" }", 3);
  mtev_http_response_end(ctx);
  return 0;
}

static int noit_httptrap_initiate_check(noit_module_t *self,
                                        noit_check_t *check,
                                        int once, noit_check_t *cause) {
  check->flags |= NP_PASSIVE_COLLECTION;
  if (check->closure == NULL) {
    httptrap_closure_t *ccl;
    ccl = check->closure = (void *)calloc(1, sizeof(httptrap_closure_t));
    ccl->self = self;
  }
  INITIATE_CHECK(httptrap_submit, self, check, cause);
  return 0;
}

static int noit_httptrap_config(noit_module_t *self, mtev_hash_table *options) {
  httptrap_mod_config_t *conf;
  conf = noit_module_get_userdata(self);
  if(conf) {
    if(conf->options) {
      mtev_hash_destroy(conf->options, free, free);
      free(conf->options);
    }
  }
  else
    conf = calloc(1, sizeof(*conf));
  conf->options = options;
  noit_module_set_userdata(self, conf);
  return 1;
}

static int noit_httptrap_onload(mtev_image_t *self) {
  if(!nlerr) nlerr = mtev_log_stream_find("error/httptrap");
  if(!nldeb) nldeb = mtev_log_stream_find("debug/httptrap");
  if(!nlyajl) nlyajl = mtev_log_stream_find("debug/httptrap_yajl");
  if(!nlerr) nlerr = noit_error;
  if(!nldeb) nldeb = noit_debug;
  return 0;
}

static int noit_httptrap_init(noit_module_t *self) {
  const char *config_val;
  httptrap_mod_config_t *conf;
  global_self = self;
  conf = noit_module_get_userdata(self);

  conf->asynch_metrics = mtev_true;
  if(mtev_hash_retr_str(conf->options,
                        "asynch_metrics", strlen("asynch_metrics"),
                        (const char **)&config_val)) {
    if(!strcasecmp(config_val, "false") || !strcasecmp(config_val, "off"))
      conf->asynch_metrics = mtev_false;
  }

  conf->fanout = mtev_true;
  if(mtev_hash_retr_str(conf->options,
                        "fanout", strlen("fanout"),
                        (const char **)&config_val)) {
    if(!strcasecmp(config_val, "false") || !strcasecmp(config_val, "off"))
      conf->fanout = mtev_false;
  }

  httptrap_surrogate = mtev_false;
  if(mtev_hash_retr_str(conf->options,
                        "surrogate", strlen("surrogate"),
                        (const char **)&config_val)) {
    if(!strcasecmp(config_val, "true") || !strcasecmp(config_val, "on"))
      httptrap_surrogate = mtev_true;
  }

  noit_module_set_userdata(self, conf);

  /* register rest handler */
  mtev_http_rest_register("OPTIONS", "/module/httptrap/",
                          "^(" UUID_REGEX ")/([^/]*).*$",
                          rest_httptrap_options_handler);
  mtev_http_rest_register("PUT", "/module/httptrap/",
                          "^(" UUID_REGEX ")/([^/]*).*$",
                          rest_httptrap_handler);
  mtev_http_rest_register("POST", "/module/httptrap/",
                          "^(" UUID_REGEX ")/([^/]*).*$",
                          rest_httptrap_handler);
  return 0;
}

#include "httptrap.xmlh"
noit_module_t httptrap = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "httptrap",
    .description = "httptrap collection",
    .xml_description = httptrap_xml_description,
    .onload = noit_httptrap_onload
  },
  noit_httptrap_config,
  noit_httptrap_init,
  noit_httptrap_initiate_check,
  NULL
};
