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

#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <assert.h>
#include <arpa/inet.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "utils/noit_log.h"
#include "utils/noit_atomic.h"
#include "udns/udns.h"

static void eventer_dns_utm_fn(struct dns_ctx *, int, void *);
static int dns_eventer_callback(eventer_t, int, void *, struct timeval *);

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;

static noit_hash_table dns_rtypes = NOIT_HASH_EMPTY;
static noit_hash_table dns_ctypes = NOIT_HASH_EMPTY;

static noit_hash_table dns_ctx_store = NOIT_HASH_EMPTY;
static pthread_mutex_t dns_ctx_store_lock;
typedef struct dns_ctx_handle {
  char *ns;
  struct dns_ctx *ctx;
  noit_atomic32_t refcnt;
  eventer_t e; /* evetner handling UDP traffic */
  eventer_t timeout; /* the timeout managed by libudns */
} dns_ctx_handle_t;

static dns_ctx_handle_t *default_ctx_handle = NULL;
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
      noitL(nlerr, "dns_open failed\n");
      free(h->ns);
      free(h);
      h = NULL;
      goto bail;
    }
    dns_set_tmcbck(h->ctx, eventer_dns_utm_fn, h);
    h->e = eventer_alloc();
    h->e->mask = EVENTER_READ | EVENTER_EXCEPTION;
    h->e->closure = h;
    h->e->callback = dns_eventer_callback;
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

static noit_hash_table active_events = NOIT_HASH_EMPTY;
static pthread_mutex_t active_events_lock;

typedef struct dns_check_info {
  stats_t current;
  int timed_out;
  noit_module_t *self;
  noit_check_t *check;
  eventer_t timeout_event;
  dns_ctx_handle_t *h;
  char *error;
  int nrr;

  /* These make up the query itself */
  unsigned char dn[DNS_MAXDN];
  enum dns_class query_ctype;
  enum dns_type query_rtype;
} dns_check_info_t;

static int __isactive_ci(struct dns_check_info *ci) {
  void *u;
  int exists = 0;
  pthread_mutex_lock(&active_events_lock);
  if(noit_hash_retrieve(&active_events, (void *)&ci, sizeof(ci), &u))
    exists = 1;
  pthread_mutex_unlock(&active_events_lock);
  return exists;
}
static void __activate_ci(struct dns_check_info *ci) {
  struct dns_check_info **holder;
  holder = calloc(1, sizeof(*holder));
  *holder = ci;
  pthread_mutex_lock(&active_events_lock);
  assert(noit_hash_store(&active_events, (void *)holder, sizeof(*holder), ci));
  pthread_mutex_unlock(&active_events_lock);
}
static void __deactivate_ci(struct dns_check_info *ci) {
  pthread_mutex_lock(&active_events_lock);
  assert(noit_hash_delete(&active_events, (void *)&ci, sizeof(ci), free, NULL));
  pthread_mutex_unlock(&active_events_lock);
}

static void dns_check_log_results(struct dns_check_info *ci) {
  struct timeval duration;

  gettimeofday(&ci->current.whence, NULL);
  sub_timeval(ci->current.whence, ci->check->last_fire_time, &duration);
  ci->current.duration = duration.tv_sec * 1000 + duration.tv_usec / 1000;

  ci->current.state = (ci->error || ci->nrr == 0) ? NP_BAD : NP_GOOD;
  ci->current.available = ci->timed_out ? NP_UNAVAILABLE : NP_AVAILABLE;
  if(ci->error) {
    ci->current.status = strdup(ci->error);
  }
  else if(!ci->current.status) {
    char buff[48];
    snprintf(buff, sizeof(buff), "%d %s",
             ci->nrr, ci->nrr == 1 ? "record" : "records");
    ci->current.status = strdup(buff);
  }

  noit_check_set_stats(ci->self, ci->check, &ci->current);
  if(ci->error) free(ci->error);
  if(ci->current.status) free(ci->current.status);
  ci->error = NULL;
  memset(&ci->current, 0, sizeof(ci->current));
}

