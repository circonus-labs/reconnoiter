/*
 * Copyright (c) 2011, OmniTI Computer Consulting, Inc.
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
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include "noit_defines.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <math.h>
#include <ctype.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_rest.h"
#include "json-lib/json.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"


static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;

typedef struct _mod_config {
  noit_hash_table *options;
  noit_boolean asynch_metrics;
} httptrap_mod_config_t;

typedef struct httptrap_closure_s {
  noit_module_t *self;
  stats_t current;
  int stats_count;
} httptrap_closure_t;

struct rest_json_payload {
  struct json_tokener *tok;
  struct json_object *root;
  int len;
  int complete;
  char *error;
  int nput;
};

static void
rest_json_payload_free(void *f) {
  struct rest_json_payload *json = f;
  if(json->tok) json_tokener_free(json->tok);
  if(json->root) json_object_put(json->root);
  if(json->error) free(json->error);
  free(json);
}

static struct rest_json_payload *
rest_get_json_upload(noit_http_rest_closure_t *restc,
                    int *mask, int *complete) {
  struct rest_json_payload *rxc;
  noit_http_request *req = noit_http_session_request(restc->http_ctx);
  int content_length;
  char buffer[32768];

  content_length = noit_http_request_content_length(req);
  if(restc->call_closure == NULL) {
    rxc = restc->call_closure = calloc(1, sizeof(*rxc));
    rxc->tok = json_tokener_new();
    restc->call_closure_free = rest_json_payload_free;
  }
  rxc = restc->call_closure;
  while(!rxc->complete) {
    int len;
    len = noit_http_session_req_consume(
            restc->http_ctx, buffer,
            MIN(content_length - rxc->len, sizeof(buffer)),
            mask);
    if(len > 0) {
      struct json_object *o;
      o = json_tokener_parse_ex(rxc->tok, buffer, len);
      rxc->len += len;
      if(!is_error(o)) {
        rxc->root = o;
      }
    }
    if(len < 0 && errno == EAGAIN) return NULL;
    else if(len < 0) {
      *complete = 1;
      return NULL;
    }
    if(rxc->len == content_length) {
      rxc->complete = 1;
    }
  }

  *complete = 1;
  return rxc;
}

static noit_boolean
noit_httptrap_check_aynsch(noit_module_t *self,
                           noit_check_t *check) {
  const char *config_val;
  httptrap_mod_config_t *conf = noit_module_get_userdata(self);
  noit_boolean is_asynch = conf->asynch_metrics;
  if(noit_hash_retr_str(check->config,
                        "asynch_metrics", strlen("asynch_metrics"),
                        (const char **)&config_val)) {
    if(!strcasecmp(config_val, "false") || !strcasecmp(config_val, "off"))
      is_asynch = noit_false;
  }

  if(is_asynch) check->flags |= NP_SUPPRESS_METRICS;
  else check->flags &= ~NP_SUPPRESS_METRICS;
  return is_asynch;
}

static void clear_closure(httptrap_closure_t *ccl) {
  ccl->stats_count = 0;
  noit_check_stats_clear(&ccl->current);
}

static int httptrap_submit(noit_module_t *self, noit_check_t *check,
                           noit_check_t *cause) {
  httptrap_closure_t *ccl;
  struct timeval duration;
  /* We are passive, so we don't do anything for transient checks */
  if(check->flags & NP_TRANSIENT) return 0;

  noit_httptrap_check_aynsch(self, check);
  if(!check->closure) {
    ccl = check->closure = (void *)calloc(1, sizeof(httptrap_closure_t)); 
    memset(ccl, 0, sizeof(httptrap_closure_t));
  } else {
    // Don't count the first run
    char human_buffer[256];
    ccl = (httptrap_closure_t*)check->closure;
    gettimeofday(&ccl->current.whence, NULL);
    sub_timeval(ccl->current.whence, check->last_fire_time, &duration);
    ccl->current.duration = duration.tv_sec * 1000 + duration.tv_usec / 1000;

    snprintf(human_buffer, sizeof(human_buffer),
             "dur=%d,run=%d,stats=%d", ccl->current.duration,
             check->generation, ccl->stats_count);
    noitL(nldeb, "httptrap(%s) [%s]\n", check->target, human_buffer);

    // Not sure what to do here
    ccl->current.available = (ccl->stats_count > 0) ?
        NP_AVAILABLE : NP_UNAVAILABLE;
    ccl->current.state = (ccl->stats_count > 0) ?
        NP_GOOD : NP_BAD;
    ccl->current.status = human_buffer;
    if(check->last_fire_time.tv_sec)
      noit_check_passive_set_stats(self, check, &ccl->current);

    memcpy(&check->last_fire_time, &ccl->current.whence, sizeof(duration));
  }
  clear_closure(ccl);
  return 0;
}

