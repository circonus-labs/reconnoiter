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
#include "noit_jlog_listener.h"
#include "noit_rest.h"
#include "stratcon_datastore.h"
#include "stratcon_jlog_streamer.h"
#include "stratcon_iep.h"

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

pthread_mutex_t noits_lock;
noit_hash_table noits = NOIT_HASH_EMPTY;

static void noit_connection_initiate_connection(noit_connection_ctx_t *ctx);

static const char *feed_type_to_str(int jlog_feed_cmd) {
  switch(jlog_feed_cmd) {
    case NOIT_JLOG_DATA_FEED: return "durable/storage";
    case NOIT_JLOG_DATA_TEMP_FEED: return "transient/iep";
  }
  return "unknown";
}

static int
remote_str_sort(const void *a, const void *b) {
  int rv;
  noit_connection_ctx_t * const *actx = a;
  noit_connection_ctx_t * const *bctx = b;
  jlog_streamer_ctx_t *ajctx = (*actx)->consumer_ctx;
  jlog_streamer_ctx_t *bjctx = (*bctx)->consumer_ctx;
  rv = strcmp((*actx)->remote_str, (*bctx)->remote_str);
  if(rv) return rv;
  return (ajctx->jlog_feed_cmd < bjctx->jlog_feed_cmd) ? -1 :
           ((ajctx->jlog_feed_cmd == bjctx->jlog_feed_cmd) ? 0 : 1);
}
static void
nc_print_noit_conn_brief(noit_console_closure_t ncct,
                          noit_connection_ctx_t *ctx) {
  jlog_streamer_ctx_t *jctx = ctx->consumer_ctx;
  struct timeval now, diff, session_duration;
  const char *feedtype = "unknown";
  const char *lasttime = "never";
  if(ctx->last_connect.tv_sec != 0) {
    char cmdbuf[4096];
    time_t r = ctx->last_connect.tv_sec;
    struct tm tbuf, *tm;
    tm = gmtime_r(&r, &tbuf);
    strftime(cmdbuf, sizeof(cmdbuf), "%Y-%m-%d %H:%M:%S UTC", tm);
    lasttime = cmdbuf;
  }
  nc_printf(ncct, "%s [%s]:\n\tLast connect: %s\n", ctx->remote_str,
            ctx->remote_cn ? "connected" :
                             (ctx->timeout_event ? "disconnected" :
                                                   "connecting"), lasttime);
  if(ctx->e) {
    char buff[128];
    const char *addrstr = NULL;
    struct sockaddr_in6 addr6;
    socklen_t len = sizeof(addr6);
    if(getsockname(ctx->e->fd, (struct sockaddr *)&addr6, &len) == 0) {
      unsigned short port = 0;
      if(addr6.sin6_family == AF_INET) {
        addrstr = inet_ntop(addr6.sin6_family,
                            &((struct sockaddr_in *)&addr6)->sin_addr,
                            buff, sizeof(buff));
        port = ntohs(((struct sockaddr_in *)&addr6)->sin_port);
      }
      else if(addr6.sin6_family == AF_INET6) {
        addrstr = inet_ntop(addr6.sin6_family, &addr6.sin6_addr,
                            buff, sizeof(buff));
        port = ntohs(addr6.sin6_port);
      }
      if(addrstr != NULL)
        nc_printf(ncct, "\tLocal address is %s:%u\n", buff, port);
      else
        nc_printf(ncct, "\tLocal address not interpretable\n");
    }
    else {
      nc_printf(ncct, "\tLocal address error[%d]: %s\n",
                ctx->e->fd, strerror(errno));
    }
  }
  feedtype = feed_type_to_str(ntohl(jctx->jlog_feed_cmd));
  nc_printf(ncct, "\tJLog event streamer [%s]\n", feedtype);
  gettimeofday(&now, NULL);
  if(ctx->timeout_event) {
    sub_timeval(ctx->timeout_event->whence, now, &diff);
    nc_printf(ncct, "\tNext attempt in %lld.%06us\n",
              (long long)diff.tv_sec, (unsigned int) diff.tv_usec);
  }
  else if(ctx->remote_cn) {
    nc_printf(ncct, "\tRemote CN: '%s'\n",
              ctx->remote_cn ? ctx->remote_cn : "???");
    if(ctx->consumer_callback == stratcon_jlog_recv_handler) {
      struct timeval last;
      double session_duration_seconds;
      const char *state = "unknown";

      switch(jctx->state) {
        case JLOG_STREAMER_WANT_INITIATE: state = "initiate"; break;
        case JLOG_STREAMER_WANT_COUNT: state = "waiting for next batch"; break;
        case JLOG_STREAMER_WANT_ERROR: state = "waiting for error"; break;
        case JLOG_STREAMER_WANT_HEADER: state = "reading header"; break;
        case JLOG_STREAMER_WANT_BODY: state = "reading body"; break;
        case JLOG_STREAMER_IS_ASYNC: state = "asynchronously processing"; break;
        case JLOG_STREAMER_WANT_CHKPT: state = "checkpointing"; break;
      }
      last.tv_sec = jctx->header.tv_sec;
      last.tv_usec = jctx->header.tv_usec;
      sub_timeval(now, last, &diff);
      sub_timeval(now, ctx->last_connect, &session_duration);
      session_duration_seconds = session_duration.tv_sec +
                                 (double)session_duration.tv_usec/1000000.0;
      nc_printf(ncct, "\tState: %s\n"
                      "\tNext checkpoint: [%08x:%08x]\n"
                      "\tLast event: %lld.%06us ago\n"
                      "\tEvents this session: %llu (%0.2f/s)\n"
                      "\tOctets this session: %llu (%0.2f/s)\n",
                state,
                jctx->header.chkpt.log, jctx->header.chkpt.marker,
                (long long)diff.tv_sec, (unsigned int)diff.tv_usec,
                jctx->total_events,
                (double)jctx->total_events/session_duration_seconds,
                jctx->total_bytes_read,
                (double)jctx->total_bytes_read/session_duration_seconds);
    }
    else {
      nc_printf(ncct, "\tUnknown type.\n");
    }
  }
}

