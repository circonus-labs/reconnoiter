/*
 * Copyright (c) 2011, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2015-2017, Circonus, Inc. All rights reserved.
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

#include "noit_config.h"
#include <mtev_defines.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <udns.h>

#include <eventer/eventer.h>
#include <mtev_log.h>
#include <mtev_skiplist.h>
#include <mtev_hash.h>
#include <mtev_hooks.h>
#include <mtev_conf.h>
#include <mtev_console.h>
#include "noit_mtev_bridge.h"

#define MAX_RR 256
#define DEFAULT_FAILED_TTL 60
#define DEFAULT_PURGE_AGE  1200 /* 20 minutes */

static struct dns_ctx *dns_ctx;
static pthread_mutex_t nc_dns_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static mtev_skiplist *nc_dns_cache;
static eventer_t dns_cache_timeout = NULL;
static mtev_hash_table etc_hosts_cache;
static int dns_search_flag = DNS_NOSRCH;

MTEV_HOOK_IMPL(noit_resolver_cache_store,
               (const char *key, const void *data, int len),
               void *, closure,
               (void *closure, const char *key, const void *data, int len),
               (closure,key,data,len))

MTEV_HOOK_IMPL(noit_resolver_cache_load,
               (char **key, void **data, int *len),
               void *, closure,
               (void *closure, char **key, void **data, int *len),
               (closure,key,data,len))

#define DCLOCK() pthread_mutex_lock(&nc_dns_cache_lock)
#define DCUNLOCK() pthread_mutex_unlock(&nc_dns_cache_lock)

#define dns_cache_SHARED \
  time_t last_needed; \
  time_t last_updated; \
  time_t ttl; \
  int ip4_cnt; \
  int ip6_cnt; \
  unsigned char dn[DNS_MAXDN]

typedef struct {
  dns_cache_SHARED;
  char *target;
  mtev_boolean lookup_inflight_v4;
  mtev_boolean lookup_inflight_v6;
  struct in_addr *ip4;
  struct in6_addr *ip6;
} dns_cache_node;

typedef struct {
  dns_cache_SHARED;
} dns_cache_serial_t;

static int dns_cache_node_serialize(void *b, int blen, dns_cache_node *n) {
  int needed_len, ip4len, ip6len;
  ip4len = n->ip4_cnt * sizeof(struct in_addr);
  ip6len = n->ip6_cnt * sizeof(struct in6_addr);
  needed_len = sizeof(dns_cache_serial_t) + ip4len + ip6len;

  if(needed_len > blen) return -1;
  memcpy(b, n, sizeof(dns_cache_serial_t));
  memcpy(b + sizeof(dns_cache_serial_t), n->ip4, ip4len);
  memcpy(b + sizeof(dns_cache_serial_t) + ip4len, n->ip6, ip6len);
  return needed_len;
}

static int dns_cache_node_deserialize(dns_cache_node *n, void *b, int blen) {
  int ip4len, ip6len;
  if(n->ip4) free(n->ip4);
  n->ip4 = NULL;
  if(n->ip6) free(n->ip6);
  n->ip6 = NULL;
  if(blen < sizeof(dns_cache_serial_t)) return -1;
  memcpy(n, b, sizeof(dns_cache_serial_t));
  ip4len = n->ip4_cnt * sizeof(struct in_addr);
  ip6len = n->ip6_cnt * sizeof(struct in6_addr);
  if(blen != (sizeof(dns_cache_serial_t) + ip4len + ip6len)) {
    n->ip4_cnt = 0;
    n->ip6_cnt = 0;
    return -1;
  }
  if(ip4len) {
    n->ip4 = malloc(ip4len);
    memcpy(n->ip4, b + sizeof(dns_cache_serial_t), ip4len);
  }
  if(ip6len) {
    n->ip6 = malloc(ip6len);
    memcpy(n->ip6, b + sizeof(dns_cache_serial_t) + ip4len, ip6len);
  }
  return sizeof(dns_cache_serial_t) + ip4len + ip6len;
}

