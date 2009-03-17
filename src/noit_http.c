/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "noit_http.h"
#include "utils/noit_str.h"

#include <errno.h>
#include <ctype.h>
#include <assert.h>
#include <zlib.h>

#define REQ_PAT "\r\n\r\n"
#define REQ_PATSIZE 4

#define CTX_ADD_HEADER(a,b) \
    noit_hash_replace(&ctx->res.headers, \
                      strdup(a), strlen(a), strdup(b), free, free)
static const char _hexchars[16] =
  {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};

struct bchain *bchain_alloc(size_t size) {
  struct bchain *n;
  n = malloc(size + ((void *)n->buff - (void *)n));
  if(!n) return NULL;
  n->prev = n->next = NULL;
  n->start = n->size = 0;
  n->allocd = size;
  return n;
}
#define RELEASE_BCHAIN(a) do { \
  while(a) { \
    struct bchain *__b; \
    __b = a; \
    a = __b->next; \
    bchain_free(__b); \
  } \
} while(0)
struct bchain *bchain_from_data(void *d, size_t size) {
  struct bchain *n;
  n = bchain_alloc(size);
  if(!n) return NULL;
  memcpy(n->buff, d, size);
  n->size = size;
  return n;
}
void bchain_free(struct bchain *b) {
  free(b);
}