jlog_streamer_ctx_t *
stratcon_jlog_streamer_datastore_ctx_alloc(void) {
  jlog_streamer_ctx_t *ctx;
  ctx = stratcon_jlog_streamer_ctx_alloc();
  ctx->jlog_feed_cmd = htonl(NOIT_JLOG_DATA_FEED);
  ctx->push = stratcon_datastore_push;
  return ctx;
}
jlog_streamer_ctx_t *
stratcon_jlog_streamer_ctx_alloc(void) {
  jlog_streamer_ctx_t *ctx;
  ctx = calloc(1, sizeof(*ctx));
  return ctx;
}
noit_connection_ctx_t *
noit_connection_ctx_alloc(void) {
  noit_connection_ctx_t *ctx, **pctx;
  ctx = calloc(1, sizeof(*ctx));
  ctx->refcnt = 1;
  pctx = malloc(sizeof(*pctx));
  *pctx = ctx;
  pthread_mutex_lock(&noits_lock);
  noit_hash_store(&noits, (const char *)pctx, sizeof(*pctx), ctx);
  pthread_mutex_unlock(&noits_lock);
  return ctx;
}
int
noit_connection_reinitiate(eventer_t e, int mask, void *closure,
                         struct timeval *now) {
  noit_connection_ctx_t *ctx = closure;
  ctx->timeout_event = NULL;
  noit_connection_initiate_connection(closure);
  return 0;
}
void
noit_connection_schedule_reattempt(noit_connection_ctx_t *ctx,
                                   struct timeval *now) {
  struct timeval __now, interval;
  const char *v;
  u_int32_t min_interval = 1000, max_interval = 8000;
  if(ctx->remote_cn) {
    free(ctx->remote_cn);
    ctx->remote_cn = NULL;
  }
  if(noit_hash_retr_str(ctx->config,
                        "reconnect_initial_interval",
                        strlen("reconnect_initial_interval"),
                        &v)) {
    min_interval = MAX(atoi(v), 100); /* .1 second minimum */
  }
  if(noit_hash_retr_str(ctx->config,
                        "reconnect_maximum_interval",
                        strlen("reconnect_maximum_interval"),
                        &v)) {
    max_interval = MIN(atoi(v), 3600*1000); /* 1 hour maximum */
  }
  if(ctx->current_backoff == 0) ctx->current_backoff = min_interval;
  else {
    ctx->current_backoff *= 2;
    ctx->current_backoff = MAX(min_interval, ctx->current_backoff);
    ctx->current_backoff = MIN(max_interval, ctx->current_backoff);
  }
  if(!now) {
    gettimeofday(&__now, NULL);
    now = &__now;
  }
  interval.tv_sec = ctx->current_backoff / 1000;
  interval.tv_usec = (ctx->current_backoff % 1000) * 1000;
  noitL(noit_debug, "Next jlog_streamer attempt in %ums\n",
        ctx->current_backoff);
  if(ctx->timeout_event)
    eventer_remove(ctx->timeout_event);
  else
    ctx->timeout_event = eventer_alloc();
  ctx->timeout_event->callback = noit_connection_reinitiate;
  ctx->timeout_event->closure = ctx;
  ctx->timeout_event->mask = EVENTER_TIMER;
  add_timeval(*now, interval, &ctx->timeout_event->whence);
  eventer_add(ctx->timeout_event);
}
static void
noit_connection_ctx_free(noit_connection_ctx_t *ctx) {
  if(ctx->remote_cn) free(ctx->remote_cn);
  if(ctx->remote_str) free(ctx->remote_str);
  if(ctx->timeout_event) {
    eventer_remove(ctx->timeout_event);
    eventer_free(ctx->timeout_event);
  }
  ctx->consumer_free(ctx->consumer_ctx);
  free(ctx);
}
void
noit_connection_ctx_deref(noit_connection_ctx_t *ctx) {
  if(noit_atomic_dec32(&ctx->refcnt) == 0)
    noit_connection_ctx_free(ctx);
}
void
noit_connection_ctx_dealloc(noit_connection_ctx_t *ctx) {
  noit_connection_ctx_t **pctx = &ctx;
  pthread_mutex_lock(&noits_lock);
  noit_hash_delete(&noits, (const char *)pctx, sizeof(*pctx),
                   free, (void (*)(void *))noit_connection_ctx_deref);
  pthread_mutex_unlock(&noits_lock);
}
void
jlog_streamer_ctx_free(void *cl) {
  jlog_streamer_ctx_t *ctx = cl;
  if(ctx->buffer) free(ctx->buffer);
  free(ctx);
}

#define Eread(a,b) e->opset->read(e->fd, (a), (b), &mask, e)
static int
__read_on_ctx(eventer_t e, jlog_streamer_ctx_t *ctx, int *newmask) {
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
    ctx->total_bytes_read += len;
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
      noitL(noit_error, "malloc(%lu) failed.\n", (long unsigned int)size + 1); \
      goto socket_error; \
    } \
    ctx->buffer[size] = '\0'; \
  } \
  len = __read_on_ctx(e, ctx, &mask); \
  if(len < 0) { \
    if(errno == EAGAIN) return mask | EVENTER_EXCEPTION; \
    noitL(noit_error, "[%s] SSL read error: %s\n", nctx->remote_str, strerror(errno)); \
    goto socket_error; \
  } \
  ctx->bytes_read = 0; \
  ctx->bytes_expected = 0; \
  if(len != size) { \
    noitL(noit_error, "[%s] SSL short read [%d] (%d/%lu).  Reseting connection.\n", \
          nctx->remote_str, ctx->state, len, (long unsigned int)size); \
    goto socket_error; \
  } \
} while(0)