typedef struct {
  char *target;
  struct in_addr ip4;
  struct in6_addr ip6;
  int has_ip4:1;
  int has_ip6:1;
} static_host_node;

void dns_cache_node_free(void *vn) {
  dns_cache_node *n = vn;
  if(!n) return;
  if(n->target) free(n->target);
  if(n->ip4) free(n->ip4);
  if(n->ip6) free(n->ip6);
  free(n);
}

static int name_lookup(const void *av, const void *bv) {
  const dns_cache_node *a = av;
  const dns_cache_node *b = bv;
  return strcmp(a->target, b->target);
}
static int name_lookup_k(const void *akv, const void *bv) {
  const char *ak = akv;
  const dns_cache_node *b = bv;
  return strcmp(ak, b->target);
}
static int refresh_idx(const void *av, const void *bv) {
  const dns_cache_node *a = av;
  const dns_cache_node *b = bv;
  if((a->last_updated + a->ttl) < (b->last_updated + b->ttl)) return -1;
  return 1;
}
static int refresh_idx_k(const void *akv, const void *bv) {
  time_t f = (time_t) akv;
  const dns_cache_node *b = bv;
  if(f < (b->last_updated + b->ttl)) return -1;
  return 1;
}

static void
noit_check_resolver_remind_internal(const char *target,
                                    mtev_boolean needs_lock) {
  dns_cache_node *n;
  if(!target) return;
  if(needs_lock) DCLOCK();
  n = mtev_skiplist_find(nc_dns_cache, target, NULL);
  if(n != NULL) {
    n->last_needed = time(NULL);
    if(needs_lock) DCUNLOCK();
    return; 
  }
  n = calloc(1, sizeof(*n));
  n->target = strdup(target);
  n->last_needed = time(NULL);
  mtev_skiplist_insert(nc_dns_cache, n);
  if(needs_lock) DCUNLOCK();
}

void noit_check_resolver_remind(const char *target) {
  noit_check_resolver_remind_internal(target, mtev_true);
}

int noit_check_resolver_fetch(const char *target, char *buff, int len,
                              uint8_t prefer_family) {
  int i, rv;
  uint8_t progression[2];
  dns_cache_node *n;
  void *vnode;

  buff[0] = '\0';
  if(!target) return -1;
  progression[0] = prefer_family;
  progression[1] = (prefer_family == AF_INET) ? AF_INET6 : AF_INET;

  if(mtev_hash_retrieve(&etc_hosts_cache, target, strlen(target), &vnode)) {
    static_host_node *node = vnode;
    for(i=0; i<2; i++) {
      switch(progression[i]) {
        case AF_INET:
          if(node->has_ip4) {
            inet_ntop(AF_INET, &node->ip4, buff, len);
            return 1;
          }
          break;
        case AF_INET6:
          if(node->has_ip6) {
            inet_ntop(AF_INET6, &node->ip6, buff, len);
            return 1;
          }
          break;
      }
    }
  }

  rv = -1;
  DCLOCK();
  n = mtev_skiplist_find(nc_dns_cache, target, NULL);
  if(n != NULL) {
    if(n->last_updated == 0) goto leave; /* not resolved yet */
    rv = n->ip4_cnt + n->ip6_cnt;
    for(i=0; i<2; i++) {
      switch(progression[i]) {
        case AF_INET:
          if(n->ip4_cnt > 0) {
            inet_ntop(AF_INET, &n->ip4[0], buff, len);
            goto leave;
          }
          break;
        case AF_INET6:
          if(n->ip6_cnt > 0) {
            inet_ntop(AF_INET6, &n->ip6[0], buff, len);
            goto leave;
          }
          break;
      }
    }
  }
 leave:
  DCUNLOCK();
  return rv;
}

