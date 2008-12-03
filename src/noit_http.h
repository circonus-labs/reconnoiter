/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_HTTP_H
#define _NOIT_HTTP_H

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "utils/noit_hash.h"

typedef enum {
  NOIT_HTTP_OTHER, NOIT_HTTP_GET, NOIT_HTTP_HEAD
} noit_http_method;
typedef enum {
  NOIT_HTTP09, NOIT_HTTP10, NOIT_HTTP11
} noit_http_protocol;

#define NOIT_HTTP_CHUNKED 0x0001
#define NOIT_HTTP_CLOSE   0x0002
#define NOIT_HTTP_GZIP    0x0010
#define NOIT_HTTP_DEFLATE 0x0020

#define DEFAULT_BCHAINSIZE ((1 << 15)-(3*sizeof(size_t))-(2*sizeof(void *)))
/* 64k - delta */
#define DEFAULT_BCHAINMINREAD (DEFAULT_BCHAINSIZE/4)

struct bchain {
  struct bchain *next, *prev;
  size_t start; /* where data starts (buff + start) */
  size_t size;  /* data length (past start) */
  size_t allocd;/* total allocation */
  char buff[1]; /* over allocate as needed */
};

#define BCHAIN_SPACE(a) ((a)->allocd - (a)->size - (a)->start)

API_EXPORT(struct bchain *) bchain_alloc(size_t size);
API_EXPORT(void) bchain_free(struct bchain *);

typedef struct {
  eventer_t e;
  int needs_close;
} noit_http_connection;

typedef struct {
  struct bchain *first_input; /* The start of the input chain */
  struct bchain *last_input;  /* The end of the input chain */
  struct bchain *current_input;  /* The point of the input where we */
  size_t         current_offset; /* analyzing. */

  struct bchain *current_request_chain;
  char *method_str;
  char *uri_str;
  char *protocol_str;
  noit_http_method method;
  noit_http_protocol protocol;
  noit_hash_table headers;
  noit_boolean complete;
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
} noit_http_response;

struct noit_http_session_ctx;
typedef int (*noit_http_dispatch_func) (struct noit_http_session_ctx *);

typedef struct noit_http_session_ctx {
  noit_http_connection conn;
  noit_http_request req;
  noit_http_response res;
  noit_http_dispatch_func dispatcher;
  eventer_func_t drive;
} noit_http_session_ctx;

API_EXPORT(noit_http_session_ctx *)
  noit_http_session_ctx_new(noit_http_dispatch_func, eventer_t);

API_EXPORT(int)
  noit_http_session_drive(eventer_t, int, void *, struct timeval *);

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
  noit_http_response_flush(noit_http_session_ctx *, noit_boolean);
API_EXPORT(noit_boolean) noit_http_response_end(noit_http_session_ctx *);

#endif
