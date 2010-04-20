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

#include <assert.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif

#include "noit_conf.h"
#include "lua_noit.h"
#include "udns/udns.h"

static noit_hash_table dns_rtypes = NOIT_HASH_EMPTY;
static noit_hash_table dns_ctypes = NOIT_HASH_EMPTY;
static noit_hash_table dns_ctx_store = NOIT_HASH_EMPTY;
static pthread_mutex_t dns_ctx_store_lock;

typedef struct dns_ctx_handle {
  char *ns;
  struct dns_ctx *ctx;
  noit_atomic32_t refcnt;
  eventer_t e; /* eventer handling UDP traffic */
  eventer_t timeout; /* the timeout managed by libudns */
} dns_ctx_handle_t;

typedef struct dns_lookup_ctx {
  noit_lua_check_info_t *ci;
  dns_ctx_handle_t *h;
  char *error;
  unsigned char dn[DNS_MAXDN];
  enum dns_class query_ctype;
  enum dns_type query_rtype;
  int active;
  noit_atomic32_t refcnt;
} dns_lookup_ctx_t;

static dns_ctx_handle_t *default_ctx_handle = NULL;

static int noit_lua_dns_eventer(eventer_t e, int mask, void *closure,
                                struct timeval *now) {
  dns_ctx_handle_t *h = closure;
  dns_ioevent(h->ctx, now->tv_sec);
  return EVENTER_READ | EVENTER_EXCEPTION;
}

static int noit_lua_dns_timeouts(eventer_t e, int mask, void *closure,
                                 struct timeval *now) {
  dns_ctx_handle_t *h = closure;
  dns_timeouts(h->ctx, 0, now->tv_sec);
  return 0;
}

static void eventer_dns_utm_fn(struct dns_ctx *ctx, int timeout, void *data) {
  dns_ctx_handle_t *h = data;
  eventer_t e = NULL, newe = NULL;
  if(ctx == NULL) e = eventer_remove(h->timeout);
  else {
    assert(h->ctx == ctx);
    if(timeout < 0) e = eventer_remove(h->timeout);
    else {
      newe = eventer_alloc();
      newe->mask = EVENTER_TIMER;
      newe->callback = noit_lua_dns_timeouts;
      newe->closure = h;
      gettimeofday(&newe->whence, NULL);
      newe->whence.tv_sec += timeout;
    }
  }
  if(e) eventer_free(e);
  if(newe) eventer_add(newe);
  h->timeout = newe;
}

static void dns_ctx_handle_free(void *vh) {
  dns_ctx_handle_t *h = vh;
  assert(h->timeout == NULL);
  free(h->ns);
  dns_close(h->ctx);
  dns_free(h->ctx);
}

static dns_ctx_handle_t *dns_ctx_alloc(const char *ns) {
  void *vh;
  dns_ctx_handle_t *h = NULL;
  pthread_mutex_lock(&dns_ctx_store_lock);
  if(ns == NULL && default_ctx_handle != NULL) {
    /* special case -- default context */
    h = default_ctx_handle;
    noit_atomic_inc32(&h->refcnt);
    goto bail;
  }
  if(ns &&
     noit_hash_retrieve(&dns_ctx_store, ns, strlen(ns), &vh)) {
    h = (dns_ctx_handle_t *)vh;
    noit_atomic_inc32(&h->refcnt);
  }
  else {
    int failed = 0;
    h = calloc(1, sizeof(*h));
    h->ns = ns ? strdup(ns) : NULL;
    h->ctx = dns_new(NULL);
    if(dns_init(h->ctx, 0) != 0) failed++;
    if(ns) {
      if(dns_add_serv(h->ctx, NULL) < 0) failed++;
      if(dns_add_serv(h->ctx, ns) < 0) failed++;
    }
    if(dns_open(h->ctx) < 0) failed++;
    if(failed) {
      noitL(noit_error, "dns_open failed\n");
      free(h->ns);
      free(h);
      h = NULL;
      goto bail;
    }
    dns_set_tmcbck(h->ctx, eventer_dns_utm_fn, h);
    h->e = eventer_alloc();
    h->e->mask = EVENTER_READ | EVENTER_EXCEPTION;
    h->e->closure = h;
    h->e->callback = noit_lua_dns_eventer;
    h->e->fd = dns_sock(h->ctx);
    eventer_add(h->e);
    h->refcnt = 1;
    if(!ns)
      default_ctx_handle = h;
    else
      noit_hash_store(&dns_ctx_store, h->ns, strlen(h->ns), h);
  }
 bail:
  pthread_mutex_unlock(&dns_ctx_store_lock);
  return h;
}

