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
#include "eventer/eventer.h"
#include "noit_conf.h"
#include "utils/noit_hash.h"
#include "utils/noit_log.h"
#include "utils/noit_str.h"
#include <jlog.h>
#include "noit_jlog_listener.h"
#include "noit_listener.h"
#include "noit_http.h"
#include "noit_rest.h"
#include "noit_check.h"
#include "noit_check_log_helpers.h"
#include "noit_livestream_listener.h"
#include "stratcon_realtime_http.h"
#include "stratcon_jlog_streamer.h"
#include "stratcon_datastore.h"

#include <ctype.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>

/*
 * it appears that GCC 4.5.2 incorrectly thinks that FULLREAD uses "mask"
 * without initializing it, so disable that specific warning for this file
 * for now
 */

#if __GNUC__ == 4 && __GNUC_MINOR__ == 5 && __GNUC_PATCHLEVEL__ == 2
#pragma GCC diagnostic ignored "-Wuninitialized"
#endif

typedef struct realtime_recv_ctx_t {
  int bytes_expected;
  int bytes_read;
  int bytes_written;
  int body_len;
  char *buffer;         /* These guys are for doing partial reads */

  enum {
    REALTIME_HTTP_WANT_INITIATE = 0,
    REALTIME_HTTP_WANT_SEND_INTERVAL = 1,
    REALTIME_HTTP_WANT_SEND_UUID = 2,
    REALTIME_HTTP_WANT_HEADER = 3,
    REALTIME_HTTP_WANT_BODY = 4,
  } state;
  int count;            /* Number of jlog messages we need to read */
  u_int32_t hack_inc_id;
  noit_http_session_ctx *ctx;
  struct realtime_tracker *rt;
} realtime_recv_ctx_t;

typedef struct realtime_context {
  enum { RC_INITIAL = 0, RC_REQ_RECV, RC_INTERESTS_RESOLVED, RC_FEEDING } setup;
  struct realtime_tracker *checklist;
  char *document_domain;
} realtime_context;

static realtime_context *alloc_realtime_context(const char *domain) {
  realtime_context *ctx;
  ctx = calloc(sizeof(*ctx), 1);
  ctx->document_domain = strdup(domain);
  return ctx;
}
static void free_realtime_tracker(struct realtime_tracker *rt) {
  if(rt->noit) free(rt->noit);
  free(rt);
}
static void clear_realtime_context(realtime_context *rc) {
 rc->setup = RC_INITIAL;
  while(rc->checklist) {
    struct realtime_tracker *tofree;
    tofree = rc->checklist;
    rc->checklist = tofree->next;
    free_realtime_tracker(tofree);
  }
  if(rc->document_domain) free(rc->document_domain);
  rc->document_domain = NULL;
}
int
stratcon_line_to_javascript(noit_http_session_ctx *ctx, char *in_buff,
                            u_int32_t *inc_id) {
  char buffer[1024];
  char *scp, *ecp, *token, *buff;
  int i, len, cnt;
  const char *v, *cb = NULL;
  noit_hash_table json = NOIT_HASH_EMPTY;
  noit_http_request *req = noit_http_session_request(ctx);
  char s_inc_id[42];
  char **outrows = NULL;

  cb = noit_http_request_querystring(req, "cb"); 
  for(v = cb; v && *v; v++)
    if(!((*v >= '0' && *v <= '9') ||
         (*v >= 'a' && *v <= 'z') ||
         (*v >= 'A' && *v <= 'Z') ||
         (*v == '_') || (*v == '.'))) {
      cb = NULL;
      break;
    }
  if(!cb) cb = "window.parent.plot_iframe_data";

#define BAIL_HTTP_WRITE do { \
  if(outrows) { \
    for(i=0;i<cnt;i++) if(outrows[i]) free(outrows[i]); \
    free(outrows); \
  } \
  noit_hash_destroy(&json, NULL, free); \
  noitL(noit_error, "javascript emit failed: %s:%s:%d\n", \
        __FILE__, __FUNCTION__, __LINE__); \
  return -1; \
} while(0)