int
stratcon_jlog_recv_handler(eventer_t e, int mask, void *closure,
                           struct timeval *now) {
  noit_connection_ctx_t *nctx = closure;
  jlog_streamer_ctx_t *ctx = nctx->consumer_ctx;
  jlog_streamer_ctx_t dummy;
  int len;
  jlog_id n_chkpt;

  if(mask & EVENTER_EXCEPTION || nctx->wants_shutdown) {
    if(write(e->fd, e, 0) == -1)
      noitL(noit_error, "socket error: %s\n", strerror(errno));
 socket_error:
    ctx->state = JLOG_STREAMER_WANT_INITIATE;
    ctx->count = 0;
    ctx->bytes_read = 0;
    ctx->bytes_expected = 0;
    if(ctx->buffer) free(ctx->buffer);
    ctx->buffer = NULL;
    noit_connection_schedule_reattempt(nctx, now);
    eventer_remove_fd(e->fd);
    nctx->e = NULL;
    e->opset->close(e->fd, &mask, e);
    return 0;
  }

  while(1) {
    switch(ctx->state) {
      case JLOG_STREAMER_WANT_INITIATE:
        len = e->opset->write(e->fd, &ctx->jlog_feed_cmd,
                              sizeof(ctx->jlog_feed_cmd),
                              &mask, e);
        if(len < 0) {
          if(errno == EAGAIN) return mask | EVENTER_EXCEPTION;
          goto socket_error;
        }
        if(len != sizeof(ctx->jlog_feed_cmd)) {
          noitL(noit_error, "short write [%d/%d] on initiating stream.\n", 
                (int)len, (int)sizeof(ctx->jlog_feed_cmd));
          goto socket_error;
        }
        ctx->state = JLOG_STREAMER_WANT_COUNT;
        break;

      case JLOG_STREAMER_WANT_ERROR:
        FULLREAD(e, ctx, 0 - ctx->count);
        noitL(noit_error, "[%s] %.*s\n", nctx->remote_str,
              0 - ctx->count, ctx->buffer);
        free(ctx->buffer); ctx->buffer = NULL;
        goto socket_error;
        break;

      case JLOG_STREAMER_WANT_COUNT:
        FULLREAD(e, ctx, sizeof(u_int32_t));
        memcpy(&dummy.count, ctx->buffer, sizeof(u_int32_t));
        ctx->count = ntohl(dummy.count);
        free(ctx->buffer); ctx->buffer = NULL;
        if(ctx->count < 0)
          ctx->state = JLOG_STREAMER_WANT_ERROR;
        else
          ctx->state = JLOG_STREAMER_WANT_HEADER;
        break;

      case JLOG_STREAMER_WANT_HEADER:
        if(ctx->count == 0) {
          ctx->state = JLOG_STREAMER_WANT_COUNT;
          break;
        }
        FULLREAD(e, ctx, sizeof(ctx->header));
        memcpy(&dummy.header, ctx->buffer, sizeof(ctx->header));
        ctx->header.chkpt.log = ntohl(dummy.header.chkpt.log);
        ctx->header.chkpt.marker = ntohl(dummy.header.chkpt.marker);
        ctx->header.tv_sec = ntohl(dummy.header.tv_sec);
        ctx->header.tv_usec = ntohl(dummy.header.tv_usec);
        ctx->header.message_len = ntohl(dummy.header.message_len);
        free(ctx->buffer); ctx->buffer = NULL;
        ctx->state = JLOG_STREAMER_WANT_BODY;
        break;

      case JLOG_STREAMER_WANT_BODY:
        FULLREAD(e, ctx, (unsigned long)ctx->header.message_len);
        if(ctx->header.message_len > 0)
          ctx->push(DS_OP_INSERT, &nctx->r.remote, nctx->remote_cn,
                    ctx->buffer, NULL);
        else if(ctx->buffer)
          free(ctx->buffer);
        /* Don't free the buffer, it's used by the datastore process. */
        ctx->buffer = NULL;
        ctx->count--;
        ctx->total_events++;
        if(ctx->count == 0) {
          eventer_t completion_e;
          eventer_remove_fd(e->fd);
          completion_e = eventer_alloc();
          memcpy(completion_e, e, sizeof(*e));
          nctx->e = completion_e;
          completion_e->mask = EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
          ctx->state = JLOG_STREAMER_IS_ASYNC;
          ctx->push(DS_OP_CHKPT, &nctx->r.remote, nctx->remote_cn,
                    NULL, completion_e);
          noitL(noit_debug, "Pushing %s batch async [%s]: [%u/%u]\n",
                feed_type_to_str(ntohl(ctx->jlog_feed_cmd)),
                nctx->remote_cn ? nctx->remote_cn : "(null)",
                ctx->header.chkpt.log, ctx->header.chkpt.marker);
          return 0;
        } else
          ctx->state = JLOG_STREAMER_WANT_HEADER;
        break;

      case JLOG_STREAMER_IS_ASYNC:
        ctx->state = JLOG_STREAMER_WANT_CHKPT; /* falls through */
      case JLOG_STREAMER_WANT_CHKPT:
        noitL(noit_debug, "Pushing %s checkpoint [%s]: [%u/%u]\n",
              feed_type_to_str(ntohl(ctx->jlog_feed_cmd)),
              nctx->remote_cn ? nctx->remote_cn : "(null)",
              ctx->header.chkpt.log, ctx->header.chkpt.marker);
        n_chkpt.log = htonl(ctx->header.chkpt.log);
        n_chkpt.marker = htonl(ctx->header.chkpt.marker);

        /* screw short writes.  I'd rather die than not write my data! */
        len = e->opset->write(e->fd, &n_chkpt, sizeof(jlog_id),
                              &mask, e);
        if(len < 0) {
          if(errno == EAGAIN) return mask | EVENTER_EXCEPTION;
          goto socket_error;
        }
        if(len != sizeof(jlog_id)) {
          noitL(noit_error, "short write on checkpointing stream.\n");
          goto socket_error;
        }
        ctx->state = JLOG_STREAMER_WANT_COUNT;
        break;
    }
  }
  /* never get here */
}

