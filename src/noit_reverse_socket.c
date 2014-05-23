/*
 * Copyright (c) 2013, Circonus, Inc.
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
 *     * Neither the name Circonus, Inc. nor the names of its contributors
 *       may be used to endorse or promote products derived from this
 *       software without specific prior written permission.
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

#define NOIT_CONTROL_REVERSE   0x52455645 /* "REVE" */
#define DEFAULT_NOIT_CONNECTION_TIMEOUT 60000 /* 60 seconds */

#define IFCMD(f, s) if((f)->command && (f)->buff_len == strlen(s) && !strncmp((f)->buff, s, strlen(s)))
#define GET_EXPECTED_CN(nctx, cn) do { \
  void *vcn; \
  cn = NULL; \
  if(nctx->config && \
     noit_hash_retrieve(nctx->config, "cn", 2, &vcn)) { \
     cn = vcn; \
  } \
} while(0)

#include "noit_defines.h"
#include "noit_listener.h"
#include "noit_conf.h"
#include "noit_rest.h"
#include "utils/noit_hash.h"
#include "utils/noit_str.h"
#include "noit_reverse_socket.h"
#include "noit_dtrace_probes.h"

#include <errno.h>
#include <assert.h>
#include <poll.h>

static const int MAX_CHANNELS = 128;
static const int MAX_FRAME_LEN = 65530;
static const int CMD_BUFF_LEN = 4096;

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;
static noit_hash_table reverse_sockets = NOIT_HASH_EMPTY;
static pthread_rwlock_t reverse_sockets_lock;
static pthread_mutex_t reverses_lock;
static noit_hash_table reverses = NOIT_HASH_EMPTY;


static void noit_connection_initiate_connection(noit_connection_ctx_t *ctx);
static void noit_connection_schedule_reattempt(noit_connection_ctx_t *ctx,
                                               struct timeval *now);
typedef struct reverse_frame {
  u_int16_t channel_id;
  void *buff;            /* payload */
  size_t buff_len;       /* length of frame */
  size_t buff_filled;    /* length of frame populated */
  size_t offset;         /* length of frame already processed */
  int command;
  struct reverse_frame *next;
} reverse_frame_t;

static void reverse_frame_free(void *vrf) {
  reverse_frame_t *f = vrf;
  if(f->buff) free(f->buff);
  free(f);
}

typedef struct {
  char *id;
  int up;
  struct timeval create_time;
  uint64_t in_bytes;
  uint64_t in_frames;
  uint64_t out_bytes;
  uint64_t out_frames;
  pthread_mutex_t lock;
  eventer_t e;
  reverse_frame_t *outgoing;
  reverse_frame_t *outgoing_tail;

  /* used to write the next frame */
  char frame_hdr_out[6];
  int frame_hdr_written;

  /* used to read the next frame */
  char frame_hdr[6];
  int frame_hdr_read;
  reverse_frame_t incoming_inflight;

  struct {
    struct timeval create_time;
    uint64_t in_bytes;
    uint64_t in_frames;
    uint64_t out_bytes;
    uint64_t out_frames;
    int pair[2]; /* pair[0] is under our control... */
                 /* pair[1] might be the other size of a socketpair */
    reverse_frame_t *incoming;
    reverse_frame_t *incoming_tail;
  } channels[MAX_CHANNELS];
  char *buff;
  int buff_len;
  int buff_read;
  int buff_written;
  union {
    struct sockaddr ip;
    struct sockaddr_in ipv4;
    struct sockaddr_in6 ipv6;
  } tgt;
  int tgt_len;

  /* This are how we're bound */
  char *xbind;
  struct sockaddr_in proxy_ip4;
  eventer_t proxy_ip4_e;
  struct sockaddr_in6 proxy_ip6;
  eventer_t proxy_ip6_e;

  /* If we have one of these, we need to refresh timeouts */
  noit_connection_ctx_t *nctx;
} reverse_socket_t;

typedef struct {
  u_int16_t channel_id;
  reverse_socket_t *parent;
} channel_closure_t;

static void *noit_reverse_socket_alloc() {
  int i;
  reverse_socket_t *rc = calloc(1, sizeof(*rc));
  gettimeofday(&rc->create_time, NULL);
  for(i=0;i<MAX_CHANNELS;i++)
    rc->channels[i].pair[0] = rc->channels[i].pair[1] = -1;
  return rc;
}
static void
noit_reverse_socket_free(void *vrc) {
  reverse_socket_t *rc = vrc;
  if(rc->id) {
    pthread_rwlock_wrlock(&reverse_sockets_lock);
    noit_hash_delete(&reverse_sockets, rc->id, strlen(rc->id), NULL, NULL);
    pthread_rwlock_unlock(&reverse_sockets_lock);
  }

  if(rc->buff) free(rc->buff);
  if(rc->id) free(rc->id);
  free(rc);
}

static void APPEND_IN(reverse_socket_t *rc, reverse_frame_t *frame_to_copy) {
  eventer_t e;
  uint16_t id = frame_to_copy->channel_id;
  reverse_frame_t *frame = malloc(sizeof(*frame));
  memcpy(frame, frame_to_copy, sizeof(*frame));
  pthread_mutex_lock(&rc->lock);
  rc->in_bytes += frame->buff_len;
  rc->in_frames += 1;
  rc->channels[id].in_bytes += frame->buff_len;
  rc->channels[id].in_frames += 1;
  if(rc->channels[id].incoming_tail) {
    rc->channels[id].incoming_tail->next = frame;
    rc->channels[id].incoming_tail = frame;
  }
  else {
    rc->channels[id].incoming = rc->channels[id].incoming_tail = frame;
  }
  pthread_mutex_unlock(&rc->lock);
  memset(frame_to_copy, 0, sizeof(*frame_to_copy));
  e = eventer_find_fd(rc->channels[id].pair[0]);
  if(!e) noitL(nlerr, "WHAT? No event on my side [%d] of the socketpair()\n", rc->channels[id].pair[0]);
  if(e && !(e->mask & EVENTER_WRITE)) eventer_trigger(e, EVENTER_WRITE|EVENTER_READ);
}

static void POP_OUT(reverse_socket_t *rc) {
  reverse_frame_t *f = rc->outgoing;
  if(rc->outgoing == rc->outgoing_tail) rc->outgoing_tail = NULL;
  rc->outgoing = rc->outgoing->next;
  /* free f */
  reverse_frame_free(f);
}
static void APPEND_OUT(reverse_socket_t *rc, reverse_frame_t *frame_to_copy) {
  int rv = 0;
  reverse_frame_t *frame = malloc(sizeof(*frame));
  memcpy(frame, frame_to_copy, sizeof(*frame));
  pthread_mutex_lock(&rc->lock);
  rc->out_bytes += frame->buff_len;
  rc->out_frames += 1;
  rc->channels[(frame->channel_id & 0x7fff)].out_bytes += frame->buff_len;
  rc->channels[(frame->channel_id & 0x7fff)].out_frames += 1;
  if(rc->outgoing_tail) {
    rc->outgoing_tail->next = frame;
    rc->outgoing_tail = frame;
    rv = 1;
  }
  else {
    rc->outgoing = rc->outgoing_tail = frame;
  }
  pthread_mutex_unlock(&rc->lock);
  if(!rc->e) noitL(nlerr, "No event to trigger for reverse_socket framing\n");
  if(rc->e) eventer_trigger(rc->e, EVENTER_WRITE|EVENTER_READ);
}

static void
command_out(reverse_socket_t *rc, uint16_t id, const char *command) {
  reverse_frame_t frame = { id | 0x8000 };
  noitL(nldeb, "command out channel:%d '%s'\n", id, command);
  frame.buff = strdup(command);
  frame.buff_len = frame.buff_filled = strlen(frame.buff);
  APPEND_OUT(rc, &frame);
}

