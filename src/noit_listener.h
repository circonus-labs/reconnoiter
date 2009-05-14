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

#ifndef _NOIT_LISTENER_H
#define _NOIT_LISTENER_H

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "utils/noit_hash.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
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
  eventer_func_t dispatch;
  u_int32_t cmd;
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

API_EXPORT(void)
  noit_control_dispatch_delegate(eventer_func_t listener_dispatch,
                                 u_int32_t cmd,
                                 eventer_func_t delegate_dispatch);

API_EXPORT(int)
  noit_control_dispatch(eventer_t, int, void *, struct timeval *);

API_EXPORT(noit_hash_table *)
  noit_listener_commands();

#endif