int
noit_connection_ssl_upgrade(eventer_t e, int mask, void *closure,
                            struct timeval *now) {
  noit_connection_ctx_t *nctx = closure;
  int rv;

  rv = eventer_SSL_connect(e, &mask);
  if(rv > 0) {
    eventer_ssl_ctx_t *sslctx;
    e->callback = nctx->consumer_callback;
    /* We must make a copy of the acceptor_closure_t for each new
     * connection.
     */
    if((sslctx = eventer_get_eventer_ssl_ctx(e)) != NULL) {
      const char *cn, *end;
      cn = eventer_ssl_get_peer_subject(sslctx);
      if(cn && (cn = strstr(cn, "CN=")) != NULL) {
        cn += 3;
        end = cn;
        while(*end && *end != '/') end++;
        nctx->remote_cn = malloc(end - cn + 1);
        memcpy(nctx->remote_cn, cn, end - cn);
        nctx->remote_cn[end-cn] = '\0';
      }
    }
    return e->callback(e, mask, e->closure, now);
  }
  if(errno == EAGAIN) return mask | EVENTER_EXCEPTION;

  noitL(noit_error, "jlog streamer SSL upgrade failed.\n");
  eventer_remove_fd(e->fd);
  nctx->e = NULL;
  e->opset->close(e->fd, &mask, e);
  noit_connection_schedule_reattempt(nctx, now);
  return 0;
}
int
noit_connection_complete_connect(eventer_t e, int mask, void *closure,
                                 struct timeval *now) {
  noit_connection_ctx_t *nctx = closure;
  const char *cert, *key, *ca, *ciphers;
  char remote_str[128], tmp_str[128];
  eventer_ssl_ctx_t *sslctx;
  int aerrno, len;
  socklen_t aerrno_len = sizeof(aerrno);

  if(getsockopt(e->fd,SOL_SOCKET,SO_ERROR, &aerrno, &aerrno_len) == 0)
    if(aerrno != 0) goto connect_error;
  aerrno = 0;

  if(mask & EVENTER_EXCEPTION) {
    if(aerrno == 0 && (write(e->fd, e, 0) == -1))
      aerrno = errno;
 connect_error:
    switch(nctx->r.remote.sa_family) {
      case AF_INET:
        len = sizeof(struct sockaddr_in);
        inet_ntop(nctx->r.remote.sa_family, &nctx->r.remote_in.sin_addr,
                  tmp_str, len);
        snprintf(remote_str, sizeof(remote_str), "%s:%d",
                 tmp_str, ntohs(nctx->r.remote_in.sin_port));
        break;
      case AF_INET6:
        len = sizeof(struct sockaddr_in6);
        inet_ntop(nctx->r.remote.sa_family, &nctx->r.remote_in6.sin6_addr,
                  tmp_str, len);
        snprintf(remote_str, sizeof(remote_str), "%s:%d",
                 tmp_str, ntohs(nctx->r.remote_in6.sin6_port));
       break;
      case AF_UNIX:
        len = SUN_LEN(&(nctx->r.remote_un));
        snprintf(remote_str, sizeof(remote_str), "%s", nctx->r.remote_un.sun_path);
        break;
      default:
        snprintf(remote_str, sizeof(remote_str), "(unknown)");
    }
    noitL(noit_error, "Error connecting to %s: %s\n",
          remote_str, strerror(aerrno));
    eventer_remove_fd(e->fd);
    nctx->e = NULL;
    e->opset->close(e->fd, &mask, e);
    noit_connection_schedule_reattempt(nctx, now);
    return 0;
  }

#define SSLCONFGET(var,name) do { \
  if(!noit_hash_retr_str(nctx->sslconfig, name, strlen(name), \
                         &var)) var = NULL; } while(0)
  SSLCONFGET(cert, "certificate_file");
  SSLCONFGET(key, "key_file");
  SSLCONFGET(ca, "ca_chain");
  SSLCONFGET(ciphers, "ciphers");
  sslctx = eventer_ssl_ctx_new(SSL_CLIENT, cert, key, ca, ciphers);
  if(!sslctx) goto connect_error;

  memcpy(&nctx->last_connect, now, sizeof(*now));
  eventer_ssl_ctx_set_verify(sslctx, eventer_ssl_verify_cert,
                             nctx->sslconfig);
  EVENTER_ATTACH_SSL(e, sslctx);
  e->callback = noit_connection_ssl_upgrade;
  return e->callback(e, mask, closure, now);
}
static void
noit_connection_initiate_connection(noit_connection_ctx_t *nctx) {
  struct timeval __now;
  eventer_t e;
  int rv, fd = -1;
#ifdef SO_KEEPALIVE
  int optval;
  socklen_t optlen = sizeof(optval);
#endif

  nctx->e = NULL;
  if(nctx->wants_permanent_shutdown) {
    noit_connection_ctx_dealloc(nctx);
    return;
  }
  /* Open a socket */
  fd = socket(nctx->r.remote.sa_family, SOCK_STREAM, 0);
  if(fd < 0) goto reschedule;

  /* Make it non-blocking */
  if(eventer_set_fd_nonblocking(fd)) goto reschedule;
#define set_or_bail(type, opt, val) do { \
  optval = val; \
  optlen = sizeof(optval); \
  if(setsockopt(fd, type, opt, &optval, optlen) < 0) { \
    noitL(noit_error, "Cannot set " #type "/" #opt " on jlog socket: %s\n", \
          strerror(errno)); \
    goto reschedule; \
  } \
} while(0)
#ifdef SO_KEEPALIVE
  set_or_bail(SOL_SOCKET, SO_KEEPALIVE, 1);