static void
noit_reverse_socket_channel_shutdown(reverse_socket_t *rc, uint16_t i, eventer_t e) {
  if(rc->channels[i].pair[0] >= 0) {
    eventer_t ce = eventer_find_fd(rc->channels[i].pair[0]);
    if(ce) eventer_trigger(ce, EVENTER_EXCEPTION);
    else close(rc->channels[i].pair[0]);
    rc->channels[i].pair[0] = -1;
  }

  rc->channels[i].in_bytes = rc->channels[i].out_bytes =
    rc->channels[i].in_frames = rc->channels[i].out_frames = 0;

  while(rc->channels[i].incoming) {
    reverse_frame_t *f = rc->channels[i].incoming;
    rc->channels[i].incoming = rc->channels[i].incoming->next;
    reverse_frame_free(f);
  }
}

static void
noit_reverse_socket_shutdown(reverse_socket_t *rc, eventer_t e) {
  char *id = rc->id;
  int mask, i;
  if(rc->buff) free(rc->buff);
  if(rc->xbind) {
    free(rc->xbind);
    if(rc->proxy_ip4_e) {
      eventer_remove_fd(rc->proxy_ip4_e->fd);
      rc->proxy_ip4_e->opset->close(rc->proxy_ip4_e->fd, &mask, rc->proxy_ip4_e);
      eventer_free(rc->proxy_ip4_e);
    }
    if(rc->proxy_ip6_e) {
      eventer_remove_fd(rc->proxy_ip6_e->fd);
      rc->proxy_ip6_e->opset->close(rc->proxy_ip6_e->fd, &mask, rc->proxy_ip4_e);
      eventer_free(rc->proxy_ip6_e);
    }
  }
  if(rc->incoming_inflight.buff) free(rc->incoming_inflight.buff);
  while(rc->outgoing) {
    reverse_frame_t *f = rc->outgoing;
    rc->outgoing = rc->outgoing->next;
    reverse_frame_free(f);
  }
  for(i=0;i<MAX_CHANNELS;i++) {
    noit_reverse_socket_channel_shutdown(rc, i, NULL);
  }
  memset(rc, 0, sizeof(*rc));
  /* Must preserve the ID, so we can be removed from our socket inventory later */
  rc->id = id;
  for(i=0;i<MAX_CHANNELS;i++) {
    rc->channels[i].pair[0] = rc->channels[i].pair[1] = -1;
  }
  eventer_remove_fd(e->fd);
  e->opset->close(e->fd, &mask, e);
}

static int
noit_reverse_socket_channel_handler(eventer_t e, int mask, void *closure,
                                    struct timeval *now) {
  char buff[MAX_FRAME_LEN];
  channel_closure_t *cct = closure;
  ssize_t len;
  int write_success = 1, read_success = 1;
  int write_mask = EVENTER_EXCEPTION, read_mask = EVENTER_EXCEPTION;
#define CHANNEL cct->parent->channels[cct->channel_id]

  if(mask & EVENTER_EXCEPTION) goto snip;

  /* this damn-well better be our side of the socketpair */
  if(CHANNEL.pair[0] != e->fd) {
   noitL(nlerr, "noit_reverse_socket_channel_handler: misaligned events, this is a bug\n");
   shutdown:
    command_out(cct->parent, cct->channel_id, "SHUTDOWN");
   snip:
    e->opset->close(e->fd, &write_mask, e);
    eventer_remove_fd(e->fd);
    CHANNEL.pair[0] = CHANNEL.pair[1] = -1;
    noit_reverse_socket_channel_shutdown(cct->parent, cct->channel_id, e);
    free(cct);
    return 0;
  }
  while(write_success || read_success) {
    read_success = write_success = 0;
    pthread_mutex_lock(&cct->parent->lock);

    /* try to write some stuff */
    while(CHANNEL.incoming) {
      reverse_frame_t *f = CHANNEL.incoming;
      assert(f->buff_len == f->buff_filled); /* we only expect full frames here */
      if(f->command) {
        IFCMD(f, "RESET") goto snip;
        IFCMD(f, "CLOSE") goto snip;
        IFCMD(f, "SHUTDOWN") goto shutdown;
        goto shutdown;
      }
      if(f->buff_len == 0) goto shutdown;
      len = e->opset->write(e->fd, f->buff + f->offset, f->buff_filled - f->offset, &write_mask, e);
      if(len < 0 && errno == EAGAIN) break;
      if(len <= 0) goto shutdown;
      if(len > 0) {
        noitL(nldeb, "reverse_socket_channel write[%d]: %lld\n", e->fd, (long long)len);
        f->offset += len;
        write_success = 1;
      }
      if(f->offset == f->buff_filled) {
        CHANNEL.incoming = CHANNEL.incoming->next;
        if(CHANNEL.incoming_tail == f) CHANNEL.incoming_tail = CHANNEL.incoming;
        reverse_frame_free(f);
      }
    }

    /* try to read some stuff */
    len = e->opset->read(e->fd, buff, sizeof(buff), &read_mask, e);
    if(len < 0 && (errno != EINPROGRESS && errno != EAGAIN)) goto shutdown;
    if(len == 0) goto shutdown;
    if(len > 0) {
      noitL(nldeb, "reverse_socket_channel read[%d]: %lld %s\n", e->fd, (long long)len, len==-1 ? strerror(errno) : "");
      reverse_frame_t frame = { cct->channel_id };
      frame.buff = malloc(len);
      memcpy(frame.buff, buff, len);
      frame.buff_len = frame.buff_filled = len;
      APPEND_OUT(cct->parent, &frame);
      read_success = 1;
    }
    pthread_mutex_unlock(&cct->parent->lock);
  }
  return read_mask | write_mask | EVENTER_EXCEPTION;
}

static int
noit_support_connection(reverse_socket_t *rc) {
  int fd, rv;
  fd = socket(rc->tgt.ip.sa_family, NE_SOCK_CLOEXEC|SOCK_STREAM, 0);
  if(fd < 0) goto fail;

  /* Make it non-blocking */
  if(eventer_set_fd_nonblocking(fd)) goto fail;

  rv = connect(fd, &rc->tgt.ip, rc->tgt_len);

  if(rv == -1 && errno != EINPROGRESS) goto fail;

  return fd;
 fail:
  noitL(nldeb, "noit_support_connect -> %s\n", strerror(errno));
  if(fd >= 0) close(fd);
  return -1;
}