/* You are assumed to be holding the lock when in these blanking functions */
static void blank_update_v4(dns_cache_node *n) {
  if(n->ip4) free(n->ip4);
  n->ip4 = NULL;
  n->ip4_cnt = 0;
  mtev_skiplist_remove(nc_dns_cache, n->target, NULL);
  n->last_updated = time(NULL);
  if (n->lookup_inflight_v6) {
    n->ttl = DEFAULT_FAILED_TTL;
  }
  n->lookup_inflight_v4 = mtev_false;
  mtev_skiplist_insert(nc_dns_cache, n);
}
static void blank_update_v6(dns_cache_node *n) {
  if(n->ip6) free(n->ip6);
  n->ip6 = NULL;
  n->ip6_cnt = 0;
  mtev_skiplist_remove(nc_dns_cache, n->target, NULL);
  n->last_updated = time(NULL);
  if (n->lookup_inflight_v4) {
    n->ttl = DEFAULT_FAILED_TTL;
  }
  n->lookup_inflight_v6 = mtev_false;
  mtev_skiplist_insert(nc_dns_cache, n);
}
static void blank_update(dns_cache_node *n) {
  blank_update_v4(n);
  blank_update_v6(n);
}

static int dns_cache_callback(eventer_t e, int mask, void *closure,
                              struct timeval *now) {
  struct dns_ctx *ctx = closure;
  dns_ioevent(ctx, now->tv_sec);
  return EVENTER_READ | EVENTER_EXCEPTION;
}

static int dns_invoke_timeouts(eventer_t e, int mask, void *closure,
                               struct timeval *now) {
  struct dns_ctx *ctx = closure;
  dns_timeouts(ctx, 0, now->tv_sec);
  return 0;
}

static void dns_cache_utm_fn(struct dns_ctx *ctx, int timeout, void *data) {
  eventer_t e = NULL, newe = NULL;
  if(ctx == NULL) e = eventer_remove(dns_cache_timeout);
  else {
    if(timeout < 0) e = eventer_remove(dns_cache_timeout);
    else {
      newe = eventer_in_s_us(dns_invoke_timeouts, dns_ctx, timeout, 0);
    }
  }
  if(e) eventer_free(e);
  if(newe) eventer_add(newe);
  dns_cache_timeout = newe;
}