#define PROCESS_NEXT_FIELD(t,l) do { \
  if(!*scp) goto bad_row; \
  ecp = strchr(scp, '\t'); \
  if(!ecp) goto bad_row; \
  t = scp; \
  l = (ecp-scp); \
  scp = ecp + 1; \
} while(0)
#define PROCESS_LAST_FIELD(t,l) do { \
  if(!*scp) ecp = scp; \
  else { \
    ecp = scp + strlen(scp); /* Puts us at the '\0' */ \
    if(*(ecp-1) == '\n') ecp--; /* We back up on letter if we ended in \n */ \
  } \
  t = scp; \
  l = (ecp-scp); \
} while(0)

  noitL(noit_error, "recv(%s)\n", in_buff);
  if(in_buff[0] == 'B' && in_buff[1] != '\0' && in_buff[2] == '\t') {
    cnt = noit_check_log_b_to_sm(in_buff, strlen(in_buff), &outrows, 0);
  }
  else {
    cnt = 1;
    outrows = malloc(sizeof(*outrows));
    outrows[0] = strdup(in_buff);
  }
  for(i=0; i<cnt; i++) {
    buff = outrows[i];
    if(!buff) continue;
    noitL(noit_error, "recv_xlt(%s)\n", buff);
    scp = buff;
    PROCESS_NEXT_FIELD(token,len); /* Skip the leader */
    if(buff[1] == '\t' && (buff[0] == 'M' || buff[0] == 'S')) {
      char target[256], module[256], name[256], uuid_str[UUID_STR_LEN+1];
      noit_http_request *req = noit_http_session_request(ctx);
      noit_hash_table *qs;
      noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
      const char *key;
      int klen, i=0;
      void *vval;
      char type[2] = { '\0', '\0' };
      type[0] = buff[0];

#define ra_write(a,b) if(noit_http_response_append(ctx, a, b) == noit_false) BAIL_HTTP_WRITE

      snprintf(s_inc_id, sizeof(s_inc_id), "script-%08x", (*inc_id)++);
      snprintf(buffer, sizeof(buffer), "<script id=\"%s\">%s({", s_inc_id, cb);
      ra_write(buffer, strlen(buffer));

      qs = noit_http_request_querystring_table(req);
      while(noit_hash_next(qs, &iter, &key, &klen, &vval)) {
        if(!strcmp(key, "cb")) continue;
        noit_hash_store(&json, key, klen, strdup(vval ?(char *)vval : "true"));
      }
      /* Time */
      noit_hash_store(&json, "script_id", 9, strdup(s_inc_id));
      noit_hash_store(&json, "type", 4, strdup(type));
      PROCESS_NEXT_FIELD(token,len);
      noit_hash_store(&json, "time", 4, noit__strndup(token, len));
      /* UUID */
      PROCESS_NEXT_FIELD(token,len);
      noit_check_extended_id_split(token, len, target, sizeof(target),
                                   module, sizeof(module), name, sizeof(name),
                                   uuid_str, sizeof(uuid_str));
      if(*uuid_str)
        noit_hash_store(&json, "id", 2,
                        noit__strndup(uuid_str, strlen(uuid_str)));
      if(*target)
        noit_hash_store(&json, "check_target", 12,
                        noit__strndup(target, strlen(target)));
      if(*module)
        noit_hash_store(&json, "check_module", 12,
                        noit__strndup(module, strlen(module)));
      if(*name)
        noit_hash_store(&json, "check_name", 10,
                        noit__strndup(name, strlen(name)));
      if(buff[0] == 'M') {
        /* name */
        PROCESS_NEXT_FIELD(token,len);
        noit_hash_store(&json, "metric_name", 11, noit__strndup(token, len));
        /* type */
        PROCESS_NEXT_FIELD(token,len);
        noit_hash_store(&json, "metric_type", 11, noit__strndup(token, len));
        /* value */
        PROCESS_LAST_FIELD(token,len); /* value */
        noit_hash_store(&json, "value", 5, noit__strndup(token, len));
      }
      else if(buff[0] == 'S') {
        /* state */
        PROCESS_NEXT_FIELD(token,len);
        noit_hash_store(&json, "check_state", 11, noit__strndup(token, len));
        /* availability */
        PROCESS_NEXT_FIELD(token,len);
        noit_hash_store(&json, "check_availability", 18, noit__strndup(token, len));
        /* duration */
        PROCESS_NEXT_FIELD(token,len);
        noit_hash_store(&json, "check_duration_ms", 17, noit__strndup(token, len));
        /* status */
        PROCESS_LAST_FIELD(token,len);
        noit_hash_store(&json, "status_message", 14, noit__strndup(token, len));
      }

      memset(&iter, 0, sizeof(iter));
      while(noit_hash_next(&json, &iter, &key, &klen, &vval)) {
        char *val = (char *)vval;
        if(i++) ra_write(",", 1);
        ra_write("\"", 1);
        ra_write(key, klen);
        ra_write("\":\"", 3);
        while(*val) {
          if(*val == '\"' || *val == '\\') {
            ra_write((char *)"\\", 1);
          }
          if(isprint(*val)) {
            ra_write((char *)val, 1);
          }
          else {
            char od[5];
            snprintf(od, sizeof(od), "\\%03o", *((unsigned char *)val));
            ra_write(od, strlen(od));
          }
          val++;
        }
        ra_write("\"", 1);
      }
      snprintf(buffer, sizeof(buffer), "});</script>\n");
      ra_write(buffer, strlen(buffer));

      if(noit_http_response_flush(ctx, noit_false) == noit_false) BAIL_HTTP_WRITE;
    }

    noit_hash_destroy(&json, NULL, free);
    memset(&json, 0, sizeof(json));
  }
  if(outrows) {
    for(i=0;i<cnt;i++) if(outrows[i]) free(outrows[i]);
    free(outrows);
  }

  return 0;

 bad_row:
  BAIL_HTTP_WRITE;
}
int
stratcon_realtime_uri_parse(realtime_context *rc, const char *uri) {
  int len, cnt = 0;
  const char *cp, *interest;
  char *copy, *brk;
  if(strncmp(uri, "/data/", 6)) return 0;
  cp = uri + 6;
  len = strlen(cp);
  copy = alloca(len + 1);
  if(!copy) return 0;
  memcpy(copy, cp, len);
  copy[len] = '\0';

  for (interest = strtok_r(copy, "/", &brk);
       interest;
       interest = strtok_r(NULL, "/", &brk)) {
    uuid_t in_uuid;
    struct realtime_tracker *node;
    char *interval;

    interval = strchr(interest, '@');
    if(!interval)
      interval = "5000";
    else
      *interval++ = '\0';
    if(uuid_parse((char *)interest, in_uuid)) continue;
    node = calloc(1, sizeof(*node));
    node->rc = rc;
    uuid_copy(node->checkid, in_uuid);
    node->interval = atoi(interval);
    node->next = rc->checklist;
    rc->checklist = node;
    cnt++;
  }
  return cnt;
}
static void
free_realtime_recv_ctx(void *vctx) {
  realtime_recv_ctx_t *rrctx = vctx;
  noit_http_session_ctx *ctx = rrctx->ctx;
  realtime_context *rc = noit_http_session_dispatcher_closure(ctx);

  if(noit_http_session_ref_dec(ctx) == 1) {
    noit_http_response_end(ctx);
    clear_realtime_context(rc);
    noit_http_session_trigger(ctx, EVENTER_WRITE | EVENTER_EXCEPTION);
  }
  free(rrctx);
}
#define Eread(a,b) e->opset->read(e->fd, (a), (b), &mask, e)
static int
__read_on_ctx(eventer_t e, realtime_recv_ctx_t *ctx, int *newmask) {
  int len, mask;
  while(ctx->bytes_read < ctx->bytes_expected) {
    len = Eread(ctx->buffer + ctx->bytes_read,
                ctx->bytes_expected - ctx->bytes_read);
    if(len < 0) {
      *newmask = mask;
      return -1;
    }
    /* if we get 0 inside SSL, and there was a real error, we
     * will actually get a -1 here.
     * if(len == 0) return ctx->bytes_read;
     */
    ctx->bytes_read += len;
  }
  assert(ctx->bytes_read == ctx->bytes_expected);
  return ctx->bytes_read;
}
#define FULLREAD(e,ctx,size) do { \
  int mask, len; \
  if(!ctx->bytes_expected) { \
    ctx->bytes_expected = size; \
    if(ctx->buffer) free(ctx->buffer); \
    ctx->buffer = malloc(size + 1); \
    if(ctx->buffer == NULL) { \
      noitL(noit_error, "malloc(%lu) failed.\n", (unsigned long)size + 1); \
      goto socket_error; \
    } \
    ctx->buffer[size] = '\0'; \
  } \
  len = __read_on_ctx(e, ctx, &mask); \
  if(len < 0) { \
    if(errno == EAGAIN) return mask | EVENTER_EXCEPTION; \
    noitL(noit_error, "SSL read error: %s\n", strerror(errno)); \
    goto socket_error; \
  } \
  ctx->bytes_read = 0; \
  ctx->bytes_expected = 0; \
  if(len != size) { \
    noitL(noit_error, "SSL short read [%d] (%d/%lu).  Reseting connection.\n", \
          ctx->state, len, (unsigned long)size); \
    goto socket_error; \
  } \
} while(0)