static int
noit_reverse_socket_handler(eventer_t e, int mask, void *closure,
                            struct timeval *now) {
  int rmask = EVENTER_EXCEPTION;
  int wmask = EVENTER_EXCEPTION;
  int len;
  int success = 0;
  reverse_socket_t *rc = closure;
  const char *socket_error_string = "protocol error";

  if(mask & EVENTER_EXCEPTION) {
socket_error:
    /* Exceptions cause us to simply snip the connection */
    noitL(nlerr, "%s reverse_socket: %s\n", rc->id, socket_error_string);
    noit_reverse_socket_shutdown(rc, e);
    return 0;    
  }

  if(mask & EVENTER_READ) {
   next_frame:
    while(rc->frame_hdr_read < sizeof(rc->frame_hdr)) {
      len = e->opset->read(e->fd, rc->frame_hdr + rc->frame_hdr_read, sizeof(rc->frame_hdr) - rc->frame_hdr_read, &rmask, e);
      if(len < 0 && errno == EAGAIN) goto try_writes;
      if(len < 0) goto socket_error;
      rc->frame_hdr_read += len;
      success = 1;
    }
    if(rc->incoming_inflight.buff_len == 0) {
      uint16_t nchannel_id;
      uint32_t nframelen;
      memcpy(&nchannel_id, rc->frame_hdr, sizeof(nchannel_id));
      memcpy(&nframelen, rc->frame_hdr + sizeof(nchannel_id), sizeof(nframelen));
      rc->incoming_inflight.channel_id = ntohs(nchannel_id);
      if(rc->incoming_inflight.channel_id & 0x8000) {
        rc->incoming_inflight.channel_id &= 0x7fff;
        rc->incoming_inflight.command = 1;
      }
      rc->incoming_inflight.buff_len = ntohl(nframelen);
      noitL(nldeb, "next_frame_in(%s%d,%llu)\n",
            rc->incoming_inflight.command ? "!" : "",
            rc->incoming_inflight.channel_id,
            (unsigned long long)rc->incoming_inflight.buff_len);
      if(rc->incoming_inflight.buff_len > MAX_FRAME_LEN) {
        socket_error_string = "oversized_frame";
        goto socket_error;
      }
      if(rc->incoming_inflight.channel_id >= MAX_CHANNELS) {
        socket_error_string = "invalid_channel";
        goto socket_error;
      }
    }
    while(rc->incoming_inflight.buff_filled < rc->incoming_inflight.buff_len) {
      if(!rc->incoming_inflight.buff) rc->incoming_inflight.buff = malloc(rc->incoming_inflight.buff_len);
      len = e->opset->read(e->fd, rc->incoming_inflight.buff + rc->incoming_inflight.buff_filled,
                           rc->incoming_inflight.buff_len - rc->incoming_inflight.buff_filled, &rmask, e);
      if(len < 0 && errno == EAGAIN) goto try_writes;
      if(len <= 0) goto socket_error;
      noitL(nldeb, "frame payload read -> %llu (@%llu/%llu)\n",
            (unsigned long long)len, (unsigned long long)rc->incoming_inflight.buff_filled,
            (unsigned long long)rc->incoming_inflight.buff_len);
      rc->incoming_inflight.buff_filled += len;
      success = 1;
    }

    /* we have a complete inbound frame here (w/ data) */
    if(rc->incoming_inflight.command) {
      noitL(nldeb, "inbound command channel:%d '%.*s'\n",
            rc->incoming_inflight.channel_id,
            (int)rc->incoming_inflight.buff_len, rc->incoming_inflight.buff);
    }
    IFCMD(&rc->incoming_inflight, "CONNECT") {
      if(rc->channels[rc->incoming_inflight.channel_id].pair[0] == -1) {
        int fd = noit_support_connection(rc);
        if(fd >= 0) {
          eventer_t newe = eventer_alloc();
          channel_closure_t *cct;
          newe->fd = fd;
          newe->callback = noit_reverse_socket_channel_handler;
          cct = malloc(sizeof(*cct));
          cct->channel_id = rc->incoming_inflight.channel_id;
          cct->parent = rc;
          newe->closure = cct;
          newe->mask = EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
          eventer_add(newe);
          gettimeofday(&rc->channels[rc->incoming_inflight.channel_id].create_time, NULL);
          rc->channels[rc->incoming_inflight.channel_id].pair[0] = fd;
          memset(&rc->incoming_inflight, 0, sizeof(rc->incoming_inflight));
          rc->frame_hdr_read = 0;
          goto next_frame;
        }
      }
    }
    if(rc->channels[rc->incoming_inflight.channel_id].pair[0] == -1) {
      /* but the channel disappeared */
      /* send a reset, but not in response to a reset */
      IFCMD(&rc->incoming_inflight, "RESET") { } /* noop */
      else command_out(rc, rc->incoming_inflight.channel_id, "RESET");
      free(rc->incoming_inflight.buff);
      memset(&rc->incoming_inflight, 0, sizeof(rc->incoming_inflight));
      rc->frame_hdr_read = 0;
      goto next_frame;
    }
    APPEND_IN(rc, &rc->incoming_inflight);
    rc->frame_hdr_read = 0;
    goto next_frame;
  }

  if(mask & EVENTER_WRITE) {
   try_writes:
    if(rc->outgoing) {
      ssize_t len;
      reverse_frame_t *f = rc->outgoing;
      while(rc->frame_hdr_written < sizeof(rc->frame_hdr_out)) {
        uint16_t nchannel_id = htons(f->channel_id);
        uint32_t nframelen = htonl(f->buff_len);
        memcpy(rc->frame_hdr_out, &nchannel_id, sizeof(nchannel_id));
        memcpy(rc->frame_hdr_out + sizeof(nchannel_id), &nframelen, sizeof(nframelen));
        len = e->opset->write(e->fd, rc->frame_hdr_out + rc->frame_hdr_written,
                              sizeof(rc->frame_hdr_out) - rc->frame_hdr_written, &wmask, e);
        if(len < 0 && errno == EAGAIN) goto done_for_now;
        else if(len <= 0) goto socket_error;
        rc->frame_hdr_written += len;
        success = 1;
      }
      while(f->offset < f->buff_len) {
        len = e->opset->write(e->fd, f->buff + f->offset,
                              f->buff_len - f->offset, &wmask, e);
        if(len < 0 && errno == EAGAIN) goto done_for_now;
        else if(len <= 0) goto socket_error;
        f->offset += len;
        success = 1;
      }
      noitL(nldeb, "frame_out(%04x, %llu) %llu/%llu of body\n",
            f->channel_id, (unsigned long long)f->buff_len,
            (unsigned long long)f->offset, (unsigned long long)f->buff_len);
      /* reset for next frame */
      rc->frame_hdr_written = 0;
      POP_OUT(rc);
    }
  }
 done_for_now: 
  if(success && rc->nctx) noit_connection_update_timeout(rc->nctx);
  return rmask|wmask;
}

int
noit_reverse_socket_server_handler(eventer_t e, int mask, void *closure,
                                   struct timeval *now) {
  int rv;
  acceptor_closure_t *ac = closure;
  reverse_socket_t *rc = ac->service_ctx;
  rv = noit_reverse_socket_handler(e, mask, rc, now);
  if(rv == 0) acceptor_closure_free(ac);
  return rv;
}

int
noit_reverse_socket_client_handler(eventer_t e, int mask, void *closure,
                                   struct timeval *now) {
  int rv;
  noit_connection_ctx_t *nctx = closure;
  reverse_socket_t *rc = nctx->consumer_ctx;
  rv = noit_reverse_socket_handler(e, mask, rc, now);
  if(rv == 0) {
    /* This is closed already in noit_reverse_socket_handler */
    nctx->schedule_reattempt(nctx, now);
  }
  return rv;
}

static char *
extract_xbind(char *in, struct sockaddr_in *in4, struct sockaddr_in6 *in6) {
  char *hdr, *eol, *port;
  hdr = strstr(in, "\r\nX-Bind:");
  if(!hdr) return NULL;
  hdr += strlen("\r\nX-Bind:");
  while(*hdr && isspace(*hdr)) hdr++;
  eol = strstr(hdr, "\r\n");
  if(!eol) return NULL;
  *eol = '\0';

  port = strchr(hdr, ' ');
  if(port) { *port = '\0'; in4->sin_port = in6->sin6_port = htons(atoi(port+1)); }
  if(!strcmp(hdr, "*")) {
    in4->sin_family = AF_INET;
    in4->sin_addr.s_addr = INADDR_ANY;
    in6->sin6_family = AF_INET6;
    memcpy(&in6->sin6_addr, &in6addr_any, sizeof(in6addr_any));
  }
  else {
    if(inet_pton(AF_INET, hdr, &in4->sin_addr) == 1)
      in4->sin_family = AF_INET;
    if(inet_pton(AF_INET6, hdr, &in6->sin6_addr) == 1)
      in6->sin6_family = AF_INET6;
  }
  if(port) { *port = ' '; }
  return strdup(hdr);
}