static void dns_cache_resolve(struct dns_ctx *ctx, void *result, void *data,
                              enum dns_type rtype) {
  int ttl, acnt, r = dns_status(ctx), idnlen;
  dns_cache_node *n = data;
  unsigned char idn[DNS_MAXDN], dn[DNS_MAXDN];
  struct dns_parse p;
  struct dns_rr rr;
  unsigned nrr;
  struct in_addr *answers4 = NULL;
  struct in6_addr *answers6 = NULL;
  const unsigned char *pkt, *cur, *end;

  if(!result) goto blank;

  dns_dntodn(n->dn, idn, sizeof(idn));
  idnlen = dns_dnlen(idn);

  pkt = result; end = pkt + r; cur = dns_payload(pkt);
  dns_getdn(pkt, &cur, end, dn, sizeof(dn));
  dns_initparse(&p, NULL, pkt, cur, end);
  p.dnsp_qcls = 0;
  p.dnsp_qtyp = 0;
  nrr = 0;
  ttl = -1;

  while((r = dns_nextrr(&p, &rr)) > 0) {
    int dnlen = dns_dnlen(rr.dnsrr_dn);
    /* if we aren't searching and the don't match...
     * or if we are searching and the prefixes don't match...
     */
    if ((dns_search_flag && !dns_dnequal(idn, rr.dnsrr_dn)) ||
        (dns_search_flag == 0 &&
         (idnlen > dnlen || memcmp(idn, rr.dnsrr_dn, idnlen-1)))) continue;
    if (DNS_C_IN == rr.dnsrr_cls && rtype == rr.dnsrr_typ) ++nrr;
    else if (rr.dnsrr_typ == DNS_T_CNAME && !nrr) {
      if (dns_getdn(pkt, &rr.dnsrr_dptr, end,
                    p.dnsp_dnbuf, sizeof(p.dnsp_dnbuf)) <= 0 ||
          rr.dnsrr_dptr != rr.dnsrr_dend) {
        break;
      }
      else {
        if(ttl < 0 || (int)rr.dnsrr_ttl < ttl)
          ttl = rr.dnsrr_ttl;
        dns_dntodn(p.dnsp_dnbuf, idn, sizeof(idn));
        idnlen = dns_dnlen(idn);
      }
    }
  }
  if(!r && !nrr) goto blank;

  dns_rewind(&p, NULL);
  p.dnsp_qcls = DNS_C_IN;
  p.dnsp_qtyp = rtype;
  if(rtype == DNS_T_A)
    answers4 = calloc(nrr, sizeof(*answers4));
  else if(rtype == DNS_T_AAAA)
    answers6 = calloc(nrr, sizeof(*answers6));
  acnt = 0;
  while(dns_nextrr(&p, &rr) && nrr < MAX_RR) {
    int dnlen = dns_dnlen(rr.dnsrr_dn);
    if ((dns_search_flag && !dns_dnequal(idn, rr.dnsrr_dn)) ||
        (dns_search_flag == 0 &&
         (idnlen > dnlen || memcmp(idn, rr.dnsrr_dn, idnlen-1)))) continue;
    if (p.dnsp_rrl && !rr.dnsrr_dn[0] && rr.dnsrr_typ == DNS_T_OPT) continue;
    if (rtype == rr.dnsrr_typ) {
      if(ttl < 0 || (int)rr.dnsrr_ttl < ttl)
        ttl = rr.dnsrr_ttl;
      switch(rr.dnsrr_typ) {
        case DNS_T_A:
          if(rr.dnsrr_dsz != 4) continue;
          memcpy(&answers4[acnt++], rr.dnsrr_dptr, rr.dnsrr_dsz);
          break;
        case DNS_T_AAAA:
          if(rr.dnsrr_dsz != 16) continue;
          memcpy(&answers6[acnt++], rr.dnsrr_dptr, rr.dnsrr_dsz);
          break;
        default:
          break;
      }
    }
  }

  if(ttl < 0)
    ttl = 0;
  n->ttl = ttl;
  if(rtype == DNS_T_A) {
    if(n->ip4) free(n->ip4);
    n->ip4_cnt = acnt;
    n->ip4 = answers4;
    n->lookup_inflight_v4 = mtev_false;
  }
  else if(rtype == DNS_T_AAAA) {
    if(n->ip6) free(n->ip6);
    n->ip6_cnt = acnt;
    n->ip6 = answers6;
    n->lookup_inflight_v6 = mtev_false;
  }
  else {
    if(result) free(result);
    return;
  }
  DCLOCK();
  mtev_skiplist_remove(nc_dns_cache, n->target, NULL);
  n->last_updated = time(NULL);
  mtev_skiplist_insert(nc_dns_cache, n);
  DCUNLOCK();
  mtevL(noit_debug, "Resolved %s/%s -> %d records\n", n->target,
        (rtype == DNS_T_AAAA ? "IPv6" : (rtype == DNS_T_A ? "IPv4" : "???")),
        acnt);
  if(result) free(result);
  return;

 blank:
  DCLOCK();
  if(rtype == DNS_T_A) blank_update_v4(n);
  if(rtype == DNS_T_AAAA) blank_update_v6(n);
  DCUNLOCK();
  mtevL(noit_debug, "Resolved %s/%s -> blank\n", n->target,
        (rtype == DNS_T_AAAA ? "IPv6" : (rtype == DNS_T_A ? "IPv4" : "???")));
  if(result) free(result);
  return;
}
static void dns_cache_resolve_v4(struct dns_ctx *ctx, void *result, void *data) {
  dns_cache_resolve(ctx, result, data, DNS_T_A);
}
static void dns_cache_resolve_v6(struct dns_ctx *ctx, void *result, void *data) {
  dns_cache_resolve(ctx, result, data, DNS_T_AAAA);
}

