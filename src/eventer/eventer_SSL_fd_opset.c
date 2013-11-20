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

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "eventer/eventer_SSL_fd_opset.h"
#include "eventer/OETS_asn1_helper.h"
#include "noit_dtrace_probes.h"

#include <sys/socket.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/engine.h>
#include <openssl/x509v3.h>

#define EVENTER_SSL_DATANAME "eventer_ssl"

#define SSL_CTX_KEYLEN (PATH_MAX * 4 + 5)
struct cache_finfo {
  ino_t ino;
  time_t mtime;
};

typedef struct {
  char *key;
  SSL_CTX *internal_ssl_ctx;
  time_t creation_time;
  time_t last_stat_time;
  unsigned crl_loaded:1;
  struct cache_finfo cert_finfo;
  struct cache_finfo key_finfo;
  struct cache_finfo ca_finfo;
  noit_atomic32_t refcnt;
} ssl_ctx_cache_node;

static noit_hash_table ssl_ctx_cache;
static pthread_mutex_t ssl_ctx_cache_lock;
static int ssl_ctx_cache_expiry = 3600;
static int ssl_ctx_cache_finfo_expiry = 5;

struct eventer_ssl_ctx_t {
  ssl_ctx_cache_node *ssl_ctx_cn;
  SSL     *ssl;
  char    *issuer;
  char    *subject;
  time_t   start_time;
  time_t   end_time;
  char    *cert_error;
  char    *san_list;
  char    *last_error;
  eventer_ssl_verify_func_t verify_cb;
  void    *verify_cb_closure;
  unsigned no_more_negotiations:1;
  unsigned renegotiated:1;
};

#define ssl_ctx ssl_ctx_cn->internal_ssl_ctx
#define ssl_ctx_crl_loaded ssl_ctx_cn->crl_loaded

/* Static function prototypes */
static void SSL_set_eventer_ssl_ctx(SSL *ssl, eventer_ssl_ctx_t *ctx);
static eventer_ssl_ctx_t *SSL_get_eventer_ssl_ctx(const SSL *ssl);
static void
_eventer_ssl_ctx_save_last_error(eventer_ssl_ctx_t *ctx, int note_errno,
                                const char *file, int line);

#define eventer_ssl_ctx_save_last_error(a,b) \
  _eventer_ssl_ctx_save_last_error((a),(b),__FILE__,__LINE__)
#define MAX_ERR_UNWIND 10

static void
populate_finfo(struct cache_finfo *f, const char *path) {
  struct stat sb;
  memset(&sb, 0, sizeof(sb));
  if(path) while(stat(path, &sb) == -1 && errno == EINTR);
  f->ino = sb.st_ino;
  f->mtime = sb.st_mtime;
}
static int
validate_finfo(const struct cache_finfo *f, const char *path) {
  struct stat sb;
  memset(&sb, 0, sizeof(sb));
  if(path) while(stat(path, &sb) == -1 && errno == EINTR);
  if(f->ino == sb.st_ino && f->mtime == sb.st_mtime) return 0;
  return -1;
}

