/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _STRATCON_LOG_STREAMER_H
#define _STRATCON_LOG_STREAMER_H

#include "noit_conf.h"
#include "jlog/jlog.h"
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include "stratcon_datastore.h"

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
    JLOG_STREAMER_WANT_CHKPT = 4,
  } state;
  int count;            /* Number of jlog messages we need to read */
  struct {
    jlog_id   chkpt;
    u_int32_t tv_sec;
    u_int32_t tv_usec;
    u_int32_t message_len;
  } header;

  void (*push)(stratcon_datastore_op_t, struct sockaddr *, void *);
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
  stratcon_streamer_connection(const char *toplevel, const char *destination,
                               eventer_func_t handler,
                               void *(*handler_alloc)(void), void *handler_ctx,
                               void (*handler_free)(void *));

#endif