int
stratcon_realtime_recv_handler(eventer_t e, int mask, void *closure,
                               struct timeval *now) {
  static u_int32_t livestream_cmd = 0;
  noit_connection_ctx_t *nctx = closure;
  realtime_recv_ctx_t *ctx = nctx->consumer_ctx;
  int len;
  u_int32_t nint;
  char uuid_str[37];

  if(!livestream_cmd) livestream_cmd = htonl(NOIT_LIVESTREAM_DATA_FEED);

  if(mask & EVENTER_EXCEPTION || nctx->wants_shutdown) {
 socket_error:
    ctx->state = REALTIME_HTTP_WANT_INITIATE;
    ctx->count = 0;
    ctx->bytes_read = 0;
    ctx->bytes_written = 0;
    ctx->bytes_expected = 0;
    if(ctx->buffer) free(ctx->buffer);
    ctx->buffer = NULL;
    /* We close the event here and null it in the context
     * because the noit_connection_ctx_dealloc() will both close
     * it and free it (which our caller will double free) and
     * we consider double frees to be harmful.
     */
    eventer_remove_fd(e->fd);
    e->opset->close(e->fd, &mask, e);
    nctx->e = NULL;
    noit_connection_ctx_dealloc(nctx);
    return 0;
  }

#define full_nb_write(data, wlen) do { \
  if(!ctx->bytes_expected) { \
    ctx->bytes_written = 0; \
    ctx->bytes_expected = wlen; \
  } \
  while(ctx->bytes_written < ctx->bytes_expected) { \
    while(-1 == (len = e->opset->write(e->fd, ((char *)data) + ctx->bytes_written, \
                                       ctx->bytes_expected - ctx->bytes_written, \
                                       &mask, e)) && errno == EINTR); \
    if(len < 0) { \
      if(errno == EAGAIN) return mask | EVENTER_EXCEPTION; \
      goto socket_error; \
    } \
    ctx->bytes_written += len; \
  } \
  if(ctx->bytes_written != ctx->bytes_expected) { \
    noitL(noit_error, "short write on initiating stream [%d != %d].\n", \
          ctx->bytes_written, ctx->bytes_expected); \
    goto socket_error; \
  } \
  ctx->bytes_expected = 0; \
} while(0)

  noit_connection_update_timeout(nctx);
  while(1) {
    u_int32_t net_body_len;

    switch(ctx->state) {
      case REALTIME_HTTP_WANT_INITIATE:
        full_nb_write(&livestream_cmd, sizeof(livestream_cmd));
        ctx->state = REALTIME_HTTP_WANT_SEND_INTERVAL;
        /* FALLTHROUGH */
      case REALTIME_HTTP_WANT_SEND_INTERVAL:
        nint = htonl(ctx->rt->interval);
        full_nb_write(&nint, sizeof(nint));
        ctx->state = REALTIME_HTTP_WANT_SEND_UUID;
        /* FALLTHROUGH */
      case REALTIME_HTTP_WANT_SEND_UUID:
        uuid_unparse_lower(ctx->rt->checkid, uuid_str);
        full_nb_write(uuid_str, 36);
        ctx->state = REALTIME_HTTP_WANT_HEADER;
        /* FALLTHROUGH */
      case REALTIME_HTTP_WANT_HEADER:
        FULLREAD(e, ctx, sizeof(u_int32_t));
        memcpy(&net_body_len, ctx->buffer, sizeof(u_int32_t));
        ctx->body_len = ntohl(net_body_len);
        free(ctx->buffer); ctx->buffer = NULL;
        ctx->state = REALTIME_HTTP_WANT_BODY;
        break;
      case REALTIME_HTTP_WANT_BODY:
        FULLREAD(e, ctx, ctx->body_len);
        if(stratcon_line_to_javascript(ctx->ctx, ctx->buffer, &ctx->hack_inc_id)) goto socket_error;
        free(ctx->buffer); ctx->buffer = NULL;
        ctx->state = REALTIME_HTTP_WANT_HEADER;
        break;
    }
  }

}