static void
_eventer_ssl_ctx_save_last_error(eventer_ssl_ctx_t *ctx, int note_errno,
                                const char *file, int line) {
  /* ERR_error_string(3): buf must be at least 120 bytes...
   * no strerrno is more than 120 bytes...
   * file:line:code is never more than 80.
   * So, no line will be longer than 200.
   */
  int used, i = 0, allocd = 0;
  unsigned long err = 0, errors[MAX_ERR_UNWIND] = { 0 };
  char errstr[200], scratch[200];
  errstr[0] = '\0';
  if(note_errno && errno)
    snprintf(errstr, sizeof(errstr), "[%s:%d:%d] %s, ",
             file, line, errno, strerror(errno));
  /* Unwind all, storing up to MAX_ERR_UNWIND */
  while((err = ERR_get_error()) != 0) {
    if(i < MAX_ERR_UNWIND) errors[i] = err;
    i++;
  }
  used = (i > MAX_ERR_UNWIND) ? MAX_ERR_UNWIND : i;

  if(ctx->last_error) {
    free(ctx->last_error);
    ctx->last_error = NULL;
  }
  allocd = 120 * (((errstr[0] == '\0') ? 0 : 1) + used);
  if(allocd == 0) return;
  ctx->last_error = malloc(allocd);
  ctx->last_error[0] = '\0';
  if(errstr[0] != '\0')
    strlcat(ctx->last_error, errstr, allocd);
  for(i=0;i<used;i++) {
    ERR_error_string(errors[i], scratch);
    snprintf(errstr, sizeof(errstr), "[%s:%d:%08lx] %s, ",
             file, line, errors[i], scratch);
    strlcat(ctx->last_error, errstr, allocd);
  }
  /* now clip off the last ", " */
  i = strlen(ctx->last_error);
  if(i>=2) ctx->last_error[i-2] = '\0';
}

static int
eventer_ssl_verify_dates(eventer_ssl_ctx_t *ctx, int ok,
                         X509_STORE_CTX *x509ctx, void *closure) {
  time_t now;
  int err;
  X509 *peer;
  ASN1_TIME *t;
  if(!x509ctx) return -1;
  peer = X509_STORE_CTX_get_current_cert(x509ctx);
  time(&now);
  t = X509_get_notBefore(peer);
  ctx->start_time = OETS_ASN1_TIME_get(t, &err);
  if(X509_cmp_time(t, &now) > 0) return -1;
  t = X509_get_notAfter(peer);
  ctx->end_time = OETS_ASN1_TIME_get(t, &err);
  if(X509_cmp_time(t, &now) < 0) return 1;
  return 0;
}
int
eventer_ssl_get_san_values(eventer_ssl_ctx_t *ctx,
                        X509_STORE_CTX *x509ctx) {
  STACK_OF(GENERAL_NAME) * altnames;
  X509 *peer;
  int pos = 0;

  if(!x509ctx) return 0;
  peer = X509_STORE_CTX_get_current_cert(x509ctx);
  altnames = X509_get_ext_d2i(peer, NID_subject_alt_name, NULL, NULL);
  if (altnames) {
    int i;
    int numalts = sk_GENERAL_NAME_num(altnames);
    char cn[4096];

    memset(cn, 0, 4096);
    for (i = 0; i < numalts; i++) {
      const GENERAL_NAME *check = sk_GENERAL_NAME_value(altnames, i);
      if (check->type != GEN_DNS) {
        continue;
      }
      ASN1_STRING *data = check->d.dNSName;
      if ((data->length + pos) > (int)sizeof(cn) - 1) {
        continue;
      }
      memcpy(cn+pos, data->data, data->length);
      cn[data->length+pos] = ',';
      cn[data->length+pos+1] = ' ';
      cn[data->length+pos+2] = '\0';
      pos = strlen(cn);
    }
    if (pos > 0) {
      cn[pos-1]=0;
      cn[pos-2]=0;
      pos--;
    }
    if (pos > 0) {
      if (ctx->san_list != NULL) {
        free(ctx->san_list);
      }
      ctx->san_list = strdup(cn);
    }
  }
  return 1;
}
int
eventer_ssl_verify_cert(eventer_ssl_ctx_t *ctx, int ok,
                        X509_STORE_CTX *x509ctx, void *closure) {
  noit_hash_table *options = closure;
  const char *opt_no_ca, *ignore_dates;
  int v_res;

  /* Clear any previous error */
  if(ctx->cert_error) free(ctx->cert_error);
  ctx->cert_error = NULL;

  if(!x509ctx) {
    ctx->cert_error = strdup("No certificate present.");
    return 0;
  }

  if(!noit_hash_retr_str(options, "optional_no_ca", strlen("optional_no_ca"),
                         &opt_no_ca))
    opt_no_ca = "false";
  if(!noit_hash_retr_str(options, "ignore_dates", strlen("ignore_dates"),
                         &ignore_dates))
    ignore_dates = "false";
  if(options == NULL) {
    /* Don't care about anything */
    opt_no_ca = "true";
    ignore_dates = "true";
  }
  eventer_ssl_get_san_values(ctx, x509ctx);
  X509_STORE_CTX_get_ex_data(x509ctx,
                             SSL_get_ex_data_X509_STORE_CTX_idx());
  v_res = X509_STORE_CTX_get_error(x509ctx);

  if((v_res == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT) ||
     (v_res == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN) ||
     (v_res == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY) ||
     (v_res == X509_V_ERR_CERT_UNTRUSTED) ||
     (v_res == X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE)) {
    ctx->cert_error = strdup(X509_verify_cert_error_string(v_res));
    if(!strcmp(opt_no_ca, "true")) ok = 1;
    else {
      noitL(eventer_deb, "SSL client cert invalid: %s\n",
            X509_verify_cert_error_string(v_res));
      ok = 0;
      goto set_out;
    }
  }
  v_res = eventer_ssl_verify_dates(ctx, ok, x509ctx, closure);
  if(v_res != 0) {
    if(!strcmp(ignore_dates, "true")) ok = 1;
    else {
      noitL(eventer_deb, "SSL client cert is %s valid.\n",
            (v_res < 0) ? "not yet" : "no longer");
      ctx->cert_error = strdup((v_res < 0) ?
                               "Certificate not yet valid." :
                               "Certificate expired.");
      ok = 0;
      goto set_out;
    }
  }
 set_out:
  return ok;
}

