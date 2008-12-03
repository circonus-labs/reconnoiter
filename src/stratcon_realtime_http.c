/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "noit_conf.h"
#include "utils/noit_hash.h"
#include "utils/noit_log.h"
#include "jlog/jlog.h"
#include "noit_jlog_listener.h"
#include "noit_listener.h"c
#include "noit_http.h"

int
stratcon_request_dispatcher(noit_http_session_ctx *ctx) {
  const char *key, *value;
  int klen;
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  noit_http_request *req = &ctx->req;

  noitL(noit_error, "http: %s %s %s\n",
        req->method_str, req->uri_str, req->protocol_str);
  while(noit_hash_next(&req->headers, &iter, &key, &klen, (void **)&value)) {
    noitL(noit_error, "http: [%s: %s]\n", key, value);
  }
  noit_http_response_status_set(ctx, 200, "OK");
  noit_http_response_option_set(ctx, NOIT_HTTP_CHUNKED);
  noit_http_response_append(ctx, "Hello there!", 12);
  noit_http_response_flush(ctx, false);
  noit_http_response_append(ctx, "Hello there again!", 18);
  noit_http_response_end(ctx);
  ctx->conn.needs_close = noit_true;
  return 0;
}

int
stratcon_realtime_http_handler(eventer_t e, int mask, void *closure,
                               struct timeval *now) {
  acceptor_closure_t *ac = closure;
  noit_http_session_ctx *http_ctx = ac->service_ctx;
  if(!http_ctx) {
    http_ctx = ac->service_ctx =
      noit_http_session_ctx_new(stratcon_request_dispatcher, e);
  }
  return http_ctx->drive(e, mask, http_ctx, now);
}

void
stratcon_realtime_http_init(const char *toplevel) {
  eventer_name_callback("stratcon_realtime_http",
                        stratcon_realtime_http_handler);
}
