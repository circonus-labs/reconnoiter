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
#include "noit_listener.h"
#include "noit_http.h"
#include "noit_livestream_listener.h"
#include "stratcon_realtime_http.h"
#include "stratcon_jlog_streamer.h"
#include "stratcon_datastore.h"

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


typedef struct realtime_recv_ctx_t {
  int bytes_expected;
  int bytes_read;
  int bytes_written;
  int body_len;
  char *buffer;         /* These guys are for doing partial reads */

  enum {
    WANT_INITIATE = 0,
    WANT_SEND_INTERVAL = 1,
    WANT_SEND_UUID = 2,
    WANT_HEADER = 3,
    WANT_BODY = 4,
  } state;
  int count;            /* Number of jlog messages we need to read */
  noit_http_session_ctx *ctx;
  struct realtime_tracker *rt;
} realtime_recv_ctx_t;

typedef struct realtime_context {
  enum { RC_INITIAL = 0, RC_REQ_RECV, RC_INTERESTS_RESOLVED, RC_FEEDING } setup;
  struct realtime_tracker *checklist;
} realtime_context;

static realtime_context *alloc_realtime_context() {
  realtime_context *ctx;
  return calloc(sizeof(*ctx), 1);
}
static void free_realtime_tracker(struct realtime_tracker *rt) {
  if(rt->noit) free(rt->noit);
  free(rt);
}
static void clear_realtime_context(realtime_context *rc) {
  while(rc->checklist) {
    struct realtime_tracker *tofree;
    tofree = rc->checklist;
    rc->checklist = tofree->next;
    free_realtime_tracker(tofree);
  }
}
int
stratcon_line_to_javascript(noit_http_session_ctx *ctx, char *buff) {
  char buffer[1024];
  char *scp, *ecp, *token;
  int len;

#define BAIL_HTTP_WRITE do { \
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

  scp = buff;
  PROCESS_NEXT_FIELD(token,len); /* Skip the leader */
  if(buff[0] == 'M') {
    snprintf(buffer, sizeof(buffer), "<script>window.parent.plot_iframe_data('");
    if(noit_http_response_append(ctx, buffer, strlen(buffer)) == noit_false) BAIL_HTTP_WRITE;

    /* Time */
    PROCESS_NEXT_FIELD(token,len);
    if(noit_http_response_append(ctx, token, len) == noit_false) BAIL_HTTP_WRITE;

    snprintf(buffer, sizeof(buffer), "', '");
    if(noit_http_response_append(ctx, buffer, strlen(buffer)) == noit_false) BAIL_HTTP_WRITE;

    /* UUID */
    PROCESS_NEXT_FIELD(token,len);
    if(noit_http_response_append(ctx, token, len) == noit_false) BAIL_HTTP_WRITE;

    snprintf(buffer, sizeof(buffer), "', '");
    if(noit_http_response_append(ctx, buffer, strlen(buffer)) == noit_false) BAIL_HTTP_WRITE;

    /* name */
    PROCESS_NEXT_FIELD(token,len);
    if(noit_http_response_append(ctx, token, len) == noit_false) BAIL_HTTP_WRITE;

    snprintf(buffer, sizeof(buffer), "', '");
    if(noit_http_response_append(ctx, buffer, strlen(buffer)) == noit_false) BAIL_HTTP_WRITE;

    PROCESS_NEXT_FIELD(token,len); /* skip type */
    PROCESS_LAST_FIELD(token,len); /* value */
    if(noit_http_response_append(ctx, token, len) == noit_false) BAIL_HTTP_WRITE;

    snprintf(buffer, sizeof(buffer), "');</script>\n");
    if(noit_http_response_append(ctx, buffer, strlen(buffer)) == noit_false) BAIL_HTTP_WRITE;

    if(noit_http_response_flush(ctx, noit_false) == noit_false) BAIL_HTTP_WRITE;
  }

  return 0;

 bad_row:
  BAIL_HTTP_WRITE;
  if(0) {
    noit_http_response_end(ctx);
    memset(ctx->dispatcher_closure, 0, sizeof(realtime_context));
    if(ctx->conn.e) eventer_trigger(ctx->conn.e, EVENTER_WRITE);
    return 0;
  }
}
int
stratcon_realtime_uri_parse(realtime_context *rc, char *uri) {
  int len, cnt = 0;
  char *cp, *copy, *interest, *brk;
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
    struct realtime_tracker *node;
    char *interval;

    interval = strchr(interest, '@');
    if(!interval)
      interval = "5000";
    else
      *interval++ = '\0';
    node = calloc(1, sizeof(*node));
    node->rc = rc;
    node->sid = atoi(interest);
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
  noit_atomic_dec32(&rrctx->ctx->ref_cnt);
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
    ctx->state = WANT_INITIATE;
    ctx->count = 0;
    ctx->bytes_read = 0;
    ctx->bytes_written = 0;
    ctx->bytes_expected = 0;
    if(ctx->buffer) free(ctx->buffer);
    ctx->buffer = NULL;
    free_realtime_recv_ctx(ctx);
    eventer_remove_fd(e->fd);
    e->opset->close(e->fd, &mask, e);
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

  while(1) {
    u_int32_t net_body_len;

    switch(ctx->state) {
      case WANT_INITIATE:
        full_nb_write(&livestream_cmd, sizeof(livestream_cmd));
        ctx->state = WANT_SEND_INTERVAL;
      case WANT_SEND_INTERVAL:
        nint = htonl(ctx->rt->interval);
        full_nb_write(&nint, sizeof(nint));
        ctx->state = WANT_SEND_UUID;
      case WANT_SEND_UUID:
        uuid_unparse_lower(ctx->rt->checkid, uuid_str);
        full_nb_write(uuid_str, 36);
        ctx->state = WANT_HEADER;
      case WANT_HEADER:
        FULLREAD(e, ctx, sizeof(u_int32_t));
        memcpy(&net_body_len, ctx->buffer, sizeof(u_int32_t));
        ctx->body_len = ntohl(net_body_len);
        free(ctx->buffer); ctx->buffer = NULL;
        ctx->state = WANT_BODY;
        break;
      case WANT_BODY:
        FULLREAD(e, ctx, ctx->body_len);
        noitL(noit_error, "Read: '%s'\n", ctx->buffer);
        if(stratcon_line_to_javascript(ctx->ctx, ctx->buffer)) goto socket_error;
        free(ctx->buffer); ctx->buffer = NULL;
        ctx->state = WANT_HEADER;
        break;
    }
  }

}

