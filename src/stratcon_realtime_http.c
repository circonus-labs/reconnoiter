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

typedef struct {
  int setup;
} realtime_context;

static realtime_context *alloc_realtime_context() {
  realtime_context *ctx;
  return calloc(sizeof(*ctx), 1);
}
int
stratcon_realtime_ticker(eventer_t old, int mask, void *closure,
                         struct timeval *now) {
  int f;
  char buffer[100];
  noit_http_session_ctx *ctx = closure;
  f = rand() % 10;

  if(!f) {
    noit_http_response_end(ctx);
    memset(ctx->dispatcher_closure, 0, sizeof(realtime_context));
    if(ctx->conn.e) eventer_trigger(ctx->conn.e, EVENTER_WRITE);
    return 0;
  }

  eventer_t e = eventer_alloc();
  gettimeofday(&e->whence, NULL);
  snprintf(buffer, sizeof(buffer), "[%lu] Ticker...<br />\n", e->whence.tv_sec);
  noit_http_response_append(ctx, buffer, strlen(buffer));
  noit_http_response_flush(ctx, false);

  fprintf(stderr, " Next tick in %d seconds\n", f);
  e->mask = EVENTER_TIMER;
  e->whence.tv_sec += f;
  e->callback = stratcon_realtime_ticker;
  e->closure = closure;
  eventer_add(e);
  return 0;
}
int
stratcon_request_dispatcher(noit_http_session_ctx *ctx) {
  const char *key, *value;
  realtime_context *rc = ctx->dispatcher_closure;
  int klen;
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  noit_http_request *req = &ctx->req;

  if(strcmp(ctx->req.uri_str, "/data")) {
    noit_http_response_status_set(ctx, 404, "OK");
    noit_http_response_option_set(ctx, NOIT_HTTP_CLOSE);
    noit_http_response_end(ctx);
    return 0;
  }
  if(!rc->setup) {
    const char *c = "<html><body><div id=\"foo\">Here</div>\n";
    noitL(noit_error, "http: %s %s %s\n",
          req->method_str, req->uri_str, req->protocol_str);
    while(noit_hash_next(&req->headers, &iter, &key, &klen, (void **)&value)) {
      noitL(noit_error, "http: [%s: %s]\n", key, value);
    }
    noit_http_response_status_set(ctx, 200, "OK");
    noit_http_response_option_set(ctx, NOIT_HTTP_CHUNKED);
    noit_http_response_option_set(ctx, NOIT_HTTP_DEFLATE);
    noit_http_response_header_set(ctx, "Content-Type", "text/html");
    noit_http_response_append(ctx, c, strlen(c));
    noit_http_response_flush(ctx, false);
    stratcon_realtime_ticker(NULL, 0, ctx, NULL);
    rc->setup = 1;
  }
  return EVENTER_EXCEPTION;
}

int
stratcon_realtime_http_handler(eventer_t e, int mask, void *closure,
                               struct timeval *now) {
  acceptor_closure_t *ac = closure;
  noit_http_session_ctx *http_ctx = ac->service_ctx;
  if(!http_ctx) {
    http_ctx = ac->service_ctx =
      noit_http_session_ctx_new(stratcon_request_dispatcher,
                                alloc_realtime_context(),
                                e);
  }
  return http_ctx->drive(e, mask, http_ctx, now);
}

void
stratcon_realtime_http_init(const char *toplevel) {
  eventer_name_callback("stratcon_realtime_http",
                        stratcon_realtime_http_handler);
}