#define GET_SET_X509_NAME(type) \
static void \
eventer_ssl_set_peer_##type(eventer_ssl_ctx_t *ctx, \
                             X509_STORE_CTX *x509ctx) { \
  char buffer[1024]; \
  X509 *peer; \
  peer = X509_STORE_CTX_get_current_cert(x509ctx); \
  X509_NAME_oneline(X509_get_##type##_name(peer), buffer, sizeof(buffer)-1); \
  if(ctx->type) free(ctx->type); \
  ctx->type = strdup(buffer); \
} \
const char * \
eventer_ssl_get_peer_##type(eventer_ssl_ctx_t *ctx) { \
  return ctx->type; \
}

GET_SET_X509_NAME(issuer)
GET_SET_X509_NAME(subject)

time_t
eventer_ssl_get_peer_start_time(eventer_ssl_ctx_t *ctx) {
  return ctx->start_time;
}
time_t
eventer_ssl_get_peer_end_time(eventer_ssl_ctx_t *ctx) {
  return ctx->end_time;
}
const char *
eventer_ssl_get_peer_error(eventer_ssl_ctx_t *ctx) {
  return ctx->cert_error;
}
const char *
eventer_ssl_get_last_error(eventer_ssl_ctx_t *ctx) {
  return ctx->last_error;
}
const char *
eventer_ssl_get_peer_san_list(eventer_ssl_ctx_t *ctx) {
  return ctx->san_list;
}

static int
verify_cb(int ok, X509_STORE_CTX *x509ctx) {
  eventer_ssl_ctx_t *ctx;
  SSL *ssl;

  ssl = X509_STORE_CTX_get_ex_data(x509ctx,
                                   SSL_get_ex_data_X509_STORE_CTX_idx());
  ctx = SSL_get_eventer_ssl_ctx(ssl);
  eventer_ssl_set_peer_subject(ctx, x509ctx);
  eventer_ssl_set_peer_issuer(ctx, x509ctx);
  if(ctx->verify_cb)
    return ctx->verify_cb(ctx, ok, x509ctx, ctx->verify_cb_closure);
  return ok;
}

/*
 * Helpers to create and destroy our context.
 */
