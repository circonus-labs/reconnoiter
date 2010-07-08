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
#include "noit_http.h"
#include "utils/noit_str.h"

#include <errno.h>
#include <ctype.h>
#include <assert.h>
#include <zlib.h>
#include <libxml/tree.h>

#define REQ_PAT "\r\n\r\n"
#define REQ_PATSIZE 4
#define HEADER_CONTENT_LENGTH "content-length"
#define HEADER_EXPECT "expect"

static noit_log_stream_t http_debug = NULL;
static noit_log_stream_t http_io = NULL;
static noit_log_stream_t http_access = NULL;

#define CTX_ADD_HEADER(a,b) \
    noit_hash_replace(&ctx->res.headers, \
                      strdup(a), strlen(a), strdup(b), free, free)
static const char _hexchars[16] =
  {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
static void inplace_urldecode(char *c) {
  char *o = c;
  while(*c) {
    if(*c == '%') {
      int i, ord = 0;
      for(i = 0; i < 2; i++) {
        if(c[i] >= '0' && c[i] <= '9') ord = (ord << 4) | (c[i] - '0');
        else if (c[i] >= 'a' && c[i] <= 'f') ord = (ord << 4) | (c[i] - 'a');
        else if (c[i] >= 'A' && c[i] <= 'F') ord = (ord << 4) | (c[i] - 'A');
        else break;
      }
      if(i==2) {
        *((unsigned char *)o++) = ord;
        c+=3;
        continue;
      }
    }
    *o++ = *c++;
  }
  *o = '\0';
}

struct bchain *bchain_alloc(size_t size, int line) {
  struct bchain *n;
  n = malloc(size + (int)((char *)((struct bchain *)0)->buff));
  /*noitL(noit_error, "bchain_alloc(%p) : %d\n", n, line);*/
  if(!n) return NULL;
  n->prev = n->next = NULL;
  n->start = n->size = 0;
  n->allocd = size;
  return n;
}
void bchain_free(struct bchain *b, int line) {
  /*noitL(noit_error, "bchain_free(%p) : %d\n", b, line);*/
  free(b);
}
#define ALLOC_BCHAIN(s) bchain_alloc(s, __LINE__)
#define FREE_BCHAIN(a) bchain_free(a, __LINE__)
#define RELEASE_BCHAIN(a) do { \
  while(a) { \
    struct bchain *__b; \
    __b = a; \
    a = __b->next; \
    bchain_free(__b, __LINE__); \
  } \
} while(0)
struct bchain *bchain_from_data(const void *d, size_t size) {
  struct bchain *n;
  n = ALLOC_BCHAIN(size);
  if(!n) return NULL;
  memcpy(n->buff, d, size);
  n->size = size;
  return n;
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
   case 'P':
    if(!strcasecmp(s, "POST")) return NOIT_HTTP_POST;
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
        if(b->buff[start_in_b] == '\r' && b->buff[start_in_b+1] == '\n') {
          start_in_b += 2;
          break;
        }
        start_in_b--;
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
    f = ALLOC_BCHAIN(new_size);
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
static void
noit_http_log_request(noit_http_session_ctx *ctx) {
  char ip[64], timestr[64];
  double time_ms;
  struct tm *tm, tbuf;
  time_t now;
  struct timeval end_time, diff;

  if(ctx->req.start_time.tv_sec == 0) return;
  gettimeofday(&end_time, NULL);
  now = end_time.tv_sec;
  tm = gmtime_r(&now, &tbuf);
  strftime(timestr, sizeof(timestr), "%d/%b/%Y:%H:%M:%S -0000", tm);
  sub_timeval(end_time, ctx->req.start_time, &diff);
  time_ms = diff.tv_sec * 1000 + diff.tv_usec / 1000;
  noit_convert_sockaddr_to_buff(ip, sizeof(ip), &ctx->ac->remote.remote_addr);
  noitL(http_access, "%s - - [%s] \"%s %s%s%s %s\" %d %llu %.3f\n",
        ip, timestr,
        ctx->req.method_str, ctx->req.uri_str,
        ctx->req.orig_qs ? "?" : "", ctx->req.orig_qs ? ctx->req.orig_qs : "",
        ctx->req.protocol_str,
        ctx->res.status_code,
        (long long unsigned)ctx->res.bytes_written,
        time_ms);
}

static int
_http_perform_write(noit_http_session_ctx *ctx, int *mask) {
  int len, tlen = 0;
  struct bchain **head, *b;
 choose_bucket:
  head = ctx->res.leader ? &ctx->res.leader : &ctx->res.output_raw;
  b = *head;

  if(!ctx->conn.e) return 0;
#if 0
  if(ctx->res.output_started == noit_false) return EVENTER_EXCEPTION;
#endif
  if(!b) {
    if(ctx->res.closed) ctx->res.complete = noit_true;
    *mask = EVENTER_EXCEPTION;
    return tlen;
  }

  if(ctx->res.output_raw_offset >= b->size) {
    *head = b->next;
    FREE_BCHAIN(b);
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
    ctx->res.complete = noit_true;
    ctx->conn.needs_close = noit_true;
    noit_http_log_request(ctx);
    *mask |= EVENTER_EXCEPTION;
    return -1;
  }
  noitL(http_io, " http_write(%d) => %d [\n%.*s\n]\n", ctx->conn.e->fd,
        len, len, b->buff + b->start + ctx->res.output_raw_offset);
  ctx->res.output_raw_offset += len;
  ctx->res.bytes_written += len;
  tlen += len;
  goto choose_bucket;
}
static noit_boolean
noit_http_request_finalize_headers(noit_http_request *req, noit_boolean *err) {
  int start;
  void *vval;
  const char *mstr, *last_name = NULL;
  struct bchain *b;

  if(req->state != NOIT_HTTP_REQ_HEADERS) return noit_false;
  if(!req->current_input) req->current_input = req->first_input;
  if(!req->current_input) return noit_false;
  if(req->start_time.tv_sec == 0) gettimeofday(&req->start_time, NULL);
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
  noitL(http_debug, " noit_http_request_finalize : match(%d in %d)\n",
        (int)(req->current_offset - req->current_input->start),
        (int)req->current_input->size);
  if(req->current_offset <
     req->current_input->start + req->current_input->size) {
    /* There are left-overs */
    int lsize = req->current_input->size - req->current_offset;
    noitL(http_debug, " noit_http_request_finalize -- leftovers: %d\n", lsize);
    req->first_input = ALLOC_BCHAIN(lsize);
    req->first_input->prev = NULL;
    req->first_input->next = req->current_input->next;
    req->first_input->start = 0;
    req->first_input->size = lsize;
    memcpy(req->first_input->buff,
           req->current_input->buff + req->current_offset,
           req->first_input->size);
    req->current_input->size -= lsize;
    if(req->last_input == req->current_input)
      req->last_input = req->first_input;
    else
      FREE_BCHAIN(req->current_input);
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
        req->opts |= NOIT_HTTP_CLOSE;
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
          b = ALLOC_BCHAIN(l1 + l2 + 2);
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

  /* headers are done... we could need to read a payload */
  if(noit_hash_retrieve(&req->headers,
                        HEADER_CONTENT_LENGTH,
                        sizeof(HEADER_CONTENT_LENGTH)-1, &vval)) {
    const char *val = vval;
    req->has_payload = noit_true;
    req->content_length = strtoll(val, NULL, 10);
  }
  if(noit_hash_retrieve(&req->headers, HEADER_EXPECT,
                        sizeof(HEADER_EXPECT)-1, &vval)) {
    const char *val = vval;
    if(strncmp(val, "100-", 4) || /* Bad expect header */
       req->has_payload == noit_false) /* expect, but no content length */
      FAIL;
    /* We need to tell the client to "go-ahead" -- HTTP sucks */
    req->state = NOIT_HTTP_REQ_EXPECT;
    return noit_false;
  }
  if(req->content_length > 0) {
    /* switch modes... let's go read the payload */
    req->state = NOIT_HTTP_REQ_PAYLOAD;
    return noit_false;
  }

  req->complete = noit_true;
  return noit_true;
}
void
noit_http_process_querystring(noit_http_request *req) {
  char *cp, *interest, *brk;
  cp = strchr(req->uri_str, '?');
  if(!cp) return;
  *cp++ = '\0';
  req->orig_qs = strdup(cp);
  for (interest = strtok_r(cp, "&", &brk);
       interest;
       interest = strtok_r(NULL, "&", &brk)) {
    char *eq;
    eq = strchr(interest, '=');
    if(!eq) {
      inplace_urldecode(interest);
      noit_hash_store(&req->querystring, interest, strlen(interest), NULL);
    }
    else {
      *eq++ = '\0';
      inplace_urldecode(interest);
      inplace_urldecode(eq);
      noit_hash_store(&req->querystring, interest, strlen(interest), eq);
    }
  }
}
static noit_boolean
noit_http_request_finalize_payload(noit_http_request *req, noit_boolean *err) {
  req->complete = noit_true;
  return noit_true;
}
static noit_boolean
noit_http_request_finalize(noit_http_request *req, noit_boolean *err) {
  if(req->state == NOIT_HTTP_REQ_HEADERS)
    if(noit_http_request_finalize_headers(req, err)) return noit_true;
  if(req->state == NOIT_HTTP_REQ_EXPECT) return noit_false;
  if(req->state == NOIT_HTTP_REQ_PAYLOAD)
    if(noit_http_request_finalize_payload(req, err)) return noit_true;
  return noit_false;
}
static int
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

  while(1) {
    int len;

    in = ctx->req.last_input;
    if(!in) {
      in = ctx->req.first_input = ctx->req.last_input =
        ALLOC_BCHAIN(DEFAULT_BCHAINSIZE);
      if(!in) goto full_error;
    }
    if(in->size > 0 && /* we've read something */
       DEFAULT_BCHAINMINREAD > BCHAIN_SPACE(in) && /* we'd like read more */
       DEFAULT_BCHAINMINREAD < DEFAULT_BCHAINSIZE) { /* and we can */
      in->next = ctx->req.last_input =
        ALLOC_BCHAIN(DEFAULT_BCHAINSIZE);
      in->next->prev = in;
      in = in->next;
      if(!in) goto full_error;
    }

    len = ctx->conn.e->opset->read(ctx->conn.e->fd,
                                   in->buff + in->start + in->size,
                                   in->allocd - in->size - in->start,
                                   &mask, ctx->conn.e);
    noitL(http_debug, " noit_http -> read(%d) = %d\n", ctx->conn.e->fd, len);
    noitL(http_io, " noit_http:read(%d) => %d [\n%.*s\n]\n", ctx->conn.e->fd, len, len, in->buff + in->start + in->size);
    if(len == -1 && errno == EAGAIN) return mask;
    if(len <= 0) goto full_error;
    if(len > 0) in->size += len;
    rv = noit_http_request_finalize(&ctx->req, &err);
    if(len == -1 || err == noit_true) goto full_error;
    if(ctx->req.state == NOIT_HTTP_REQ_EXPECT) {
      const char *expect;
      ctx->req.state = NOIT_HTTP_REQ_PAYLOAD;
      assert(ctx->res.leader == NULL);
      expect = "HTTP/1.1 100 Continue\r\n\r\n";
      ctx->res.leader = bchain_from_data(expect, strlen(expect));
      _http_perform_write(ctx, &mask);
      ctx->req.complete = noit_true;
      if(ctx->res.leader != NULL) return mask;
    }
    if(rv == noit_true) return mask | EVENTER_WRITE | EVENTER_EXCEPTION;
  }
  /* Not reached:
   * return EVENTER_READ | EVENTER_EXCEPTION;
   */
}
noit_boolean
noit_http_session_prime_input(noit_http_session_ctx *ctx,
                              const void *data, size_t len) {
  if(ctx->req.first_input != NULL) return noit_false;
  if(len > DEFAULT_BCHAINSIZE) return noit_false;
  ctx->req.first_input = ctx->req.last_input =
      ALLOC_BCHAIN(DEFAULT_BCHAINSIZE);
  memcpy(ctx->req.first_input->buff, data, len);
  ctx->req.first_input->size = len;
  return noit_true;
}

void
noit_http_request_release(noit_http_session_ctx *ctx) {
  noit_hash_destroy(&ctx->req.querystring, NULL, NULL);
  noit_hash_destroy(&ctx->req.headers, NULL, NULL);
  /* If we expected a payload, we expect a trailing \r\n */
  if(ctx->req.has_payload) {
    int drained, mask;
    ctx->drainage = ctx->req.content_length - ctx->req.content_length_read;
    /* best effort, we'll drain it before the next request anyway */
    drained = noit_http_session_req_consume(ctx, NULL, ctx->drainage, &mask);
    ctx->drainage -= drained;
  }
  RELEASE_BCHAIN(ctx->req.current_request_chain);
  if(ctx->req.orig_qs) free(ctx->req.orig_qs);
  memset(&ctx->req.state, 0,
         sizeof(ctx->req) - (unsigned long)&(((noit_http_request *)0)->state));
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
    if(ctx->req.first_input) RELEASE_BCHAIN(ctx->req.first_input);
    noit_http_response_release(ctx);
    free(ctx);
  }
}
void
noit_http_ctx_acceptor_free(void *v) {
  noit_http_ctx_session_release((noit_http_session_ctx *)v);
}
int
noit_http_session_req_consume(noit_http_session_ctx *ctx,
                              void *buf, size_t len, int *mask) {
  size_t bytes_read = 0;
  /* We attempt to consume from the first_input */
  struct bchain *in, *tofree;
  noitL(http_debug, " ... noit_http_session_req_consume(%d) %d of %d\n",
        ctx->conn.e->fd, (int)len,
        (int)(ctx->req.content_length - ctx->req.content_length_read));
  len = MIN(len, ctx->req.content_length - ctx->req.content_length_read);
  while(bytes_read < len) {
    int crlen = 0;
    in = ctx->req.first_input;
    while(in && bytes_read < len) {
      int partial_len = MIN(in->size, len - bytes_read);
      if(buf) memcpy((char *)buf+bytes_read, in->buff+in->start, partial_len);
      bytes_read += partial_len;
      ctx->req.content_length_read += partial_len;
      noitL(http_debug, " ... filling %d bytes (read through %d/%d)\n",
            (int)bytes_read, (int)ctx->req.content_length_read,
            (int)ctx->req.content_length);
      in->start += partial_len;
      in->size -= partial_len;
      if(in->size == 0) {
        tofree = in;
        ctx->req.first_input = in = in->next;
        tofree->next = NULL;
        RELEASE_BCHAIN(tofree);
        if(in == NULL) {
          ctx->req.last_input = NULL;
          noitL(http_debug, " ... noit_http_session_req_consume = %d\n",
                (int)bytes_read);
          return bytes_read;
        }
      }
    }
    while(bytes_read + crlen < len) {
      int rlen;
      in = ctx->req.last_input;
      if(!in)
        in = ctx->req.first_input = ctx->req.last_input =
            ALLOC_BCHAIN(DEFAULT_BCHAINSIZE);
      else if(in->start + in->size >= in->allocd) {
        in->next = ALLOC_BCHAIN(DEFAULT_BCHAINSIZE);
        in = ctx->req.last_input = in->next;
      }
      /* pull next chunk */
      rlen = ctx->conn.e->opset->read(ctx->conn.e->fd,
                                      in->buff + in->start + in->size,
                                      in->allocd - in->size - in->start,
                                      mask, ctx->conn.e);
      noitL(http_debug, " noit_http -> read(%d) = %d\n", ctx->conn.e->fd, rlen);
    noitL(http_io, " noit_http:read(%d) => %d [\n%.*s\n]\n", ctx->conn.e->fd, rlen, rlen, in->buff + in->start + in->size);
      if(rlen == -1 && errno == EAGAIN) {
        /* We'd block to read more, but we have data,
         * so do a short read */
        if(ctx->req.first_input && ctx->req.first_input->size) break;
        /* We've got nothing... */
        noitL(http_debug, " ... noit_http_session_req_consume = -1 (EAGAIN)\n");
        return -1;
      }
      if(rlen <= 0) {
        noitL(http_debug, " ... noit_http_session_req_consume = -1 (error)\n");
        return -1;
      }
      in->size += rlen;
      crlen += rlen;
    }
  }
  /* NOT REACHED */
  return bytes_read;
}