#endif
#ifdef TCP_KEEPALIVE_THRESHOLD
  set_or_bail(IPPROTO_TCP, TCP_KEEPALIVE_THRESHOLD, 10 * 1000);
#endif
#ifdef TCP_KEEPALIVE_ABORT_THRESHOLD
  set_or_bail(IPPROTO_TCP, TCP_KEEPALIVE_ABORT_THRESHOLD, 30 * 1000);
#endif
#ifdef TCP_CONN_NOTIFY_THRESHOLD
  set_or_bail(IPPROTO_TCP, TCP_CONN_NOTIFY_THRESHOLD, 10 * 1000);
#endif
#ifdef TCP_CONN_ABORT_THRESHOLD
  set_or_bail(IPPROTO_TCP, TCP_CONN_ABORT_THRESHOLD, 30 * 1000);
#endif

  /* Initiate a connection */
  rv = connect(fd, &nctx->r.remote, nctx->remote_len);
  if(rv == -1 && errno != EINPROGRESS) goto reschedule;

  /* Register a handler for connection completion */
  e = eventer_alloc();
  e->fd = fd;
  e->mask = EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
  e->callback = noit_connection_complete_connect;
  e->closure = nctx;
  nctx->e = e;
  eventer_add(e);
  return;

 reschedule:
  if(fd >= 0) close(fd);
  gettimeofday(&__now, NULL);
  noit_connection_schedule_reattempt(nctx, &__now);
  return;
}

int
initiate_noit_connection(const char *host, unsigned short port,
                         noit_hash_table *sslconfig, noit_hash_table *config,
                         eventer_func_t handler, void *closure,
                         void (*freefunc)(void *)) {
  noit_connection_ctx_t *ctx;

  int8_t family;
  int rv;
  union {
    struct in_addr addr4;
    struct in6_addr addr6;
  } a;

  if(host[0] == '/') {
    family = AF_UNIX;
  }
  else {
    family = AF_INET;
    rv = inet_pton(family, host, &a);
    if(rv != 1) {
      family = AF_INET6;
      rv = inet_pton(family, host, &a);
      if(rv != 1) {
        noitL(noit_stderr, "Cannot translate '%s' to IP\n", host);
        return -1;
      }
    }
  }

  ctx = noit_connection_ctx_alloc();
  ctx->remote_str = calloc(1, strlen(host) + 7);
  snprintf(ctx->remote_str, strlen(host) + 7,
           "%s:%d", host, port);
  
  memset(&ctx->r, 0, sizeof(ctx->r));
  if(family == AF_UNIX) {
    struct sockaddr_un *s = &ctx->r.remote_un;
    s->sun_family = AF_UNIX;
    strncpy(s->sun_path, host, sizeof(s->sun_path)-1);
    ctx->remote_len = sizeof(*s);
  }
  else if(family == AF_INET) {
    struct sockaddr_in *s = &ctx->r.remote_in;
    s->sin_family = family;
    s->sin_port = htons(port);
    memcpy(&s->sin_addr, &a, sizeof(struct in_addr));
    ctx->remote_len = sizeof(*s);
  }
  else {
    struct sockaddr_in6 *s = &ctx->r.remote_in6;
    s->sin6_family = family;
    s->sin6_port = htons(port);
    memcpy(&s->sin6_addr, &a, sizeof(a));
    ctx->remote_len = sizeof(*s);
  }

  if(ctx->sslconfig)
    noit_hash_delete_all(ctx->sslconfig, free, free);
  else
    ctx->sslconfig = calloc(1, sizeof(noit_hash_table));
  noit_hash_merge_as_dict(ctx->sslconfig, sslconfig);
  if(ctx->config)
    noit_hash_delete_all(ctx->config, free, free);
  else
    ctx->config = calloc(1, sizeof(noit_hash_table));
  noit_hash_merge_as_dict(ctx->config, config);

  ctx->consumer_callback = handler;
  ctx->consumer_free = freefunc;
  ctx->consumer_ctx = closure;
  noit_connection_initiate_connection(ctx);
  return 0;
}

