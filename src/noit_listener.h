/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_LISTENER_H
#define _NOIT_LISTENER_H

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "utils/noit_hash.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>

typedef struct {
  union {
    struct sockaddr remote_addr;
    struct sockaddr_in remote_addr4;
    struct sockaddr_in6 remote_addr6;
  } remote;
  char *remote_cn;
  noit_hash_table *config;
  void *service_ctx;
} acceptor_closure_t;

typedef struct {
  int8_t family;
  unsigned short port;
  eventer_func_t dispatch_callback;
  acceptor_closure_t *dispatch_closure;
  noit_hash_table *sslconfig;
} * listener_closure_t;

API_EXPORT(void) noit_listener_init(const char *toplevel);

API_EXPORT(int)
  noit_listener(char *host, unsigned short port, int type,
                int backlog, noit_hash_table *sslconfig,
                noit_hash_table *config,
                eventer_func_t handler, void *service_ctx);

API_EXPORT(void)
  acceptor_closure_free(acceptor_closure_t *ac);

#endif