static int
json_parse_descent(noit_check_t *check, noit_boolean immediate,
                   json_object *o, char *key) {
  char subkey[256];
  httptrap_closure_t *ccl;
  int cnt = 0;

#define setstat(key, mt, v) do { \
  cnt++; \
  noit_stats_set_metric(&ccl->current, key, mt, v); \
  if(immediate) noit_stats_log_immediate_metric(check, key, mt, v); \
} while(0)

  ccl = check->closure;
  switch(json_object_get_type(o)) {
    case json_type_array: {
        int i, alen = json_object_array_length(o);
        for(i=0;i<alen;i++) {
          snprintf(subkey, sizeof(subkey), "%s%s%d", key ? key : "",
                   (key && *key) ? "`" : "", i);
          cnt += json_parse_descent(check, immediate,
                                    json_object_array_get_idx(o,i), subkey);
        }
      }
      break;

    case json_type_object: {
        char *ekey;
        struct lh_table *table;
        struct lh_entry *entry;
        struct json_object *val;
        table = json_object_get_object(o);
        if(table->count == 2) {
          /* this is the special key: { _type: , _value: } notation */
          json_object *type;
          type = json_object_object_get(o, "_type");
          val = json_object_object_get(o, "_value");
          if(type && json_object_is_type(type, json_type_string) &&
             val && (json_object_is_type(val, json_type_string) ||
                     json_object_is_type(val, json_type_null))) {
            const char *type_str = json_object_get_string(type);
            const char *val_str = json_object_is_type(val, json_type_null) ? NULL : json_object_get_string(val);
            if(type_str[1] == '\0') {
              int32_t __i, *i = &__i;
              u_int32_t __I, *I = &__I;
              int64_t __l, *l = &__l;
              u_int64_t __L, *L = &__L;
              double __n, *n = &__n;
              if(val_str == NULL)
                i = NULL, I = NULL, l = NULL, L = NULL, n = NULL;
              switch(*type_str) {
                case 'i': if(val_str) __i = strtol(val_str, NULL, 10);
                          setstat(key, METRIC_INT32, i); break;
                case 'I': if(val_str) __I = strtoul(val_str, NULL, 10);
                          setstat(key, METRIC_UINT32, I); break;
                case 'l': if(val_str) __l = strtoll(val_str, NULL, 10);
                          setstat(key, METRIC_INT64, I); break;
                case 'L': if(val_str) __L = strtoull(val_str, NULL, 10);
                          setstat(key, METRIC_UINT64, I); break;
                case 'n': if(val_str) __n = strtod(val_str, NULL);
                          setstat(key, METRIC_DOUBLE, n); break;
                case 's': setstat(key, METRIC_STRING, (void *)val_str); break;
                default: break;
              }
            }
            break;
          }
        }
        for(entry = table->head;
            (entry ? (ekey = (char*)entry->k,
               val = (struct json_object*)entry->v, entry) : 0);
            entry = entry->next) {
          snprintf(subkey, sizeof(subkey), "%s%s%s", key ? key : "",
                   (key && *key) ? "`" : "", ekey);
          cnt += json_parse_descent(check, immediate, val, subkey);
        }
      }
      break;

    case json_type_null: {
        if(!key || !*key) break;
        break;
      }
      break;
    case json_type_boolean: {
        if(!key || !*key) break;
        int32_t value = json_object_get_boolean(o) ? 1 : 0;
        setstat(key, METRIC_INT32, &value);
      }
      break;
    case json_type_double: {
        if(!key || !*key) break;
        double value = json_object_get_double(o);
        setstat(key, METRIC_DOUBLE, &value);
      }
      break;
    case json_type_int: {
      if(!key || !*key) break;
      int32_t value = json_object_get_int(o);
      setstat(key, METRIC_INT32, &value);
    }
    case json_type_string: {
        if(!key || !*key) break;
        const char *val = json_object_get_string(o);
        setstat(key, METRIC_GUESS, (void *)val);
      }
      break;
  }
  return cnt;
}
static int
push_payload_at_check(noit_check_t *check, json_object *root) {
  httptrap_closure_t *ccl;
  noit_boolean immediate;
  char key[256];
  int cnt;

  if (check->closure == NULL) return 0;
  ccl = check->closure;
  if (!check || strcmp(check->module, "httptrap")) return 0;
  immediate = noit_httptrap_check_aynsch(ccl->self,check);

  /* do it here */
  key[0] = '\0';
  cnt = json_parse_descent(check, immediate, root, key);
  ccl->stats_count += cnt;
  return cnt;
}

