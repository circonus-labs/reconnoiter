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

#ifndef _NOIT_HTTP_H
#define _NOIT_HTTP_H

#include "noit_defines.h"
#include <libxml/tree.h>
#include "eventer/eventer.h"
#include "utils/noit_hash.h"
#include "utils/noit_atomic.h"
#include "utils/noit_hooks.h"
#include "noit_listener.h"

typedef enum {
  NOIT_HTTP_OTHER, NOIT_HTTP_GET, NOIT_HTTP_HEAD, NOIT_HTTP_POST
} noit_http_method;
typedef enum {
  NOIT_HTTP09, NOIT_HTTP10, NOIT_HTTP11
} noit_http_protocol;

#define NOIT_HTTP_CHUNKED 0x0001
#define NOIT_HTTP_CLOSE   0x0002
#define NOIT_HTTP_GZIP    0x0010
#define NOIT_HTTP_DEFLATE 0x0020

typedef enum {
  BCHAIN_INLINE = 0,
  BCHAIN_MMAP
} bchain_type_t;

struct bchain;

#define DEFAULT_MAXWRITE 1<<14 /* 32k */
#define DEFAULT_BCHAINSIZE ((1 << 15)-(offsetof(struct bchain, _buff)))
/* 64k - delta */
#define DEFAULT_BCHAINMINREAD (DEFAULT_BCHAINSIZE/4)
#define BCHAIN_SPACE(a) ((a)->allocd - (a)->size - (a)->start)

struct noit_http_connection;
typedef struct noit_http_connection noit_http_connection;
struct noit_http_request;
typedef struct noit_http_request noit_http_request;
struct noit_http_response;
typedef struct noit_http_response noit_http_response;

struct bchain {
  bchain_type_t type;
  struct bchain *next, *prev;
  size_t start; /* where data starts (buff + start) */
  size_t size;  /* data length (past start) */
  size_t allocd;/* total allocation */
  char *buff;
  char _buff[1]; /* over allocate as needed */
};

struct noit_http_session_ctx;
typedef struct noit_http_session_ctx noit_http_session_ctx;
typedef int (*noit_http_dispatch_func) (noit_http_session_ctx *);

API_EXPORT(noit_http_session_ctx *)
  noit_http_session_ctx_new(noit_http_dispatch_func, void *, eventer_t,
                            acceptor_closure_t *);
API_EXPORT(void)
  noit_http_ctx_session_release(noit_http_session_ctx *ctx);
API_EXPORT(uint32_t)
  noit_http_session_ref_cnt(noit_http_session_ctx *);
API_EXPORT(uint32_t)
  noit_http_session_ref_dec(noit_http_session_ctx *);
API_EXPORT(uint32_t)
  noit_http_session_ref_inc(noit_http_session_ctx *);
API_EXPORT(void)
  noit_http_session_trigger(noit_http_session_ctx *, int state);

API_EXPORT(noit_http_request *)
  noit_http_session_request(noit_http_session_ctx *);
API_EXPORT(noit_http_response *)
  noit_http_session_response(noit_http_session_ctx *);
API_EXPORT(noit_http_connection *)
  noit_http_session_connection(noit_http_session_ctx *);

API_EXPORT(void *)
  noit_http_session_dispatcher_closure(noit_http_session_ctx *);
API_EXPORT(void)
  noit_http_session_set_dispatcher(noit_http_session_ctx *,
                                   int (*)(noit_http_session_ctx *), void *);

API_EXPORT(eventer_t)
  noit_http_connection_event(noit_http_connection *);

API_EXPORT(void)
  noit_http_request_start_time(noit_http_request *, struct timeval *);
API_EXPORT(const char *)
  noit_http_request_uri_str(noit_http_request *);
API_EXPORT(const char *)
  noit_http_request_method_str(noit_http_request *);
API_EXPORT(const char *)
  noit_http_request_protocol_str(noit_http_request *);
API_EXPORT(size_t)
  noit_http_request_content_length(noit_http_request *);
API_EXPORT(const char *)
  noit_http_request_querystring(noit_http_request *, const char *);
API_EXPORT(noit_hash_table *)
  noit_http_request_querystring_table(noit_http_request *);
API_EXPORT(noit_hash_table *)
  noit_http_request_headers_table(noit_http_request *);

API_EXPORT(noit_boolean)
  noit_http_response_closed(noit_http_response *);
API_EXPORT(noit_boolean)
  noit_http_response_complete(noit_http_response *);

API_EXPORT(void)
  noit_http_ctx_acceptor_free(void *); /* just calls noit_http_session_ctx_release */

API_EXPORT(void)
  noit_http_process_querystring(noit_http_request *);

API_EXPORT(int)
  noit_http_session_drive(eventer_t, int, void *, struct timeval *, int *done);

API_EXPORT(noit_boolean)
  noit_http_session_prime_input(noit_http_session_ctx *, const void *, size_t);
API_EXPORT(int)
  noit_http_session_req_consume(noit_http_session_ctx *ctx,
                                void *buf, size_t len, int *mask);
API_EXPORT(noit_boolean)
  noit_http_response_status_set(noit_http_session_ctx *, int, const char *);
API_EXPORT(noit_boolean)
  noit_http_response_header_set(noit_http_session_ctx *,
                                const char *, const char *);
API_EXPORT(noit_boolean)
  noit_http_response_option_set(noit_http_session_ctx *, u_int32_t);
API_EXPORT(noit_boolean)
  noit_http_response_append(noit_http_session_ctx *, const void *, size_t);
API_EXPORT(noit_boolean)
  noit_http_response_append_bchain(noit_http_session_ctx *, struct bchain *);
API_EXPORT(noit_boolean)
  noit_http_response_append_mmap(noit_http_session_ctx *,
                                 int fd, size_t len, int flags, off_t offset);
API_EXPORT(noit_boolean)
  noit_http_response_flush(noit_http_session_ctx *, noit_boolean);
API_EXPORT(noit_boolean)
  noit_http_response_flush_asynch(noit_http_session_ctx *, noit_boolean);
API_EXPORT(noit_boolean) noit_http_response_end(noit_http_session_ctx *);

#define noit_http_response_server_error(ctx, type) \
  noit_http_response_standard(ctx, 500, "ERROR", type)
#define noit_http_response_ok(ctx, type) \
  noit_http_response_standard(ctx, 200, "OK", type)
#define noit_http_response_not_found(ctx, type) \
  noit_http_response_standard(ctx, 404, "NOT FOUND", type)
#define noit_http_response_denied(ctx, type) \
  noit_http_response_standard(ctx, 403, "DENIED", type)

#define noit_http_response_standard(ctx, code, name, type) do { \
  noit_http_response_status_set(ctx, code, name); \
  noit_http_response_header_set(ctx, "Content-Type", type); \
  if(noit_http_response_option_set(ctx, NOIT_HTTP_CHUNKED) == noit_false) \
    noit_http_response_option_set(ctx, NOIT_HTTP_CLOSE); \
  noit_http_response_option_set(ctx, NOIT_HTTP_GZIP); \
} while(0)

API_EXPORT(void)
  noit_http_response_xml(noit_http_session_ctx *, xmlDocPtr);

API_EXPORT(void)
  noit_http_init();

NOIT_HOOK_PROTO(http_request_log,
                (noit_http_session_ctx *ctx),
                void *, closure,
                (void *closure, noit_http_session_ctx *ctx))

#endif