void
stratcon_streamer_connection(const char *toplevel, const char *destination,
                             eventer_func_t handler,
                             void *(*handler_alloc)(void), void *handler_ctx,
                             void (*handler_free)(void *)) {
  int i, cnt = 0;
  noit_conf_section_t *noit_configs;
  char path[256];

  snprintf(path, sizeof(path), "/%s/noits//noit", toplevel ? toplevel : "*");
  noit_configs = noit_conf_get_sections(NULL, path, &cnt);
  noitL(noit_error, "Found %d %s stanzas\n", cnt, path);
  for(i=0; i<cnt; i++) {
    char address[256];
    unsigned short port;
    int portint;
    noit_hash_table *sslconfig, *config;

    if(!noit_conf_get_stringbuf(noit_configs[i],
                                "ancestor-or-self::node()/@address",
                                address, sizeof(address))) {
      noitL(noit_error, "address attribute missing in noit %d\n", i+1);
      continue;
    }
    /* if destination is specified, exact match it */
    if(destination && strcmp(address, destination)) continue;

    if(!noit_conf_get_int(noit_configs[i],
                          "ancestor-or-self::node()/@port", &portint))
      portint = 0;
    port = (unsigned short) portint;
    if(address[0] != '/' && (portint == 0 || (port != portint))) {
      /* UNIX sockets don't require a port (they'll ignore it if specified */
      noitL(noit_stderr,
            "Invalid port [%d] specified in stanza %d\n", port, i+1);
      continue;
    }
    sslconfig = noit_conf_get_hash(noit_configs[i], "sslconfig");
    config = noit_conf_get_hash(noit_configs[i], "config");

    noitL(noit_error, "initiating to %s\n", address);
    initiate_noit_connection(address, port, sslconfig, config,
                             handler,
                             handler_alloc ? handler_alloc() : handler_ctx,
                             handler_free);
    noit_hash_destroy(sslconfig,free,free);
    free(sslconfig);
    noit_hash_destroy(config,free,free);
    free(config);
  }
  free(noit_configs);
}
void
stratcon_jlog_streamer_reload(const char *toplevel) {
  stratcon_streamer_connection(toplevel, NULL,
                               stratcon_jlog_recv_handler,
                               (void *(*)())stratcon_jlog_streamer_datastore_ctx_alloc,
                               NULL,
                               jlog_streamer_ctx_free);
}

static int
stratcon_console_show_noits(noit_console_closure_t ncct,
                            int argc, char **argv,
                            noit_console_state_t *dstate,
                            void *closure) {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen, n = 0, i;
  void *vconn;
  noit_connection_ctx_t **ctx;

  pthread_mutex_lock(&noits_lock);
  ctx = malloc(sizeof(*ctx) * noits.size);
  while(noit_hash_next(&noits, &iter, (const char **)key_id, &klen,
                       &vconn)) {
    ctx[n] = (noit_connection_ctx_t *)vconn;
    noit_atomic_inc32(&ctx[n]->refcnt);
    n++;
  }
  pthread_mutex_unlock(&noits_lock);
  qsort(ctx, n, sizeof(*ctx), remote_str_sort);
  for(i=0; i<n; i++) {
    nc_print_noit_conn_brief(ncct, ctx[i]);
    noit_connection_ctx_deref(ctx[i]);
  }
  free(ctx);
  return 0;
}