static void
ssl_ctx_cache_node_free(ssl_ctx_cache_node *node) {
  if(!node) return;
  if(noit_atomic_dec32(&node->refcnt) == 0) {
    SSL_CTX_free(node->internal_ssl_ctx);
    free(node->key);
    free(node);
  }
}

void
eventer_ssl_ctx_free(eventer_ssl_ctx_t *ctx) {
  if(ctx->ssl) SSL_free(ctx->ssl);
  if(ctx->ssl_ctx_cn) ssl_ctx_cache_node_free(ctx->ssl_ctx_cn);
  if(ctx->issuer) free(ctx->issuer);
  if(ctx->subject) free(ctx->subject);
  if(ctx->cert_error) free(ctx->cert_error);
  if(ctx->last_error) free(ctx->last_error);
  if(ctx->san_list) free(ctx->san_list);
  free(ctx);
}

static void
eventer_SSL_server_info_callback(const SSL *ssl, int type, int val) {
  eventer_ssl_ctx_t *ctx;

  if (ssl->state != SSL3_ST_SR_CLNT_HELLO_A &&
      ssl->state != SSL23_ST_SR_CLNT_HELLO_A)
    return;

  ctx = SSL_get_eventer_ssl_ctx(ssl);
  if(ctx && ctx->no_more_negotiations) {
    noitL(eventer_deb, "eventer_SSL_server_info_callback ... reneg is bad\n");
    ctx->renegotiated = 1;
  }
}

static void
ssl_ctx_key_write(char *b, int blen, eventer_ssl_orientation_t type,
                  const char *certificate, const char *key,
                  const char *ca, const char *ciphers) {
  snprintf(b, blen, "%c:%s:%s:%s:%s",
           (type == SSL_SERVER) ? 'S' : 'C',
           certificate ? certificate : "", key ? key : "",
           ca ? ca : "", ciphers ? ciphers : "");
}

static void
ssl_ctx_cache_remove(const char *key) {
  noitL(eventer_deb, "ssl_ctx_cache->remove(%s)\n", key);
  pthread_mutex_lock(&ssl_ctx_cache_lock);
  noit_hash_delete(&ssl_ctx_cache, key, strlen(key),
                   NULL, (void (*)(void *))ssl_ctx_cache_node_free);
  pthread_mutex_unlock(&ssl_ctx_cache_lock);
}

static ssl_ctx_cache_node *
ssl_ctx_cache_get(const char *key) {
  void *vnode;
  ssl_ctx_cache_node *node = NULL;
  pthread_mutex_lock(&ssl_ctx_cache_lock);
  if(noit_hash_retrieve(&ssl_ctx_cache, key, strlen(key), &vnode)) {
    node = vnode;
    noit_atomic_inc32(&node->refcnt);
  }
  pthread_mutex_unlock(&ssl_ctx_cache_lock);
  noitL(eventer_deb, "ssl_ctx_cache->get(%s) -> %p\n", key, node);
  return node;
}

static ssl_ctx_cache_node *
ssl_ctx_cache_set(ssl_ctx_cache_node *node) {
  void *vnode;
  pthread_mutex_lock(&ssl_ctx_cache_lock);
  if(noit_hash_retrieve(&ssl_ctx_cache, node->key, strlen(node->key),
                        &vnode)) {
    ssl_ctx_cache_node_free(node);
    node = vnode;
  }
  else {
    noit_hash_store(&ssl_ctx_cache, node->key, strlen(node->key), node);
  }
  noit_atomic_inc32(&node->refcnt);
  pthread_mutex_unlock(&ssl_ctx_cache_lock);
  return node;
}