int
noit_http_session_drive(eventer_t e, int origmask, void *closure,
                        struct timeval *now, int *done) {
  noit_http_session_ctx *ctx = closure;
  int rv = 0;
  int mask = origmask;

  if(origmask & EVENTER_EXCEPTION)
    goto abort_drive;

  /* Drainage -- this is as nasty as it sounds 
   * The last request could have unread upload content, we would have
   * noted that in noit_http_request_release.
   */
  noitL(http_debug, " -> noit_http_session_drive(%d) [%x]\n", e->fd, origmask);
  while(ctx->drainage > 0) {
    int len;
    noitL(http_debug, "   ... draining last request(%d)\n", e->fd);
    len = noit_http_session_req_consume(ctx, NULL, ctx->drainage, &mask);
    if(len == -1 && errno == EAGAIN) {
      noitL(http_debug, " <- noit_http_session_drive(%d) [%x]\n", e->fd, mask);
      return mask;
    }
    if(len <= 0) goto abort_drive;
    ctx->drainage -= len;
  }

 next_req:
  if(ctx->req.complete != noit_true) {
    int maybe_write_mask;
    noitL(http_debug, "   -> noit_http_complete_request(%d)\n", e->fd);
    mask = noit_http_complete_request(ctx, origmask);
    noitL(http_debug, "   <- noit_http_complete_request(%d) = %d\n",
          e->fd, mask);
    _http_perform_write(ctx, &maybe_write_mask);
    if(ctx->conn.e == NULL) goto release;
    if(ctx->req.complete != noit_true) {
      noitL(http_debug, " <- noit_http_session_drive(%d) [%x]\n", e->fd,
            mask|maybe_write_mask);
      return mask | maybe_write_mask;
    }
    noitL(http_debug, "HTTP start request (%s)\n", ctx->req.uri_str);
    noit_http_process_querystring(&ctx->req);
  }

  /* only dispatch if the response is not closed */
  if(ctx->res.closed == noit_false) {
    noitL(http_debug, "   -> dispatch(%d)\n", e->fd);
    rv = ctx->dispatcher(ctx);
    noitL(http_debug, "   <- dispatch(%d) = %d\n", e->fd, rv);
  }

  _http_perform_write(ctx, &mask);
  if(ctx->res.complete == noit_true &&
     ctx->conn.e &&
     ctx->conn.needs_close == noit_true) {
   abort_drive:
    noit_http_log_request(ctx);
    if(ctx->conn.e) {
      ctx->conn.e->opset->close(ctx->conn.e->fd, &mask, ctx->conn.e);
      ctx->conn.e = NULL;
    }
    goto release;
  }
  if(ctx->res.complete == noit_true) {
    noit_http_log_request(ctx);
    noit_http_request_release(ctx);
    noit_http_response_release(ctx);
  }
  if(ctx->req.complete == noit_false) goto next_req;
  if(ctx->conn.e) {
    noitL(http_debug, " <- noit_http_session_drive(%d) [%x]\n", e->fd, mask|rv);
    return mask | rv;
  }
  noitL(http_debug, " <- noit_http_session_drive(%d) [%x]\n", e->fd, 0);
  goto abort_drive;

 release:
  *done = 1;
  /* We're about to release, unhook us from the acceptor_closure so we
   * don't get double freed */
  if(ctx->ac->service_ctx == ctx) ctx->ac->service_ctx = NULL;
  noit_http_ctx_session_release(ctx);
  noitL(http_debug, " <- noit_http_session_drive(%d) [%x]\n", e->fd, 0);
  return 0;
}

