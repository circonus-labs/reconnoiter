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
#include "stratcon_datastore.h"

#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>

noit_hash_table noits = NOIT_HASH_EMPTY;

typedef struct jlog_streamer_ctx_t {
  union {
    struct sockaddr remote;
    struct sockaddr_un remote_un;
    struct sockaddr_in remote_in;
    struct sockaddr_in6 remote_in6;
  } r;
  socklen_t remote_len;
  char *remote_cn;
  u_int32_t current_backoff;
  int wants_shutdown;
  noit_hash_table *config;
  noit_hash_table *sslconfig;
  int bytes_expected;
  int bytes_read;
  char *buffer;         /* These guys are for doing partial reads */

  enum {
    WANT_COUNT = 0,
    WANT_HEADER = 1,
    WANT_BODY = 2,
    WANT_CHKPT = 3,
  } state;
  int count;            /* Number of jlog messages we need to read */
  struct {
    jlog_id   chkpt;
    u_int32_t tv_sec;
    u_int32_t tv_usec;
    u_int32_t message_len;
  } header;

  eventer_t timeout_event;
} jlog_streamer_ctx_t;

static void jlog_streamer_initiate_connection(jlog_streamer_ctx_t *ctx);

jlog_streamer_ctx_t *
jlog_streamer_ctx_alloc(void) {
  jlog_streamer_ctx_t *ctx;
  ctx = calloc(1, sizeof(*ctx));
  return ctx;
}
int
jlog_streamer_reinitiate(eventer_t e, int mask, void *closure,
                         struct timeval *now) {
  jlog_streamer_ctx_t *ctx = closure;
  ctx->timeout_event = NULL;
  jlog_streamer_initiate_connection(closure);
  return 0;
}
void
jlog_streamer_schedule_reattempt(jlog_streamer_ctx_t *ctx,
                                 struct timeval *now) {
  struct timeval __now, interval;
  const char *v;
  u_int32_t min_interval = 1000, max_interval = 60000;
  if(noit_hash_retrieve(ctx->config,
                        "reconnect_initial_interval",
                        strlen("reconnect_initial_interval"),
                        (void **)&v)) {
    min_interval = MAX(atoi(v), 100); /* .1 second minimum */
  }
  if(noit_hash_retrieve(ctx->config,
                        "reconnect_maximum_interval",
                        strlen("reconnect_maximum_interval"),
                        (void **)&v)) {
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
  ctx->timeout_event->callback = jlog_streamer_reinitiate;
  ctx->timeout_event->closure = ctx;
  ctx->timeout_event->mask = EVENTER_TIMER;
  add_timeval(*now, interval, &ctx->timeout_event->whence);
  eventer_add(ctx->timeout_event);
}
void
jlog_streamer_ctx_free(jlog_streamer_ctx_t *ctx) {
  if(ctx->buffer) free(ctx->buffer);
  if(ctx->remote_cn) free(ctx->remote_cn);
  if(ctx->timeout_event) {
    eventer_remove(ctx->timeout_event);
    eventer_free(ctx->timeout_event);
  }
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
    ctx->buffer[size] = '\0'; \
  } \
  len = __read_on_ctx(e, ctx, &mask); \
  if(len < 0) { \
    if(errno == EAGAIN) return mask | EVENTER_EXCEPTION; \
    goto socket_error; \
  } \
  ctx->bytes_read = 0; \
  ctx->bytes_expected = 0; \
} while(0)