int
noit_reverse_socket_proxy_accept(eventer_t e, int mask, void *closure,
                                 struct timeval *now) {
  int fd;
  socklen_t salen;
  union {
    struct sockaddr in;
    struct sockaddr_in in4;
    struct sockaddr_in6 in6;
  } remote;
  reverse_socket_t *rc = closure;

  salen = sizeof(remote);
  fd = e->opset->accept(e->fd, &remote.in, &salen, &mask, e);
  if(fd >= 0) {
    if(eventer_set_fd_nonblocking(fd)) {
      close(fd);
    }
    else {
      if(noit_reverse_socket_connect(rc->id, fd) < 0) {
        close(fd);
      }
    }
  }
  return EVENTER_READ | EVENTER_EXCEPTION;
}
void
noit_reverse_socket_proxy_setup(reverse_socket_t *rc) {
  int fd;
  socklen_t salen;
  if(rc->proxy_ip4.sin_family == AF_INET) {
    if(-1 == (fd = socket(AF_INET, SOCK_STREAM, 0))) goto bad4;
    if(eventer_set_fd_nonblocking(fd)) goto bad4;
    if(-1 == bind(fd, (struct sockaddr *)&rc->proxy_ip4, sizeof(rc->proxy_ip4))) goto bad4;
    if(-1 == listen(fd, 5)) goto bad4;
    salen = sizeof(rc->proxy_ip4);
    if(getsockname(fd, (struct sockaddr *)&rc->proxy_ip4, &salen)) goto bad4;
    rc->proxy_ip4_e = eventer_alloc();
    rc->proxy_ip4_e->fd = fd;
    rc->proxy_ip4_e->mask = EVENTER_READ | EVENTER_EXCEPTION;
    rc->proxy_ip4_e->closure = rc;
    rc->proxy_ip4_e->callback = noit_reverse_socket_proxy_accept;
    eventer_add(rc->proxy_ip4_e);
    fd = -1;
   bad4:
    if(fd >= 0) close(fd);
  }
  if(rc->proxy_ip6.sin6_family == AF_INET6) {
    if(-1 == (fd = socket(AF_INET6, SOCK_STREAM, 0))) goto bad6;
    if(eventer_set_fd_nonblocking(fd)) goto bad6;
    if(-1 == bind(fd, (struct sockaddr *)&rc->proxy_ip6, sizeof(rc->proxy_ip6))) goto bad6;
    if(-1 == listen(fd, 5)) goto bad6;
    salen = sizeof(rc->proxy_ip6);
    if(getsockname(fd, (struct sockaddr *)&rc->proxy_ip6, &salen)) goto bad6;
    rc->proxy_ip6_e = eventer_alloc();
    rc->proxy_ip6_e->fd = fd;
    rc->proxy_ip6_e->mask = EVENTER_READ | EVENTER_EXCEPTION;
    rc->proxy_ip6_e->closure = rc;
    rc->proxy_ip6_e->callback = noit_reverse_socket_proxy_accept;
    eventer_add(rc->proxy_ip6_e);
    fd = -1;
   bad6:
    if(fd >= 0) close(fd);
  }
}

int
noit_reverse_socket_acceptor(eventer_t e, int mask, void *closure,
                             struct timeval *now) {
  int newmask = EVENTER_READ | EVENTER_EXCEPTION, rv;
  ssize_t len = 0;
  const char *socket_error_string = "unknown socket error";
  acceptor_closure_t *ac = closure;
  reverse_socket_t *rc = ac->service_ctx;

  if(mask & EVENTER_EXCEPTION) {
socket_error:
    /* Exceptions cause us to simply snip the connection */
    acceptor_closure_free(ac);
    noit_reverse_socket_shutdown(rc, e);
    noitL(nlerr, "reverse_socket: %s\n", socket_error_string);
    return 0;
  }

  if(!ac->service_ctx) {
    rc = ac->service_ctx = noit_reverse_socket_alloc();
    ac->service_ctx_free = noit_reverse_socket_free;
    if(!rc->buff) rc->buff = malloc(CMD_BUFF_LEN);
    /* expect: "REVERSE /<id>\r\n"  ... "REVE" already read */
    /*              [ 5 ][ X][ 2]  */
  }

  /* Herein we read one byte at a time to ensure we don't accidentally
   * consume pipelined frames.
   */
  while(!rc->up && rc->buff_read < CMD_BUFF_LEN) {
    char *crlf;

    len = e->opset->read(e->fd, rc->buff + rc->buff_read, 1, &newmask, e);
    if(len < 0 && errno == EAGAIN) return newmask | EVENTER_EXCEPTION;
    if(len < 0) {
      socket_error_string = strerror(errno);
      goto socket_error;
    }
    rc->buff_read += len;
    crlf = (char *)strnstrn("\r\n\r\n", 4, rc->buff, rc->buff_read); /* end of request */
    if(crlf) {
      *(crlf + 2) = '\0';
      if(memcmp(rc->buff, "RSE /", 5)) {
        socket_error_string = "bad command";
        goto socket_error;
      }
      if(rc->buff_read < 8) goto socket_error; /* no room for an <id> */

      /* Find a X-Bind "header" */
      rc->xbind = extract_xbind(rc->buff, &rc->proxy_ip4, &rc->proxy_ip6);

      if(NULL == (crlf = (char *)strnstrn("\r\n", 2, rc->buff, rc->buff_read))) {
        socket_error_string = "no end of line found";
        goto socket_error;
      }
      *crlf = '\0'; /* end of line */
      for(crlf = rc->buff; *crlf && !isspace(*crlf); crlf++);
      *crlf = '\0'; /* end of line */
      rc->id = strdup(rc->buff + 5);
      free(rc->buff);
      rc->buff = NULL;
      break;
    }
  }
  if(!rc->id) {
    socket_error_string = "no identifier";
    goto socket_error;
  }

  pthread_rwlock_wrlock(&reverse_sockets_lock);
  rv = noit_hash_store(&reverse_sockets, rc->id, strlen(rc->id), rc);
  pthread_rwlock_unlock(&reverse_sockets_lock);
  if(rv == 0) {
    if(rc->id) free(rc->id);
    rc->id = NULL;
    socket_error_string = "id in use";
    goto socket_error;
  }
  else {
    noitL(nldeb, "reverse_socket to %s\n", rc->id);
    /* Setup proxies if we've got them */
    noit_reverse_socket_proxy_setup(rc);
    rc->e = e;
    e->callback = noit_reverse_socket_server_handler;
    return e->callback(e, EVENTER_READ | EVENTER_WRITE, closure, now);
  }
  return 0;
}

int noit_reverse_socket_connect(const char *id, int existing_fd) {
  const char *op = "";
  int i, fd = -1;
  void *vrc;
  reverse_socket_t *rc = NULL;

  pthread_rwlock_rdlock(&reverse_sockets_lock);
  if(noit_hash_retrieve(&reverse_sockets, id, strlen(id), &vrc)) {
    rc = vrc;
    for(i=0;i<MAX_CHANNELS;i++) if(rc->channels[i].pair[0] == -1) break;
    if(i<MAX_CHANNELS) {
      reverse_frame_t f = { i | 0x8000 };
      f.buff = strdup("CONNECT");
      f.buff_len = strlen(f.buff);
      op = "socketpair";
      if(existing_fd >= 0) {
        fd = existing_fd;
        rc->channels[i].pair[0] = fd;
      }
      else if(socketpair(AF_LOCAL, SOCK_STREAM, 0, rc->channels[i].pair) < 0) {
        rc->channels[i].pair[0] = rc->channels[i].pair[1] = -1;
      }
      else {
        op = "O_NONBLOCK";
        if(eventer_set_fd_nonblocking(rc->channels[i].pair[0]) ||
           eventer_set_fd_nonblocking(rc->channels[i].pair[1])) {
          close(rc->channels[i].pair[0]);
          close(rc->channels[i].pair[1]);
          rc->channels[i].pair[0] = rc->channels[i].pair[1] = -1;
        }
        else {
          fd = rc->channels[i].pair[1];
          existing_fd = rc->channels[i].pair[0];
        }
      }
      if(existing_fd) {
        eventer_t e;
        channel_closure_t *cct;
        e = eventer_alloc();
        gettimeofday(&rc->channels[i].create_time, NULL);
        e->fd = existing_fd;
        e->callback = noit_reverse_socket_channel_handler;
        cct = malloc(sizeof(*cct));
        cct->channel_id = i;
        cct->parent = rc;
        e->closure = cct;
        e->mask = EVENTER_READ | EVENTER_EXCEPTION;
        eventer_add(e);
        APPEND_OUT(rc, &f);
      }
    }
  }
  pthread_rwlock_unlock(&reverse_sockets_lock);
  if(rc && fd < 0)
    noitL(nlerr, "noit_support_socket[%s] failed %s: %s\n", rc->id, op, strerror(errno));
  return fd;
}