static int dns_interpolate_inaddr_arpa(char *buff, int len, const char *ip) {
  const char *b, *e;
  char *o = buff;
  int il;
  /* This function takes a dot delimited string as input and
   * reverses the parts split on dot.
   */
  il = strlen(ip);
  if(len <= il) {
    /* not enough room for ip and '\0' */
    if(len > 0) buff[0] = '\0';
    return 0;
  }
  e = ip + il;
  b = e - 1;
  while(b >= ip) {
    const char *term;
    while(b >= ip && *b != '.') b--;  /* Rewind to previous part */
    term = b + 1; /* term is one ahead, we went past it */
    if(term != e) memcpy(o, term, e - term); /* no sense in copying nothing */
    o += e - term; /* advance the term length */
    e = b;
    b = e - 1;
    if(e >= ip) *o++ = '.'; /* we must be at . */
  }
  *o = '\0';
  assert((o - buff) == il);
  return o - buff;
}

static int dns_module_init(noit_module_t *self) {
  const struct dns_nameval *nv;
  struct dns_ctx *pctx;
  int i;
  pthread_mutex_init(&dns_ctx_store_lock, NULL);
  pthread_mutex_init(&active_events_lock, NULL);
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

  noit_check_interpolate_register_oper_fn("inaddrarpa",
                                          dns_interpolate_inaddr_arpa);

  if (dns_init(NULL, 0) < 0 || (pctx = dns_new(NULL)) == NULL) {
    noitL(nlerr, "Unable to initialize dns subsystem\n");
    return -1;
  }
  dns_free(pctx);
  if(dns_ctx_alloc(NULL) == NULL) {
    noitL(nlerr, "Error setting up default dns resolver context.\n");
    return -1;
  }
  return 0;
}

static void dns_check_cleanup(noit_module_t *self, noit_check_t *check) {
}

static int dns_eventer_callback(eventer_t e, int mask, void *closure,
                                struct timeval *now) {
  dns_ctx_handle_t *h = closure;
  dns_ioevent(h->ctx, now->tv_sec);
  return EVENTER_READ | EVENTER_EXCEPTION;
}

static int dns_check_timeout(eventer_t e, int mask, void *closure,
                             struct timeval *now) {
  struct dns_check_info *ci;
  ci = closure;
  ci->timeout_event = NULL;
  ci->check->flags &= ~NP_RUNNING;
  dns_check_log_results(ci);
  __deactivate_ci(ci);
  return 0;
}

static int dns_invoke_timeouts(eventer_t e, int mask, void *closure,
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
      newe->callback = dns_invoke_timeouts;
      newe->closure = h;
      gettimeofday(&newe->whence, NULL);
      newe->whence.tv_sec += timeout;
    }
  }
  if(e) eventer_free(e);
  if(newe) eventer_add(newe);
  h->timeout = newe;
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