void noit_check_resolver_maintain() {
  time_t now;
  mtev_skiplist *tlist;
  mtev_skiplist_node *sn;

  now = time(NULL);
  sn = mtev_skiplist_getlist(mtev_skiplist_indexes(nc_dns_cache));
  mtevAssert(sn);
  tlist = mtev_skiplist_data(sn);
  mtevAssert(tlist);
  sn = mtev_skiplist_getlist(tlist);
  while(sn) {
    dns_cache_node *n = mtev_skiplist_data(sn);
    mtev_skiplist_next(tlist, &sn); /* move forward */
    /* remove if needed */
    if(n->last_updated + n->ttl > now) break;
    if(n->last_needed + DEFAULT_PURGE_AGE < now &&
       !(n->lookup_inflight_v4 || n->lookup_inflight_v6))
      mtev_skiplist_remove(nc_dns_cache, n->target, dns_cache_node_free);
    else {
      int abs;
      if(!dns_ptodn(n->target, strlen(n->target),
                    n->dn, sizeof(n->dn), &abs)) {
        blank_update(n);
      }
      else {
        if(!n->lookup_inflight_v4) {
          n->lookup_inflight_v4 = mtev_true;
          if(!dns_submit_dn(dns_ctx, n->dn, DNS_C_IN, DNS_T_A,
                            abs | dns_search_flag, NULL, dns_cache_resolve_v4, n))
            blank_update_v4(n);
          else
            dns_timeouts(dns_ctx, -1, now);
        }
        if(!n->lookup_inflight_v6) {
          n->lookup_inflight_v6 = mtev_true;
          if(!dns_submit_dn(dns_ctx, n->dn, DNS_C_IN, DNS_T_AAAA,
                            abs | dns_search_flag, NULL, dns_cache_resolve_v6, n))
            blank_update_v6(n);
          else
            dns_timeouts(dns_ctx, -1, now);
        }
      }
      mtevL(noit_debug, "Firing lookup for '%s'\n", n->target);
      continue;
    }
  }

  /* If we have a cache implementation */
  if(noit_resolver_cache_store_hook_exists()) {
    /* And that implementation is interested in getting a dump... */
    if(noit_resolver_cache_store_hook_invoke(NULL, NULL, 0) == MTEV_HOOK_CONTINUE) {
      mtev_skiplist_node *sn;
      /* dump it all */
      DCLOCK();
      for(sn = mtev_skiplist_getlist(nc_dns_cache); sn;
          mtev_skiplist_next(nc_dns_cache, &sn)) {
        int sbuffsize;
        char sbuff[1024];
        dns_cache_node *n = (dns_cache_node *)mtev_skiplist_data(sn);
        sbuffsize = dns_cache_node_serialize(sbuff, sizeof(sbuff), n);
        if(sbuffsize > 0)
          noit_resolver_cache_store_hook_invoke(n->target, sbuff, sbuffsize);
      }
      DCUNLOCK();
    }
  }
}

int noit_check_resolver_loop(eventer_t e, int mask, void *c,
                             struct timeval *now) {
  noit_check_resolver_maintain();
  eventer_add_in_s_us(noit_check_resolver_loop, NULL, 1, 0);
  return 0;
}