static void
noit_connection_close(noit_connection_ctx_t *ctx, eventer_t e) {
  int mask = 0;
  const char *cn_expected;
  GET_EXPECTED_CN(ctx, cn_expected);
  NOIT_CONNECT_CLOSE(e->fd, ctx->remote_str,
                     (char *)cn_expected,
                     ctx->wants_shutdown, errno);
  eventer_remove_fd(e->fd);
  ctx->e = NULL;
  e->opset->close(e->fd, &mask, e);
}

static void
noit_reverse_socket_close(noit_connection_ctx_t *ctx, eventer_t e) {
  noit_reverse_socket_shutdown(ctx->consumer_ctx,e);
  ctx->e = NULL;
}

noit_connection_ctx_t *
noit_connection_ctx_alloc(noit_hash_table *t, pthread_mutex_t *l) {
  noit_connection_ctx_t *ctx, **pctx;
  ctx = calloc(1, sizeof(*ctx));
  ctx->tracker = t;
  ctx->tracker_lock = l;
  ctx->refcnt = 1;
  ctx->schedule_reattempt = noit_connection_schedule_reattempt;
  ctx->close = noit_connection_close;
  pctx = malloc(sizeof(*pctx));
  *pctx = ctx;
  if(l) pthread_mutex_lock(l);
  if(t) noit_hash_store(t, (const char *)pctx, sizeof(*pctx), ctx);
  if(l) pthread_mutex_unlock(l);
  return ctx;
}

static void
noit_connection_ctx_free(noit_connection_ctx_t *ctx) {
  if(ctx->remote_cn) free(ctx->remote_cn);
  if(ctx->remote_str) free(ctx->remote_str);
  if(ctx->retry_event) {
    eventer_remove(ctx->retry_event);
    eventer_free(ctx->retry_event);
  }
  if(ctx->timeout_event) {
    eventer_remove(ctx->timeout_event);
    eventer_free(ctx->timeout_event);
  }
  ctx->consumer_free(ctx->consumer_ctx);
  if (ctx->e) {
    int mask = 0;
    eventer_remove_fd(ctx->e->fd);
    ctx->e->opset->close(ctx->e->fd, &mask, ctx->e);
    eventer_free(ctx->e);
  }
  free(ctx);
}

void
noit_connection_ctx_ref(noit_connection_ctx_t *ctx) {
  noit_atomic_inc32(&ctx->refcnt);
}
void
noit_connection_ctx_deref(noit_connection_ctx_t *ctx) {
  if(noit_atomic_dec32(&ctx->refcnt) == 0) {
    noit_connection_ctx_free(ctx);
  }
}
void
noit_connection_ctx_dealloc(noit_connection_ctx_t *ctx) {
  noit_connection_ctx_t **pctx = &ctx;
  if(ctx->tracker_lock) pthread_mutex_lock(ctx->tracker_lock);
  if(ctx->tracker)
    noit_hash_delete(ctx->tracker, (const char *)pctx, sizeof(*pctx),
                     free, (void (*)(void *))noit_connection_ctx_deref);
  if(ctx->tracker_lock) pthread_mutex_unlock(ctx->tracker_lock);
}

int
noit_connection_reinitiate(eventer_t e, int mask, void *closure,
                         struct timeval *now) {
  noit_connection_ctx_t *ctx = closure;
  ctx->retry_event = NULL;
  noit_connection_initiate_connection(closure);
  return 0;
}

int
noit_connection_disable_timeout(noit_connection_ctx_t *nctx) {
  if(nctx->timeout_event) {
    eventer_remove(nctx->timeout_event);
    eventer_free(nctx->timeout_event);
    nctx->timeout_event = NULL;
  }
  return 0;
}

static void
noit_connection_schedule_reattempt(noit_connection_ctx_t *ctx,
                                   struct timeval *now) {
  struct timeval __now, interval;
  const char *v, *cn_expected;
  u_int32_t min_interval = 1000, max_interval = 8000;

  GET_EXPECTED_CN(ctx, cn_expected);
  noit_connection_disable_timeout(ctx);
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
  if(ctx->retry_event)
    eventer_remove(ctx->retry_event);
  else
    ctx->retry_event = eventer_alloc();
  ctx->retry_event->callback = noit_connection_reinitiate;
  ctx->retry_event->closure = ctx;
  ctx->retry_event->mask = EVENTER_TIMER;
  add_timeval(*now, interval, &ctx->retry_event->whence);
  NOIT_RESCHEDULE(-1, ctx->remote_str, (char *)cn_expected, ctx->current_backoff);
  eventer_add(ctx->retry_event);
}

int
noit_connection_ssl_upgrade(eventer_t e, int mask, void *closure,
                            struct timeval *now) {
  noit_connection_ctx_t *nctx = closure;
  int rv;
  const char *error = NULL, *cn_expected;
  char error_buff[500];
  eventer_ssl_ctx_t *sslctx = NULL;

  GET_EXPECTED_CN(nctx, cn_expected);
  NOIT_CONNECT_SSL(e->fd, nctx->remote_str, (char *)cn_expected);

  if(mask & EVENTER_EXCEPTION) goto error;

  rv = eventer_SSL_connect(e, &mask);
  sslctx = eventer_get_eventer_ssl_ctx(e);

  if(rv > 0) {
    e->callback = nctx->consumer_callback;
    /* We must make a copy of the acceptor_closure_t for each new
     * connection.
     */
    if(sslctx != NULL) {
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
      if(cn_expected && (!nctx->remote_cn ||
                         strcmp(nctx->remote_cn, cn_expected))) {
        snprintf(error_buff, sizeof(error_buff), "jlog connect CN mismatch - expected %s, got %s",
            cn_expected ? cn_expected : "(null)", nctx->remote_cn ? nctx->remote_cn : "(null)");
        error = error_buff;
        goto error;
      }
    }
    NOIT_CONNECT_SSL_SUCCESS(e->fd, nctx->remote_str, (char *)cn_expected);
    return e->callback(e, mask, e->closure, now);
  }
  if(errno == EAGAIN) return mask | EVENTER_EXCEPTION;
  if(sslctx) error = eventer_ssl_get_last_error(sslctx);
  noitL(noit_debug, "noit SSL upgrade failed.\n");

 error:
  NOIT_CONNECT_SSL_FAILED(e->fd,
                          nctx->remote_str, (char *)cn_expected,
                          (char *)error, errno);
  if(error) {
    const char *cert_error = eventer_ssl_get_peer_error(sslctx);
    noitL(noit_error, "[%s] [%s] noit_connection_ssl_upgrade: %s [%s]\n",
      nctx->remote_str ? nctx->remote_str : "(null)",
      cn_expected ? cn_expected : "(null)", error, cert_error);
  }
  nctx->close(nctx, e);
  noit_connection_schedule_reattempt(nctx, now);
  return 0;
}

int
noit_connection_complete_connect(eventer_t e, int mask, void *closure,
                                 struct timeval *now) {
  noit_connection_ctx_t *nctx = closure;
  const char *layer = NULL, *cert, *key, *ca, *ciphers, *crl = NULL, *cn_expected;
  char remote_str[128], tmp_str[128];
  eventer_ssl_ctx_t *sslctx;
  int aerrno, len;
  socklen_t aerrno_len = sizeof(aerrno);

  GET_EXPECTED_CN(nctx, cn_expected);
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
        snprintf(remote_str, sizeof(remote_str), "%s", nctx->r.remote_un.sun_path);
        break;
      default:
        snprintf(remote_str, sizeof(remote_str), "(unknown)");
    }
    noitL(noit_error, "Error connecting to %s (%s): %s\n",
          remote_str, cn_expected ? cn_expected : "(null)", strerror(aerrno));
    NOIT_CONNECT_FAILED(e->fd, remote_str, (char *)cn_expected, aerrno);
    nctx->close(nctx, e);
    nctx->schedule_reattempt(nctx, now);
    return 0;
  }