static void decode_rr(struct dns_check_info *ci, struct dns_parse *p,
                      struct dns_rr *rr, char **output) {
  char buff[DNS_MAXDN], *txt_str, *c;
  u_int32_t ttl, vu;
  int32_t vs;
  int totalsize;
  const unsigned char *pkt = p->dnsp_pkt;
  const unsigned char *end = p->dnsp_end;
  const unsigned char *dptr = rr->dnsrr_dptr;
  const unsigned char *dend = rr->dnsrr_dend;
  unsigned char *dn = rr->dnsrr_dn;
  const unsigned char *tmp;

  /* Not interested unless it is the answer to my exact question */
  if (!dns_dnequal(ci->dn, dn)) return;

  if (!p->dnsp_rrl && !rr->dnsrr_dn[0] && rr->dnsrr_typ == DNS_T_OPT) {
    /* We don't handle EDNS0 OPT records */
    goto decode_err;
  }
  noitL(nldeb, "%s. %u %s %s\n", dns_dntosp(dn), rr->dnsrr_ttl,
        dns_classname(rr->dnsrr_cls),
        dns_typename(rr->dnsrr_typ));

  ttl = rr->dnsrr_ttl;
  noit_stats_set_metric(&ci->current, "ttl", METRIC_UINT32, &ttl);

  switch(rr->dnsrr_typ) {
   case DNS_T_A:
    if (rr->dnsrr_dsz != 4) goto decode_err;
    snprintf(buff, sizeof(buff), "%d.%d.%d.%d",
             dptr[0], dptr[1], dptr[2], dptr[3]);
    break;

   case DNS_T_AAAA:
    if (rr->dnsrr_dsz != 16) goto decode_err;
    inet_ntop(AF_INET6, dptr, buff, 16);
    break;

   case DNS_T_TXT:
    totalsize = 0;
    for(tmp = dptr; tmp < dend; totalsize += *tmp, tmp += *tmp + 1)
      if(tmp + *tmp + 1 > dend) goto decode_err;
    /* worst case: every character escaped + '\0' */
    txt_str = alloca(totalsize * 3 + 1);
    if(!txt_str) goto decode_err;
    c = txt_str;
    for(tmp = dptr; tmp < dend; tmp += *tmp + 1)
      c = encode_txt(c, tmp+1, *tmp);
    break;

   case DNS_T_MX:
    snprintf(buff, sizeof(buff), "%d ", dns_get16(dptr));
    tmp = dptr + 2;
    if(dns_getdn(pkt, &tmp, end, dn, DNS_MAXDN) <= 0 || tmp != dend)
      goto decode_err;
    dns_dntop(dn, buff + strlen(buff), sizeof(buff) - strlen(buff));
    break;

   case DNS_T_SOA:
     if(dns_getdn(pkt, &dptr, end, dn, DNS_MAXDN) <= 0) goto decode_err;
     dns_dntop(dn, buff, sizeof(buff));
     noit_stats_set_metric(&ci->current, "name-server", METRIC_STRING, buff);
     if(dns_getdn(pkt, &dptr, end, dn, DNS_MAXDN) <= 0) goto decode_err;
     dns_dntop(dn, buff, sizeof(buff));
     noit_stats_set_metric(&ci->current, "email-addr", METRIC_STRING, buff);
     if(dptr + 5 * sizeof(u_int32_t) != dend) goto decode_err;
     vu = dns_get32(dptr); dptr += sizeof(u_int32_t);
     noit_stats_set_metric(&ci->current, "serial", METRIC_UINT32, &vu);
     /* the serial is what we elect to store as the "answer" as text...
      * because it rarely changes and that seems the most interesting thing
      * to track change-log-style.
      */
     snprintf(buff, sizeof(buff), "%u", vu);
     vs = dns_get32(dptr); dptr += sizeof(int32_t);
     noit_stats_set_metric(&ci->current, "refresh", METRIC_UINT32, &vs);
     vs = dns_get32(dptr); dptr += sizeof(int32_t);
     noit_stats_set_metric(&ci->current, "retry", METRIC_UINT32, &vs);
     vs = dns_get32(dptr); dptr += sizeof(int32_t);
     noit_stats_set_metric(&ci->current, "expiry", METRIC_UINT32, &vs);
     vs = dns_get32(dptr); dptr += sizeof(int32_t);
     noit_stats_set_metric(&ci->current, "minimum", METRIC_UINT32, &vs);
     break;

   case DNS_T_CNAME:
   case DNS_T_PTR:
   case DNS_T_NS:
   case DNS_T_MB:
   case DNS_T_MD:
   case DNS_T_MF:
   case DNS_T_MG:
   case DNS_T_MR:
    if(dns_getdn(pkt, &dptr, end, dn, DNS_MAXDN) <= 0) goto decode_err;
    dns_dntop(dn, buff, sizeof(buff));
    break;

   default:
    break;
  }
  if(*output) {
    int newlen = strlen(*output) + strlen(", ") + strlen(buff) + 1;
    char *newstr;
    newstr = malloc(newlen);
    snprintf(newstr, newlen, "%s, %s", *output, buff);
    free(*output);
    *output = newstr;
  }
  else
    *output = strdup(buff);
  ci->nrr++;
  return;

 decode_err:
  ci->error = strdup("RR decode error");
  return;
}

