/*
 * Copyright (c) 2016, Circonus, Inc. All rights reserved.
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
 *     * Neither the name Circonus, Inc. nor the names
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
#include <eventer/eventer.h>
#include <mtev_listener.h>
#include <mtev_memory.h>
#include <mtev_sem.h>
#include <mtev_rest.h>
#include <mtev_json_tokener.h>
#include <mtev_json_object.h>
#include <mtev_arraylist.h>

#include "noit_check.h"
#include "noit_check_log_helpers.h"
#include "noit_message_decoder.h"
#include "noit_mtev_bridge.h"
#include "noit_websocket_handler.h"

#ifdef HAVE_WSLAY
#include <wslay/wslay.h>
#endif

#include <unistd.h>
#include <errno.h>

static mtev_atomic32_t ls_counter = 0;

typedef struct {
  char **filters;
  int filter_count;
  mtev_boolean use_filter;
  mtev_http_rest_closure_t *restc;
  char uuid_str[37];
  char *feed;
  uint32_t period;
  uuid_t uuid;
  noit_check_t *check;
  int wants_shutdown;
  mtev_log_stream_t log_stream;
} noit_websocket_closure_t;

noit_websocket_closure_t *
noit_websocket_closure_alloc(void) {
  noit_websocket_closure_t *jcl;
  jcl = calloc(1, sizeof(*jcl));
  return jcl;
}

void
noit_websocket_closure_free(void *jcl) {
  noit_websocket_closure_t *w = jcl;
  noit_check_transient_remove_feed(w->check, w->feed);
  free(w->feed);

  mtev_log_stream_close(w->log_stream);
  mtev_log_stream_free(w->log_stream);

  for (int i = 0; i < w->filter_count; i++) {
    free(w->filters[i]);
  }
  free(w->filters);

  free(w);
}

static void
send_individual_metric(noit_websocket_closure_t *wcl, const char *metric_string, size_t len)
{
#ifdef HAVE_WSLAY
  noit_metric_message_t message;
  char *json = NULL;
  size_t json_len = 0;

  int rval = noit_message_decoder_parse_line(metric_string, len, &message.id.id, &message.id.name,
                                             &message.id.name_len, NULL, NULL, &message.value, mtev_false);
  if (rval < 0) {
    return;
  }

  message.type = metric_string[0];

  if (wcl->use_filter == mtev_true) {
    for (int i = 0; i < wcl->filter_count; i++) {
      if (message.id.name_len > 0 &&
        strncmp(wcl->filters[i], message.id.name, message.id.name_len) == 0) {
        noit_metric_to_json(&message, &json, &json_len, mtev_false);
        mtev_http_websocket_queue_msg(wcl->restc->http_ctx,
                                      WSLAY_TEXT_FRAME,
                                      (const unsigned char *)json, json_len);
        free(json);
        break;
      }
    }
  } else {
        noit_metric_to_json(&message, &json, &json_len, mtev_false);
        mtev_http_websocket_queue_msg(wcl->restc->http_ctx,
                                      WSLAY_TEXT_FRAME,
                                      (const unsigned char *)json, json_len);
        free(json);
  }
#endif
}


static void
filter_and_send(noit_websocket_closure_t *wcl, const char *buf, size_t len)
{
  char **out = NULL;
  int count = 0;

  if (buf == NULL || len == 0) {
    return;
  }

  if (buf[0] == 'B') {
    count = noit_check_log_b_to_sm(buf, len, &out, 0);
    for (int i = 0; i < count; i++) {
      send_individual_metric(wcl, out[i], strlen(out[i]));
      free(out[i]);
    }
    free(out);
  } else {
    send_individual_metric(wcl, buf, len);
  }
}

static int
noit_websocket_logio_open(mtev_log_stream_t ls) {
  return 0;
}

static int
noit_websocket_logio_reopen(mtev_log_stream_t ls) {
  /* no op */
  return 0;
}

static int
noit_websocket_logio_write(mtev_log_stream_t ls, const struct timeval *whence,
                            const void *buf, size_t len) {
  noit_websocket_closure_t *jcl;
  (void)whence;

  jcl = mtev_log_stream_get_ctx(ls);
  if(!jcl) return 0;

  if(jcl->wants_shutdown) {
    /* This has been terminated by the client, _fail here_ */
    return 0;
  }

  /* the send side of the websocket is already handled via queueing
   * so there is no need to spawn a thread to deal with IO
   */
  filter_and_send(jcl, buf, len);

  return len;
}

static int
noit_websocket_logio_close(mtev_log_stream_t ls) {
  mtev_log_stream_set_ctx(ls, NULL);
  return 0;
}

static logops_t noit_websocket_logio_ops = {
  mtev_false,
  noit_websocket_logio_open,
  noit_websocket_logio_reopen,
  noit_websocket_logio_write,
  NULL,
  noit_websocket_logio_close,
  NULL,
  NULL
};

void
noit_websocket_handler_init() {
  mtev_register_logops("noit_websocket_livestream", &noit_websocket_logio_ops);
  int rval = mtev_http_rest_websocket_register(NOIT_WEBSOCKET_DATA_FEED_PATH, "^(.*)$", NOIT_WEBSOCKET_DATA_FEED_PROTOCOL,
                                                    noit_websocket_msg_handler);
  if (rval == -1) {
    mtevFatal(mtev_error, "Unabled to register websocket handler for /livestream/");
  }
}

