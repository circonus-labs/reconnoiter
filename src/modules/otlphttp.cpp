/*
 * Copyright (c) 2022-2023, Circonus, Inc. All rights reserved.
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

#include "otlp.hpp"

otlp_mod_config *make_new_mod_config()
{
  return new otlp_mod_config;
}

static otlp_upload *
rest_get_upload(mtev_http_rest_closure_t *restc, int *mask, int *complete)
{

  otlp_upload *rxc;
  mtev_http_request *req = mtev_http_session_request(restc->http_ctx);

  rxc = (otlp_upload *)restc->call_closure;

  while(!rxc->complete) {
    int len;
    mtev_dyn_buffer_ensure(&rxc->data, READ_CHUNK);
    len = mtev_http_session_req_consume(restc->http_ctx,
                                        mtev_dyn_buffer_write_pointer(&rxc->data), READ_CHUNK,
                                        READ_CHUNK,
                                        mask);
    if(len > 0) {
      mtev_dyn_buffer_advance(&rxc->data, len);
    }
    if(len < 0 && errno == EAGAIN) return nullptr;
    else if(len < 0) {
      *complete = 1;
      return nullptr;
    }
    if(len == 0 && mtev_http_request_payload_complete(req)) {
      rxc->complete = mtev_true;
    }
  }

  *complete = 1;
  return rxc;
}

static int
rest_otlphttp_handler(mtev_http_rest_closure_t *restc, int npats, char **pats)
{
  int mask, complete = 0, cnt = 0;
  otlp_upload *rxc{nullptr};
  const char *error = "internal error", *secret{nullptr};
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

  if(!restc->call_closure) {
    mtev_boolean allowed = mtev_false;
    check = noit_poller_lookup(check_id);
    if(!check) {
      error = "no such check";
      goto error;
    }
    if(strcmp(check->module, "otlphttp")) {
      error = "no such otlphttp check";
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

    mtevL(nldeb_verbose, "[otlphttp] new http payload for %s\n", pats[0]);
    rxc = new otlp_upload(check);
    if(const char *mode_str = mtev_hash_dict_get(check->config, "hist_approx_mode")) {
      if(!strcmp(mode_str, "low")) rxc->mode = HIST_APPROX_LOW;
      else if(!strcmp(mode_str, "mid")) rxc->mode = HIST_APPROX_MID;
      else if(!strcmp(mode_str, "harmonic_mean")) rxc->mode = HIST_APPROX_HARMONIC_MEAN;
      else if(!strcmp(mode_str, "high")) rxc->mode = HIST_APPROX_HIGH;
      // Else it just sticks the with initial defaults */
    }
    restc->call_closure = static_cast<void *>(rxc);
    restc->call_closure_free = free_otlp_upload;
  }
  else rxc = static_cast<otlp_upload*>(restc->call_closure);

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
  if (!rxc && !complete) return mask;

  if(!rxc) {
    error = "No data?";
    goto error;
  }
  
  if(OtelCollectorMetrics::ExportMetricsServiceRequest msg;
     msg.ParseFromArray(mtev_dyn_buffer_data(&rxc->data),
                        static_cast<int>(mtev_dyn_buffer_used(&rxc->data)))) {
    mtev_memory_begin();
    mtevL(nldeb_verbose, "[otlphttp] http payload %zu bytes\n", mtev_dyn_buffer_used(&rxc->data));
    handle_message(rxc, msg);
    metric_local_batch_flush_immediate(rxc);
    mtev_memory_end();
  }
  else {
    mtevL(nlerr, "[otlphttp] error parsing http input %zu bytes\n",
          mtev_dyn_buffer_used(&rxc->data));
    error = "cannot parse protobuf";
    goto error;
  }

  mtev_http_response_status_set(ctx, 200, "OK");
  mtev_http_response_option_set(ctx, MTEV_HTTP_CLOSE);

  /*Examine headers for x-circonus-otlphttp-debug flag*/
  req = mtev_http_session_request(ctx);
  hdrs = mtev_http_request_headers_table(req);

  /*Check if debug header passed in. If present and set to true, set debugflag value to one.*/
  if(mtev_hash_retr_str(hdrs, "x-circonus-otlphttp-debug", strlen("x-circonus-otlphttp-debug"), &debugchkflag))
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
    json_object *obj{nullptr};
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
  mtev_http_response_append(ctx, "{ \"Status\": \"", 13);
  mtev_http_response_append(ctx, error, strlen(error));
  mtevL(nlerr, "[otlphttp] ERROR during http processing: %s\n", error);
  mtev_http_response_append(ctx, "\" }", 3);
  mtev_http_response_end(ctx);
  return 0;
}

static int noit_otlphttp_onload(mtev_image_t *self) {
  if (!nlerr) nlerr = mtev_log_stream_find("error/otlphttp");
  if (!nldeb) nldeb = mtev_log_stream_find("debug/otlphttp");
  if (!nldeb_verbose) nldeb_verbose = mtev_log_stream_find("debug/otlphttp_verbose");
  if (!nlerr) nlerr = noit_error;
  if (!nldeb) nldeb = noit_debug;
  return 0;
}

static int noit_otlphttp_init(noit_module_t *self) {
  const char *config_val;
  otlp_mod_config *conf = static_cast<otlp_mod_config*>(noit_module_get_userdata(self));

  noit_module_set_userdata(self, conf);

  eventer_pool_t *dp = noit_check_choose_pool_by_module(self->hdr.name);
  /* register rest handler */
  mtev_rest_mountpoint_t *rule;
  rule = mtev_http_rest_new_rule("POST", "/module/otlphttp/v1/",
                                "^(" UUID_REGEX ")/([^/]*)$",
                                rest_otlphttp_handler);
  if(dp) mtev_rest_mountpoint_set_eventer_pool(rule, dp);
  rule = mtev_http_rest_new_rule("POST", "/module/otlphttp/",
                                "^(" UUID_REGEX ")/([^/]*)/v1/metrics",
                                rest_otlphttp_handler);
  if(dp) mtev_rest_mountpoint_set_eventer_pool(rule, dp);

  mtevL(nldeb, "[otlphttp] REST endpoint now active\n");

  return 0;
}

#include "otlphttp.xmlh"
noit_module_t otlphttp = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "otlphttp",
    .description = "otlphttp collection",
    .xml_description = otlphttp_xml_description,
    .onload = noit_otlphttp_onload
  },
  noit_otlp_config,
  noit_otlphttp_init,
  noit_otlp_initiate_check,
  nullptr
};