static void dns_cb(struct dns_ctx *ctx, void *result, void *data) {
  int r = dns_status(ctx);
  struct dns_check_info *ci = data;
  struct dns_parse p;
  struct dns_rr rr;
  unsigned nrr;
  unsigned char dn[DNS_MAXDN];
  const unsigned char *pkt, *cur, *end;
  char *result_str = NULL;

  /* If out ci isn't active, we must have timed out already */
  if(!__isactive_ci(ci)) {
    if(result) free(result);
    return;
  }

  ci->timed_out = 0;
  /* If we don't have a result, explode */
  if (!result) {
    ci->error = strdup(dns_strerror(r));
    goto cleanup;
  }

  /* Process the packet */
  pkt = result; end = pkt + r; cur = dns_payload(pkt);
  dns_getdn(pkt, &cur, end, dn, sizeof(dn));
  dns_initparse(&p, NULL, pkt, cur, end);
  p.dnsp_qcls = p.dnsp_qtyp = 0;
  nrr = 0;

  while((r = dns_nextrr(&p, &rr)) > 0) {
    if (!dns_dnequal(dn, rr.dnsrr_dn)) continue;
    if ((ci->query_ctype == DNS_C_ANY || ci->query_ctype == rr.dnsrr_cls) &&
        (ci->query_rtype == DNS_T_ANY || ci->query_rtype == rr.dnsrr_typ))
      ++nrr;
    else if (rr.dnsrr_typ == DNS_T_CNAME && !nrr) {
      if (dns_getdn(pkt, &rr.dnsrr_dptr, end,
                    p.dnsp_dnbuf, sizeof(p.dnsp_dnbuf)) <= 0 ||
          rr.dnsrr_dptr != rr.dnsrr_dend) {
        ci->error = strdup("protocol error");
        break;
      }
      else {
        noitL(nldeb, "%s.\n", dns_dntosp(dn));
        noitL(nldeb, " CNAME %s.\n", dns_dntosp(p.dnsp_dnbuf));
        dns_dntodn(p.dnsp_dnbuf, dn, sizeof(dn));
        noitL(nldeb, " ---> '%s'\n", dn);
      }
    }
  }
  if (!r && !nrr) {
    ci->error = strdup("no data");
  }

  dns_rewind(&p, NULL);
  p.dnsp_qtyp = ci->query_rtype == DNS_T_ANY ? 0 : ci->query_rtype;
  p.dnsp_qcls = ci->query_ctype == DNS_C_ANY ? 0 : ci->query_ctype;
  while(dns_nextrr(&p, &rr))
    decode_rr(ci, &p, &rr, &result_str);
  noit_stats_set_metric(&ci->current, "answer", METRIC_STRING, result_str);

 cleanup:
  if(result) free(result);
  if(ci->timeout_event) {
    eventer_t e = eventer_remove(ci->timeout_event);
    ci->timeout_event = NULL;
    if(e) eventer_free(e);
  }
  ci->check->flags &= ~NP_RUNNING;
  dns_check_log_results(ci);
  __deactivate_ci(ci);
}

