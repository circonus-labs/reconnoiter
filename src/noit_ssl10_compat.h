#ifndef NOIT_SSL10_COMPAT_H
#define NOIT_SSL10_COMPAT_H

#include <openssl/opensslv.h>

#if OPENSSL_VERSION_NUMBER < 0x10100000L

#include <string.h>
#include <openssl/engine.h>

void *OPENSSL_zalloc(size_t num);

EVP_MD_CTX *EVP_MD_CTX_new(void);

void EVP_MD_CTX_free(EVP_MD_CTX *ctx);
#endif

#endif // NOIT_SSL10_COMPAT_H