static int
nc_print_dns_cache_node(mtev_console_closure_t ncct,
                        const char *target, dns_cache_node *n) {
  nc_printf(ncct, "==== %s ====\n", target);
  if(!n) nc_printf(ncct, "NOT FOUND\n");
  else {
    int i;
    char buff[INET6_ADDRSTRLEN];
    time_t now = time(NULL);
    nc_printf(ncct, "%16s: %ds ago\n", "last needed", now - n->last_needed);
    nc_printf(ncct, "%16s: %ds ago\n", "resolved", now - n->last_updated);
    nc_printf(ncct, "%16s: %ds\n", "ttl", n->ttl);
    if(n->lookup_inflight_v4) nc_printf(ncct, "actively resolving A RRs\n");
    if(n->lookup_inflight_v6) nc_printf(ncct, "actively resolving AAAA RRs\n");
    for(i=0;i<n->ip4_cnt;i++) {
      inet_ntop(AF_INET, &n->ip4[i], buff, sizeof(buff));
      nc_printf(ncct, "%17s %s\n", i?"":"IPv4:", buff);
    }
    for(i=0;i<n->ip6_cnt;i++) {
      inet_ntop(AF_INET6, &n->ip6[i], buff, sizeof(buff));
      nc_printf(ncct, "%17s %s\n", i?"":"IPv6:", buff);
    }
  }
  return 0;
}
static int
noit_console_show_dns_cache(mtev_console_closure_t ncct,
                            int argc, char **argv,
                            mtev_console_state_t *dstate,
                            void *closure) {
  int i;

  DCLOCK();
  if(argc == 0) {
    mtev_skiplist_node *sn;
    for(sn = mtev_skiplist_getlist(nc_dns_cache); sn;
        mtev_skiplist_next(nc_dns_cache, &sn)) {
      dns_cache_node *n = (dns_cache_node *)mtev_skiplist_data(sn);
      nc_print_dns_cache_node(ncct, n->target, n);
    }
  }
  for(i=0;i<argc;i++) {
    dns_cache_node *n;
    n = mtev_skiplist_find(nc_dns_cache, argv[i], NULL);
    nc_print_dns_cache_node(ncct, argv[i], n);
  }
  DCUNLOCK();
  return 0;
}
static int
noit_console_manip_dns_cache(mtev_console_closure_t ncct,
                             int argc, char **argv,
                             mtev_console_state_t *dstate,
                             void *closure) {
  int i;
  if(argc == 0) {
    nc_printf(ncct, "dns_cache what?\n");
    return 0;
  }
  DCLOCK();
  if(closure == NULL) {
    /* adding */
    for(i=0;i<argc;i++) {
      dns_cache_node *n;
      n = mtev_skiplist_find(nc_dns_cache, argv[i], NULL);
      if(NULL != n) {
        nc_printf(ncct, " == Already in system ==\n");
        nc_print_dns_cache_node(ncct, argv[i], n);
      }
      else {
        nc_printf(ncct, "%s submitted.\n", argv[i]);
        noit_check_resolver_remind_internal(argv[i], mtev_false);
      }
    }
  }
  else {
    for(i=0;i<argc;i++) {
      dns_cache_node *n;
      n = mtev_skiplist_find(nc_dns_cache, argv[i], NULL);
      if(NULL != n) {
        if(n->lookup_inflight_v4 || n->lookup_inflight_v6)
          nc_printf(ncct, "%s is currently resolving and cannot be removed.\n");
        else {
          mtev_skiplist_remove(nc_dns_cache, argv[i], dns_cache_node_free);
          nc_printf(ncct, "%s removed.\n", argv[i]);
        }
      }
      else nc_printf(ncct, "%s not in system.\n", argv[i]);
    }
  }
  DCUNLOCK();
  return 0;
}

static void
register_console_dns_cache_commands() {
  mtev_console_state_t *tl;
  cmd_info_t *showcmd, *nocmd;

  tl = mtev_console_state_initial();
  showcmd = mtev_console_state_get_cmd(tl, "show");
  mtevAssert(showcmd && showcmd->dstate);

  nocmd = mtev_console_state_get_cmd(tl, "no");
  mtevAssert(nocmd && nocmd->dstate);

  mtev_console_state_add_cmd(showcmd->dstate,
    NCSCMD("dns_cache", noit_console_show_dns_cache, NULL, NULL, NULL));

  mtev_console_state_add_cmd(tl,
    NCSCMD("dns_cache", noit_console_manip_dns_cache, NULL, NULL, NULL));

  mtev_console_state_add_cmd(nocmd->dstate,
    NCSCMD("dns_cache", noit_console_manip_dns_cache, NULL, NULL, (void *)0x1));
}