noit_http_session_ctx *
noit_http_session_ctx_new(noit_http_dispatch_func f, void *c, eventer_t e,
                          acceptor_closure_t *ac) {
  noit_http_session_ctx *ctx;
  ctx = calloc(1, sizeof(*ctx));
  ctx->ref_cnt = 1;
  ctx->req.complete = noit_false;
  ctx->conn.e = e;
  ctx->dispatcher = f;
  ctx->dispatcher_closure = c;
  ctx->ac = ac;
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
    assert(ctx->res.output = ALLOC_BCHAIN(DEFAULT_BCHAINSIZE));
  o = ctx->res.output;
  while(o->next) o = o->next;
  while(l > 0) {
    if(o->allocd == o->start + o->size) {
      /* Filled up, need another */
      o->next = ALLOC_BCHAIN(DEFAULT_BCHAINSIZE);
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
  ctx->res.leader = b = ALLOC_BCHAIN(DEFAULT_BCHAINSIZE);

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
    b->next = ALLOC_BCHAIN(DEFAULT_BCHAINSIZE); \
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
  int ilen, maxlen = in->size, hexlen;
  int opts = ctx->res.output_options;

  /* a chunked header looks like: hex*\r\ndata\r\n */
  /* let's assume that content never gets "larger" */
  if(opts & NOIT_HTTP_GZIP) maxlen = deflateBound(NULL, in->size);
  else if(opts & NOIT_HTTP_DEFLATE) maxlen = compressBound(in->size);

  /* So, the link size is the len(data) + 4 + ceil(log(len(data))/log(16)) */
  ilen = maxlen;
  hexlen = 0;
  while(ilen) { ilen >>= 4; hexlen++; }
  if(hexlen == 0) hexlen = 1;

  out = ALLOC_BCHAIN(hexlen + 4 + maxlen);
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
  int mask, rv;

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
    tofree = o; o = o->next; FREE_BCHAIN(tofree); /* advance and free */
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
    }
    else {
      ctx->res.output_raw = n;
    }
  }

  rv = _http_perform_write(ctx, &mask);
  if(ctx->conn.e) {
    eventer_update(ctx->conn.e, mask);
  }
  if(rv < 0) return noit_false;
  /* If the write fails completely, the event will not be closed,
   * the following should not trigger the false case.
   */
  return ctx->conn.e ? noit_true : noit_false;
}

noit_boolean
noit_http_response_end(noit_http_session_ctx *ctx) {
  if(!noit_http_response_flush(ctx, noit_true)) return noit_false;
  return noit_true;
}


/* Helper functions */

static int
noit_http_write_xml(void *vctx, const char *buffer, int len) {
  if(noit_http_response_append((noit_http_session_ctx *)vctx, buffer, len))
    return len;
  return -1;
}
static int
noit_http_close_xml(void *vctx) {
  noit_http_response_end((noit_http_session_ctx *)vctx);
  return 0;
}
void
noit_http_response_xml(noit_http_session_ctx *ctx, xmlDocPtr doc) {
  xmlOutputBufferPtr out;
  xmlCharEncodingHandlerPtr enc;
  enc = xmlGetCharEncodingHandler(XML_CHAR_ENCODING_UTF8);
  out = xmlOutputBufferCreateIO(noit_http_write_xml,
                                noit_http_close_xml,
                                ctx, enc);
  xmlSaveFormatFileTo(out, doc, "utf8", 1);
}

void
noit_http_init() {
  http_debug = noit_log_stream_find("debug/http");
  http_access = noit_log_stream_find("http/access");
  http_io = noit_log_stream_find("http/io");
}
