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

#include <mtev_defines.h>

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>
#include <yajl/yajl_parse.h>

#include <mtev_rest.h>
#include <mtev_hash.h>
#include <mtev_json.h>
#include <mtev_uuid.h>

#include "noit_metric.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_mtev_bridge.h"

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

#define _YD(fmt...) mtevL(nlyajl, fmt)

static mtev_boolean httptrap_surrogate;
static const char *TRUNCATE_ERROR = "at least one metric truncated to 255 characters";

typedef struct _mod_config {
  mtev_hash_table *options;
  mtev_boolean asynch_metrics;
} httptrap_mod_config_t;

typedef struct httptrap_closure_s {
  noit_module_t *self;
  int stats_count;
} httptrap_closure_t;

typedef enum {
  HTTPTRAP_VOP_REPLACE,
  HTTPTRAP_VOP_AVERAGE,
  HTTPTRAP_VOP_ACCUMULATE
} httptrap_vop_t;

struct value_list {
  char *v;
  struct value_list *next;
};
struct rest_json_payload {
  noit_check_t *check;
  uuid_t check_id;
  yajl_handle parser;
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
  httptrap_vop_t vop_flag;
  
  metric_type_t last_type;
  struct value_list *last_value;
  int cnt;
  mtev_boolean immediate;
};

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
      if(uplen + 1 + strLen > 255) {
	   strLen = 255 - uplen - 1;
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
    noit_stats_set_metric(json->check,
        json->keys[json->depth], METRIC_INT32, NULL);
    if(json->immediate)
      noit_stats_log_immediate_metric(json->check,
          json->keys[json->depth], METRIC_INT32, NULL);
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
    noit_stats_set_metric(json->check,
        json->keys[json->depth], METRIC_INT32, &ival);
    if(json->immediate)
      noit_stats_log_immediate_metric(json->check,
          json->keys[json->depth], METRIC_INT32, &ival);
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
  if(rv) return 1;
  /* If we get a number for _flags, it simply doesn't
   * match any flags, so... no op
   */
  if(json->last_special_key == HT_EX_FLAGS) return 1;
  if(json->last_special_key == HT_EX_TS) return 1;
  if(json->last_special_key) {
    _YD("[%3d] cb_number [BAD]\n", json->depth);
    return 0;
  }
  if(json->keys[json->depth]) {
    if(numberLen > sizeof(val)-1) numberLen = sizeof(val)-1;
    memcpy(val, numberVal, numberLen);
    val[numberLen] = '\0';
    _YD("[%3d] cb_number %s\n", json->depth, val);
    noit_stats_set_metric(json->check,
        json->keys[json->depth], METRIC_GUESS, val);
    if(json->immediate)
      noit_stats_log_immediate_metric(json->check,
          json->keys[json->depth], METRIC_GUESS, val);
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
  if(json->last_special_key == HT_EX_TS) /* handle ts */
    return 1;
  if(json->last_special_key == HT_EX_TAGS) /* handle tag */
    return 1;
  rv = set_array_key(json);
  if(json->last_special_key == HT_EX_TYPE) {
    if(stringLen != 1) return 0;
    if(*stringVal == 'L' || *stringVal == 'l' ||
        *stringVal == 'I' || *stringVal == 'i' ||
        *stringVal == 'n' || *stringVal == 's') {
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
    noit_stats_set_metric(json->check,
        json->keys[json->depth], METRIC_GUESS, val);
    if(json->immediate)
      noit_stats_log_immediate_metric(json->check,
          json->keys[json->depth], METRIC_GUESS, val);
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
  if(json->saw_complex_type == 0x3) {
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
      m = noit_stats_get_last_metric(json->check, metric_name);
      double old_total = 0.0;
      noit_metric_as_double(m, &old_total);
      total = old_total;
      break;
    }
          
    for(p=json->last_value;p;p=p->next) {
      noit_stats_set_metric_coerce(json->check, metric_name,
                                   json->last_type, p->v);
      last_p = p;
      if(p->v != NULL && IS_METRIC_TYPE_NUMERIC(json->last_type)) {
        total += strtold(p->v, NULL);
        cnt = cnt + accum;
        use_computed_value = mtev_true;
      }
      json->cnt++;
    }
    if(use_computed_value) {
      newval = (double)(total / (long double)cnt);
      /* Perform and in-place update of the metric value correcting it */
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
    if(json->immediate && last_p != NULL) {
      if(use_computed_value) {
        noit_stats_log_immediate_metric(json->check, metric_name, 'n', &newval);
      }
      else {
        noit_stats_log_immediate_metric(json->check, metric_name, json->last_type, last_p->v);
      }
    }
  }
  json->saw_complex_type = 0;
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
  set_array_key(json);
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
  if(stringLen > 255) {
    if(!json->supp_err)
      json->supp_err = strdup(TRUNCATE_ERROR);
    stringLen = 255;
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
    if(uplen + 1 + stringLen > 255) {
      if(255 - uplen - 1 < 0) stringLen = 0;
      else stringLen = 255 - uplen - 1;
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
  int content_length;
  char buffer[32768];

  content_length = mtev_http_request_content_length(req);
  rxc = restc->call_closure;
  rxc->check = noit_poller_lookup(rxc->check_id);
  if (!rxc->check) {
    *complete = 1;
    return NULL;
  }

  if(!strcmp(rxc->check->module, "httptrap")) ccl = rxc->check->closure;
  rxc->immediate = noit_httptrap_check_asynch(ccl ? ccl->self : global_self, rxc->check);
  while(!rxc->complete) {
    int len;
    len = mtev_http_session_req_consume(
            restc->http_ctx, buffer,
            MIN(content_length - rxc->len, sizeof(buffer)),
            sizeof(buffer),
            mask);
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
      *complete = 1;
      return NULL;
    }
    content_length = mtev_http_request_content_length(req);
    if((mtev_http_request_payload_chunked(req) && len == 0) ||
       (rxc->len == content_length)) {
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
    struct timeval now, *last;
    char human_buffer[256];
    ccl = (httptrap_closure_t*)check->closure;
    mtev_gettimeofday(&now, NULL);
    sub_timeval(now, check->last_fire_time, &duration);
    noit_stats_set_whence(check, &now);
    noit_stats_set_duration(check, duration.tv_sec * 1000 + duration.tv_usec / 1000);

    snprintf(human_buffer, sizeof(human_buffer),
             "dur=%ld,run=%d,stats=%d", duration.tv_sec * 1000 + duration.tv_usec / 1000,
             check->generation, ccl->stats_count);
    mtevL(nldeb, "httptrap(%s) [%s]\n", check->target, human_buffer);

    // Not sure what to do here
    noit_stats_set_available(check, (ccl->stats_count > 0) ?
        NP_AVAILABLE : NP_UNAVAILABLE);
    noit_stats_set_state(check, (ccl->stats_count > 0) ?
        NP_GOOD : NP_BAD);
    noit_stats_set_status(check, human_buffer);
    if(check->last_fire_time.tv_sec)
      noit_check_passive_set_stats(check);

    memcpy(&check->last_fire_time, &now, sizeof(now));
  }
  ccl->stats_count = 0;
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

static int
rest_httptrap_handler(mtev_http_rest_closure_t *restc,
                      int npats, char **pats) {
  int mask, complete = 0, cnt;
  struct rest_json_payload *rxc = NULL;
  const char *error = "internal error", *secret = NULL;
  mtev_http_session_ctx *ctx = restc->http_ctx;
  const unsigned int DEBUGDATA_OUT_SIZE=4096;
  const unsigned int JSON_OUT_SIZE=DEBUGDATA_OUT_SIZE+128;
  char debugdata_out[DEBUGDATA_OUT_SIZE];
  int debugflag=0;
  const char *debugchkflag;
  noit_check_t *check;
  uuid_t check_id;
  mtev_http_request *req;
  mtev_hash_table *hdrs;

  if(npats != 2) {
    error = "bad uri";
    goto error;
  }
  if(uuid_parse(pats[0], check_id)) {
    error = "uuid parse error";
    goto error;
  }

  if(restc->call_closure == NULL) {
    mtev_boolean allowed = mtev_false;
    httptrap_closure_t *ccl = NULL;
    const char *delimiter = NULL;
    rxc = restc->call_closure = calloc(1, sizeof(*rxc));
    rxc->delimiter = DEFAULT_HTTPTRAP_DELIMITER;
    check = noit_poller_lookup(check_id);
    if(!check) {
      error = "no such check";
      goto error;
    }
    if(!httptrap_surrogate && strcmp(check->module, "httptrap")) {
      error = "no such httptrap check";
      goto error;
    }
 
    /* check "secret" then "httptrap_secret" as a fallback */
    (void)mtev_hash_retr_str(check->config, "secret", strlen("secret"), &secret);
    if(!secret) (void)mtev_hash_retr_str(check->config, "httptrap_secret", strlen("httptrap_secret"), &secret);
    if(secret && !strcmp(pats[1], secret)) allowed = mtev_true;
    if(!allowed && cross_module_reverse_allowed(check, pats[1])) allowed = mtev_true;

    if(!allowed) {
      error = "secret mismatch";
      goto error;
    }

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
    yajl_config(rxc->parser, yajl_allow_partial_values, 1);
    restc->call_closure_free = rest_json_payload_free;
  }
  else rxc = restc->call_closure;

  /* flip threads */
  {
    mtev_http_connection *conn = mtev_http_session_connection(ctx);
    eventer_t e = mtev_http_connection_event(conn);
    if(e) {
      pthread_t tgt = CHOOSE_EVENTER_THREAD_FOR_CHECK(rxc->check);
      if(!pthread_equal(eventer_get_owner(e), tgt)) {
        eventer_set_owner(e, tgt);
        return EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
      }
    }
  }

  rxc = rest_get_json_upload(restc, &mask, &complete);
  if(rxc == NULL && !complete) return mask;

  if(!rxc) goto error;
  if(rxc->error) goto error;

  cnt = rxc->cnt;

  mtev_http_response_status_set(ctx, 200, "OK"); 
  mtev_http_response_header_set(ctx, "Content-Type", "application/json");
  mtev_http_response_option_set(ctx, MTEV_HTTP_CLOSE); 
  
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
   
  json_object *obj =  NULL;
  obj = json_object_new_object();
  /*If debugflag remains zero, simply output the number of metrics.*/
  if (debugflag==0)
  {
    json_object_object_add(obj, "stats", json_object_new_int(cnt));
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
      check = noit_poller_lookup(check_id);
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
        char buff[256], type_str[2];
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
  return 0;

 error:
  mtev_http_response_server_error(ctx, "application/json");
  mtev_http_response_append(ctx, "{ \"error\": \"", 12);
  if(rxc && rxc->error) error = rxc->error;
  mtev_http_response_append(ctx, error, strlen(error));
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

  httptrap_surrogate = mtev_false;
  if(mtev_hash_retr_str(conf->options,
                        "surrogate", strlen("surrogate"),
                        (const char **)&config_val)) {
    if(!strcasecmp(config_val, "true") || !strcasecmp(config_val, "on"))
      httptrap_surrogate = mtev_true;
  }

  noit_module_set_userdata(self, conf);

  /* register rest handler */
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