static int
rest_show_noits(noit_http_rest_closure_t *restc,
                int npats, char **pats) {
  xmlDocPtr doc;
  xmlNodePtr root;
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen, n = 0, i;
  void *vconn;
  noit_connection_ctx_t **ctxs;
  struct timeval now, diff, last;
  gettimeofday(&now, NULL);

  pthread_mutex_lock(&noits_lock);
  ctxs = malloc(sizeof(*ctxs) * noits.size);
  while(noit_hash_next(&noits, &iter, (const char **)key_id, &klen,
                       &vconn)) {
    ctxs[n] = (noit_connection_ctx_t *)vconn;
    noit_atomic_inc32(&ctxs[n]->refcnt);
    n++;
  }
  pthread_mutex_unlock(&noits_lock);
  qsort(ctxs, n, sizeof(*ctxs), remote_str_sort);

  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlNewDocNode(doc, NULL, (xmlChar *)"noits", NULL);
  xmlDocSetRootElement(doc, root);
  for(i=0; i<n; i++) {
    char buff[256];
    const char *feedtype = "unknown", *state = "unknown";
    xmlNodePtr node;
    noit_connection_ctx_t *ctx = ctxs[i];
    jlog_streamer_ctx_t *jctx = ctx->consumer_ctx;

    node = xmlNewNode(NULL, (xmlChar *)"noit");
    snprintf(buff, sizeof(buff), "%llu.%06d",
             (long long unsigned)ctx->last_connect.tv_sec,
             (int)ctx->last_connect.tv_usec);
    xmlSetProp(node, (xmlChar *)"last_connect", (xmlChar *)buff);
    xmlSetProp(node, (xmlChar *)"state", ctx->remote_cn ?
               (xmlChar *)"connected" :
               (ctx->timeout_event ? (xmlChar *)"disconnected" :
                                    (xmlChar *)"connecting"));
    if(ctx->e) {
      char buff[128];
      const char *addrstr = NULL;
      struct sockaddr_in6 addr6;
      socklen_t len = sizeof(addr6);
      if(getsockname(ctx->e->fd, (struct sockaddr *)&addr6, &len) == 0) {
        unsigned short port = 0;
        if(addr6.sin6_family == AF_INET) {
          addrstr = inet_ntop(addr6.sin6_family,
                              &((struct sockaddr_in *)&addr6)->sin_addr,
                              buff, sizeof(buff));
          port = ntohs(((struct sockaddr_in *)&addr6)->sin_port);
        }
        else if(addr6.sin6_family == AF_INET6) {
          addrstr = inet_ntop(addr6.sin6_family, &addr6.sin6_addr,
                              buff, sizeof(buff));
          port = ntohs(addr6.sin6_port);
        }
        if(addrstr != NULL) {
          snprintf(buff + strlen(buff), sizeof(buff) - strlen(buff),
                   ":%u", port);
          xmlSetProp(node, (xmlChar *)"local", (xmlChar *)buff);
        }
      }
    }
    xmlSetProp(node, (xmlChar *)"remote", (xmlChar *)ctx->remote_str);
    feedtype = feed_type_to_str(ntohl(jctx->jlog_feed_cmd));
    xmlSetProp(node, (xmlChar *)"type", (xmlChar *)feedtype);
    if(ctx->timeout_event) {
      sub_timeval(ctx->timeout_event->whence, now, &diff);
      snprintf(buff, sizeof(buff), "%llu.%06d",
               (long long unsigned)diff.tv_sec, (int)diff.tv_usec);
      xmlSetProp(node, (xmlChar *)"next_attempt", (xmlChar *)buff);
    }
    else if(ctx->remote_cn) {
      if(ctx->remote_cn)
        xmlSetProp(node, (xmlChar *)"remote_cn", (xmlChar *)ctx->remote_cn);
  
      switch(jctx->state) {
        case JLOG_STREAMER_WANT_INITIATE: state = "initiate"; break;
        case JLOG_STREAMER_WANT_COUNT: state = "waiting for next batch"; break;
        case JLOG_STREAMER_WANT_ERROR: state = "waiting for error"; break;
        case JLOG_STREAMER_WANT_HEADER: state = "reading header"; break;
        case JLOG_STREAMER_WANT_BODY: state = "reading body"; break;
        case JLOG_STREAMER_IS_ASYNC: state = "asynchronously processing"; break;
        case JLOG_STREAMER_WANT_CHKPT: state = "checkpointing"; break;
      }
      xmlSetProp(node, (xmlChar *)"state", (xmlChar *)state);
      snprintf(buff, sizeof(buff), "%08x:%08x", 
               jctx->header.chkpt.log, jctx->header.chkpt.marker);
      xmlSetProp(node, (xmlChar *)"checkpoint", (xmlChar *)buff);
      snprintf(buff, sizeof(buff), "%llu",
               (unsigned long long)jctx->total_events);
      xmlSetProp(node, (xmlChar *)"session_events", (xmlChar *)buff);
      snprintf(buff, sizeof(buff), "%llu",
               (unsigned long long)jctx->total_bytes_read);
      xmlSetProp(node, (xmlChar *)"session_bytes", (xmlChar *)buff);
  
      sub_timeval(now, ctx->last_connect, &diff);
      snprintf(buff, sizeof(buff), "%lld.%06d",
               (long long)diff.tv_sec, (int)diff.tv_usec);
      xmlSetProp(node, (xmlChar *)"session_duration", (xmlChar *)buff);
  
      if(jctx->header.tv_sec) {
        last.tv_sec = jctx->header.tv_sec;
        last.tv_usec = jctx->header.tv_usec;
        snprintf(buff, sizeof(buff), "%llu.%06d",
                 (unsigned long long)last.tv_sec, (int)last.tv_usec);
        xmlSetProp(node, (xmlChar *)"last_event", (xmlChar *)buff);
        sub_timeval(now, last, &diff);
        snprintf(buff, sizeof(buff), "%lld.%06d",
                 (long long)diff.tv_sec, (int)diff.tv_usec);
        xmlSetProp(node, (xmlChar *)"last_event_age", (xmlChar *)buff);
      }
    }

    xmlAddChild(root, node);
    noit_connection_ctx_deref(ctx);
  }
  free(ctxs);

  noit_http_response_ok(restc->http_ctx, "text/xml");
  noit_http_response_xml(restc->http_ctx, doc);
  noit_http_response_end(restc->http_ctx);
  xmlFreeDoc(doc);
  return 0;
}
static int
stratcon_add_noit(const char *target, unsigned short port) {
  int cnt;
  char path[256];
  char port_str[6];
  noit_conf_section_t *noit_configs, parent;
  xmlNodePtr newnoit;

  snprintf(path, sizeof(path),
           "//noits//noit[@address=\"%s\" and @port=\"%d\"]", target, port);
  noit_configs = noit_conf_get_sections(NULL, path, &cnt);
  free(noit_configs);
  if(cnt != 0) return 0;

  parent = noit_conf_get_section(NULL, "//noits");
  if(!parent) return 0;
  snprintf(port_str, sizeof(port_str), "%d", port);
  newnoit = xmlNewNode(NULL, (xmlChar *)"noit");
  xmlSetProp(newnoit, (xmlChar *)"address", (xmlChar *)target);
  xmlSetProp(newnoit, (xmlChar *)"port", (xmlChar *)port_str);
  xmlAddChild(parent, newnoit);
  stratcon_streamer_connection(NULL, target,
                               stratcon_jlog_recv_handler,
                               (void *(*)())stratcon_jlog_streamer_datastore_ctx_alloc,
                               NULL,
                               jlog_streamer_ctx_free);
  stratcon_streamer_connection(NULL, target,
                               stratcon_jlog_recv_handler,
                               (void *(*)())stratcon_jlog_streamer_iep_ctx_alloc,
                               NULL,
                               jlog_streamer_ctx_free);
  return 1;
}
static int
stratcon_remove_noit(const char *target, unsigned short port) {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  uuid_t key_id;
  int klen, n = 0, i, cnt = 0;
  void *vconn;
  noit_connection_ctx_t **ctx;
  noit_conf_section_t *noit_configs;
  char path[256];
  char remote_str[256];

  snprintf(remote_str, sizeof(remote_str), "%s:%d", target, port);

  snprintf(path, sizeof(path),
           "//noits//noit[@address=\"%s\" and @port=\"%d\"]", target, port);
  noit_configs = noit_conf_get_sections(NULL, path, &cnt);
  for(i=0; i<cnt; i++) {
    xmlUnlinkNode(noit_configs[i]);
    xmlFreeNode(noit_configs[i]);
  }
  free(noit_configs);

  pthread_mutex_lock(&noits_lock);
  ctx = malloc(sizeof(*ctx) * noits.size);
  while(noit_hash_next(&noits, &iter, (const char **)key_id, &klen,
                       &vconn)) {
    if(!strcmp(((noit_connection_ctx_t *)vconn)->remote_str, remote_str)) {
      ctx[n] = (noit_connection_ctx_t *)vconn;
      noit_atomic_inc32(&ctx[n]->refcnt);
      n++;
    }
  }
  pthread_mutex_unlock(&noits_lock);
  for(i=0; i<n; i++) {
    noit_connection_ctx_dealloc(ctx[i]); /* once for the record */
    noit_connection_ctx_deref(ctx[i]);   /* once for the aboce inc32 */
  }
  free(ctx);
  return n;
}
static int
rest_set_noit(noit_http_rest_closure_t *restc,
              int npats, char **pats) {
  noit_http_session_ctx *ctx = restc->http_ctx;
  unsigned short port = 43191;
  if(npats < 1 || npats > 2)
    noit_http_response_server_error(ctx, "text/xml");
  if(npats == 2) port = atoi(pats[1]);
  if(stratcon_add_noit(pats[0], port))
    noit_http_response_ok(ctx, "text/xml");
  else
    noit_http_response_standard(ctx, 409, "EXISTS", "text/xml");
  if(noit_conf_write_file(NULL) != 0)
    noitL(noit_error, "local config write failed\n");
  noit_conf_mark_changed();
  noit_http_response_end(ctx);
  return 0;
}
static int
rest_delete_noit(noit_http_rest_closure_t *restc,
                 int npats, char **pats) {
  noit_http_session_ctx *ctx = restc->http_ctx;
  unsigned short port = 43191;
  if(npats < 1 || npats > 2)
    noit_http_response_server_error(ctx, "text/xml");
  if(npats == 2) port = atoi(pats[1]);
  if(stratcon_remove_noit(pats[0], port))
    noit_http_response_ok(ctx, "text/xml");
  else
    noit_http_response_not_found(ctx, "text/xml");
  if(noit_conf_write_file(NULL) != 0)
    noitL(noit_error, "local config write failed\n");
  noit_conf_mark_changed();
  noit_http_response_end(ctx);
  return 0;
}
static int
stratcon_console_conf_noits(noit_console_closure_t ncct,
                            int argc, char **argv,
                            noit_console_state_t *dstate,
                            void *closure) {
  char *cp, target[128];
  unsigned short port = 43191;
  int adding = (int)(vpsized_int)closure;
  if(argc != 1)
    return -1;

  cp = strchr(argv[0], ':');
  if(cp) {
    strlcpy(target, argv[0], MIN(sizeof(target), cp-argv[0]+1));
    port = atoi(cp+1);
  }
  else strlcpy(target, argv[0], sizeof(target));
  if(adding) {
    if(stratcon_add_noit(target, port)) {
      nc_printf(ncct, "Added noit at %s:%d\n", target, port);
    }
    else {
      nc_printf(ncct, "Failed to add noit at %s:%d\n", target, port);
    }
  }
  else {
    if(stratcon_remove_noit(target, port)) {
      nc_printf(ncct, "Removed noit at %s:%d\n", target, port);
    }
    else {
      nc_printf(ncct, "Failed to remove noit at %s:%d\n", target, port);
    }
  }
  return 0;
}