static noit_http_method
_method_enum(const char *s) {
  switch(*s) {
   case 'G':
    if(!strcasecmp(s, "GET")) return NOIT_HTTP_GET;
    break;
   case 'H':
    if(!strcasecmp(s, "HEAD")) return NOIT_HTTP_HEAD;
    break;
   default:
    break;
  }
  return NOIT_HTTP_OTHER;
}
static noit_http_protocol
_protocol_enum(const char *s) {
  if(!strcasecmp(s, "HTTP/1.1")) return NOIT_HTTP11;
  if(!strcasecmp(s, "HTTP/1.0")) return NOIT_HTTP10;
  return NOIT_HTTP09;
}
static noit_boolean
_fixup_bchain(struct bchain *b) {
  /* make sure lines (CRLF terminated) don't cross chain boundaries */
  while(b) {
    struct bchain *f;
    int start_in_b, end_in_f;
    size_t new_size;
    const char *str_in_f;

    start_in_b = b->start;
    if(b->size > 2) {
      if(memcmp(b->buff + b->start + b->size - 2, "\r\n", 2) == 0) {
        b = b->next;
        continue;
      }
      start_in_b = b->start + b->size - 3; /* we already checked -2 */
      while(start_in_b >= b->start) {
        if(memcmp(b->buff + start_in_b, "\r\n", 2) == 0) {
          start_in_b += 2;
          break;
        }
      }
    }

    /* start_in_b points to the beginning of the string we need to build
     * into a new buffer.
     */
    f = b->next;
    if(!f) return noit_false; /* Nothing left, can't complete the line */
    str_in_f = strnstrn("\r\n", 2, f->buff + f->start, f->size);
    if(!str_in_f) return noit_false; /* nothing in next chain -- too long */
    str_in_f += 2;
    end_in_f = (str_in_f - f->buff - f->start);
    new_size = end_in_f + (b->start + b->size - start_in_b);
    if(new_size > DEFAULT_BCHAINSIZE) return noit_false; /* string too long */
    f = bchain_alloc(new_size);
    f->prev = b;
    f->next = b->next;
    f->start = 0;
    f->size = new_size;
    memcpy(f->buff, b->buff + start_in_b, b->start + b->size - start_in_b);
    memcpy(f->buff + b->start + b->size - start_in_b,
           f->buff + f->start, end_in_f);
    f->next->prev = f;
    f->prev->next = f;
    f->prev->size -= start_in_b - b->start;
    f->next->size -= end_in_f;
    f->next->start += end_in_f;
    b = f->next; /* skip f, we know it is right */
  }
  return noit_true;
}
static noit_boolean
_extract_header(char *l, const char **n, const char **v) {
  *n = NULL;
  if(*l == ' ' || *l == '\t') {
    while(*l == ' ' || *l == '\t') l++;
    *v = l;
    return noit_true;
  }
  *n = l;
  while(*l != ':' && *l) { *l = tolower(*l); l++; }
  if(!*l) return noit_false;
  *v = l+1;
  /* Right trim the name */
  *l-- = '\0';
  while(*l == ' ' || *l == '\t') *l-- = '\0';
  while(**v == ' ' || **v == '\t') (*v)++;
  return noit_true;
}
static int
_http_perform_write(noit_http_session_ctx *ctx, int *mask) {
  int len, tlen = 0;
  struct bchain **head, *b;
 choose_bucket:
  head = ctx->res.leader ? &ctx->res.leader : &ctx->res.output_raw;
  b = *head;

  if(!ctx->conn.e) return 0;
  if(ctx->res.output_started == noit_false) return EVENTER_EXCEPTION;
  if(!b) {
    if(ctx->res.closed) ctx->res.complete = noit_true;
    *mask = EVENTER_EXCEPTION;
    return tlen;
  }

  if(ctx->res.output_raw_offset >= b->size) {
    *head = b->next;
    bchain_free(b);
    b = *head;
    if(b) b->prev = NULL;
    ctx->res.output_raw_offset = 0;
    goto choose_bucket;
  }

  len = ctx->conn.e->opset->
          write(ctx->conn.e->fd,
                b->buff + b->start + ctx->res.output_raw_offset,
                b->size - ctx->res.output_raw_offset,
                mask, ctx->conn.e);
  if(len == -1 && errno == EAGAIN) {
    *mask |= EVENTER_EXCEPTION;
    return tlen;
  }
  if(len == -1) {
    /* socket error */
    ctx->conn.e->opset->close(ctx->conn.e->fd, mask, ctx->conn.e);
    ctx->conn.e = NULL;
    return -1;
  }
  ctx->res.output_raw_offset += len;
  tlen += len;
  goto choose_bucket;
}
static noit_boolean
noit_http_request_finalize(noit_http_request *req, noit_boolean *err) {
  int start;
  const char *mstr, *last_name = NULL;
  struct bchain *b;

  if(!req->current_input) req->current_input = req->first_input;
  if(!req->current_input) return noit_false;
 restart:
  while(req->current_input->prev &&
        (req->current_offset < (req->current_input->start + REQ_PATSIZE - 1))) {
    int inset;
    /* cross bucket */
    if(req->current_input == req->last_input &&
       req->current_offset >= (req->last_input->start + req->last_input->size))
      return noit_false;
    req->current_offset++;
    inset = req->current_offset - req->current_input->start;
    if(memcmp(req->current_input->buff + req->current_input->start,
              REQ_PAT + (REQ_PATSIZE - inset), inset) == 0 &&
       memcmp(req->current_input->prev->buff +
                req->current_input->prev->start +
                req->current_input->prev->size - REQ_PATSIZE + inset,
              REQ_PAT + inset,
              REQ_PATSIZE - inset) == 0) goto match;
  }
  start = MAX(req->current_offset - REQ_PATSIZE, req->current_input->start);
  mstr = strnstrn(REQ_PAT, REQ_PATSIZE,
                  req->current_input->buff + start,
                  req->current_input->size -
                    (start - req->current_input->start));
  if(!mstr && req->current_input->next) {
    req->current_input = req->current_input->next;
    req->current_offset = req->current_input->start;
    goto restart;
  }
  if(!mstr) return noit_false;
  req->current_offset = mstr - req->current_input->buff + REQ_PATSIZE;
 match:
  req->current_request_chain = req->first_input;
  if(req->current_offset <
     req->current_input->start + req->current_input->size) {
    /* There are left-overs */
    req->first_input = bchain_alloc(MAX(DEFAULT_BCHAINSIZE,
                                        req->current_input->size));
    req->first_input->prev = NULL;
    req->first_input->next = req->current_input->next;
    req->first_input->start = 0;
    req->first_input->size = req->current_input->size - (req->current_offset + req->current_input->start);
    memcpy(req->first_input->buff,
           req->current_input->buff +
             req->current_input->start + req->current_offset,
           req->first_input->size);
    if(req->last_input == req->current_request_chain)
      req->last_input = req->first_input;
  }
  else {
    req->first_input = req->last_input = NULL;
  }
  req->current_input = NULL;
  req->current_offset = 0;

  /* Now we need to dissect the current_request_chain into an HTTP request */
  /* First step: make sure that no line crosses a chain boundary by
   * inserting new chains as necessary.
   */
  if(!_fixup_bchain(req->current_request_chain)) {
    *err = noit_true;
    return noit_false;
  }
  /* Second step is to parse out the request itself */
  for(b = req->current_request_chain; b; b = b->next) {
    char *curr_str, *next_str;
    b->buff[b->start + b->size - 2] = '\0';
    curr_str = b->buff + b->start;
    do {
      next_str = strstr(curr_str, "\r\n");
      if(next_str) {
        *((char *)next_str) = '\0';
        next_str += 2;
      }
      if(req->method_str && *curr_str == '\0')
        break; /* our CRLFCRLF... end of req */
#define FAIL do { *err = noit_true; return noit_false; } while(0)
      if(!req->method_str) { /* request line */
        req->method_str = (char *)curr_str;
        req->uri_str = strchr(curr_str, ' ');
        if(!req->uri_str) FAIL;
        *(req->uri_str) = '\0';
        req->uri_str++;
        req->protocol_str = strchr(req->uri_str, ' ');
        if(!req->protocol_str) FAIL;
        *(req->protocol_str) = '\0';
        req->protocol_str++;
        req->method = _method_enum(req->method_str);
        req->protocol = _protocol_enum(req->protocol_str);
        if(req->protocol == NOIT_HTTP11) req->opts |= NOIT_HTTP_CHUNKED;
      }
      else { /* request headers */
        const char *name, *value;
        if(_extract_header(curr_str, &name, &value) == noit_false) FAIL;
        if(!name && !last_name) FAIL;
        if(!strcmp(name ? name : last_name, "accept-encoding")) {
          if(strstr(value, "gzip")) req->opts |= NOIT_HTTP_GZIP;
          if(strstr(value, "deflate")) req->opts |= NOIT_HTTP_DEFLATE;
        }
        if(name)
          noit_hash_replace(&req->headers, name, strlen(name), (void *)value,
                            NULL, NULL);
        else {
          struct bchain *b;
          const char *prefix = NULL;
          int l1, l2;
          noit_hash_retr_str(&req->headers, last_name, strlen(last_name),
                             &prefix);
          if(!prefix) FAIL;
          l1 = strlen(prefix);
          l2 = strlen(value);
          b = bchain_alloc(l1 + l2 + 2);
          b->next = req->current_request_chain;
          b->next->prev = b;
          req->current_request_chain = b;
          b->size = l1 + l2 + 2;
          memcpy(b->buff, prefix, l1);
          b->buff[l1] = ' ';
          memcpy(b->buff + l1 + 1, value, l2);
          b->buff[l1 + 1 + l2] = '\0';
          noit_hash_replace(&req->headers, last_name, strlen(last_name),
                            b->buff, NULL, NULL);
        }
        if(name) last_name = name;
      }
      curr_str = next_str;
    } while(next_str);
  }
  req->complete = noit_true;
  return noit_true;
}
int
noit_http_complete_request(noit_http_session_ctx *ctx, int mask) {
  struct bchain *in;
  noit_boolean rv, err = noit_false;

  if(mask & EVENTER_EXCEPTION) {
   full_error:
    ctx->conn.e->opset->close(ctx->conn.e->fd, &mask, ctx->conn.e);
    ctx->conn.e = NULL;
    return 0;
  }
  if(ctx->req.complete == noit_true) return EVENTER_EXCEPTION;

  /* We could have a complete request in the tail of a previous request */
  rv = noit_http_request_finalize(&ctx->req, &err);
  if(rv == noit_true) return EVENTER_WRITE | EVENTER_EXCEPTION;
  if(err == noit_true) goto full_error;

  in = ctx->req.last_input;
  if(!in) {
    in = ctx->req.first_input = ctx->req.last_input =
      bchain_alloc(DEFAULT_BCHAINSIZE);
    if(!in) goto full_error;
  }
  while(1) {
    int len;

    if(in->size > 0 && /* we've read something */
       DEFAULT_BCHAINMINREAD > BCHAIN_SPACE(in) && /* we'd like read more */
       DEFAULT_BCHAINMINREAD < DEFAULT_BCHAINSIZE) { /* and we can */
      in->next = ctx->req.last_input =
        bchain_alloc(DEFAULT_BCHAINSIZE);
      in->next->prev = in;
      in = in->next;
      if(!in) goto full_error;
    }

    len = ctx->conn.e->opset->read(ctx->conn.e->fd,
                                   in->buff + in->start + in->size,
                                   in->allocd - in->size - in->start,
                                   &mask, ctx->conn.e);
    if(len == -1 && errno == EAGAIN) return mask;
    if(len <= 0) goto full_error;
    if(len > 0) in->size += len;
    rv = noit_http_request_finalize(&ctx->req, &err);
    if(len == -1 || err == noit_true) {
      goto full_error;
    }
    if(rv == noit_true) return EVENTER_WRITE | EVENTER_EXCEPTION;
  }
  return EVENTER_READ | EVENTER_EXCEPTION;
}
void
noit_http_request_release(noit_http_session_ctx *ctx) {
  noit_hash_destroy(&ctx->req.headers, NULL, NULL);
  RELEASE_BCHAIN(ctx->req.first_input);
  memset(&ctx->req, 0, sizeof(ctx->req));
}
void
noit_http_response_release(noit_http_session_ctx *ctx) {
  noit_hash_destroy(&ctx->res.headers, free, free);
  if(ctx->res.status_reason) free(ctx->res.status_reason);
  RELEASE_BCHAIN(ctx->res.leader);
  RELEASE_BCHAIN(ctx->res.output);
  RELEASE_BCHAIN(ctx->res.output_raw);
  memset(&ctx->res, 0, sizeof(ctx->res));
}
void
noit_http_ctx_session_release(noit_http_session_ctx *ctx) {
  if(noit_atomic_dec32(&ctx->ref_cnt) == 0) {
    noit_http_request_release(ctx);
    noit_http_response_release(ctx);
    free(ctx);
  }
}
int
noit_http_session_drive(eventer_t e, int origmask, void *closure,
                        struct timeval *now) {
  noit_http_session_ctx *ctx = closure;
  int rv;
  int mask = origmask;
 next_req:
  if(ctx->req.complete != noit_true) {
    mask = noit_http_complete_request(ctx, origmask);
    if(ctx->req.complete != noit_true) return mask;
  }

  /* only dispatch if the response is not complete */
  if(ctx->res.complete == noit_false) rv = ctx->dispatcher(ctx);

  _http_perform_write(ctx, &mask);
  if(ctx->res.complete == noit_true &&
     ctx->conn.e &&
     ctx->conn.needs_close == noit_true) {
    ctx->conn.e->opset->close(ctx->conn.e->fd, &mask, ctx->conn.e);
    ctx->conn.e = NULL;
    goto release;
    return 0;
  }
  if(ctx->res.complete == noit_true) {
    noit_http_request_release(ctx);
    noit_http_response_release(ctx);
  }
  if(ctx->req.complete == noit_false) goto next_req;
  if(ctx->conn.e) {
    return mask;
  }
  return 0;
 release:
  noit_http_ctx_session_release(ctx);
  return 0;
}