int
noit_check_etc_hosts_cache_refresh(eventer_t e, int mask, void *closure,
                                   struct timeval *now) {
  static struct stat last_stat;
  struct stat sb;
  struct hostent *ent;
  int reload = 0, rv = 0;

  memset(&sb, 0, sizeof(sb));
  while(-1 == (rv = stat("/etc/hosts", &sb)) && errno == EINTR);
#define CSTAT(f) (sb.f == last_stat.f)
  reload = (rv == 0) && ! (CSTAT(st_dev) && CSTAT(st_ino) && CSTAT(st_mode) && CSTAT(st_uid) &&
              CSTAT(st_gid) && CSTAT(st_size) && CSTAT(st_mtime));
  memcpy(&last_stat, &sb, sizeof(sb));

  if(reload) {
    mtev_hash_delete_all(&etc_hosts_cache, free, free);
    while(NULL != (ent = gethostent())) {
      int i = 0;
      char *name = ent->h_name;
      while(name) {
        void *vnode;
        static_host_node *node;
        if(!mtev_hash_retrieve(&etc_hosts_cache, name, strlen(name), &vnode)) {
          vnode = node = calloc(1, sizeof(*node));
          node->target = strdup(name);
          mtev_hash_store(&etc_hosts_cache, node->target, strlen(node->target), node);
        }
        node = vnode;
  
        if(ent->h_addrtype == AF_INET) {
          node->has_ip4 = 1;
          memcpy(&node->ip4, ent->h_addr_list[0], ent->h_length);
        }
        if(ent->h_addrtype == AF_INET6) {
          node->has_ip6 = 1;
          memcpy(&node->ip6, ent->h_addr_list[0], ent->h_length);
        }
        
        name = ent->h_aliases[i++];
      }
    }
    endhostent();
    mtevL(noit_debug, "reloaded %d /etc/hosts targets\n", mtev_hash_size(&etc_hosts_cache));
  }

  eventer_add_in_s_us(noit_check_etc_hosts_cache_refresh, NULL, 1, 0);
  return 0;
}

