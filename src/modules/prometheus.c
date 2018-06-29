/*
 * Copyright (c) 2018, Circonus, Inc. All rights reserved.
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
 *     * Neither the name Circonnus, Inc. nor the names
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
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>

#include <mtev_rest.h>
#include <mtev_hash.h>
#include <mtev_json.h>
#include <mtev_uuid.h>
#include <mtev_b64.h>
#include <mtev_dyn_buffer.h>

#include <snappy/snappy.h>

#include "noit_metric.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_mtev_bridge.h"
#include "prometheus.pb-c.h"

static mtev_log_stream_t nlerr = NULL;
static mtev_log_stream_t nldeb = NULL;

typedef struct _mod_config {
  mtev_hash_table *options;
} prometheus_mod_config_t;

typedef struct prometheus_closure_s {
  noit_module_t *self;
} prometheus_closure_t;

struct value_list {
  char *v;
  struct value_list *next;
};

typedef struct prometheus_upload
{
  mtev_dyn_buffer_t data;
  mtev_boolean complete;
  noit_check_t *check;
  uuid_t check_id;
} prometheus_upload_t;

#define READ_CHUNK 32768

static void *__c_allocator_alloc(void *d, size_t size) {
  return malloc(size);
}
static void __c_allocator_free(void *d, void *p) {
  free(p);
}
static ProtobufCAllocator __c_allocator = {
  .alloc = __c_allocator_alloc,
  .free = __c_allocator_free,
  .allocator_data = NULL
};
#define protobuf_c_system_allocator __c_allocator

static void
free_prometheus_upload(void *pul)
{
  prometheus_upload_t *p = (prometheus_upload_t *)pul;
  mtev_dyn_buffer_destroy(&p->data);
  free(p);
}

static prometheus_upload_t *
rest_get_upload(mtev_http_rest_closure_t *restc, int *mask, int *complete)
{

  prometheus_upload_t *rxc;
  mtev_http_request *req = mtev_http_session_request(restc->http_ctx);
  int content_length;

  content_length = mtev_http_request_content_length(req);
  rxc = (prometheus_upload_t *)restc->call_closure;

  while(!rxc->complete) {
    int len;
    mtev_dyn_buffer_ensure(&rxc->data, READ_CHUNK);
    len = mtev_http_session_req_consume(restc->http_ctx,
                                        mtev_dyn_buffer_write_pointer(&rxc->data),
                                        MIN(content_length - mtev_dyn_buffer_used(&rxc->data), READ_CHUNK),
                                        READ_CHUNK,
                                        mask);
    if(len > 0) {
      mtev_dyn_buffer_advance(&rxc->data, len);
    }
    if(len < 0 && errno == EAGAIN) return NULL;
    else if(len < 0) {
      *complete = 1;
      return NULL;
    }
    content_length = mtev_http_request_content_length(req);
    if((mtev_http_request_payload_chunked(req) && len == 0) ||
       (mtev_dyn_buffer_used(&rxc->data) == content_length)) {
      rxc->complete = 1;
    }
  }

  *complete = 1;
  return rxc;
}

static int prometheus_submit(noit_module_t *self, noit_check_t *check,
                             noit_check_t *cause)
{
  prometheus_closure_t *ccl;
  struct timeval duration;
  /* We are passive, so we don't do anything for transient checks */
  if(check->flags & NP_TRANSIENT) return 0;

  if(!check->closure) {
    ccl = check->closure = (void *)calloc(1, sizeof(prometheus_closure_t));
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

    snprintf(human_buffer, sizeof(human_buffer),
             "dur=%ld,run=%d,stats=%d", duration.tv_sec * 1000 + duration.tv_usec / 1000,
             check->generation, stats_count);
    mtevL(nldeb, "prometheus(%s) [%s]\n", check->target, human_buffer);

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

static char *
metric_name_from_labels(Prometheus__Label **labels, size_t label_count)
{
  char final_name[4096] = {0};
  char *name = final_name;
  char buffer[4096] = {0};
  char encode_buffer[512] = {0};
  char *b = buffer;
  size_t tag_count = 0;
  for (size_t i = 0; i < label_count; i++) {
    Prometheus__Label *l = labels[i];
    if (strcmp("__name__", l->name) == 0) {
      strncpy(name, l->value, sizeof(final_name) - 1);
    } else {
      if (tag_count > 0) {
        strlcat(b, ",", sizeof(buffer));
      }
      /* make base64 encoded tags out of the incoming prometheus tags for safety */
      /* TODO base64 encode these */
      size_t tl = strlen(l->name);
      mtev_b64_encode((const unsigned char *)l->name, tl, encode_buffer, sizeof(encode_buffer));

      strlcat(b, "b\"", sizeof(buffer));
      strlcat(b, encode_buffer, sizeof(buffer));
      strlcat(b, "\":b\"", sizeof(buffer));

      tl = strlen(l->value);
      mtev_b64_encode((const unsigned char *)l->value, tl, encode_buffer, sizeof(encode_buffer));

      strlcat(b, encode_buffer, sizeof(buffer));
      strlcat(b, "\"", sizeof(buffer));
      tag_count++;
    }
  }
  strlcat(name, "|ST[", sizeof(final_name));
  strlcat(name, buffer, sizeof(final_name));
  strlcat(name, "]", sizeof(final_name));

  /* we don't have to canonicalize here as reconnoiter will do that for us */
  return strdup(final_name);
}

static int
rest_prometheus_handler(mtev_http_rest_closure_t *restc, int npats, char **pats)
{
  int mask, complete = 0, cnt;
  prometheus_upload_t *rxc = NULL;
  const char *error = "internal error", *secret = NULL;
  mtev_http_session_ctx *ctx = restc->http_ctx;
  const unsigned int DEBUGDATA_OUT_SIZE=4096;
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
  if(mtev_uuid_parse(pats[0], check_id)) {
    error = "uuid parse error";
    goto error;
  }

  if(restc->call_closure == NULL) {
    mtev_boolean allowed = mtev_false;
    check = noit_poller_lookup(check_id);
    if(!check) {
      error = "no such check";
      goto error;
    }
    if(strcmp(check->module, "prometheus")) {
      error = "no such prometheus check";
      goto error;
    }

    /* check "secret"  */
    (void)mtev_hash_retr_str(check->config, "secret", strlen("secret"), &secret);
    if(secret && !strcmp(pats[1], secret)) allowed = mtev_true;
    if(!allowed && cross_module_reverse_allowed(check, pats[1])) allowed = mtev_true;

    if(!allowed) {
      error = "secret mismatch";
      goto error;
    }

    rxc = restc->call_closure = calloc(1, sizeof(*rxc));
    rxc->check = check;
    memcpy(rxc->check_id, check_id, UUID_SIZE);
    restc->call_closure_free = free_prometheus_upload;
    mtev_dyn_buffer_init(&rxc->data);
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

  rxc = rest_get_upload(restc, &mask, &complete);
  if(rxc == NULL && !complete) return mask;

  if(!rxc) {
    error = "No data?";
    goto error;
  }

  /* prometheus arrives as snappy encoded.  we must uncompress */
  mtev_dyn_buffer_t uncompressed;
  mtev_dyn_buffer_init(&uncompressed);
  size_t uncompressed_size;
  if (!snappy_uncompressed_length((const char *)mtev_dyn_buffer_data(&rxc->data), 
                                  mtev_dyn_buffer_used(&rxc->data), &uncompressed_size)) {
    error = "Cannot snappy decompress incoming prometheus data";
    mtevL(noit_error, "%s\n", error);
    goto error;
  }
  mtev_dyn_buffer_ensure(&uncompressed, uncompressed_size);
  int x = snappy_uncompress((const char *)mtev_dyn_buffer_data(&rxc->data), 
                            mtev_dyn_buffer_used(&rxc->data), 
                            (char *)mtev_dyn_buffer_write_pointer(&uncompressed));
  if (x) {
    mtev_dyn_buffer_destroy(&uncompressed);
    error = "Cannot snappy decompress incoming prometheus data";
    mtevL(noit_error, "%s, error code: %d\n", error, x);
    goto error;
  }
  mtev_dyn_buffer_advance(&uncompressed, uncompressed_size);

  /* decode prometheus protobuf */
  /* decode the protobuf */
  Prometheus__WriteRequest *write = prometheus__write_request__unpack(&protobuf_c_system_allocator, 
                                                                      mtev_dyn_buffer_used(&uncompressed),
                                                                      mtev_dyn_buffer_data(&uncompressed));
  if(!write) {
    mtev_dyn_buffer_destroy(&uncompressed);
    error = "Prometheus__WriteRequest decode: protobuf invalid";
    mtevL(noit_error, "%s\n", error);
    goto error;
  }

  cnt = write->n_timeseries;
  for (size_t i = 0; i < write->n_timeseries; i++) {
    Prometheus__TimeSeries *ts = write->timeseries[i];
    /* each timeseries has a list of labels (Tags) and a list of samples */
    char *metric_name = metric_name_from_labels(ts->labels, ts->n_labels);

    for (size_t j = 0; j < ts->n_samples; j++) {
      Prometheus__Sample *sample = ts->samples[j];
      struct timeval tv;
      tv.tv_sec = (time_t)(sample->timestamp / 1000L);
      tv.tv_usec = (suseconds_t)((sample->timestamp % 1000L) * 1000);
      noit_stats_log_immediate_metric_timed(rxc->check,
                                            metric_name,
                                            METRIC_DOUBLE,
                                            &sample->value,
                                            &tv);
    }
    free(metric_name);
  }
  prometheus__write_request__free_unpacked(write, &protobuf_c_system_allocator);
  mtev_dyn_buffer_destroy(&uncompressed);

  mtev_http_response_status_set(ctx, 200, "OK");
  mtev_http_response_option_set(ctx, MTEV_HTTP_CLOSE);

  /*Examine headers for x-circonus-prometheus-debug flag*/
  req = mtev_http_session_request(ctx);
  hdrs = mtev_http_request_headers_table(req);

  /*Check if debug header passed in. If present and set to true, set debugflag value to one.*/
  if(mtev_hash_retr_str(hdrs, "x-circonus-prometheus-debug", strlen("x-circonus-prometheus-debug"), &debugchkflag))
  {
    if (strcmp(debugchkflag,"true")==0)
    {
      debugflag=1;
    }
  }
  /*Otherwise, if set to one, output current metrics in addition to number of current metrics.*/
  if (debugflag == 1)
  {
    mtev_http_response_header_set(ctx, "Content-Type", "application/json");
    json_object *obj =  NULL;
    obj = json_object_new_object();
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
    while(mtev_hash_next(metrics, &iter, &k, &klen, &data)) {
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
    const char *json_out = json_object_to_json_string(obj);
    mtev_http_response_append(ctx, json_out, strlen(json_out));
    json_object_put(obj);
  }

  mtev_http_response_end(ctx);
  return 0;

 error:
  mtev_http_response_server_error(ctx, "application/json");
  mtev_http_response_append(ctx, "{ \"error\": \"", 12);
  mtev_http_response_append(ctx, error, strlen(error));
  mtev_http_response_append(ctx, "\" }", 3);
  mtev_http_response_end(ctx);
  return 0;
}

static int noit_prometheus_initiate_check(noit_module_t *self,
                                          noit_check_t *check,
                                          int once, noit_check_t *cause) {
  check->flags |= NP_PASSIVE_COLLECTION;
  if (check->closure == NULL) {
    prometheus_closure_t *ccl;
    ccl = check->closure = (void *)calloc(1, sizeof(prometheus_closure_t));
    ccl->self = self;
  }
  INITIATE_CHECK(prometheus_submit, self, check, cause);
  return 0;
}

static int noit_prometheus_config(noit_module_t *self, mtev_hash_table *options) {
  prometheus_mod_config_t *conf;
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

static int noit_prometheus_onload(mtev_image_t *self) {
  if(!nlerr) nlerr = mtev_log_stream_find("error/prometheus");
  if(!nldeb) nldeb = mtev_log_stream_find("debug/prometheus");
  if(!nlerr) nlerr = noit_error;
  if(!nldeb) nldeb = noit_debug;
  return 0;
}

static int noit_prometheus_init(noit_module_t *self) {
  prometheus_mod_config_t *conf = noit_module_get_userdata(self);

  noit_module_set_userdata(self, conf);

  /* register rest handler */
  mtev_http_rest_register("PUT", "/module/prometheus/",
                          "^(" UUID_REGEX ")/([^/]*).*$",
                          rest_prometheus_handler);
  mtev_http_rest_register("POST", "/module/prometheus/",
                          "^(" UUID_REGEX ")/([^/]*).*$",
                          rest_prometheus_handler);
  return 0;
}

#include "prometheus.xmlh"
noit_module_t prometheus = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "prometheus",
    .description = "prometheus collection",
    .xml_description = prometheus_xml_description,
    .onload = noit_prometheus_onload
  },
  noit_prometheus_config,
  noit_prometheus_init,
  noit_prometheus_initiate_check,
  NULL
};
