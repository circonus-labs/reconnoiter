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

struct bchain {
  bchain_type_t type;
  struct bchain *next, *prev;
  size_t start; /* where data starts (buff + start) */
  size_t size;  /* data length (past start) */
  size_t allocd;/* total allocation */
  char *buff;
  char _buff[1]; /* over allocate as needed */
};

#define DEFAULT_MAXWRITE 1<<14 /* 32k */
#define DEFAULT_BCHAINSIZE ((1 << 15)-(offsetof(struct bchain, _buff)))
/* 64k - delta */
#define DEFAULT_BCHAINMINREAD (DEFAULT_BCHAINSIZE/4)
#define BCHAIN_SPACE(a) ((a)->allocd - (a)->size - (a)->start)

typedef struct {
  eventer_t e;
  int needs_close;
} noit_http_connection;

typedef struct {
  struct bchain *first_input; /* The start of the input chain */
  struct bchain *last_input;  /* The end of the input chain */
  struct bchain *current_input;  /* The point of the input where we */
  size_t         current_offset; /* analyzing. */

  enum { NOIT_HTTP_REQ_HEADERS = 0,
         NOIT_HTTP_REQ_EXPECT,
         NOIT_HTTP_REQ_PAYLOAD } state;
  struct bchain *current_request_chain;
  noit_boolean has_payload;
  int64_t content_length;
  int64_t content_length_read;
  char *method_str;
  char *uri_str;
  char *protocol_str;
  noit_hash_table querystring;
  u_int32_t opts;
  noit_http_method method;
  noit_http_protocol protocol;
  noit_hash_table headers;
  noit_boolean complete;
  struct timeval start_time;
  char *orig_qs;
} noit_http_request;

typedef struct {
  noit_http_protocol protocol;
  int status_code;
  char *status_reason;

  noit_hash_table headers;
  struct bchain *leader; /* serialization of status line and headers */

  u_int32_t output_options;
  struct bchain *output;       /* data is pushed in here */
  struct bchain *output_raw;   /* internally transcoded here for output */
  size_t output_raw_offset;    /* tracks our offset */
  noit_boolean output_started; /* locks the options and leader */
                               /*   and possibly output. */
  noit_boolean closed;         /* set by _end() */
  noit_boolean complete;       /* complete, drained and disposable */
  size_t bytes_written;        /* tracks total bytes written */
} noit_http_response;

struct noit_http_session_ctx;
typedef int (*noit_http_dispatch_func) (struct noit_http_session_ctx *);

typedef struct noit_http_session_ctx {
  noit_atomic32_t ref_cnt;
  int64_t drainage;
  int max_write;
  noit_http_connection conn;
  noit_http_request req;
  noit_http_response res;
  noit_http_dispatch_func dispatcher;
  void *dispatcher_closure;
  acceptor_closure_t *ac;
} noit_http_session_ctx;

API_EXPORT(noit_http_session_ctx *)
  noit_http_session_ctx_new(noit_http_dispatch_func, void *, eventer_t,
                            acceptor_closure_t *);
API_EXPORT(void)
  noit_http_ctx_session_release(noit_http_session_ctx *ctx);


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
  noit_http_response_option_set(ctx, NOIT_HTTP_DEFLATE); \
} while(0)

API_EXPORT(void)
  noit_http_response_xml(noit_http_session_ctx *, xmlDocPtr);

API_EXPORT(void)
  noit_http_init();

#endif
