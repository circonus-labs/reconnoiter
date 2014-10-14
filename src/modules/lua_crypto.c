/*
 * Copyright (c) 2014, Circonus, Inc.
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

#include "noit_defines.h"

#include "noit_conf.h"
#include "lua_noit.h"
#include <openssl/bn.h>
#include <openssl/rsa.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#ifndef sk_OPENSSL_STRING_num
#define sk_OPENSSL_STRING_num sk_num
#endif

#ifndef sk_OPENSSL_STRING_value
#define sk_OPENSSL_STRING_value sk_value
#endif


#define PUSH_OBJ(L, tname, obj) do { \
  *(void **)(lua_newuserdata(L, sizeof(void *))) = (obj); \
  luaL_getmetatable(L, tname); \
  lua_setmetatable(L, -2); \
} while(0)

int
noit_lua_crypto_newx509(lua_State *L, X509 *x509) {
  if(x509 == NULL) return 0;
  PUSH_OBJ(L, "crypto.x509", x509);
  return 1;
}

static int
noit_lua_crypto_x509_index_func(lua_State *L) {
  const char *k;
  void *udata;
  X509 *cert;
  int j;

  assert(lua_gettop(L) == 2);
  if(!luaL_checkudata(L, 1, "crypto.x509")) {
    luaL_error(L, "metatable error, arg1 not a crypto.rsa!");
  }
  udata = lua_touserdata(L, 1);
  k = lua_tostring(L, 2);
  cert = *((X509 **)udata);
  if(!strcmp(k, "signature_algorithm")) {
    int nid;
    nid = OBJ_obj2nid(cert->sig_alg->algorithm);
    lua_pushstring(L, OBJ_nid2sn(nid));
    return 1;
  }
  if(!strcmp(k, "purpose")) {
    int i, j, pret;
    int cnt = X509_PURPOSE_get_count();
    lua_newtable(L);
    for(i=0; i<cnt; i++) {
      int id;
      char *pname;
      X509_PURPOSE *pt;
      pt = X509_PURPOSE_get0(i);
      id = X509_PURPOSE_get_id(pt);
      pname = X509_PURPOSE_get0_name(pt);
      for(j=0; j<2; j++) {
        char name_full[1024];
        pret = X509_check_purpose(cert, id, j);
        snprintf(name_full, sizeof(name_full), "%s%s", pname, j ? "_ca" : "");
        lua_pushstring(L, name_full);
        lua_pushinteger(L, pret);
        lua_settable(L, -3);
      }
    }
    return 1;
  }
  if(!strcmp(k, "serial")) {
    lua_pushinteger(L, ASN1_INTEGER_get(X509_get_serialNumber(cert)));
    return 1;
  }
  if(!strcmp(k, "bits")) {
    EVP_PKEY *pkey;
    pkey = X509_get_pubkey(cert);
    if (pkey == NULL) return 0;
    else if (pkey->type == EVP_PKEY_RSA)
      lua_pushinteger(L, BN_num_bits(pkey->pkey.rsa->n));
    else if (pkey->type == EVP_PKEY_DSA)
      lua_pushinteger(L, BN_num_bits(pkey->pkey.dsa->p));
    else lua_pushnil(L);
    EVP_PKEY_free(pkey);
    return 1;
  }
  if(!strcmp(k, "type")) {
    EVP_PKEY *pkey;
    pkey = X509_get_pubkey(cert);
    if (pkey == NULL) return 0;
    else if (pkey->type == EVP_PKEY_RSA) lua_pushstring(L, "rsa");
    else if (pkey->type == EVP_PKEY_DSA) lua_pushstring(L, "dsa");
    else lua_pushstring(L, "unknown");
    EVP_PKEY_free(pkey);
    return 1;
  }
  if(!strcmp(k, "ocsp")) {
    STACK_OF(OPENSSL_STRING) *emlst;
    emlst = X509_get1_ocsp(cert);
    for (j = 0; j < sk_OPENSSL_STRING_num(emlst); j++) {
      lua_pushstring(L, sk_OPENSSL_STRING_value(emlst, j));
    }
    X509_email_free(emlst);
    return j;
  }
  luaL_error(L, "crypto.x509 no such element: %s", k);
  return 0;
}

static int
noit_lua_crypto_x509_gc(lua_State *L) {
  return 0;
}
static int
noit_lua_crypto_newrsa(lua_State *L) {
  int bits = 2048;
  int e = 65537;
  BIGNUM *bn = NULL;
  RSA *rsa = NULL;

  if(lua_gettop(L) > 0) {
    if(lua_isnumber(L,1))
      bits = lua_tointeger(L,1);
    else {
      BIO *bio;
      size_t len;
      const char *key;
      key = lua_tolstring(L,1,&len);
      bio = BIO_new_mem_buf((void *)key,len);
      if(bio && PEM_read_bio_RSAPrivateKey(bio, &rsa, NULL, NULL)) {
        PUSH_OBJ(L, "crypto.rsa", rsa);
        return 1;
      }
      lua_pushnil(L);
      return 1;
    }
  }
  if(lua_gettop(L) > 1) e = lua_tointeger(L,2);

  rsa = RSA_new();
  if(!rsa) goto fail;
  bn = BN_new();
  if(!bn) goto fail;
  if(!BN_set_word(bn, e)) goto fail;
  if(!RSA_generate_key_ex(rsa, bits, bn, NULL)) goto fail;
  BN_free(bn);

  PUSH_OBJ(L, "crypto.rsa", rsa);
  return 1;

 fail:
  if(bn) BN_free(bn);
  if(rsa) RSA_free(rsa);
  lua_pushnil(L);
  return 1;
}

static int
noit_lua_crypto_rsa_gencsr(lua_State *L) {
  RSA *rsa;
  X509_REQ *req = NULL;
  X509_NAME *subject = NULL;
  const EVP_MD *md = NULL;
  EVP_PKEY *pkey = NULL;
  const char *lua_string;
  char buf[1024];
  const char *error = buf;
  char *errbuf;
  void **udata;

  strlcpy(buf, "crypto.rsa:gencsr ", sizeof(buf));
  errbuf = buf + strlen(buf);
#define REQERR(err) do { \
  strlcpy(errbuf, err, sizeof(buf) - (errbuf - buf)); \
  goto fail; \
} while(0)

  if(!luaL_checkudata(L, 1, "crypto.rsa")) {
    luaL_error(L, "metatable error, arg1 not a crypto.rsa!");
  }

  if(!lua_istable(L,2)) REQERR("requires table as second argument");
  lua_pushvalue(L,2);
  udata = lua_touserdata(L, lua_upvalueindex(1));
  rsa = (RSA *)*udata;

#define GET_OR(str, name,fallback) do { \
  lua_getfield(L,-1,name); \
  str = lua_isstring(L,-1) ? lua_tostring(L,-1) : fallback; \
  lua_pop(L,1); \
} while(0)
  GET_OR(lua_string, "digest", "sha1");
  md = EVP_get_digestbyname(lua_string);
  if(!md) REQERR("unknown digest");
  pkey = EVP_PKEY_new();
  if(!EVP_PKEY_assign_RSA(pkey, RSAPrivateKey_dup(rsa)))
    REQERR("crypto.rsa:gencsr could not use private key");
  req = X509_REQ_new();
  if(!req) REQERR("crypto.rsa:gencsr allocation failure");
  if (!X509_REQ_set_version(req,0L)) /* version 1 */
    REQERR("crypto.rsa:gencsr could not set request version");
  lua_getfield(L,-1,"subject");
  if(!lua_istable(L,-1)) REQERR("subject value must be a table");

  subject = X509_NAME_new();
  lua_pushnil(L);
  while(lua_next(L, -2)) {
    int nid;
    const char *subj_part = lua_tostring(L, -2);
    const char *subj_value = lua_tostring(L, -1);

    if((nid=OBJ_txt2nid(subj_part)) == NID_undef) {
      noitL(noit_error, "crypto.rsa:gencsr unknown subject part %s\n", subj_part);
    }
    else if(subj_value == NULL || *subj_value == '\0') {
      noitL(noit_error, "crypto.rsa:gencsr subject part %s is blank\n", subj_part);
    }
    else if(!X509_NAME_add_entry_by_NID(subject, nid, MBSTRING_ASC,
                                        (unsigned char*)subj_value,-1,-1,0)) {
      REQERR("failure building subject");
    }
    lua_pop(L,1);
  }
  if(!X509_REQ_set_subject_name(req, subject)) {
    ERR_error_string(ERR_get_error(), errbuf);
    goto fail;
  }
  X509_NAME_free(subject);
  subject = NULL;
  if(!X509_REQ_set_pubkey(req,pkey)) {
    ERR_error_string(ERR_get_error(), errbuf);
    goto fail;
  }
  if(!X509_REQ_sign(req,pkey,md)) {
    pkey = NULL;
    ERR_error_string(ERR_get_error(), errbuf);
    goto fail;
  }
  pkey = NULL;
  PUSH_OBJ(L, "crypto.req", req);
  return 1;

 fail:
  if(subject) X509_NAME_free(subject);
  if(pkey) EVP_PKEY_free(pkey);
  if(req) X509_REQ_free(req);
  luaL_error(L, error);
  return 0;
}

