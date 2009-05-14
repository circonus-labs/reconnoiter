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

#ifndef _EVENTER_EVENTER_SSL_FD_OPSET_H
#define _EVENTER_EVENTER_SSL_FD_OPSET_H

#include "noit_defines.h"
#include "eventer/eventer.h"

#include <openssl/ssl.h>

enum {
  SSL_OP_READ,
  SSL_OP_WRITE,
  SSL_OP_CONNECT,
  SSL_OP_ACCEPT
};

typedef enum {
  SSL_SERVER,
  SSL_CLIENT
} eventer_ssl_orientation_t;

extern eventer_fd_opset_t eventer_SSL_fd_opset;

struct eventer_ssl_ctx_t;
typedef struct eventer_ssl_ctx_t eventer_ssl_ctx_t;
typedef int (*eventer_ssl_verify_func_t)(eventer_ssl_ctx_t *,
                                         int, X509_STORE_CTX *, void *);

/* Only the scheduler calls this */
void eventer_ssl_init();

/* Helper functions */
API_EXPORT(eventer_ssl_ctx_t *)
  eventer_ssl_ctx_new(eventer_ssl_orientation_t type,
                      const char *certificate, const char *key,
                      const char *ca, const char *ciphers);
API_EXPORT(void)
  eventer_ssl_ctx_free(eventer_ssl_ctx_t *ctx);

API_EXPORT(eventer_ssl_ctx_t *)
  eventer_get_eventer_ssl_ctx(eventer_t e);

API_EXPORT(void)
  eventer_set_eventer_ssl_ctx(eventer_t e, eventer_ssl_ctx_t *ctx);

/* This makes it more obvious how to turn SSL on */
#define EVENTER_ATTACH_SSL(e,ctx) eventer_set_eventer_ssl_ctx(e,ctx)

API_EXPORT(void)
  eventer_ssl_ctx_set_verify(eventer_ssl_ctx_t *ctx,
                             eventer_ssl_verify_func_t f, void *c);

/* These happen _after_ a socket accept and thus require their
 * strings being pulled from the outside.
 */
API_EXPORT(int) eventer_SSL_accept(eventer_t e, int *mask);
API_EXPORT(int) eventer_SSL_connect(eventer_t e, int *mask);

API_EXPORT(int)
  eventer_ssl_verify_cert(eventer_ssl_ctx_t *ctx, int ok,
                          X509_STORE_CTX *x509ctx, void *closure);

#define GET_SET_X509_NAME_PROTO(type) \
API_EXPORT(char *) \
  eventer_ssl_get_peer_##type(eventer_ssl_ctx_t *ctx)
GET_SET_X509_NAME_PROTO(issuer);
GET_SET_X509_NAME_PROTO(subject);

#endif