#define SSLCONFGET(var,name) do { \
  if(!noit_hash_retr_str(nctx->sslconfig, name, strlen(name), \
                         &var)) var = NULL; } while(0)
  SSLCONFGET(layer, "layer");
  SSLCONFGET(cert, "certificate_file");
  SSLCONFGET(key, "key_file");
  SSLCONFGET(ca, "ca_chain");
  SSLCONFGET(ciphers, "ciphers");
  SSLCONFGET(crl, "crl");
  sslctx = eventer_ssl_ctx_new(SSL_CLIENT, layer, cert, key, ca, ciphers);
  if(!sslctx) goto connect_error;
  if(crl) {
    if(!eventer_ssl_use_crl(sslctx, crl)) {
      noitL(noit_error, "Failed to load CRL from %s\n", crl);
      eventer_ssl_ctx_free(sslctx);
      goto connect_error;
    }
  }

  memcpy(&nctx->last_connect, now, sizeof(*now));
  eventer_ssl_ctx_set_verify(sslctx, eventer_ssl_verify_cert,
                             nctx->sslconfig);
  EVENTER_ATTACH_SSL(e, sslctx);
  e->callback = noit_connection_ssl_upgrade;
  NOIT_CONNECT_SUCCESS(e->fd, nctx->remote_str, cn_expected);
  return e->callback(e, mask, closure, now);
}

int
noit_connection_session_timeout(eventer_t e, int mask, void *closure,
                                struct timeval *now) {
  noit_connection_ctx_t *nctx = closure;
  eventer_t fde = nctx->e;
  nctx->timeout_event = NULL;
  noitL(noit_error, "Timing out noit session: %s, %s\n",
        nctx->remote_cn ? nctx->remote_cn : "(null)",
        nctx->remote_str ? nctx->remote_str : "(null)");
  if(fde)
    eventer_trigger(fde, EVENTER_EXCEPTION);
  return 0;
}

int
noit_connection_update_timeout(noit_connection_ctx_t *nctx) {
  struct timeval now, diff;
  if(nctx->max_silence == 0) return 0;

  diff.tv_sec = nctx->max_silence / 1000;
  diff.tv_usec = (nctx->max_silence % 1000) * 1000;
  gettimeofday(&now, NULL);

  if(!nctx->timeout_event) {
    nctx->timeout_event = eventer_alloc();
    nctx->timeout_event->mask = EVENTER_TIMER;
    nctx->timeout_event->closure = nctx;
    nctx->timeout_event->callback = noit_connection_session_timeout;
    add_timeval(now, diff, &nctx->timeout_event->whence);
    eventer_add(nctx->timeout_event);
  }
  else {
    add_timeval(now, diff, &nctx->timeout_event->whence);
    eventer_update(nctx->timeout_event, EVENTER_TIMER);
  }
  return 0;
}

void
noit_connection_initiate_connection(noit_connection_ctx_t *nctx) {
  struct timeval __now;
  eventer_t e;
  const char *cn_expected;
  char reverse_path[256];
  int rv, fd = -1, needs_connect = 1;
#ifdef SO_KEEPALIVE
  int optval;
  socklen_t optlen = sizeof(optval);
#endif

  GET_EXPECTED_CN(nctx, cn_expected);
  nctx->e = NULL;
  if(nctx->wants_permanent_shutdown) {
    NOIT_SHUTDOWN_PERMANENT(-1, nctx->remote_str, cn_expected);
    noit_connection_ctx_dealloc(nctx);
    return;
  }
  if(cn_expected) {
    snprintf(reverse_path, sizeof(reverse_path), "noit/%s", cn_expected);
    fd = noit_reverse_socket_connect(reverse_path, -1);
    if(fd < 0) {
      noitL(nldeb, "No reverse_socket connection to %s\n", reverse_path);
    }
    else {
      noitL(nldeb, "Got a reverse_socket connection to %s\n", reverse_path);
      needs_connect = 0;
    }
  }

  if(fd < 0) {
    /* If that didn't work, open a socket */
    fd = socket(nctx->r.remote.sa_family, NE_SOCK_CLOEXEC|SOCK_STREAM, 0);
  }
  if(fd < 0) goto reschedule;

  /* Make it non-blocking */
  if(eventer_set_fd_nonblocking(fd)) goto reschedule;
#define set_or_bail(type, opt, val) do { \
  optval = val; \
  optlen = sizeof(optval); \
  if(setsockopt(fd, type, opt, &optval, optlen) < 0) { \
    noitL(noit_error, "[%s] Cannot set " #type "/" #opt " on noit socket: %s\n", \
          nctx->remote_str ? nctx->remote_str : "(null)", \
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
  if(needs_connect) {
    rv = connect(fd, &nctx->r.remote, nctx->remote_len);
    if(rv == -1 && errno != EINPROGRESS) goto reschedule;
  }

  /* Register a handler for connection completion */
  e = eventer_alloc();
  e->fd = fd;
  e->mask = EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
  e->callback = noit_connection_complete_connect;
  e->closure = nctx;
  nctx->e = e;
  eventer_add(e);

  NOIT_CONNECT(e->fd, nctx->remote_str, cn_expected);
  noit_connection_update_timeout(nctx);
  return;
 reschedule:
  if(fd >= 0) close(fd);
  gettimeofday(&__now, NULL);
  nctx->schedule_reattempt(nctx, &__now);
  return;
}

noit_connection_ctx_t *
initiate_noit_connection(noit_hash_table *tracking, pthread_mutex_t *tracking_lock,
                         const char *host, unsigned short port,
                         noit_hash_table *sslconfig, noit_hash_table *config,
                         eventer_func_t handler, void *closure,
                         void (*freefunc)(void *)) {
  noit_connection_ctx_t *ctx;
  const char *stimeout;
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
        return NULL;
      }
    }
  }

  ctx = noit_connection_ctx_alloc(tracking, tracking_lock);
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

  if(noit_hash_retr_str(ctx->config, "timeout", strlen("timeout"), &stimeout))
    ctx->max_silence = atoi(stimeout);
  else
    ctx->max_silence = DEFAULT_NOIT_CONNECTION_TIMEOUT;
  ctx->consumer_callback = handler;
  ctx->consumer_free = freefunc;
  ctx->consumer_ctx = closure;
  noit_connection_initiate_connection(ctx);
  return ctx;
}

int
noit_connections_from_config(noit_hash_table *tracker, pthread_mutex_t *tracker_lock,
                             const char *toplevel, const char *destination,
                             const char *type,
                             eventer_func_t handler,
                             void *(*handler_alloc)(void), void *handler_ctx,
                             void (*handler_free)(void *)) {
  int i, cnt = 0, found = 0;
  noit_conf_section_t *noit_configs;
  char path[256];

  snprintf(path, sizeof(path), "/%s/%ss//%s", toplevel ? toplevel : "*", type, type);
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
    initiate_noit_connection(tracker, tracker_lock,
                             address, port, sslconfig, config,
                             handler,
                             handler_alloc ? handler_alloc() : handler_ctx,
                             handler_free);
    found++;
    noit_hash_destroy(sslconfig,free,free);
    free(sslconfig);
    noit_hash_destroy(config,free,free);
    free(config);
  }
  free(noit_configs);
  return found;
}

#define GET_CONF_STR(nctx, key, cn) do { \
  void *vcn; \
  cn = NULL; \
  if(nctx->config && \
     noit_hash_retrieve(nctx->config, key, strlen(key), &vcn)) { \
     cn = vcn; \
  } \
} while(0)