static void dns_ctx_release(dns_ctx_handle_t *h) {
  if(h->ns == NULL) {
    /* Special case for the default */
    noit_atomic_dec32(&h->refcnt);
    return;
  }
  pthread_mutex_lock(&dns_ctx_store_lock);
  if(noit_atomic_dec32(&h->refcnt) == 0) {
    /* I was the last one */
    assert(noit_hash_delete(&dns_ctx_store, h->ns, strlen(h->ns),
                            NULL, dns_ctx_handle_free));
  }
  pthread_mutex_unlock(&dns_ctx_store_lock);
}

void lookup_ctx_release(dns_lookup_ctx_t *v) {
  if(!v) return;
  if(v->error) free(v->error);
  v->error = NULL;
  if(noit_atomic_dec32(&v->refcnt) == 0) {
    dns_ctx_release(v->h);
    free(v);
  }
}

int nl_dns_lookup(lua_State *L) {
  dns_lookup_ctx_t *dlc, **holder;
  const char *nameserver = NULL;
  noit_lua_check_info_t *ci;

  ci = get_ci(L);
  assert(ci);

  holder = (dns_lookup_ctx_t **)lua_newuserdata(L, sizeof(*holder));
  dlc = calloc(1, sizeof(*dlc));
  dlc->refcnt = 1;
  dlc->ci = ci;
  dlc->h = dns_ctx_alloc(nameserver);
  *holder = dlc;
  luaL_getmetatable(L, "noit.dns");
  lua_setmetatable(L, -2);
  return 1;
}

static char *encode_txt(char *dst, const unsigned char *src, int len) {
  int i;
  for(i=0; i<len; i++) {
    if(src[i] >= 127 || src[i] <= 31) {
      snprintf(dst, 4, "\\%02x", src[i]);
      dst += 3;
    }
    else if(src[i] == '\\') {
      *dst++ = '\\';
      *dst++ = '\\';
    }
    else {
      *dst++ = (char)src[i];
    }
  }
  *dst = '\0';
  return dst;
}

