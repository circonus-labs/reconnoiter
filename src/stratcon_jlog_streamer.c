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
#include "stratcon_datastore.h"
#include "stratcon_jlog_streamer.h"

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

noit_hash_table noits = NOIT_HASH_EMPTY;

static void noit_connection_initiate_connection(noit_connection_ctx_t *ctx);

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
  noit_connection_ctx_t *ctx;
  ctx = calloc(1, sizeof(*ctx));
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
void
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

      case JLOG_STREAMER_WANT_COUNT:
        FULLREAD(e, ctx, sizeof(u_int32_t));
        memcpy(&ctx->count, ctx->buffer, sizeof(u_int32_t));
        ctx->count = ntohl(ctx->count);
        free(ctx->buffer); ctx->buffer = NULL;
        ctx->state = JLOG_STREAMER_WANT_HEADER;
        break;

      case JLOG_STREAMER_WANT_HEADER:
        if(ctx->count == 0) {
          ctx->state = JLOG_STREAMER_WANT_COUNT;
          break;
        }
        FULLREAD(e, ctx, sizeof(ctx->header));
        memcpy(&ctx->header, ctx->buffer, sizeof(ctx->header));
        ctx->header.chkpt.log = ntohl(ctx->header.chkpt.log);
        ctx->header.chkpt.marker = ntohl(ctx->header.chkpt.marker);
        ctx->header.tv_sec = ntohl(ctx->header.tv_sec);
        ctx->header.tv_usec = ntohl(ctx->header.tv_usec);
        ctx->header.message_len = ntohl(ctx->header.message_len);
        free(ctx->buffer); ctx->buffer = NULL;
        ctx->state = JLOG_STREAMER_WANT_BODY;
        break;

      case JLOG_STREAMER_WANT_BODY:
        FULLREAD(e, ctx, (unsigned long)ctx->header.message_len);
        if(ctx->header.message_len > 0)
          ctx->push(DS_OP_INSERT, &nctx->r.remote, ctx->buffer);
        else if(ctx->buffer)
          free(ctx->buffer);
        /* Don't free the buffer, it's used by the datastore process. */
        ctx->buffer = NULL;
        ctx->count--;
        if(ctx->count == 0) {
          eventer_t completion_e;
          eventer_remove_fd(e->fd);
          completion_e = eventer_alloc();
          memcpy(completion_e, e, sizeof(*e));
          completion_e->mask = EVENTER_WRITE | EVENTER_EXCEPTION;
          ctx->state = JLOG_STREAMER_WANT_CHKPT;
          ctx->push(DS_OP_CHKPT, &nctx->r.remote, completion_e);
          noitL(noit_debug, "Pushing batch asynch...\n");
          return 0;
        } else
          ctx->state = JLOG_STREAMER_WANT_HEADER;
        break;

      case JLOG_STREAMER_WANT_CHKPT:
        noitL(noit_debug, "Pushing checkpoint: [%u/%u]\n",
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
      char *cn, *end;
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
  long on;

  /* Open a socket */
  fd = socket(nctx->r.remote.sa_family, SOCK_STREAM, 0);
  if(fd < 0) goto reschedule;

  /* Make it non-blocking */
  on = 1;
  if(ioctl(fd, FIONBIO, &on)) goto reschedule;

  /* Initiate a connection */
  rv = connect(fd, &nctx->r.remote, nctx->remote_len);
  if(rv == -1 && errno != EINPROGRESS) goto reschedule;

  /* Register a handler for connection completion */
  e = eventer_alloc();
  e->fd = fd;
  e->mask = EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
  e->callback = noit_connection_complete_connect;
  e->closure = nctx;
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

void
stratcon_jlog_streamer_init(const char *toplevel) {
  eventer_name_callback("noit_connection_reinitiate",
                        noit_connection_reinitiate);
  eventer_name_callback("stratcon_jlog_recv_handler",
                        stratcon_jlog_recv_handler);
  eventer_name_callback("noit_connection_ssl_upgrade",
                        noit_connection_ssl_upgrade);
  eventer_name_callback("noit_connection_complete_connect",
                        noit_connection_complete_connect);
  stratcon_jlog_streamer_reload(toplevel);
}