static int
rest_httptrap_handler(noit_http_rest_closure_t *restc,
                      int npats, char **pats) {
  int mask, complete = 0, cnt;
  struct rest_json_payload *rxc = NULL;
  const char *error = "internal error", *secret = NULL;
  noit_http_session_ctx *ctx = restc->http_ctx;
  char json_out[128];
  noit_check_t *check;
  uuid_t check_id;

  if(npats != 2) {
    error = "bad uri";
    goto error;
  }
  if(uuid_parse(pats[0], check_id)) {
    error = "uuid parse error";
    goto error;
  }

  rxc = rest_get_json_upload(restc, &mask, &complete);
  if(rxc == NULL && !complete) return mask;

  check = noit_poller_lookup(check_id);
  if(!check || strcmp(check->module, "httptrap")) {
    error = "no such httptrap check";
    goto error;
  }
  noit_hash_retr_str(check->config, "secret", strlen("secret"), &secret);
  if(!secret) secret = "";
  if(strcmp(pats[1], secret)) {
    error = "secret mismatch";
    goto error;
  }

  if(!rxc) goto error;
  if(!rxc->root) {
    error = "parse failure";
    goto error;
  }
  if(rxc->error) goto error;

  cnt = push_payload_at_check(check, rxc->root);

  noit_http_response_ok(ctx, "application/json");
  snprintf(json_out, sizeof(json_out),
           "{ \"stats\": %d }", cnt);
  noit_http_response_append(ctx, json_out, strlen(json_out));
  noit_http_response_end(ctx);
  return 0;

 error:
  noit_http_response_server_error(ctx, "application/json");
  noit_http_response_append(ctx, "{ error: \"", 10);
  if(rxc && rxc->error) error = rxc->error;
  noit_http_response_append(ctx, error, strlen(error));
  noit_http_response_append(ctx, "\" }", 3);
  noit_http_response_end(ctx);
  return 0;
}

static int noit_httptrap_initiate_check(noit_module_t *self,
                                        noit_check_t *check,
                                        int once, noit_check_t *cause) {
  if (check->closure == NULL) {
    httptrap_closure_t *ccl;
    ccl = check->closure = (void *)calloc(1, sizeof(httptrap_closure_t));
    ccl->self = self;
  }
  INITIATE_CHECK(httptrap_submit, self, check, cause);
  return 0;
}

static int noit_httptrap_config(noit_module_t *self, noit_hash_table *options) {
  httptrap_mod_config_t *conf;
  conf = noit_module_get_userdata(self);
  if(conf) {
    if(conf->options) {
      noit_hash_destroy(conf->options, free, free);
      free(conf->options);
    }
  }
  else
    conf = calloc(1, sizeof(*conf));
  conf->options = options;
  noit_module_set_userdata(self, conf);
  return 1;
}

static int noit_httptrap_onload(noit_image_t *self) {
  if(!nlerr) nlerr = noit_log_stream_find("error/httptrap");
  if(!nldeb) nldeb = noit_log_stream_find("debug/httptrap");
  if(!nlerr) nlerr = noit_error;
  if(!nldeb) nldeb = noit_debug;
  return 0;
}

static int noit_httptrap_init(noit_module_t *self) {
  const char *config_val;
  httptrap_mod_config_t *conf;
  conf = noit_module_get_userdata(self);

  conf->asynch_metrics = noit_true;
  if(noit_hash_retr_str(conf->options,
                        "asynch_metrics", strlen("asynch_metrics"),
                        (const char **)&config_val)) {
    if(!strcasecmp(config_val, "false") || !strcasecmp(config_val, "off"))
      conf->asynch_metrics = noit_false;
  }

  noit_module_set_userdata(self, conf);

  /* register rest handler */
  noit_http_rest_register("PUT", "/module/httptrap/",
                          "^(" UUID_REGEX ")/([^/]*)$",
                          rest_httptrap_handler);
  return 0;
}

#include "httptrap.xmlh"
noit_module_t httptrap = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "httptrap",
    "httptrap collection",
    httptrap_xml_description,
    noit_httptrap_onload
  },
  noit_httptrap_config,
  noit_httptrap_init,
  noit_httptrap_initiate_check,
  NULL
};
