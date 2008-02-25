/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
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