static void
register_console_streamer_commands() {
  noit_console_state_t *tl;
  cmd_info_t *showcmd, *confcmd, *conftcmd, *conftnocmd;

  tl = noit_console_state_initial();
  showcmd = noit_console_state_get_cmd(tl, "show");
  assert(showcmd && showcmd->dstate);
  confcmd = noit_console_state_get_cmd(tl, "configure");
  conftcmd = noit_console_state_get_cmd(confcmd->dstate, "terminal");
  conftnocmd = noit_console_state_get_cmd(conftcmd->dstate, "no");

  noit_console_state_add_cmd(conftcmd->dstate,
    NCSCMD("noit", stratcon_console_conf_noits, NULL, NULL, (void *)1));
  noit_console_state_add_cmd(conftnocmd->dstate,
    NCSCMD("noit", stratcon_console_conf_noits, NULL, NULL, (void *)0));

  noit_console_state_add_cmd(showcmd->dstate,
    NCSCMD("noits", stratcon_console_show_noits, NULL, NULL, NULL));
}

void
stratcon_jlog_streamer_init(const char *toplevel) {
  pthread_mutex_init(&noits_lock, NULL);
  eventer_name_callback("noit_connection_reinitiate",
                        noit_connection_reinitiate);
  eventer_name_callback("stratcon_jlog_recv_handler",
                        stratcon_jlog_recv_handler);
  eventer_name_callback("noit_connection_ssl_upgrade",
                        noit_connection_ssl_upgrade);
  eventer_name_callback("noit_connection_complete_connect",
                        noit_connection_complete_connect);
  register_console_streamer_commands();
  stratcon_jlog_streamer_reload(toplevel);
  assert(noit_http_rest_register_auth(
    "GET", "/noits/", "^show$", rest_show_noits,
             noit_http_rest_client_cert_auth
  ) == 0);
  assert(noit_http_rest_register_auth(
    "PUT", "/noits/", "^set/([^/:]+)$", rest_set_noit,
             noit_http_rest_client_cert_auth
  ) == 0);
  assert(noit_http_rest_register_auth(
    "PUT", "/noits/", "^set/([^/:]+):(\\d+)$", rest_set_noit,
             noit_http_rest_client_cert_auth
  ) == 0);
  assert(noit_http_rest_register_auth(
    "DELETE", "/noits/", "^delete/([^/:]+)$", rest_delete_noit,
             noit_http_rest_client_cert_auth
  ) == 0);
  assert(noit_http_rest_register_auth(
    "DELETE", "/noits/", "^delete/([^/:]+):(\\d+)$", rest_delete_noit,
             noit_http_rest_client_cert_auth
  ) == 0);
}

