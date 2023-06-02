#ifndef NOIT_SSL10_COMPAT_H
#define NOIT_SSL10_COMPAT_H

#include <openssl/opensslv.h>

#if OPENSSL_VERSION_NUMBER < 0x10100000L

#include <string.h>
#include <openssl/engine.h>

inline void *OPENSSL_zalloc(size_t num)
{
   void *ret = OPENSSL_malloc(num);

   if (ret) {
       memset(ret, 0, num);
   }
   return ret;
}

inline EVP_MD_CTX *EVP_MD_CTX_new(void)
{
  return OPENSSL_zalloc(sizeof(EVP_MD_CTX));
}

inline void EVP_MD_CTX_free(EVP_MD_CTX *ctx)
{
  EVP_MD_CTX_cleanup(ctx);
  OPENSSL_free(ctx);
}
#endif

#endif // NOIT_SSL10_COMPAT_H