eventer_ssl_ctx_t *
eventer_ssl_ctx_new(eventer_ssl_orientation_t type,
                    const char *certificate, const char *key,
                    const char *ca, const char *ciphers) {
  char ssl_ctx_key[SSL_CTX_KEYLEN];
  eventer_ssl_ctx_t *ctx;
  time_t now;
  ctx = calloc(1, sizeof(*ctx));
  if(!ctx) return NULL;

  now = time(NULL);
  ssl_ctx_key_write(ssl_ctx_key, sizeof(ssl_ctx_key),
                    type, certificate, key, ca, ciphers);
  ctx->ssl_ctx_cn = ssl_ctx_cache_get(ssl_ctx_key);
  if(ctx->ssl_ctx_cn) {
    if(now - ctx->ssl_ctx_cn->creation_time > ssl_ctx_cache_expiry ||
       (now - ctx->ssl_ctx_cn->last_stat_time > ssl_ctx_cache_finfo_expiry &&
           (validate_finfo(&ctx->ssl_ctx_cn->cert_finfo, certificate) ||
            validate_finfo(&ctx->ssl_ctx_cn->key_finfo, key) ||
            validate_finfo(&ctx->ssl_ctx_cn->ca_finfo, ca) || 
            (ctx->ssl_ctx_cn->last_stat_time = now) == 0))) { /* assignment */
      noit_atomic_dec32(&ctx->ssl_ctx_cn->refcnt);
      ssl_ctx_cache_remove(ssl_ctx_key);
      ctx->ssl_ctx_cn = NULL;
    }
  }

  if(!ctx->ssl_ctx_cn) {
    ctx->ssl_ctx_cn = calloc(1, sizeof(*ctx->ssl_ctx_cn));
    ctx->ssl_ctx_cn->key = strdup(ssl_ctx_key);
    ctx->ssl_ctx_cn->refcnt = 1;
    ctx->ssl_ctx_cn->creation_time = now;
    ctx->ssl_ctx_cn->last_stat_time = now;
    populate_finfo(&ctx->ssl_ctx_cn->cert_finfo, certificate);
    populate_finfo(&ctx->ssl_ctx_cn->key_finfo, key);
    populate_finfo(&ctx->ssl_ctx_cn->ca_finfo, ca);
    ctx->ssl_ctx = SSL_CTX_new(type == SSL_SERVER ?
                               SSLv23_server_method() : SSLv23_client_method());
    if(!ctx->ssl_ctx) goto bail;
    if (type == SSL_SERVER)
      SSL_CTX_set_session_id_context(ctx->ssl_ctx,
              (unsigned char *)EVENTER_SSL_DATANAME,
              sizeof(EVENTER_SSL_DATANAME)-1);
    SSL_CTX_set_options(ctx->ssl_ctx, SSL_OP_ALL);
#ifdef SSL_MODE_RELEASE_BUFFERS
    SSL_CTX_set_mode(ctx->ssl_ctx, SSL_MODE_RELEASE_BUFFERS);
#endif
    if(certificate &&
       SSL_CTX_use_certificate_chain_file(ctx->ssl_ctx, certificate) != 1)
      goto bail;
    if(key &&
       SSL_CTX_use_RSAPrivateKey_file(ctx->ssl_ctx,key,
                                      SSL_FILETYPE_PEM) != 1)
      goto bail;
    if(ca) {
      STACK_OF(X509_NAME) *cert_stack;
      if(!SSL_CTX_load_verify_locations(ctx->ssl_ctx,ca,NULL) ||
         (cert_stack = SSL_load_client_CA_file(ca)) == NULL)
        goto bail;
      SSL_CTX_set_client_CA_list(ctx->ssl_ctx, cert_stack);
    }
    SSL_CTX_set_cipher_list(ctx->ssl_ctx, ciphers ? ciphers : "DEFAULT");
    SSL_CTX_set_verify(ctx->ssl_ctx, SSL_VERIFY_PEER, verify_cb);
    SSL_CTX_set_options(ctx->ssl_ctx, SSL_OP_NO_SSLv2);
    ssl_ctx_cache_set(ctx->ssl_ctx_cn);
  }

  ctx->ssl = SSL_new(ctx->ssl_ctx);
  if(!ctx->ssl) goto bail;
  SSL_set_info_callback(ctx->ssl, eventer_SSL_server_info_callback);
  SSL_set_eventer_ssl_ctx(ctx->ssl, ctx);
  return ctx;

 bail:
  eventer_ssl_ctx_save_last_error(ctx, 1);
  eventer_ssl_ctx_free(ctx);
  return NULL;
}