/**
 * The "noit_livestream" protocol requires a well formed request to come
 * in via websocket that resembles:
 *
 * {
 *   "period_ms": <milliseconds>,
 *   "check_uuid": "<uuid_string>",
 *   "metrics" : ["<metric_name1>", "<metric_name2>"]
 * }
 *
 * There can be only one active livestream request for a single websocket at one time.
 * However, you can send down a new livestream request at any time to alter the
 * stream based on new requirements.
 *
 * If the "metrics" member exists in the incoming request, then the metrics that are sent will be
 * filtered to those that match the strings in the array by exact match.  If you want to get
 * all metrics in the check, omit the "metrics" member from the request object.
 */
int
noit_websocket_msg_handler(mtev_http_rest_closure_t *restc, int opcode,
                           const unsigned char *msg, size_t msg_len)
{
#ifdef HAVE_WSLAY
  const char *error = NULL;
  noit_websocket_closure_t *handler_data = restc->call_closure;

  /* for now this message is JSON, that might change in the future */

  struct mtev_json_tokener *tok = mtev_json_tokener_new();
  struct mtev_json_object *request = mtev_json_tokener_parse_ex(tok, (const char *)msg, msg_len);
  enum mtev_json_tokener_error err = tok->err;
  mtev_json_tokener_free(tok);
  if (err != mtev_json_tokener_success) {
    error = "Unable to parse incoming json";
    goto websocket_handler_error;
  }

  uint64_t period_ms = mtev_json_object_get_uint64(mtev_json_object_object_get(request, "period_ms"));
  const char *check_uuid = mtev_json_object_get_string(mtev_json_object_object_get(request, "check_uuid"));
  struct mtev_json_object *metrics = mtev_json_object_object_get(request, "metrics");

  if (handler_data != NULL) {
    /* destroy old subscription */
    noit_websocket_closure_free(handler_data);
    handler_data = NULL;
  }

  handler_data = restc->call_closure = noit_websocket_closure_alloc();
  handler_data->use_filter = mtev_false;
  restc->call_closure_free = noit_websocket_closure_free;
  handler_data->period = period_ms;
  if (metrics != NULL) {
    handler_data->use_filter = mtev_true;
    handler_data->filter_count = mtev_json_object_array_length(metrics);
    handler_data->filters = calloc(sizeof(char *), handler_data->filter_count);
    jl_array_list* a = mtev_json_object_get_array(metrics);
    for (int i = 0; i < handler_data->filter_count; i++) {
      struct mtev_json_object *o = jl_array_list_get_idx(a, i);
      if (o != NULL) {
        handler_data->filters[i] = strdup(mtev_json_object_get_string(o));
      }
    }
  }

  handler_data->restc = restc;

  strncpy(handler_data->uuid_str, check_uuid, UUID_STR_LEN);
  handler_data->uuid_str[36] = '\0';
  if(uuid_parse(handler_data->uuid_str, handler_data->uuid)) {
    mtevL(noit_error, "bad uuid received in livestream websocket_handler '%s'\n", handler_data->uuid_str);
    error = "bad uuid received in livestream subscription request";
    goto websocket_handler_error;
  }
  mtev_json_object_put(request);

  /* setup subscription to noit_livestream */
  asprintf(&handler_data->feed, "websocket_livestream/%d", mtev_atomic_inc32(&ls_counter));
  handler_data->log_stream = mtev_log_stream_new(handler_data->feed, "noit_websocket_livestream", handler_data->feed,
                                                 handler_data, NULL);

  handler_data->check = noit_check_watch(handler_data->uuid, handler_data->period);
  if(!handler_data->check) {
    error = "Cannot locate check";
    goto websocket_handler_error;
  }

  /* This check must be watched from the livestream */
  noit_check_transient_add_feed(handler_data->check, handler_data->feed);

  /* Note the check */
  noit_check_log_check(handler_data->check);

  /* kick it off, if it isn't running already */
  if(!NOIT_CHECK_LIVE(handler_data->check)) noit_check_activate(handler_data->check);

  return 0;

 websocket_handler_error:
  {
    struct mtev_json_object *e = mtev_json_object_new_object();
    mtev_json_object_object_add(e, "success", mtev_json_object_new_int(0));
    struct mtev_json_object *errors = mtev_json_object_new_array();
    struct mtev_json_object *error_o = mtev_json_object_new_object();
    mtev_json_object_object_add(error_o, "error", mtev_json_object_new_string(error));
    mtev_json_object_array_add(errors, error_o);
    mtev_json_object_object_add(e, "errors", errors);

    const char *json_error = mtev_json_object_to_json_string(e);
    mtev_http_websocket_queue_msg(restc->http_ctx,
                                  WSLAY_TEXT_FRAME | WSLAY_CONNECTION_CLOSE,
                                  (const unsigned char *)json_error, strlen(json_error));
    mtev_json_object_put(e);
  }
#endif
  return -1;
}
