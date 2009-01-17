/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _STRATCON_LOG_STREAMER_H
#define _STRATCON_LOG_STREAMER_H

#include "noit_conf.h"
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>

typedef struct noit_connection_ctx_t {
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
  eventer_t timeout_event;

  eventer_func_t consumer_callback;
  void (*consumer_free)(void *);
  void *consumer_ctx;
} noit_connection_ctx_t;

API_EXPORT(void)
  stratcon_jlog_streamer_init(const char *toplevel);
API_EXPORT(void)
  stratcon_jlog_streamer_reload(const char *toplevel);
API_EXPORT(void)
  stratcon_streamer_connection(const char *toplevel, const char *destination,
                               eventer_func_t handler,
                               void *(*handler_alloc)(void), void *handler_ctx,
                               void (*handler_free)(void *));

#endif