static void dns_cb(struct dns_ctx *ctx, void *result, void *data) {
  int r = dns_status(ctx);
  dns_lookup_ctx_t *dlc = data;
  struct dns_parse p;
  struct dns_rr rr;
  unsigned nrr = 0;
  unsigned char dn[DNS_MAXDN];
  const unsigned char *pkt, *cur, *end;
  lua_State *L;

  if(!dlc->active) goto cleanup;
  if(!result) goto cleanup;

  L = dlc->ci->coro_state;

  pkt = result; end = pkt + r; cur = dns_payload(pkt);
  dns_getdn(pkt, &cur, end, dn, sizeof(dn));
  dns_initparse(&p, NULL, pkt, cur, end);
  p.dnsp_qcls = 0;
  p.dnsp_qtyp = 0;

  while((r = dns_nextrr(&p, &rr)) > 0) {
    const char *fieldname = NULL;
    char buff[DNS_MAXDN], *txt_str, *c;
    int totalsize;
    const unsigned char *pkt = p.dnsp_pkt;
    const unsigned char *end = p.dnsp_end;
    const unsigned char *dptr = rr.dnsrr_dptr;
    const unsigned char *dend = rr.dnsrr_dend;
    unsigned char *dn = rr.dnsrr_dn;
    const unsigned char *tmp;

    if (!dns_dnequal(dn, rr.dnsrr_dn)) continue;
    if ((dlc->query_ctype == DNS_C_ANY || dlc->query_ctype == rr.dnsrr_cls) &&
        (dlc->query_rtype == DNS_T_ANY || dlc->query_rtype == rr.dnsrr_typ)) {
      lua_newtable(L);
      lua_pushinteger(L, rr.dnsrr_ttl);
      lua_setfield(L, -2, "ttl");

      switch(rr.dnsrr_typ) {
        case DNS_T_A:
          if(rr.dnsrr_dsz == 4) {
            snprintf(buff, sizeof(buff), "%d.%d.%d.%d",
                     dptr[0], dptr[1], dptr[2], dptr[3]);
            lua_pushstring(L, buff);
            lua_setfield(L, -2, "a");
          }
          break;

        case DNS_T_AAAA:
          if(rr.dnsrr_dsz == 16) {
            inet_ntop(AF_INET6, dptr, buff, 16);
            lua_pushstring(L, buff);
            lua_setfield(L, -2, "aaaa");
          }
          break;

        case DNS_T_TXT:
          totalsize = 0;
          for(tmp = dptr; tmp < dend; totalsize += *tmp, tmp += *tmp + 1)
            if(tmp + *tmp + 1 > dend) break;
          /* worst case: every character escaped + '\0' */
          txt_str = alloca(totalsize * 3 + 1);
          if(!txt_str) break;
          c = txt_str;
          for(tmp = dptr; tmp < dend; tmp += *tmp + 1)
            c = encode_txt(c, tmp+1, *tmp);
          lua_pushstring(L, txt_str);
          lua_setfield(L, -2, "txt");
          break;

        case DNS_T_MX:
          lua_pushinteger(L, dns_get16(dptr));
          lua_setfield(L, -2, "preference");
          tmp = dptr + 2;
          if(dns_getdn(pkt, &tmp, end, dn, DNS_MAXDN) <= 0 || tmp != dend)
            break;
          dns_dntop(dn, buff + strlen(buff), sizeof(buff) - strlen(buff));
          lua_pushstring(L, buff);
          lua_setfield(L, -2, "mx");
          break;

        case DNS_T_CNAME: if(!fieldname) fieldname = "cname";
        case DNS_T_PTR: if(!fieldname) fieldname = "ptr";
        case DNS_T_NS: if(!fieldname) fieldname = "ns";
        case DNS_T_MB: if(!fieldname) fieldname = "mb";
        case DNS_T_MD: if(!fieldname) fieldname = "md";
        case DNS_T_MF: if(!fieldname) fieldname = "mf";
        case DNS_T_MG: if(!fieldname) fieldname = "mg";
        case DNS_T_MR: if(!fieldname) fieldname = "mr";
         if(dns_getdn(pkt, &dptr, end, dn, DNS_MAXDN) <= 0) break;
         dns_dntop(dn, buff, sizeof(buff));
         lua_pushstring(L, buff);
         lua_setfield(L, -2, fieldname);
         break;

        default:
          break;
      }
      ++nrr;
    }
    else if (rr.dnsrr_typ == DNS_T_CNAME && !nrr) {
      if (dns_getdn(pkt, &rr.dnsrr_dptr, end,
                    p.dnsp_dnbuf, sizeof(p.dnsp_dnbuf)) <= 0 ||
          rr.dnsrr_dptr != rr.dnsrr_dend) {
        break;
      }
    }
  }

 cleanup:
  if(result) free(result);
  if(dlc->active) noit_lua_resume(dlc->ci, nrr);
  lookup_ctx_release(dlc);
}