static int
noit_reverse_client_handler(eventer_t e, int mask, void *closure,
                             struct timeval *now) {
  noit_connection_ctx_t *nctx = closure;
  const char *my_cn;
  reverse_socket_t *rc = nctx->consumer_ctx;
  const char *target, *port_str;
  char channel_name[256];
  char reverse_intro[300];
  int8_t family = AF_INET;
  int rv;
  union {
    struct in_addr addr4;
    struct in6_addr addr6;
  } a;
  uuid_t tmpid;
  char tmpidstr[UUID_STR_LEN+1];
  char client_id[ /* client/ */ 7 + UUID_STR_LEN + 1];

  rc->nctx = nctx;
  nctx->close = noit_reverse_socket_close;

  if(rc->buff) {
    /* We've been here before, we just need to complete the write */
    goto finish_write;
  }

  GET_CONF_STR(nctx, "endpoint", my_cn);
  GET_CONF_STR(nctx, "address", target);
  GET_CONF_STR(nctx, "port", port_str);

  if(my_cn) {
    snprintf(channel_name, sizeof(channel_name), "noit/%s", my_cn);
  }
  else {
    eventer_ssl_ctx_t *sslctx;
    strlcpy(channel_name, "noit/", sizeof(channel_name));
    sslctx = eventer_get_eventer_ssl_ctx(e);
    if(!sslctx || eventer_ssl_get_local_commonname(sslctx, channel_name+5, sizeof(channel_name)-5) < 1)
      goto fail;
  }
  if(!target) target = "127.0.0.1";
  if(!port_str) port_str = "43191";

  rv = inet_pton(family, target, &a);
  if(rv != 1) {
    family = family == AF_INET ? AF_INET6 : AF_INET;
    rv = inet_pton(family, target, &a);
    if(rv != 1) {
      family = AF_INET;
      memset(&a, 0, sizeof(a));
      goto fail;
    }
  }

  if(family == AF_INET) {
    rc->tgt.ipv4.sin_family = AF_INET;
    rc->tgt.ipv4.sin_port = htons(atoi(port_str));
    memcpy(&rc->tgt.ipv4.sin_addr, &a.addr4, sizeof(a.addr4));
    rc->tgt_len = sizeof(struct sockaddr_in);
  }
  else {
    rc->tgt.ipv6.sin6_family = AF_INET6;
    rc->tgt.ipv6.sin6_port = htons(atoi(port_str));
    memcpy(&rc->tgt.ipv6.sin6_addr, &a.addr6, sizeof(a.addr6));
    rc->tgt_len = sizeof(struct sockaddr_in6);
  }

  snprintf(reverse_intro, sizeof(reverse_intro),
           "REVERSE /%s\r\nX-Bind: *\r\n\r\n", channel_name);
  rc->buff = strdup(reverse_intro);
  rc->buff_len = strlen(rc->buff);

 finish_write: 
  while(rc->buff_written < rc->buff_len) {
    ssize_t len;
    len = e->opset->write(e->fd, rc->buff + rc->buff_written,
                          rc->buff_len - rc->buff_written,
                          &mask, e);
    if(len < 0 && errno == EAGAIN) return mask | EVENTER_EXCEPTION;
    if(len <= 0) goto fail;
    rc->buff_written += len;
  }
  free(rc->buff);
  rc->buff = NULL;
  rc->buff_len = rc->buff_written = rc->buff_read = 0;

  /* We've finished our preamble... so now we will switch handlers */
  if(!rc->id) {
    uuid_generate(tmpid);
    uuid_unparse_lower(tmpid, tmpidstr);
    snprintf(client_id, sizeof(client_id), "client/%s", tmpidstr);
    rc->id = strdup(client_id);
    pthread_rwlock_wrlock(&reverse_sockets_lock);
    noit_hash_store(&reverse_sockets, rc->id, strlen(rc->id), rc);
    pthread_rwlock_unlock(&reverse_sockets_lock);
  }

  rc->e = e;
  e->callback = noit_reverse_socket_client_handler;
  return e->callback(e, EVENTER_READ|EVENTER_WRITE, closure, now);

 fail:
  if(rc->buff) {
    free(rc->buff);
    rc->buff = NULL;
    rc->buff_len = rc->buff_written = rc->buff_read = 0;
  }
  nctx->close(nctx, e);
  nctx->schedule_reattempt(nctx, now);
  return 0;
}

static int
reverse_socket_str_sort(const void *a, const void *b) {
  reverse_socket_t * const *actx = a;
  reverse_socket_t * const *bctx = b;
  return strcmp((*actx)->id, (*bctx)->id);
}

static void
nc_print_reverse_socket_brief(noit_console_closure_t ncct,
                               reverse_socket_t *ctx) {
  int i;
  double age;
  struct timeval now, diff;

  gettimeofday(&now, NULL);
  sub_timeval(now, ctx->create_time, &diff);
  age = diff.tv_sec + (double)diff.tv_usec/1000000.0;
  if(ctx->e) {
    char buff[INET6_ADDRSTRLEN];
    nc_printf(ncct, "%s [%d]\n", ctx->id, ctx->e->fd);
    if(ctx->proxy_ip4_e) {
      inet_ntop(AF_INET, &ctx->proxy_ip4.sin_addr, buff, sizeof(buff));
      nc_printf(ncct, "  listening (IPv4): %s :%d\n", buff, ntohs(ctx->proxy_ip4.sin_port));
    }
    if(ctx->proxy_ip6_e) {
      inet_ntop(AF_INET6, &ctx->proxy_ip6.sin6_addr, buff, sizeof(buff));
      nc_printf(ncct, "  listening (IPv6): %s :%d\n", buff, ntohs(ctx->proxy_ip6.sin6_port));
    }
    nc_printf(ncct, "  [UP: %0.3fs IN: %llub / %lluf  OUT: %llub / %lluf]\n", age,
              (unsigned long long)ctx->in_bytes, (unsigned long long)ctx->in_frames,
              (unsigned long long)ctx->out_bytes, (unsigned long long)ctx->out_frames);
  }
  else {
    nc_printf(ncct, "%s [disconnected]\n", ctx->id);
  }
  for(i=0; i<MAX_CHANNELS; i++) {
    if(ctx->channels[i].pair[0] >= 0) {
      sub_timeval(now, ctx->channels[i].create_time, &diff);
      age = diff.tv_sec + (double)diff.tv_usec/1000000.0;
      nc_printf(ncct, "  [%3d:%4d]: [UP: %0.3fs IN: %llub / %lluf  OUT: %llub / %lluf]\n",
            i, ctx->channels[i].pair[0], age,
            (unsigned long long)ctx->channels[i].in_bytes, (unsigned long long)ctx->channels[i].in_frames,
            (unsigned long long)ctx->channels[i].out_bytes, (unsigned long long)ctx->channels[i].out_frames);
    }
  }
}

static int
noit_console_show_reverse(noit_console_closure_t ncct,
                          int argc, char **argv,
                          noit_console_state_t *dstate,
                          void *closure) {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *key_id;
  int klen, n = 0, i;
  void *vconn;
  reverse_socket_t **ctx;

  pthread_rwlock_rdlock(&reverse_sockets_lock);
  ctx = malloc(sizeof(*ctx) * noit_hash_size(&reverse_sockets));
  while(noit_hash_next(&reverse_sockets, &iter, &key_id, &klen,
                       &vconn)) {
    ctx[n] = (reverse_socket_t *)vconn;
    if(argc == 0 ||
       !strcmp(ctx[n]->id, argv[0])) {
      n++;
    }
  }
  qsort(ctx, n, sizeof(*ctx), reverse_socket_str_sort);
  for(i=0; i<n; i++) {
    nc_print_reverse_socket_brief(ncct, ctx[i]);
  }
  pthread_rwlock_unlock(&reverse_sockets_lock);
  free(ctx);
  return 0;
}