static int dns_check_send(noit_module_t *self, noit_check_t *check) {
  void *vnv_pair = NULL;
  struct dns_nameval *nv_pair;
  eventer_t newe;
  struct timeval p_int, now;
  struct dns_check_info *ci = check->closure;
  const char *config_val;
  const char *rtype = NULL;
  const char *nameserver = NULL;
  const char *ctype = "IN";
  const char *query = NULL;
  char interpolated_nameserver[1024];
  char interpolated_query[1024];
  noit_hash_table check_attrs_hash = NOIT_HASH_EMPTY;

  gettimeofday(&now, NULL);
  memcpy(&check->last_fire_time, &now, sizeof(now));
  ci->current.state = NP_BAD;
  ci->current.available = NP_UNAVAILABLE;
  ci->timed_out = 1;
  ci->nrr = 0;

  if(!strcmp(check->name, "in-addr.arpa")) {
    /* in-addr.arpa defaults:
     *   nameserver to NULL
     *   rtype to PTR
     *   query to %[:inaddrarpa:target].in-addr.arpa
     */
    nameserver = NULL;
    rtype = "PTR";
    query = "%[:inaddrarpa:target].in-addr.arpa";
  }
  else {
    nameserver = "%[target]";
    rtype = "A";
    query = "%[name]";
  }
#define CONFIG_OVERRIDE(a) \
  if(noit_hash_retr_str(check->config, #a, strlen(#a), \
                        &config_val) && \
     strlen(config_val) > 0) \
    a = config_val
  CONFIG_OVERRIDE(ctype);
  CONFIG_OVERRIDE(nameserver);
  CONFIG_OVERRIDE(rtype);
  CONFIG_OVERRIDE(query);

  noit_check_make_attrs(check, &check_attrs_hash);
#define CA_STORE(a,b) noit_hash_store(&check_attrs_hash, a, strlen(a), b)
  CA_STORE("target", check->target);
  CA_STORE("name", check->name);
  CA_STORE("module", check->module);

  if(nameserver) {
    noit_check_interpolate(interpolated_nameserver,
                           sizeof(interpolated_nameserver),
                           nameserver,
                           &check_attrs_hash, check->config);
    nameserver = interpolated_nameserver;
  }
  if(query) {
    noit_check_interpolate(interpolated_query,
                           sizeof(interpolated_query),
                           query,
                           &check_attrs_hash, check->config);
    query = interpolated_query;
  }
  noit_hash_destroy(&check_attrs_hash, NULL, NULL);

  check->flags |= NP_RUNNING;
  noitL(nldeb, "dns_check_send(%p,%s,%s,%s,%s,%s)\n",
        self, check->target, nameserver ? nameserver : "default",
        query ? query : "null", ctype, rtype);

  __activate_ci(ci);
  /* If this ci has a handle and it isn't the one we need,
   * we should release it
   */
  if(ci->h &&
     ((ci->h->ns == NULL && nameserver != NULL) ||
      (ci->h->ns != NULL && nameserver == NULL) ||
      (ci->h->ns && strcmp(ci->h->ns, nameserver)))) {
    dns_ctx_release(ci->h);
    ci->h = NULL;
  }
  /* use the cached one, unless we don't have one */
  if(!ci->h) ci->h = dns_ctx_alloc(nameserver);

  /* Lookup out class */
  if(!noit_hash_retrieve(&dns_ctypes, ctype, strlen(ctype),
                         &vnv_pair)) {
    ci->error = strdup("bad class");
  }
  else {
    nv_pair = (struct dns_nameval *)vnv_pair;
    ci->query_ctype = nv_pair->val;
  }
  /* Lookup out rr type */
  if(!noit_hash_retrieve(&dns_rtypes, rtype, strlen(rtype),
                         &vnv_pair)) {
    ci->error = strdup("bad rr type");
  }
  else {
    nv_pair = (struct dns_nameval *)vnv_pair;
    ci->query_rtype = nv_pair->val;
  }

  if(!ci->error) {
    /* Submit the query */
    int abs;
    if(!dns_ptodn(query, strlen(query), ci->dn, sizeof(ci->dn), &abs) ||
       !dns_submit_dn(ci->h->ctx, ci->dn, ci->query_ctype, ci->query_rtype,
                      abs | DNS_NOSRCH, NULL, dns_cb, ci)) {
      ci->error = strdup("submission error");
    }
    else {
      dns_timeouts(ci->h->ctx, -1, now.tv_sec);
    }
  }

  /* we could have completed by now... if so, we've nothing to do */

  if(!__isactive_ci(ci)) return 0;

  if(ci->error) {
    /* Errors here are easy, fail and avoid scheduling a timeout */
    ci->check->flags &= ~NP_RUNNING;
    dns_check_log_results(ci);
    __deactivate_ci(ci);
    return 0;
  }

  newe = eventer_alloc();
  newe->mask = EVENTER_TIMER;
  gettimeofday(&now, NULL);
  p_int.tv_sec = check->timeout / 1000;
  p_int.tv_usec = (check->timeout % 1000) * 1000;
  add_timeval(now, p_int, &newe->whence);
  newe->closure = ci;
  newe->callback = dns_check_timeout;
  ci->timeout_event = newe;
  eventer_add(newe);

  return 0;
}

static int dns_initiate_check(noit_module_t *self, noit_check_t *check,
                              int once, noit_check_t *cause) {
  struct dns_check_info *ci;
  if(!check->closure)
    check->closure = calloc(1, sizeof(struct dns_check_info));
  ci = check->closure;
  ci->check = check;
  ci->self = self;
  INITIATE_CHECK(dns_check_send, self, check);
  return 0;
}

static int dns_config(noit_module_t *self, noit_hash_table *options) {
  return 0;
}

static int dns_onload(noit_image_t *self) {
  nlerr = noit_log_stream_find("error/dns");
  nldeb = noit_log_stream_find("debug/dns");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;
  eventer_name_callback("dns/dns_eventer_callback", dns_eventer_callback);
  eventer_name_callback("dns/dns_check_timeout", dns_check_timeout);
  eventer_name_callback("dns/dns_invoke_timeouts", dns_invoke_timeouts);
  return 0;
}

#include "dns.xmlh"
noit_module_t dns = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "dns",
    "DNS RR checker",
    dns_xml_description,
    dns_onload
  },
  dns_config,
  dns_module_init,
  dns_initiate_check,
  dns_check_cleanup
};