static int
noit_lua_crypto_rsa_as_pem(lua_State *L) {
  BIO *bio;
  RSA *rsa;
  long len;
  char *pem;
  void **udata;
  udata = lua_touserdata(L, lua_upvalueindex(1));
  if(udata != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  rsa = (RSA *)*udata;

  bio = BIO_new(BIO_s_mem());
  PEM_write_bio_RSAPrivateKey(bio, rsa, NULL, NULL, 0, NULL, NULL);
  len = BIO_get_mem_data(bio, &pem);
  lua_pushlstring(L, pem, len);
  BIO_free(bio);
  return 1;
}

static int
noit_lua_crypto_rsa_index_func(lua_State *L) {
  const char *k;
  void *udata;
  assert(lua_gettop(L) == 2);
  if(!luaL_checkudata(L, 1, "crypto.rsa")) {
    luaL_error(L, "metatable error, arg1 not a crypto.rsa!");
  }
  udata = lua_touserdata(L, 1);
  k = lua_tostring(L, 2);
  if(!strcmp(k,"pem")) {
    lua_pushlightuserdata(L, udata);
    lua_pushcclosure(L, noit_lua_crypto_rsa_as_pem, 1);
    return 1;
  }
  if(!strcmp(k,"gencsr")) {
    lua_pushlightuserdata(L, udata);
    lua_pushcclosure(L, noit_lua_crypto_rsa_gencsr, 1);
    return 1;
  }
  luaL_error(L, "crypto.rsa no such element: %s", k);
  return 0;
}

static int
noit_lua_crypto_rsa_gc(lua_State *L) {
  void **udata;
  udata = lua_touserdata(L,1);
  RSA_free((RSA *)*udata);
  return 0;
}

static int
noit_lua_crypto_req_as_pem(lua_State *L) {
  BIO *bio;
  X509_REQ *req;
  long len;
  char *pem;
  void **udata;
  udata = lua_touserdata(L, lua_upvalueindex(1));
  if(udata != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  req = (X509_REQ *)*udata;

  bio = BIO_new(BIO_s_mem());
  PEM_write_bio_X509_REQ(bio, req);
  len = BIO_get_mem_data(bio, &pem);
  lua_pushlstring(L, pem, len);
  BIO_free(bio);
  return 1;
}

static int
noit_lua_crypto_req_index_func(lua_State *L) {
  const char *k;
  void *udata;
  assert(lua_gettop(L) == 2);
  if(!luaL_checkudata(L, 1, "crypto.req")) {
    luaL_error(L, "metatable error, arg1 not a crypto.req!");
  }
  udata = lua_touserdata(L, 1);
  k = lua_tostring(L, 2);
  if(!strcmp(k,"pem")) {
    lua_pushlightuserdata(L, udata);
    lua_pushcclosure(L, noit_lua_crypto_req_as_pem, 1);
    return 1;
  }
  luaL_error(L, "crypto.req no such element: %s", k);
  return 0;
}

static int
noit_lua_crypto_req_gc(lua_State *L) {
  void **udata;
  udata = lua_touserdata(L,1);
  X509_REQ_free((X509_REQ *)*udata);
  return 0;
}

static const struct luaL_Reg crupto_funcs[] = {
  { "newrsa",  noit_lua_crypto_newrsa },
  { NULL, NULL }
};

int luaopen_crypto(lua_State *L) {
  luaL_newmetatable(L, "crypto.x509");
  lua_pushcclosure(L, noit_lua_crypto_x509_index_func, 0);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, noit_lua_crypto_x509_gc);
  lua_setfield(L, -2, "__gc");

  luaL_newmetatable(L, "crypto.rsa");
  lua_pushcclosure(L, noit_lua_crypto_rsa_index_func, 0);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, noit_lua_crypto_rsa_gc);
  lua_setfield(L, -2, "__gc");

  luaL_newmetatable(L, "crypto.req");
  lua_pushcclosure(L, noit_lua_crypto_req_index_func, 0);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, noit_lua_crypto_req_gc);
  lua_setfield(L, -2, "__gc");

  luaL_openlib(L, "crypto", crupto_funcs, 0);
  return 0;
}