noit_http_session_ctx *
noit_http_session_ctx_new(noit_http_dispatch_func f, void *c, eventer_t e) {
  noit_http_session_ctx *ctx;
  ctx = calloc(1, sizeof(*ctx));
  ctx->ref_cnt = 1;
  ctx->req.complete = noit_false;
  ctx->conn.e = e;
  ctx->dispatcher = f;
  ctx->dispatcher_closure = c;
  ctx->drive = noit_http_session_drive;
  return ctx;
}

noit_boolean
noit_http_response_status_set(noit_http_session_ctx *ctx,
                              int code, const char *reason) {
  if(ctx->res.output_started == noit_true) return noit_false;
  ctx->res.protocol = ctx->req.protocol;
  if(code < 100 || code > 999) return noit_false;
  ctx->res.status_code = code;
  if(ctx->res.status_reason) free(ctx->res.status_reason);
  ctx->res.status_reason = strdup(reason);
  return noit_true;
}
noit_boolean
noit_http_response_header_set(noit_http_session_ctx *ctx,
                              const char *name, const char *value) {
  if(ctx->res.output_started == noit_true) return noit_false;
  noit_hash_replace(&ctx->res.headers, strdup(name), strlen(name),
                    strdup(value), free, free);
  return noit_true;
}
noit_boolean
noit_http_response_option_set(noit_http_session_ctx *ctx, u_int32_t opt) {
  if(ctx->res.output_started == noit_true) return noit_false;
  /* transfer and content encodings only allowed in HTTP/1.1 */
  if(ctx->res.protocol != NOIT_HTTP11 &&
     (opt & NOIT_HTTP_CHUNKED))
    return noit_false;
  if(ctx->res.protocol != NOIT_HTTP11 &&
     (opt & (NOIT_HTTP_GZIP | NOIT_HTTP_DEFLATE)))
    return noit_false;
  if(((ctx->res.output_options | opt) &
      (NOIT_HTTP_GZIP | NOIT_HTTP_DEFLATE)) ==
        (NOIT_HTTP_GZIP | NOIT_HTTP_DEFLATE))
    return noit_false;

  /* Check out "accept" set */
  if(!(opt & ctx->req.opts)) return noit_false;

  ctx->res.output_options |= opt;
  if(ctx->res.output_options & NOIT_HTTP_CHUNKED)
    CTX_ADD_HEADER("Transfer-Encoding", "chunked");
  if(ctx->res.output_options & (NOIT_HTTP_GZIP | NOIT_HTTP_DEFLATE)) {
    CTX_ADD_HEADER("Vary", "Accept-Encoding");
    if(ctx->res.output_options & NOIT_HTTP_GZIP)
      CTX_ADD_HEADER("Content-Encoding", "gzip");
    else if(ctx->res.output_options & NOIT_HTTP_DEFLATE)
      CTX_ADD_HEADER("Content-Encoding", "deflate");
  }
  if(ctx->res.output_options & NOIT_HTTP_CLOSE) {
    CTX_ADD_HEADER("Connection", "close");
    ctx->conn.needs_close = noit_true;
  }
  return noit_true;
}
noit_boolean
noit_http_response_append(noit_http_session_ctx *ctx,
                          const void *b, size_t l) {
  struct bchain *o;
  int boff = 0;
  if(ctx->res.closed == noit_true) return noit_false;
  if(ctx->res.output_started == noit_true &&
     !(ctx->res.output_options & (NOIT_HTTP_CLOSE | NOIT_HTTP_CHUNKED)))
    return noit_false;
  if(!ctx->res.output)
    assert(ctx->res.output = bchain_alloc(DEFAULT_BCHAINSIZE));
  o = ctx->res.output;
  while(o->next) o = o->next;
  while(l > 0) {
    if(o->allocd == o->start + o->size) {
      /* Filled up, need another */
      o->next = bchain_alloc(DEFAULT_BCHAINSIZE);
      o->next->prev = o->next;
      o = o->next;
    }
    if(o->allocd > o->start + o->size) {
      int tocopy = MIN(l, o->allocd - o->start - o->size);
      memcpy(o->buff + o->start + o->size, (const char *)b + boff, tocopy);
      o->size += tocopy;
      boff += tocopy;
      l -= tocopy;
    }
  }
  return noit_true;
}
noit_boolean
noit_http_response_append_bchain(noit_http_session_ctx *ctx,
                                 struct bchain *b) {
  struct bchain *o;
  if(ctx->res.closed == noit_true) return noit_false;
  if(ctx->res.output_started == noit_true &&
     !(ctx->res.output_options & (NOIT_HTTP_CHUNKED | NOIT_HTTP_CLOSE)))
    return noit_false;
  if(!ctx->res.output)
    ctx->res.output = b;
  else {
    o = ctx->res.output;
    while(o->next) o = o->next;
    o->next = b;
    b->prev = o;
  }
  return noit_true;
}
static int
_http_construct_leader(noit_http_session_ctx *ctx) {
  int len = 0, tlen;
  struct bchain *b;
  const char *protocol_str;
  const char *key, *value;
  int klen;
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;

  assert(!ctx->res.leader);
  ctx->res.leader = b = bchain_alloc(DEFAULT_BCHAINSIZE);

  protocol_str = ctx->res.protocol == NOIT_HTTP11 ?
                   "HTTP/1.1" :
                   (ctx->res.protocol == NOIT_HTTP10 ?
                     "HTTP/1.0" :
                     "HTTP/0.9");
  tlen = snprintf(b->buff, b->allocd, "%s %03d %s\r\n",
                  protocol_str, ctx->res.status_code, ctx->res.status_reason);
  if(tlen < 0) return -1;
  len = b->size = tlen;

#define CTX_LEADER_APPEND(s, slen) do { \
  if(b->size + slen > DEFAULT_BCHAINSIZE) { \
    b->next = bchain_alloc(DEFAULT_BCHAINSIZE); \
    assert(b->next); \
    b->next->prev = b; \
    b = b->next; \
  } \
  assert(DEFAULT_BCHAINSIZE >= b->size + slen); \
  memcpy(b->buff + b->start + b->size, s, slen); \
  b->size += slen; \
} while(0)
  while(noit_hash_next_str(&ctx->res.headers, &iter,
                           &key, &klen, &value)) {
    int vlen = strlen(value);
    CTX_LEADER_APPEND(key, klen);
    CTX_LEADER_APPEND(": ", 2);
    CTX_LEADER_APPEND(value, vlen);
    CTX_LEADER_APPEND("\r\n", 2);
  }
  CTX_LEADER_APPEND("\r\n", 2);
  return len;
}
/* memgzip */
static int memgzip2(Bytef *dest, uLongf *destLen,
                    const Bytef *source, uLong sourceLen, int level) {
    z_stream stream;
    int err;

    memset(&stream, 0, sizeof(stream));
    stream.next_in = (Bytef*)source;
    stream.avail_in = (uInt)sourceLen;
    stream.next_out = dest;
    stream.avail_out = (uInt)*destLen;
    if ((uLong)stream.avail_out != *destLen) return Z_BUF_ERROR;

    err = deflateInit2(&stream, level, Z_DEFLATED, 15+16, 8,
                       Z_DEFAULT_STRATEGY);
    if (err != Z_OK) return err;

    err = deflate(&stream, Z_FINISH);
    if (err != Z_STREAM_END) {
        deflateEnd(&stream);
        return err == Z_OK ? Z_BUF_ERROR : err;
    }
    *destLen = stream.total_out;

    err = deflateEnd(&stream);
    return err;
}
static noit_boolean
_http_encode_chain(struct bchain *out, struct bchain *in, int opts) {
  /* implement gzip and deflate! */
  if(opts & NOIT_HTTP_GZIP) {
    uLongf olen;
    olen = out->allocd - out->start;
    if(Z_OK != memgzip2((Bytef *)(out->buff + out->start), &olen,
                        (Bytef *)(in->buff + in->start), (uLong)in->size,
                        9)) {
      noitL(noit_error, "zlib compress2 error\n");
      return noit_false;
    }
    out->size += olen;
  }
  else if(opts & NOIT_HTTP_DEFLATE) {
    uLongf olen;
    olen = out->allocd - out->start;
    if(Z_OK != compress2((Bytef *)(out->buff + out->start), &olen,
                         (Bytef *)(in->buff + in->start), (uLong)in->size,
                         9)) {
      noitL(noit_error, "zlib compress2 error\n");
      return noit_false;
    }
    out->size += olen;
  }
  else {
    if(in->size > out->allocd - out->start) return noit_false;
    memcpy(out->buff + out->start, in->buff + in->start, in->size);
    out->size += in->size;
  }
  return noit_true;
}
struct bchain *
noit_http_process_output_bchain(noit_http_session_ctx *ctx,
                                struct bchain *in) {
  struct bchain *out;
  int ilen, hexlen;
  int opts = ctx->res.output_options;

  /* a chunked header looks like: hex*\r\ndata\r\n */
  /* let's assume that content never gets "larger" */
  /* So, the link size is the len(data) + 4 + ceil(log(len(data))/log(16)) */
  ilen = in->size;
  hexlen = 0;
  while(ilen) { ilen >>= 4; hexlen++; }
  if(hexlen == 0) hexlen = 1;

  ilen = in->size;
  if(opts & NOIT_HTTP_GZIP) ilen = deflateBound(NULL, ilen);
  else if(opts & NOIT_HTTP_DEFLATE) ilen = compressBound(ilen);
  out = bchain_alloc(hexlen + 4 + ilen);
  /* if we're chunked, let's give outselved hexlen + 2 prefix space */
  if(opts & NOIT_HTTP_CHUNKED) out->start = hexlen + 2;
  if(_http_encode_chain(out, in, opts) == noit_false) {
    free(out);
    return NULL;
  }
  /* Too long! Out "larger" assumption is bad */
  if(opts & NOIT_HTTP_CHUNKED) {
    ilen = out->size;
    assert(out->start+out->size+2 <= out->allocd);
    out->buff[out->start + out->size++] = '\r';
    out->buff[out->start + out->size++] = '\n';
    out->start = 0;
    /* terminate */
    out->size += 2;
    out->buff[hexlen] = '\r';
    out->buff[hexlen+1] = '\n';
    /* backfill */
    out->size += hexlen;
    while(hexlen > 0) {
      out->buff[hexlen - 1] = _hexchars[ilen & 0xf];
      ilen >>= 4;
      hexlen--;
    }
    while(out->buff[out->start] == '0') {
      out->start++;
      out->size--;
    }
  }
  return out;
}
noit_boolean
noit_http_response_flush(noit_http_session_ctx *ctx, noit_boolean final) {
  struct bchain *o, *r;
  int mask;

  if(ctx->res.closed == noit_true) return noit_false;
  if(ctx->res.output_started == noit_false) {
    _http_construct_leader(ctx);
    ctx->res.output_started = noit_true;
  }
  /* encode output to output_raw */
  r = ctx->res.output_raw;
  while(r && r->next) r = r->next;
  /* r is the last raw output link */
  o = ctx->res.output;
  /* o is the first output link to process */
  while(o) {
    struct bchain *tofree, *n;
    n = noit_http_process_output_bchain(ctx, o);
    if(!n) {
      /* Bad, response stops here! */
      noitL(noit_error, "noit_http_process_output_bchain: NULL\n");
      while(o) { tofree = o; o = o->next; free(tofree); }
      final = noit_true;
      break;
    }
    if(r) {
      r->next = n;
      n->prev = r;
      r = n;
    }
    else {
      r = ctx->res.output_raw = n;
    }
    tofree = o; o = o->next; free(tofree); /* advance and free */
  }
  ctx->res.output = NULL;
  if(final) {
    struct bchain *n;
    ctx->res.closed = noit_true;
    if(ctx->res.output_options & NOIT_HTTP_CHUNKED)
      n = bchain_from_data("0\r\n\r\n", 5);
    else
      n = bchain_from_data("\r\n", 2);
    if(r) {
      r->next = n;
      n->prev = r;
      r = n;
    }
    else {
      r = ctx->res.output_raw = n;
    }
  }

  _http_perform_write(ctx, &mask);
  if(ctx->conn.e) {
    eventer_update(ctx->conn.e, mask);
  }
  /* If the write fails completely, the event will be closed, freed and NULL */
  return ctx->conn.e ? noit_true : noit_false;
}
noit_boolean
noit_http_response_end(noit_http_session_ctx *ctx) {
  if(!noit_http_response_flush(ctx, noit_true)) return noit_false;
  return noit_true;
}