int
eventer_ssl_use_crl(eventer_ssl_ctx_t *ctx, const char *crl_file) {
  int ret;
  X509_STORE *store;
  X509_LOOKUP *lookup;
  if(ctx->ssl_ctx_crl_loaded) return 1;
  store = SSL_CTX_get_cert_store(ctx->ssl_ctx);
  lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file());
  ret = X509_load_crl_file(lookup, crl_file, X509_FILETYPE_PEM); 
  X509_STORE_set_flags(store, X509_V_FLAG_CRL_CHECK |
                              X509_V_FLAG_CRL_CHECK_ALL);
  if(!ret) eventer_ssl_ctx_save_last_error(ctx, 1);
  else ctx->ssl_ctx_crl_loaded = 1;
  return ret;
}

/*
 * This is a set of helpers to tie the SSL stuff to the eventer_t.
 */
static int SSL_eventer_ssl_ctx_dataid = -1;
#define INIT_DATAID do { \
  if(SSL_eventer_ssl_ctx_dataid == -1) \
    SSL_eventer_ssl_ctx_dataid = \
      SSL_get_ex_new_index(0, EVENTER_SSL_DATANAME, NULL, NULL, NULL); \
} while(0)

static void
SSL_set_eventer_ssl_ctx(SSL *ssl, eventer_ssl_ctx_t *ctx) {
  INIT_DATAID;
  SSL_set_ex_data(ssl, SSL_eventer_ssl_ctx_dataid, ctx);
}

static eventer_ssl_ctx_t *
SSL_get_eventer_ssl_ctx(const SSL *ssl) {
  INIT_DATAID;
  return SSL_get_ex_data(ssl, SSL_eventer_ssl_ctx_dataid);
}

eventer_ssl_ctx_t *
eventer_get_eventer_ssl_ctx(const eventer_t e) {
  return (e->opset == eventer_SSL_fd_opset) ? e->opset_ctx : NULL;
}

void
eventer_set_eventer_ssl_ctx(eventer_t e, eventer_ssl_ctx_t *ctx) {
  e->opset = eventer_SSL_fd_opset;
  e->opset_ctx = ctx;
  SSL_set_fd(ctx->ssl, e->fd);
}

void
eventer_ssl_ctx_set_sni(eventer_ssl_ctx_t *ctx, const char *snivalue) {
#ifdef SSL_CTRL_SET_TLSEXT_SERVERNAME_CB
  SSL_set_tlsext_host_name(ctx->ssl, snivalue);
#endif
}

void
eventer_ssl_ctx_set_verify(eventer_ssl_ctx_t *ctx,
                           eventer_ssl_verify_func_t f, void *c) {
  ctx->verify_cb = f;
  ctx->verify_cb_closure = c;
}

/* Accept will perform the usual BSD socket accept and then
 * hand it into the SSL system.
 */
static int
_noallowed_eventer_SSL_accept(int fd, struct sockaddr *addr, socklen_t *len,
                              int *mask, void *closure) {
  return -1;
}

static int 
eventer_SSL_setup(eventer_ssl_ctx_t *ctx) {
  X509 *peer = NULL;
  SSL_set_mode(ctx->ssl, SSL_MODE_ENABLE_PARTIAL_WRITE);
  peer = SSL_get_peer_certificate(ctx->ssl);

  /* If have no peer, or the peer cert isn't okay, our
   * callback won't fire, so fire it explicitly here.
   * Learnt this from mod_ssl.
   */
  if(!peer ||
     (peer && SSL_get_verify_result(ctx->ssl) != X509_V_OK)) {
    if(ctx->verify_cb) {
      if(peer) X509_free(peer);
      return ctx->verify_cb(ctx, 0, NULL, ctx->verify_cb_closure);
    }
  }
  if(peer) X509_free(peer);
  return 0;
}