int
stratcon_realtime_http_postresolve(eventer_t e, int mask, void *closure,
                                   struct timeval *now) {
  noit_http_session_ctx *ctx = closure;
  realtime_context *rc = noit_http_session_dispatcher_closure(ctx);
  struct realtime_tracker *node;

  for(node = rc->checklist; node; node = node->next) {
    if(node->noit) {
      realtime_recv_ctx_t *rrctx;
      rrctx = calloc(1, sizeof(*rrctx));
      rrctx->ctx = ctx;
      rrctx->rt = node;
      stratcon_streamer_connection(NULL, node->noit, "noit",
                                   stratcon_realtime_recv_handler,
                                   NULL, rrctx,
                                   free_realtime_recv_ctx);
    }
    else
      noit_http_session_ref_dec(ctx);
  }
  if(noit_http_session_ref_cnt(ctx) == 1) {
    noit_http_response_end(ctx);
    clear_realtime_context(rc);
    noit_http_session_trigger(ctx, EVENTER_WRITE);
  }
  return 0;
}
int
stratcon_request_dispatcher(noit_http_session_ctx *ctx) {
  const char *key, *value;
  realtime_context *rc = noit_http_session_dispatcher_closure(ctx);
  int klen;
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  noit_http_request *req = noit_http_session_request(ctx);

  if(rc->setup == RC_INITIAL) {
    eventer_t completion;
    struct realtime_tracker *node;
    char c[1024];
    int num_interests;
    const char *uri_str = noit_http_request_uri_str(req);
    noit_hash_table *headers = noit_http_request_headers_table(req);

    num_interests = stratcon_realtime_uri_parse(rc, uri_str);
    if(num_interests == 0) {
      noit_http_response_status_set(ctx, 404, "OK");
      noit_http_response_option_set(ctx, NOIT_HTTP_CLOSE);
      noit_http_response_end(ctx);
      return 0;
    }

    noitL(noit_error, "http: %s %s %s\n",
          noit_http_request_method_str(req), uri_str,
          noit_http_request_protocol_str(req));
    while(noit_hash_next_str(headers, &iter, &key, &klen, &value)) {
      noitL(noit_error, "http: [%s: %s]\n", key, value);
    }
    noit_http_response_status_set(ctx, 200, "OK");
    noit_http_response_option_set(ctx, NOIT_HTTP_CHUNKED);
    /*noit_http_response_option_set(ctx, NOIT_HTTP_GZIP);*/
    /*noit_http_response_option_set(ctx, NOIT_HTTP_DEFLATE);*/
    noit_http_response_header_set(ctx, "Content-Type", "text/html");

    snprintf(c, sizeof(c),
             "<html><head><script>document.domain='%s';</script></head><body>\n",
             rc->document_domain);
    noit_http_response_append(ctx, c, strlen(c));

    /* this dumb crap is to make some browsers happy (Safari) */
    memset(c, ' ', sizeof(c));
    noit_http_response_append(ctx, c, sizeof(c));
    noit_http_response_flush(ctx, noit_false);

    rc->setup = RC_REQ_RECV;
    /* Each interest references the ctx */
    for(node = rc->checklist; node; node = node->next) {
      char uuid_str[UUID_STR_LEN+1];
      noit_http_session_ref_inc(ctx);
      uuid_unparse_lower(node->checkid, uuid_str);
      noitL(noit_error, "Resolving uuid: %s\n", uuid_str);
    }
    completion = eventer_alloc();
    completion->mask = EVENTER_TIMER;
    completion->callback = stratcon_realtime_http_postresolve;
    completion->closure = ctx;
    gettimeofday(&completion->whence, NULL);
    stratcon_datastore_push(DS_OP_FIND_COMPLETE, NULL, NULL,
                            rc->checklist, completion);
  }
  return EVENTER_EXCEPTION;
}
int
stratcon_realtime_http_handler(eventer_t e, int mask, void *closure,
                               struct timeval *now) {
  int done = 0, rv;
  acceptor_closure_t *ac = closure;
  noit_http_session_ctx *http_ctx = ac->service_ctx;
  rv = noit_http_session_drive(e, mask, http_ctx, now, &done);
  if(done) acceptor_closure_free(ac);
  return rv;
}
static int
rest_stream_data(noit_http_rest_closure_t *restc,
                 int npats, char **pats) {
  /* We're here and want to subvert the rest system */
  const char *document_domain = NULL;
  noit_http_session_ctx *ctx = restc->http_ctx;
  noit_http_connection *conn = noit_http_session_connection(ctx);
  eventer_t e;
  acceptor_closure_t *ac = restc->ac;

  /* Rewire the handler */
  if(ac->service_ctx_free)
    ac->service_ctx_free(ac->service_ctx);
  ac->service_ctx = ctx;
  ac->service_ctx_free = noit_http_ctx_acceptor_free;

  if(!noit_hash_retr_str(ac->config,
                         "document_domain", strlen("document_domain"),
                         &document_domain)) {
    noitL(noit_error, "Document domain not set!  Realtime streaming will be broken\n");
    document_domain = "";
  }

  noit_http_process_querystring(noit_http_session_request(ctx));
  /* Rewire the http context */
  e = noit_http_connection_event(conn);
  e->callback = stratcon_realtime_http_handler;
  noit_http_session_set_dispatcher(ctx, stratcon_request_dispatcher,
                                   alloc_realtime_context(document_domain));
  return stratcon_request_dispatcher(ctx);
}

void
stratcon_realtime_http_init(const char *toplevel) {
  eventer_name_callback("stratcon_realtime_http",
                        stratcon_realtime_http_handler);
  eventer_name_callback("stratcon_realtime_recv",
                        stratcon_realtime_recv_handler);
  assert(noit_http_rest_register_auth(
    "GET", "/data/",
           "^((?:" UUID_REGEX "(?:@\\d+)?)(?:/" UUID_REGEX "(?:@\\d+)?)*)$",
    rest_stream_data, noit_http_rest_client_cert_auth
  ) == 0);
  assert(noit_http_rest_register_auth(
    "GET", "/", "^(.*)$", noit_rest_simple_file_handler,
           noit_http_rest_client_cert_auth
  ) == 0);
}