int
stratcon_realtime_http_postresolve(eventer_t e, int mask, void *closure,
                                   struct timeval *now) {
  noit_http_session_ctx *ctx = closure;
  realtime_context *rc = ctx->dispatcher_closure;
  struct realtime_tracker *node;

  for(node = rc->checklist; node; node = node->next) {
    if(node->noit) {
      realtime_recv_ctx_t *rrctx;
      rrctx = calloc(1, sizeof(*rrctx));
      rrctx->ctx = ctx;
      rrctx->rt = node;
      stratcon_streamer_connection(NULL, node->noit,
                                   stratcon_realtime_recv_handler,
                                   NULL, rrctx,
                                   free_realtime_recv_ctx);
    }
    else
      noit_atomic_dec32(&ctx->ref_cnt);
  }
  if(ctx->ref_cnt == 1) {
    noit_http_response_end(ctx);
    clear_realtime_context(rc);
    memset(ctx->dispatcher_closure, 0, sizeof(realtime_context));
    if(ctx->conn.e) eventer_trigger(ctx->conn.e, EVENTER_WRITE);
  }
  return 0;
}
int
stratcon_request_dispatcher(noit_http_session_ctx *ctx) {
  const char *key, *value;
  realtime_context *rc = ctx->dispatcher_closure;
  int klen;
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  noit_http_request *req = &ctx->req;

  if(rc->setup == RC_INITIAL) {
    eventer_t completion;
    struct realtime_tracker *node;
    char c[1024];
    int num_interests;

    num_interests = stratcon_realtime_uri_parse(rc, ctx->req.uri_str);
    if(num_interests == 0) {
      noit_http_response_status_set(ctx, 404, "OK");
      noit_http_response_option_set(ctx, NOIT_HTTP_CLOSE);
      noit_http_response_end(ctx);
      return 0;
    }

    noitL(noit_error, "http: %s %s %s\n",
          req->method_str, req->uri_str, req->protocol_str);
    while(noit_hash_next(&req->headers, &iter, &key, &klen, (void **)&value)) {
      noitL(noit_error, "http: [%s: %s]\n", key, value);
    }
    noit_http_response_status_set(ctx, 200, "OK");
    noit_http_response_option_set(ctx, NOIT_HTTP_CHUNKED);
    /*noit_http_response_option_set(ctx, NOIT_HTTP_DEFLATE);*/
    noit_http_response_header_set(ctx, "Content-Type", "text/html");

    snprintf(c, sizeof(c), "<html><head><script>document.domain='omniti.com';</script></head><body>\n");
    noit_http_response_append(ctx, c, strlen(c));

    /* this dumb crap is to make some browsers happy (Safari) */
    memset(c, ' ', sizeof(c));
    noit_http_response_append(ctx, c, sizeof(c));
    noit_http_response_flush(ctx, noit_false);

    rc->setup = RC_REQ_RECV;
    /* Each interest references the ctx */
    for(node = rc->checklist; node; node = node->next) {
      noit_atomic_inc32(&ctx->ref_cnt);
      stratcon_datastore_push(DS_OP_FIND, NULL, node);
      noitL(noit_error, "Resolving sid: %d\n", node->sid);
    }
    completion = eventer_alloc();
    completion->mask = EVENTER_TIMER;
    completion->callback = stratcon_realtime_http_postresolve;
    completion->closure = ctx;
    gettimeofday(&completion->whence, NULL);
    stratcon_datastore_push(DS_OP_FIND_COMPLETE, NULL, completion);
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