int
stratcon_jlog_recv_handler(eventer_t e, int mask, void *closure,
                           struct timeval *now) {
  jlog_streamer_ctx_t *ctx = closure;
  int len;
  jlog_id n_chkpt;

  if(mask & EVENTER_EXCEPTION || ctx->wants_shutdown) {
 socket_error:
    jlog_streamer_schedule_reattempt(ctx, now);
    eventer_remove_fd(e->fd);
    return 0;
  }

  while(1) {
    switch(ctx->state) {
      case WANT_COUNT:
        FULLREAD(e, ctx, sizeof(u_int32_t));
        memcpy(&ctx->count, ctx->buffer, sizeof(u_int32_t));
        ctx->count = ntohl(ctx->count);
        free(ctx->buffer); ctx->buffer = NULL;
        ctx->state = WANT_HEADER;
        break;

      case WANT_HEADER:
        if(ctx->count == 0) {
          ctx->state = WANT_COUNT;
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
        ctx->state = WANT_BODY;
        break;

      case WANT_BODY:
        FULLREAD(e, ctx, ctx->header.message_len);
        stratcon_datastore_push(DS_OP_INSERT, &ctx->r.remote, ctx->buffer);
        /* Don't free the buffer, it's used by the datastore process. */
        ctx->buffer = NULL;
        ctx->count--;
        if(ctx->count == 0) {
          eventer_t completion_e;
          eventer_remove_fd(e->fd);
          completion_e = eventer_alloc();
          memcpy(completion_e, e, sizeof(*e));
          completion_e->mask = EVENTER_WRITE | EVENTER_EXCEPTION;
          ctx->state = WANT_CHKPT;
          stratcon_datastore_push(DS_OP_CHKPT, &ctx->r.remote, completion_e);
          noitL(noit_debug, "Pushing batch asynch...\n");
          return 0;
        } else
          ctx->state = WANT_HEADER;
        break;

      case WANT_CHKPT:
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
        ctx->state = WANT_COUNT;
        break;
    }
  }
  /* never get here */
}

int
jlog_streamer_ssl_upgrade(eventer_t e, int mask, void *closure,
                          struct timeval *now) {
  jlog_streamer_ctx_t *ctx = closure;
  int rv;

  rv = eventer_SSL_connect(e, &mask);
  if(rv > 0) {
    eventer_ssl_ctx_t *sslctx;
    e->callback = stratcon_jlog_recv_handler;
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
        ctx->remote_cn = malloc(end - cn + 1);
        memcpy(ctx->remote_cn, cn, end - cn);
        ctx->remote_cn[end-cn] = '\0';
      }
    }
    return e->callback(e, mask, e->closure, now);
  }
  if(errno == EAGAIN) return mask | EVENTER_EXCEPTION;

  eventer_remove_fd(e->fd);
  e->opset->close(e->fd, &mask, e);
  jlog_streamer_schedule_reattempt(ctx, now);
  return 0;
}
int
jlog_streamer_complete_connect(eventer_t e, int mask, void *closure,
                               struct timeval *now) {
  jlog_streamer_ctx_t *ctx = closure;
  char *cert, *key, *ca, *ciphers;
  eventer_ssl_ctx_t *sslctx;

  if(mask & EVENTER_EXCEPTION) {
 connect_error:
    eventer_remove_fd(e->fd);
    e->opset->close(e->fd, &mask, e);
    jlog_streamer_schedule_reattempt(ctx, now);
    return 0;
  }

#define SSLCONFGET(var,name) do { \
  if(!noit_hash_retrieve(ctx->sslconfig, name, strlen(name), \
                         (void **)&var)) var = NULL; } while(0)
  SSLCONFGET(cert, "certificate_file");
  SSLCONFGET(key, "key_file");
  SSLCONFGET(ca, "ca_chain");
  SSLCONFGET(ciphers, "ciphers");
  sslctx = eventer_ssl_ctx_new(SSL_CLIENT, cert, key, ca, ciphers);
  if(!sslctx) goto connect_error;

  eventer_ssl_ctx_set_verify(sslctx, eventer_ssl_verify_cert,
                             ctx->sslconfig);
  EVENTER_ATTACH_SSL(e, sslctx);
  e->callback = jlog_streamer_ssl_upgrade;
  return e->callback(e, mask, closure, now);
}
static void
jlog_streamer_initiate_connection(jlog_streamer_ctx_t *ctx) {
  struct timeval __now;
  eventer_t e;
  int rv, fd = -1;
  long on;

  /* Open a socket */
  fd = socket(ctx->r.remote.sa_family, SOCK_STREAM, 0);
  if(fd < 0) goto reschedule;

  /* Make it non-blocking */
  on = 1;
  if(ioctl(fd, FIONBIO, &on)) goto reschedule;

  /* Initiate a connection */
  ctx->r.remote.sa_len = ctx->remote_len;
  rv = connect(fd, &ctx->r.remote, ctx->remote_len);
  if(rv == -1 && errno != EINPROGRESS) goto reschedule;

  /* Register a handler for connection completion */
  e = eventer_alloc();
  e->fd = fd;
  e->mask = EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
  e->callback = jlog_streamer_complete_connect;
  e->closure = ctx;
  eventer_add(e);
  return;

 reschedule:
  if(fd >= 0) close(fd);
  gettimeofday(&__now, NULL);
  jlog_streamer_schedule_reattempt(ctx, &__now);
  return;
}

int
initiate_jlog_streamer(const char *host, unsigned short port,
                       noit_hash_table *sslconfig, noit_hash_table *config) {
  jlog_streamer_ctx_t *ctx;

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

  ctx = jlog_streamer_ctx_alloc();
  
  memset(&ctx->r, 0, sizeof(ctx->r));
  if(family == AF_UNIX) {
    struct sockaddr_un *s = &ctx->r.remote_un;
    s->sun_family = AF_UNIX;
    strncpy(s->sun_path, host, sizeof(s->sun_path)-1);
    ctx->remote_len = sizeof(*s);
  }
  else {
    struct sockaddr_in6 *s = &ctx->r.remote_in6;
    s->sin6_family = family;
    s->sin6_port = htons(port);
    memcpy(&s->sin6_addr, &a, sizeof(a));
    ctx->remote_len = (family == AF_INET) ?
                        sizeof(struct sockaddr_in) : sizeof(*s);
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

  jlog_streamer_initiate_connection(ctx);
  return 0;
}

void
stratcon_jlog_streamer_reload(const char *toplevel) {
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

    initiate_jlog_streamer(address, port, sslconfig, config);
  }
}

void
stratcon_jlog_streamer_init(const char *toplevel) {
  eventer_name_callback("jlog_streamer_reinitiate",
                        jlog_streamer_reinitiate);
  eventer_name_callback("stratcon_jlog_recv_handler",
                        stratcon_jlog_recv_handler);
  eventer_name_callback("jlog_streamer_ssl_upgrade",
                        jlog_streamer_ssl_upgrade);
  eventer_name_callback("jlog_streamer_complete_connect",
                        jlog_streamer_complete_connect);
  stratcon_jlog_streamer_reload(toplevel);
}
