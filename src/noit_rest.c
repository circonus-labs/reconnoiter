/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
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
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "noit_defines.h"
#include "noit_listener.h"
#include "noit_http.h"
#include "noit_rest.h"

static noit_http_rest_closure_t *
noit_http_rest_closure_alloc() {
  noit_http_rest_closure_t *restc;
  restc = calloc(1, sizeof(*restc));
  return restc;
}
static void
noit_http_rest_closure_free(noit_http_rest_closure_t *restc) {
  free(restc);
}

int
noit_rest_request_dispatcher(noit_http_session_ctx *ctx) {
  noit_http_response_status_set(ctx, 200, "OK");
  noit_http_response_header_set(ctx, "Content-Type", "application/json");
  noit_http_response_option_set(ctx, NOIT_HTTP_CHUNKED);
  noit_http_response_option_set(ctx, NOIT_HTTP_DEFLATE);
  noit_http_response_append(ctx, "{error: 'Foo'}", 14);
  noit_http_response_flush(ctx, noit_false);
  noit_http_response_end(ctx);
  return 0;
}

int
noit_http_rest_handler(eventer_t e, int mask, void *closure,
                       struct timeval *now) {
  int newmask = EVENTER_READ | EVENTER_EXCEPTION;
  acceptor_closure_t *ac = closure;
  noit_http_rest_closure_t *restc = ac->service_ctx;

  if(mask & EVENTER_EXCEPTION || (restc && restc->wants_shutdown)) {
socket_error:
    /* Exceptions cause us to simply snip the connection */
    eventer_remove_fd(e->fd);
    e->opset->close(e->fd, &newmask, e);
    if(restc) noit_http_rest_closure_free(restc);
    if(ac) acceptor_closure_free(ac);
    return 0;
  }

  if(!ac->service_ctx) {
    const char *primer = "";
    ac->service_ctx = restc = noit_http_rest_closure_alloc();
    restc->http_ctx =
        noit_http_session_ctx_new(noit_rest_request_dispatcher,
                                  restc, e);
    
    switch(ac->cmd) {
      case NOIT_CONTROL_POST:
        primer = "POST";
        break;
      case NOIT_CONTROL_GET:
        primer = "GET ";
        break;
      case NOIT_CONTROL_HEAD:
        primer = "HEAD";
        break;
      default:
        goto socket_error;
    }
    noit_http_session_prime_input(restc->http_ctx, primer, 4);
  }
  return restc->http_ctx->drive(e, mask, restc->http_ctx, now);
}

void noit_http_rest_init() {
  noit_control_dispatch_delegate(noit_control_dispatch,
                                 NOIT_CONTROL_GET,
                                 noit_http_rest_handler);
  noit_control_dispatch_delegate(noit_control_dispatch,
                                 NOIT_CONTROL_HEAD,
                                 noit_http_rest_handler);
  noit_control_dispatch_delegate(noit_control_dispatch,
                                 NOIT_CONTROL_POST,
                                 noit_http_rest_handler);
}