/* The read and write operations for ssl are almost identical.
 * We read or write, depending on the need and if the SSL subsystem
 * says we need more data to continue we mask for read, if it says
 * we need need to write data to continue we mask for write.  Either
 * way, we EAGAIN.
 * If there is an SSL error, we spit it out and return EIO as that
 * seems most appropriate.
 */
static int
eventer_SSL_rw(int op, int fd, void *buffer, size_t len, int *mask,
               void *closure) {
  int rv, sslerror;
  eventer_t e = closure;
  eventer_ssl_ctx_t *ctx = e->opset_ctx;
  int (*sslop)(SSL *) = NULL;
  const char *opstr = NULL;

  switch(op) {
    case SSL_OP_READ:
      opstr = "read";
      if((rv = SSL_read(ctx->ssl, buffer, len)) > 0) return rv;
      break;
    case SSL_OP_WRITE:
      opstr = "write";
      if((rv = SSL_write(ctx->ssl, buffer, len)) > 0) return rv;
      break;

    case SSL_OP_CONNECT:
      opstr = "connect";
      if(!sslop) sslop = SSL_connect;
      /* fall through */
    case SSL_OP_ACCEPT:
      if(!opstr) opstr = "accept";
      /* only set if we didn't fall through */
      if(!sslop) sslop = SSL_accept;
   
      if((rv = sslop(ctx->ssl)) > 0) {
        if(eventer_SSL_setup(ctx)) {
          errno = EIO;
          eventer_ssl_ctx_save_last_error(ctx, 1);
          return -1;
        }
        ctx->no_more_negotiations = 1;
        return rv;
      }
      break;

    default:
      abort();
  }
  /* This can't happen as we'd have already aborted... */
  if(!opstr) opstr = "none";

  if(ctx->renegotiated) {
    noitL(eventer_err, "SSL renogotiation attempted on %d\n", fd);
    errno = EIO;
    eventer_ssl_ctx_save_last_error(ctx, 1);
    return -1;
  }

  switch(sslerror = SSL_get_error(ctx->ssl, rv)) {
    case SSL_ERROR_NONE:
      return 0;
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      *mask = (sslerror == SSL_ERROR_WANT_READ) ?
                EVENTER_READ : EVENTER_WRITE;
      errno = EAGAIN;
      break;
    default:
      noitL(eventer_deb, "SSL[%s of %d] rw error: %d\n", opstr,
            (int)len, sslerror);
      eventer_ssl_ctx_save_last_error(ctx, 1);
      errno = EIO;
  }
  return -1;
}

int
eventer_SSL_renegotiate(eventer_t e) {
  eventer_ssl_ctx_t *ctx;
  ctx = eventer_get_eventer_ssl_ctx(e);
  if(!ctx) return -1;
  SSL_renegotiate(ctx->ssl);
  return 0;
}

