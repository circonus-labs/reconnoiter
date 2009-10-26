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

#ifndef _STRATCON_LOG_STREAMER_H
#define _STRATCON_LOG_STREAMER_H

#include "noit_conf.h"
#include "utils/noit_atomic.h"
#include "jlog/jlog.h"
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include "stratcon_datastore.h"

typedef struct noit_connection_ctx_t {
  noit_atomic32_t refcnt;
  union {
    struct sockaddr remote;
    struct sockaddr_un remote_un;
    struct sockaddr_in remote_in;
    struct sockaddr_in6 remote_in6;
  } r;
  socklen_t remote_len;
  char *remote_str;
  char *remote_cn;
  u_int32_t current_backoff;
  int wants_shutdown;
  int wants_permanent_shutdown;
  noit_hash_table *config;
  noit_hash_table *sslconfig;
  struct timeval last_connect;
  eventer_t timeout_event;

  eventer_func_t consumer_callback;
  void (*consumer_free)(void *);
  void *consumer_ctx;
} noit_connection_ctx_t;

typedef struct jlog_streamer_ctx_t {
  u_int32_t jlog_feed_cmd;
  int bytes_expected;
  int bytes_read;
  char *buffer;         /* These guys are for doing partial reads */

  enum {
    JLOG_STREAMER_WANT_INITIATE = 0,
    JLOG_STREAMER_WANT_COUNT = 1,
    JLOG_STREAMER_WANT_HEADER = 2,
    JLOG_STREAMER_WANT_BODY = 3,
    JLOG_STREAMER_IS_ASYNC = 4,
    JLOG_STREAMER_WANT_CHKPT = 5,
    JLOG_STREAMER_WANT_ERROR = 6,
  } state;
  int count;            /* Number of jlog messages we need to read */
  struct {
    jlog_id   chkpt;
    u_int32_t tv_sec;
    u_int32_t tv_usec;
    u_int32_t message_len;
  } header;

  u_int64_t total_events;
  u_int64_t total_bytes_read;

  void (*push)(stratcon_datastore_op_t, struct sockaddr *, const char *, void *, eventer_t);
} jlog_streamer_ctx_t;

API_EXPORT(void)
  stratcon_jlog_streamer_init(const char *toplevel);
API_EXPORT(void)
  stratcon_jlog_streamer_reload(const char *toplevel);
API_EXPORT(int)
  stratcon_jlog_recv_handler(eventer_t e, int mask, void *closure,
                             struct timeval *now);
API_EXPORT(jlog_streamer_ctx_t *)
  stratcon_jlog_streamer_ctx_alloc(void);
API_EXPORT(void)
  jlog_streamer_ctx_free(void *cl);
API_EXPORT(void)
  noit_connection_ctx_dealloc(noit_connection_ctx_t *ctx);
API_EXPORT(void)
  stratcon_streamer_connection(const char *toplevel, const char *destination,
                               eventer_func_t handler,
                               void *(*handler_alloc)(void), void *handler_ctx,
                               void (*handler_free)(void *));

#endif