static char *
noit_console_reverse_opts(noit_console_closure_t ncct,
                          noit_console_state_stack_t *stack,
                          noit_console_state_t *dstate,
                          int argc, char **argv, int idx) {
  if(argc == 1) {
    noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
    const char *key_id;
    int klen, i = 0;
    void *vconn;
    reverse_socket_t *ctx;
    noit_hash_table dedup = NOIT_HASH_EMPTY;

    pthread_rwlock_rdlock(&reverse_sockets_lock);
    while(noit_hash_next(&reverse_sockets, &iter, &key_id, &klen, &vconn)) {
      ctx = (reverse_socket_t *)vconn;
      if(!strncmp(ctx->id, argv[0], strlen(argv[0]))) {
        if(idx == i) {
          pthread_rwlock_unlock(&reverse_sockets_lock);
          noit_hash_destroy(&dedup, NULL, NULL);
          return strdup(ctx->id);
        }
        i++;
      }
    }
    pthread_rwlock_unlock(&reverse_sockets_lock);
    noit_hash_destroy(&dedup, NULL, NULL);
  }
  if(argc == 2)
    return noit_console_opt_delegate(ncct, stack, dstate, argc-1, argv+1, idx);
  return NULL;
}

static void
register_console_reverse_commands() {
  noit_console_state_t *tl;
  cmd_info_t *showcmd;

  tl = noit_console_state_initial();
  showcmd = noit_console_state_get_cmd(tl, "show");
  assert(showcmd && showcmd->dstate);

  noit_console_state_add_cmd(showcmd->dstate,
    NCSCMD("reverse", noit_console_show_reverse,
           noit_console_reverse_opts, NULL, (void *)1));
}

static int
rest_show_reverse(noit_http_rest_closure_t *restc,
                  int npats, char **pats) {
  xmlDocPtr doc;
  xmlNodePtr root;
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *key_id;
  const char *want_id = NULL;
  int klen, n = 0, i, di;
  void *vconn;
  double age;
  reverse_socket_t **ctxs;
  struct timeval now, diff;
  xmlNodePtr node, channels;
  noit_http_request *req = noit_http_session_request(restc->http_ctx);

  noit_http_process_querystring(req);
  want_id = noit_http_request_querystring(req, "id");

  gettimeofday(&now, NULL);

  pthread_rwlock_rdlock(&reverse_sockets_lock);
  ctxs = malloc(sizeof(*ctxs) * noit_hash_size(&reverse_sockets));
  while(noit_hash_next(&reverse_sockets, &iter, &key_id, &klen,
                       &vconn)) {
    ctxs[n] = (reverse_socket_t *)vconn;
    n++;
  }

  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlNewDocNode(doc, NULL, (xmlChar *)"reverses", NULL);
  xmlDocSetRootElement(doc, root);

  for(i=0; i<n; i++) {
    char scratch[128];
    char buff[INET6_ADDRSTRLEN];
    if(want_id && strcmp(want_id, ctxs[i]->id)) continue;
    reverse_socket_t *rc = ctxs[i];

    node = xmlNewNode(NULL, (xmlChar *)"reverse");
    xmlSetProp(node, (xmlChar *)"id", (xmlChar *)rc->id);
    if(rc->e) {
#define ADD_ATTR(node, name, fmt...) do { \
  snprintf(scratch, sizeof(scratch), fmt); \
  xmlSetProp(node, (xmlChar *)name, (xmlChar *)scratch); \
} while(0)
      ADD_ATTR(node, "fd", "%d", rc->e->fd);
      sub_timeval(now, rc->create_time, &diff);
      age = diff.tv_sec + (double)diff.tv_usec/1000000.0;
      ADD_ATTR(node, "uptime", "%0.3f", age);
      ADD_ATTR(node, "in_bytes", "%llu", (unsigned long long)rc->in_bytes);
      ADD_ATTR(node, "in_frames", "%llu", (unsigned long long)rc->in_frames);
      ADD_ATTR(node, "out_bytes", "%llu", (unsigned long long)rc->out_bytes);
      ADD_ATTR(node, "out_frames", "%llu", (unsigned long long)rc->out_frames);
      if(rc->proxy_ip4_e) {
        inet_ntop(AF_INET, &rc->proxy_ip4.sin_addr, buff, sizeof(buff));
        ADD_ATTR(node, "proxy_ipv4", "%s:%d", buff, ntohs(rc->proxy_ip4.sin_port));
      }
      if(rc->proxy_ip6_e) {
        inet_ntop(AF_INET6, &rc->proxy_ip6.sin6_addr, buff, sizeof(buff));
        ADD_ATTR(node, "proxy_ipv6", "[%s]:%d", buff, ntohs(rc->proxy_ip6.sin6_port));
      }
      channels = xmlNewNode(NULL, (xmlChar *)"channels");
      for(di=0;di<MAX_CHANNELS;di++) {
        xmlNodePtr channel;
        if(rc->channels[di].pair[0] < 0) continue;
        channel = xmlNewNode(NULL, (xmlChar *)"channel");
        ADD_ATTR(channel, "channel_id", "%d", di);
        ADD_ATTR(channel, "fd", "%d", rc->channels[di].pair[0]);
        sub_timeval(now, rc->channels[di].create_time, &diff);
        age = diff.tv_sec + (double)diff.tv_usec/1000000.0;
        ADD_ATTR(channel, "uptime", "%0.3f", age);
        ADD_ATTR(channel, "in_bytes", "%llu", (unsigned long long)rc->channels[di].in_bytes);
        ADD_ATTR(channel, "in_frames", "%llu", (unsigned long long)rc->channels[di].in_frames);
        ADD_ATTR(channel, "out_bytes", "%llu", (unsigned long long)rc->channels[di].out_bytes);
        ADD_ATTR(channel, "out_frames", "%llu", (unsigned long long)rc->channels[di].out_frames);
        xmlAddChild(channels, channel);
      }
      xmlAddChild(node, channels);
    }
    xmlAddChild(root, node);
  }
  
  pthread_rwlock_unlock(&reverse_sockets_lock);
  free(ctxs);

  noit_http_response_ok(restc->http_ctx, "text/xml");
  noit_http_response_xml(restc->http_ctx, doc);
  noit_http_response_end(restc->http_ctx);
  xmlFreeDoc(doc);
  return 0;
}

void noit_reverse_socket_init() {
  nlerr = noit_log_stream_find("error/reverse");
  nldeb = noit_log_stream_find("debug/reverse");
  if(!nlerr) nlerr = noit_error;
  if(!nldeb) nldeb = noit_debug;

  pthread_rwlock_init(&reverse_sockets_lock, NULL);
  pthread_mutex_init(&reverses_lock, NULL);
  eventer_name_callback("reverse_socket_accept",
                        noit_reverse_socket_acceptor);
  eventer_name_callback("reverse_socket_proxy_accept",
                        noit_reverse_socket_proxy_accept);
  eventer_name_callback("reverse_socket_server_handler",
                        noit_reverse_socket_server_handler);
  eventer_name_callback("reverse_socket_client_handler",
                        noit_reverse_socket_client_handler);
  eventer_name_callback("reverse_socket_channel_handler",
                        noit_reverse_socket_channel_handler);
  eventer_name_callback("noit_connection_reinitiate",
                        noit_connection_reinitiate);
  eventer_name_callback("noit_connection_ssl_upgrade",
                        noit_connection_ssl_upgrade);
  eventer_name_callback("noit_connection_complete_connect",
                        noit_connection_complete_connect);
  eventer_name_callback("noit_connection_session_timeout",
                        noit_connection_session_timeout);

  noit_control_dispatch_delegate(noit_control_dispatch,
                                 NOIT_CONTROL_REVERSE,
                                 noit_reverse_socket_acceptor);

  noit_connections_from_config(&reverses, &reverses_lock,
                               "", NULL, "reverse",
                               noit_reverse_client_handler,
                               (void *(*)())noit_reverse_socket_alloc,
                               NULL,
                               noit_reverse_socket_free);

  assert(noit_http_rest_register_auth(
    "GET", "/reverse/", "^show$", rest_show_reverse,
             noit_http_rest_client_cert_auth
  ) == 0);

  register_console_reverse_commands();
}