static int noit_lua_dns_lookup(lua_State *L) {
  dns_lookup_ctx_t *dlc, **holder;
  const char *c, *query = "", *ctype = "IN", *rtype = "A";
  char *ctype_up, *rtype_up, *d;
  void *vnv_pair;
  noit_lua_check_info_t *ci;

  ci = get_ci(L);
  assert(ci);

  holder = (dns_lookup_ctx_t **)lua_touserdata(L, lua_upvalueindex(1));
  if(holder != lua_touserdata(L,1))
    luaL_error(L, "Must be called as method\n");
  dlc = *holder;

  if(lua_gettop(L) > 1) query = lua_tostring(L, 2);
  if(lua_gettop(L) > 2) rtype = lua_tostring(L, 3);
  if(lua_gettop(L) > 3) ctype = lua_tostring(L, 4);

  ctype_up = alloca(strlen(ctype)+1);
  for(d = ctype_up, c = ctype; *c; d++, c++) *d = toupper(*c);
  *d = '\0';
  rtype_up = alloca(strlen(rtype)+1);
  for(d = rtype_up, c = rtype; *c; d++, c++) *d = toupper(*c);
  *d = '\0';

  if(!noit_hash_retrieve(&dns_ctypes, ctype_up, strlen(ctype_up), &vnv_pair))
    dlc->error = strdup("bad class");
  else
    dlc->query_ctype = ((struct dns_nameval *)vnv_pair)->val;

  if(!noit_hash_retrieve(&dns_rtypes, rtype_up, strlen(rtype_up), &vnv_pair)) 
    dlc->error = strdup("bad rr type");
  else
    dlc->query_rtype = ((struct dns_nameval *)vnv_pair)->val;

  dlc->active = 1;
  noit_atomic_inc32(&dlc->refcnt);
  if(!dlc->error) {
    int abs;
    if(!dns_ptodn(query, strlen(query), dlc->dn, sizeof(dlc->dn), &abs) ||
       !dns_submit_dn(dlc->h->ctx, dlc->dn, dlc->query_ctype, dlc->query_rtype,
                      abs | DNS_NOSRCH, NULL, dns_cb, dlc)) {
      dlc->error = strdup("submission error");
      noit_atomic_dec32(&dlc->refcnt);
    }
    else {
      struct timeval now;
      gettimeofday(&now, NULL);
      dns_timeouts(dlc->h->ctx, -1, now.tv_sec);
    }
  }
  if(dlc->error) {
    dlc->active = 0;
    luaL_error(L, "dns: %s\n", dlc->error);
  }
  return noit_lua_yield(ci, 0);
}

int noit_lua_dns_gc(lua_State *L) {
  dns_lookup_ctx_t **holder;
  holder = (dns_lookup_ctx_t **)lua_touserdata(L,1);
  (*holder)->active = 0;
  lookup_ctx_release(*holder);
  return 0;
}

int noit_lua_dns_index_func(lua_State *L) {
  int n;
  const char *k;
  dns_lookup_ctx_t **udata, *obj;

  n = lua_gettop(L);
  assert(n == 2);
  if(!luaL_checkudata(L, 1, "noit.dns"))
    luaL_error(L, "metatable error, arg1 is not a noit.dns");
  udata = lua_touserdata(L, 1);
  obj = *udata;
  if(!lua_isstring(L, 2))
    luaL_error(L, "metatable error, arg2 is not a string");
  k = lua_tostring(L, 2);
  if(!strcmp(k, "lookup")) {
    lua_pushlightuserdata(L, udata);
    lua_pushcclosure(L, noit_lua_dns_lookup, 1);
    return 1;
  }
  luaL_error(L, "noit.dns no such element: %s", k);
  return 0;
}

void noit_lua_init_dns() {
  int i;
  const struct dns_nameval *nv;
  struct dns_ctx *pctx;

  /* HASH the rr types */
  for(i=0, nv = dns_type_index(i); nv->name; nv = dns_type_index(++i))
    noit_hash_store(&dns_rtypes,
                    nv->name, strlen(nv->name),
                    (void *)nv);
  /* HASH the class types */
  for(i=0, nv = dns_class_index(i); nv->name; nv = dns_class_index(++i))
    noit_hash_store(&dns_ctypes,
                    nv->name, strlen(nv->name),
                    (void *)nv);

  eventer_name_callback("lua/dns_eventer", noit_lua_dns_eventer);
  eventer_name_callback("lua/dns_timeouts", noit_lua_dns_timeouts);

  if (dns_init(NULL, 0) < 0 || (pctx = dns_new(NULL)) == NULL) {
    noitL(noit_error, "Unable to initialize dns subsystem\n");
  }
  else
    dns_free(pctx);
}