void noit_check_resolver_init() {
  int32_t cnt;
  mtev_conf_section_t *servers, *searchdomains;
  eventer_t e;
  if(dns_init(NULL, 0) < 0)
    mtevL(noit_error, "dns initialization failed.\n");
  dns_ctx = dns_new(NULL);
  if(dns_init(dns_ctx, 0) != 0) {
    mtevL(noit_error, "dns initialization failed.\n");
    exit(-1);
  }

  /* Optional servers */
  servers = mtev_conf_get_sections(MTEV_CONF_ROOT, "//resolver//server", &cnt);
  if(cnt) {
    int i;
    char server[128];
    dns_add_serv(dns_ctx, NULL); /* reset */
    for(i=0;i<cnt;i++) {
      if(mtev_conf_get_stringbuf(servers[i], "self::node()",
                                 server, sizeof(server))) {
        if(mtev_conf_env_off(servers[i], NULL)) {
          mtevL(noit_error, "DNS server %s environmentally ignored.\n", server);
        }
        else if(dns_add_serv(dns_ctx, server) < 0) {
          mtevL(noit_error, "Failed adding DNS server: %s\n", server);
        }
      }
    }
  }
  mtev_conf_release_sections(servers, cnt);
  searchdomains = mtev_conf_get_sections(MTEV_CONF_ROOT, "//resolver//search", &cnt);
  if(cnt) {
    int i;
    char search[128];
    dns_add_srch(dns_ctx, NULL); /* reset */
    for(i=0;i<cnt;i++) {
      if(mtev_conf_get_stringbuf(searchdomains[i], "self::node()",
                                 search, sizeof(search))) {
        if(mtev_conf_env_off(searchdomains[i], NULL)) {
          mtevL(noit_error, "DNS search %s environmentally ignored.\n", search);
        }
        else if(dns_add_srch(dns_ctx, search) < 0) {
          mtevL(noit_error, "Failed adding DNS search path: %s\n", search);
        }
        else if(dns_search_flag) dns_search_flag = 0; /* enable search */
      }
    }
  }
  mtev_conf_release_sections(searchdomains, cnt);

  if(mtev_conf_get_int32(MTEV_CONF_ROOT, "//resolver/@ndots", &cnt))
    dns_set_opt(dns_ctx, DNS_OPT_NDOTS, cnt);

  if(mtev_conf_get_int32(MTEV_CONF_ROOT, "//resolver/@ntries", &cnt))
    dns_set_opt(dns_ctx, DNS_OPT_NTRIES, cnt);

  if(mtev_conf_get_int32(MTEV_CONF_ROOT, "//resolver/@timeout", &cnt))
    dns_set_opt(dns_ctx, DNS_OPT_TIMEOUT, cnt);

  if(dns_open(dns_ctx) < 0) {
    mtevL(noit_error, "dns open failed.\n");
    exit(-1);
  }
  eventer_name_callback("dns_cache_callback", dns_cache_callback);
  dns_set_tmcbck(dns_ctx, dns_cache_utm_fn, dns_ctx);
  e = eventer_alloc_fd(dns_cache_callback, dns_ctx, dns_sock(dns_ctx),
                       EVENTER_READ|EVENTER_EXCEPTION);
  eventer_add(e);

  nc_dns_cache = mtev_skiplist_alloc();
  mtev_skiplist_set_compare(nc_dns_cache, name_lookup, name_lookup_k);
  mtev_skiplist_add_index(nc_dns_cache, refresh_idx, refresh_idx_k);

  /* maybe load it from cache */
  if(noit_resolver_cache_load_hook_exists()) {
    struct timeval now;
    char *key;
    void *data;
    int len;
    mtev_gettimeofday(&now, NULL);
    while(noit_resolver_cache_load_hook_invoke(&key, &data, &len) == MTEV_HOOK_CONTINUE) {
      dns_cache_node *n;
      n = calloc(1, sizeof(*n));
      if(dns_cache_node_deserialize(n, data, len) >= 0) {
        n->target = strdup(key);
        /* if the TTL indicates that it will expire in less than 60 seconds
         * (including stuff that should have already expired), then fudge
         * the last_updated time to make it expire some random time within
         * the next 60 seconds.
         */
        if(n->last_needed > now.tv_sec || n->last_updated > now.tv_sec) {
          dns_cache_node_free(n);
          break; /* impossible */
        }

        n->last_needed = now.tv_sec;
        if(n->last_updated + n->ttl < now.tv_sec + 60) {
          int fudge = MIN(60, n->ttl) + 1;
          n->last_updated = now.tv_sec - n->ttl + (lrand48() % fudge);
        }
        DCLOCK();
        mtev_skiplist_insert(nc_dns_cache, n);
        DCUNLOCK();
        n = NULL;
      }
      else {
        mtevL(noit_error, "Failed to deserialize resolver cache record.\n");
      }
      if(n) dns_cache_node_free(n);
      if(key) free(key);
      if(data) free(data);
    }
  }

  noit_check_resolver_loop(NULL, 0, NULL, NULL);
  register_console_dns_cache_commands();

  mtev_hash_init(&etc_hosts_cache);
  noit_check_etc_hosts_cache_refresh(NULL, 0, NULL, NULL);
}

int noit_check_should_resolve_targets(mtev_boolean *should_resolve) {
  static int inited = 0, cached_rv;;
  static mtev_boolean cached_should_resolve;
  if(!inited) {
    cached_rv = mtev_conf_get_boolean(MTEV_CONF_ROOT, "//checks/@resolve_targets",
                                      &cached_should_resolve);
    inited = 1;
  }
  *should_resolve = cached_should_resolve;
  return cached_rv;
}
