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

#ifndef NOIT_REVERSE_SOCKET_H
#define NOIT_REVERSE_SOCKET_H

#include "noit_defines.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>

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
  int max_silence;
  noit_hash_table *config;
  noit_hash_table *sslconfig;
  noit_hash_table *tracker;
  pthread_mutex_t *tracker_lock;
  struct timeval last_connect;
  eventer_t timeout_event;
  eventer_t retry_event;
  eventer_t e;

  void (*schedule_reattempt)(struct noit_connection_ctx_t *, struct timeval *now);
  void (*close)(struct noit_connection_ctx_t *, eventer_t e);

  eventer_func_t consumer_callback;
  void (*consumer_free)(void *);
  void *consumer_ctx;
} noit_connection_ctx_t;

API_EXPORT(void) noit_reverse_socket_init();
API_EXPORT(int) noit_reverse_socket_connect(const char *id, int existing_fd);
API_EXPORT(void) noit_connection_ctx_ref(noit_connection_ctx_t *ctx);
API_EXPORT(void) noit_connection_ctx_deref(noit_connection_ctx_t *ctx);
API_EXPORT(int)
  noit_connection_update_timeout(noit_connection_ctx_t *ctx);
API_EXPORT(int)
  noit_connection_disable_timeout(noit_connection_ctx_t *ctx);
API_EXPORT(void)
  noit_connection_ctx_dealloc(noit_connection_ctx_t *ctx);
API_EXPORT(int)
  noit_connections_from_config(noit_hash_table *tracker, pthread_mutex_t *tracker_lock,
                               const char *toplevel, const char *destination,
                               const char *type,
                               eventer_func_t handler,
                               void *(*handler_alloc)(void), void *handler_ctx,
                               void (*handler_free)(void *));

API_EXPORT(int)
  noit_lua_help_initiate_noit_connection(const char *address, int port,
                                         noit_hash_table *sslconfig,
                                         noit_hash_table *config);
API_EXPORT(int)
  noit_reverse_socket_connection_shutdown(const char *address, int port);

#endif