int
eventer_SSL_accept(eventer_t e, int *mask) {
  int rv;
  EVENTER_ACCEPT_ENTRY(e->fd, NULL, 0, *mask, e->opset_ctx);
  rv = eventer_SSL_rw(SSL_OP_ACCEPT, e->fd, NULL, 0, mask, e);
  EVENTER_ACCEPT_RETURN(e->fd, NULL, 0, *mask, e->opset_ctx, rv);
  return rv;
}
int
eventer_SSL_connect(eventer_t e, int *mask) {
  return eventer_SSL_rw(SSL_OP_CONNECT, e->fd, NULL, 0, mask, e);
}
static int
eventer_SSL_read(int fd, void *buffer, size_t len, int *mask, void *closure) {
  int rv;
  EVENTER_READ_ENTRY(fd, buffer, len, *mask, closure);
  rv = eventer_SSL_rw(SSL_OP_READ, fd, buffer, len, mask, closure);
  EVENTER_READ_RETURN(fd, buffer, len, *mask, closure, rv);
  return rv;
}
static int
eventer_SSL_write(int fd, const void *buffer, size_t len, int *mask,
                  void *closure) {
  int rv;
  EVENTER_WRITE_ENTRY(fd, buffer, len, *mask, closure);
  rv = eventer_SSL_rw(SSL_OP_WRITE, fd, (void *)buffer, len, mask, closure);
  EVENTER_WRITE_RETURN(fd, buffer, len, *mask, closure, rv);
  return rv;
}

/* Close simply shuts down the SSL site and closes the file descriptor. */
static int
eventer_SSL_close(int fd, int *mask, void *closure) {
  int rv;
  eventer_t e = closure;
  eventer_ssl_ctx_t *ctx = e->opset_ctx;
  EVENTER_CLOSE_ENTRY(fd, *mask, closure);
  SSL_shutdown(ctx->ssl);
  eventer_ssl_ctx_free(ctx);
  rv = close(fd);
  if(mask) *mask = 0;
  EVENTER_CLOSE_RETURN(fd, *mask, closure, rv);
  return 0;
}

struct _fd_opset _eventer_SSL_fd_opset = {
  _noallowed_eventer_SSL_accept,
  eventer_SSL_read,
  eventer_SSL_write,
  eventer_SSL_close,
  "SSL"
};

eventer_fd_opset_t eventer_SSL_fd_opset = &_eventer_SSL_fd_opset;


/* Locking stuff to make libcrypto thread safe */
/* This stuff cribbed from the openssl examples */
struct CRYPTO_dynlock_value { pthread_mutex_t lock; };
static struct CRYPTO_dynlock_value *__lcks = NULL;
static void lock_static(int mode, int type, const char *f, int l) {
  if(mode & CRYPTO_LOCK) pthread_mutex_lock(&__lcks[type].lock);
  else pthread_mutex_unlock(&__lcks[type].lock);
}
static struct CRYPTO_dynlock_value *dynlock_create(const char *f, int l) {
  struct CRYPTO_dynlock_value *lock = CRYPTO_malloc(sizeof(*lock),f,l);
  pthread_mutex_init(&lock->lock,  NULL);
  return lock;
}
static void dynlock_destroy(struct CRYPTO_dynlock_value *lock,
                            const char *f, int l) {
  pthread_mutex_destroy(&lock->lock);
  CRYPTO_free(lock);
}
static void lock_dynamic(int mode, struct CRYPTO_dynlock_value *lock,
                         const char *f, int l) {
  if(mode & CRYPTO_LOCK) pthread_mutex_lock(&lock->lock);
  else pthread_mutex_unlock(&lock->lock);
}
void eventer_ssl_set_ssl_ctx_cache_expiry(int timeout) {
  ssl_ctx_cache_expiry = timeout;
}
void eventer_ssl_init() {
  int i, numlocks;
  if(__lcks) return;
  numlocks = CRYPTO_num_locks();
  __lcks = CRYPTO_malloc(numlocks * sizeof(*__lcks),__FILE__,__LINE__);
  for(i=0; i<numlocks; i++)
    pthread_mutex_init(&__lcks[i].lock, NULL);
  CRYPTO_set_id_callback((unsigned long (*)()) pthread_self);
  CRYPTO_set_dynlock_create_callback(dynlock_create);
  CRYPTO_set_dynlock_destroy_callback(dynlock_destroy);
  CRYPTO_set_dynlock_lock_callback(lock_dynamic);
  CRYPTO_set_locking_callback(lock_static);

  SSL_load_error_strings();
  SSL_library_init();
  OpenSSL_add_all_ciphers();
  return;
}

