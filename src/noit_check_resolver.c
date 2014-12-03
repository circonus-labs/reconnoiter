/*
 * Copyright (c) 2011, OmniTI Computer Consulting, Inc.
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
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <assert.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "utils/noit_skiplist.h"
#include "utils/noit_hash.h"
#include "udns/udns.h"
#include "noit_console.h"

#define MAX_RR 256
#define DEFAULT_FAILED_TTL 60
#define DEFAULT_PURGE_AGE  1200 /* 20 minutes */

static struct dns_ctx *dns_ctx;
static noit_skiplist nc_dns_cache;
static eventer_t dns_cache_timeout = NULL;
static noit_hash_table etc_hosts_cache;

typedef struct {
  time_t last_needed;
  time_t last_updated;
  noit_boolean lookup_inflight_v4;
  noit_boolean lookup_inflight_v6;
  char *target;
  unsigned char dn[DNS_MAXDN];
  time_t ttl;
  int ip4_cnt;
  char **ip4;
  int ip6_cnt;
  char **ip6;
} dns_cache_node;

typedef struct {
  char *target;
  struct in_addr ip4;
  struct in6_addr ip6;
  int has_ip4:1;
  int has_ip6:1;
} static_host_node;

void dns_cache_node_free(void *vn) {
  dns_cache_node *n = vn;
  int i;
  if(!n) return;
  if(n->target) free(n->target);
  for(i=0;i<n->ip4_cnt;i++) if(n->ip4[i]) free(n->ip4[i]);
  for(i=0;i<n->ip6_cnt;i++) if(n->ip6[i]) free(n->ip6[i]);
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

void noit_check_resolver_remind(const char *target) {
  dns_cache_node *n;
  if(!target) return;
  n = noit_skiplist_find(&nc_dns_cache, target, NULL);
  if(n != NULL) {
    n->last_needed = time(NULL);
    return; 
  }
  n = calloc(1, sizeof(*n));
  n->target = strdup(target);
  n->last_needed = time(NULL);
  noit_skiplist_insert(&nc_dns_cache, n);
}


int noit_check_resolver_fetch(const char *target, char *buff, int len,
                              uint8_t prefer_family) {
  int i;
  uint8_t progression[2];
  dns_cache_node *n;
  void *vnode;

  buff[0] = '\0';
  if(!target) return -1;
  progression[0] = prefer_family;
  progression[1] = (prefer_family == AF_INET) ? AF_INET6 : AF_INET;

  if(noit_hash_retrieve(&etc_hosts_cache, target, strlen(target), &vnode)) {
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

  n = noit_skiplist_find(&nc_dns_cache, target, NULL);
  if(n != NULL) {
    int rv;
    if(n->last_updated == 0) return -1; /* not resolved yet */
    rv = n->ip4_cnt + n->ip6_cnt;
    for(i=0; i<2; i++) {
      switch(progression[i]) {
        case AF_INET:
          if(n->ip4_cnt > 0) {
            strlcpy(buff, n->ip4[0], len);
            return rv;
          }
          break;
        case AF_INET6:
          if(n->ip6_cnt > 0) {
            strlcpy(buff, n->ip6[0], len);
            return rv;
          }
          break;
      }
    }
    return rv;
  }
  return -1;
}

static void blank_update_v4(dns_cache_node *n) {
  int i;
  for(i=0;i<n->ip4_cnt;i++) if(n->ip4[i]) free(n->ip4[i]);
  if(n->ip4) free(n->ip4);
  n->ip4 = NULL;
  n->ip4_cnt = 0;
  noit_skiplist_remove(&nc_dns_cache, n->target, NULL);
  n->last_updated = time(NULL);
  if (n->lookup_inflight_v6) {
    n->ttl = DEFAULT_FAILED_TTL;
  }
  n->lookup_inflight_v4 = noit_false;
  noit_skiplist_insert(&nc_dns_cache, n);
}
static void blank_update_v6(dns_cache_node *n) {
  int i;
  for(i=0;i<n->ip6_cnt;i++) if(n->ip6[i]) free(n->ip6[i]);
  if(n->ip6) free(n->ip6);
  n->ip6 = NULL;
  n->ip6_cnt = 0;
  noit_skiplist_remove(&nc_dns_cache, n->target, NULL);
  n->last_updated = time(NULL);
  if (n->lookup_inflight_v4) {
    n->ttl = DEFAULT_FAILED_TTL;
  }
  n->lookup_inflight_v6 = noit_false;
  noit_skiplist_insert(&nc_dns_cache, n);
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
      newe = eventer_alloc();
      newe->mask = EVENTER_TIMER;
      newe->callback = dns_invoke_timeouts;
      newe->closure = dns_ctx;
      gettimeofday(&newe->whence, NULL);
      newe->whence.tv_sec += timeout;
    }
  }
  if(e) eventer_free(e);
  if(newe) eventer_add(newe);
  dns_cache_timeout = newe;
}

static void dns_cache_resolve(struct dns_ctx *ctx, void *result, void *data,
                              enum dns_type rtype) {
  int i, ttl, acnt, r = dns_status(ctx);
  dns_cache_node *n = data;
  unsigned char idn[DNS_MAXDN], dn[DNS_MAXDN];
  struct dns_parse p;
  struct dns_rr rr;
  unsigned nrr;
  char **answers;
  const unsigned char *pkt, *cur, *end;

  if(!result) goto blank;

  dns_dntodn(n->dn, idn, sizeof(idn));

  pkt = result; end = pkt + r; cur = dns_payload(pkt);
  dns_getdn(pkt, &cur, end, dn, sizeof(dn));
  dns_initparse(&p, NULL, pkt, cur, end);
  p.dnsp_qcls = 0;
  p.dnsp_qtyp = 0;
  nrr = 0;
  ttl = 0;

  while((r = dns_nextrr(&p, &rr)) > 0) {
    if (!dns_dnequal(idn, rr.dnsrr_dn)) continue;
    if (DNS_C_IN == rr.dnsrr_cls && rtype == rr.dnsrr_typ) ++nrr;
    else if (rr.dnsrr_typ == DNS_T_CNAME && !nrr) {
      if (dns_getdn(pkt, &rr.dnsrr_dptr, end,
                    p.dnsp_dnbuf, sizeof(p.dnsp_dnbuf)) <= 0 ||
          rr.dnsrr_dptr != rr.dnsrr_dend) {
        break;
      }
      else {
        if(rr.dnsrr_ttl > 0 && (ttl == 0 || rr.dnsrr_ttl < ttl))
          ttl = rr.dnsrr_ttl;
        dns_dntodn(p.dnsp_dnbuf, idn, sizeof(idn));
      }
    }
  }
  if(!r && !nrr) goto blank;

  dns_rewind(&p, NULL);
  p.dnsp_qcls = DNS_C_IN;
  p.dnsp_qtyp = rtype;
  answers = calloc(nrr, sizeof(*answers));
  acnt = 0;
  while(dns_nextrr(&p, &rr) && nrr < MAX_RR) {
    char buff[INET6_ADDRSTRLEN];
    if (!dns_dnequal(idn, rr.dnsrr_dn)) continue;
    if (p.dnsp_rrl && !rr.dnsrr_dn[0] && rr.dnsrr_typ == DNS_T_OPT) continue;
    if (rtype == rr.dnsrr_typ) {
      if(rr.dnsrr_ttl > 0 && (ttl == 0 || rr.dnsrr_ttl < ttl))
        ttl = rr.dnsrr_ttl;
      switch(rr.dnsrr_typ) {
        case DNS_T_A:
          if(rr.dnsrr_dsz != 4) continue;
          inet_ntop(AF_INET, rr.dnsrr_dptr, buff, sizeof(buff));
          answers[acnt++] = strdup(buff);
          break;
        case DNS_T_AAAA:
          if(rr.dnsrr_dsz != 16) continue;
          inet_ntop(AF_INET6, rr.dnsrr_dptr, buff, sizeof(buff));
          answers[acnt++] = strdup(buff);
          break;
        default:
          break;
      }
    }
  }

  n->ttl = ttl;
  if(rtype == DNS_T_A) {
    for(i=0;i<n->ip4_cnt;i++) if(n->ip4[i]) free(n->ip4[i]);
    if(n->ip4) free(n->ip4);
    n->ip4_cnt = acnt;
    n->ip4 = answers;
    n->lookup_inflight_v4 = noit_false;
  }
  else if(rtype == DNS_T_AAAA) {
    for(i=0;i<n->ip6_cnt;i++) if(n->ip6[i]) free(n->ip6[i]);
    if(n->ip6) free(n->ip6);
    n->ip6_cnt = acnt;
    n->ip6 = answers;
    n->lookup_inflight_v6 = noit_false;
  }
  else {
    if(answers) free(answers);
    if(result) free(result);
     return;
  }
  noit_skiplist_remove(&nc_dns_cache, n->target, NULL);
  n->last_updated = time(NULL);
  noit_skiplist_insert(&nc_dns_cache, n);
  noitL(noit_debug, "Resolved %s/%s -> %d records\n", n->target,
        (rtype == DNS_T_AAAA ? "IPv6" : (rtype == DNS_T_A ? "IPv4" : "???")),
        acnt);
  if(result) free(result);
  return;

 blank:
  if(rtype == DNS_T_A) blank_update_v4(n);
  if(rtype == DNS_T_AAAA) blank_update_v6(n);
  noitL(noit_debug, "Resolved %s/%s -> blank\n", n->target,
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
  noit_skiplist *tlist;
  noit_skiplist_node *sn;

  now = time(NULL);
  sn = noit_skiplist_getlist(nc_dns_cache.index);
  assert(sn);
  tlist = sn->data;
  assert(tlist);
  sn = noit_skiplist_getlist(tlist);
  while(sn) {
    dns_cache_node *n = sn->data;
    noit_skiplist_next(tlist, &sn); /* move forward */
    /* remove if needed */
    if(n->last_updated + n->ttl > now) break;
    if(n->last_needed + DEFAULT_PURGE_AGE < now &&
       !(n->lookup_inflight_v4 || n->lookup_inflight_v6))
      noit_skiplist_remove(&nc_dns_cache, n->target, dns_cache_node_free);
    else {
      int abs;
      if(!dns_ptodn(n->target, strlen(n->target),
                    n->dn, sizeof(n->dn), &abs)) {
        blank_update(n);
      }
      else {
        if(!n->lookup_inflight_v4) {
          n->lookup_inflight_v4 = noit_true;
          if(!dns_submit_dn(dns_ctx, n->dn, DNS_C_IN, DNS_T_A,
                            abs | DNS_NOSRCH, NULL, dns_cache_resolve_v4, n))
            blank_update_v4(n);
          else
            dns_timeouts(dns_ctx, -1, now);
        }
        if(!n->lookup_inflight_v6) {
          n->lookup_inflight_v6 = noit_true;
          if(!dns_submit_dn(dns_ctx, n->dn, DNS_C_IN, DNS_T_AAAA,
                            abs | DNS_NOSRCH, NULL, dns_cache_resolve_v6, n))
            blank_update_v6(n);
          else
            dns_timeouts(dns_ctx, -1, now);
        }
      }
      noitL(noit_debug, "Firing lookup for '%s'\n", n->target);
      continue;
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
nc_print_dns_cache_node(noit_console_closure_t ncct,
                        const char *target, dns_cache_node *n) {
  nc_printf(ncct, "==== %s ====\n", target);
  if(!n) nc_printf(ncct, "NOT FOUND\n");
  else {
    int i;
    time_t now = time(NULL);
    nc_printf(ncct, "%16s: %ds ago\n", "last needed", now - n->last_needed);
    nc_printf(ncct, "%16s: %ds ago\n", "resolved", now - n->last_updated);
    nc_printf(ncct, "%16s: %ds\n", "ttl", n->ttl);
    if(n->lookup_inflight_v4) nc_printf(ncct, "actively resolving A RRs\n");
    if(n->lookup_inflight_v6) nc_printf(ncct, "actively resolving AAAA RRs\n");
    for(i=0;i<n->ip4_cnt;i++)
      nc_printf(ncct, "%17s %s\n", i?"":"IPv4:", n->ip4[i]);
    for(i=0;i<n->ip6_cnt;i++)
      nc_printf(ncct, "%17s %s\n", i?"":"IPv6:", n->ip6[i]);
  }
  return 0;
}
static int
noit_console_show_dns_cache(noit_console_closure_t ncct,
                            int argc, char **argv,
                            noit_console_state_t *dstate,
                            void *closure) {
  int i;

  if(argc == 0) {
    noit_skiplist_node *sn;
    for(sn = noit_skiplist_getlist(&nc_dns_cache); sn;
        noit_skiplist_next(&nc_dns_cache, &sn)) {
      dns_cache_node *n = (dns_cache_node *)sn->data;
      nc_print_dns_cache_node(ncct, n->target, n);
    }
  }
  for(i=0;i<argc;i++) {
    dns_cache_node *n;
    n = noit_skiplist_find(&nc_dns_cache, argv[i], NULL);
    nc_print_dns_cache_node(ncct, argv[i], n);
  }
  return 0;
}
static int
noit_console_manip_dns_cache(noit_console_closure_t ncct,
                             int argc, char **argv,
                             noit_console_state_t *dstate,
                             void *closure) {
  int i;
  if(argc == 0) {
    nc_printf(ncct, "dns_cache what?\n");
    return 0;
  }
  if(closure == NULL) {
    /* adding */
    for(i=0;i<argc;i++) {
      dns_cache_node *n;
      if(NULL != (n = noit_skiplist_find(&nc_dns_cache, argv[i], NULL))) {
        nc_printf(ncct, " == Already in system ==\n");
        nc_print_dns_cache_node(ncct, argv[i], n);
      }
      else {
        nc_printf(ncct, "%s submitted.\n", argv[i]);
        noit_check_resolver_remind(argv[i]);
      }
    }
  }
  else {
    for(i=0;i<argc;i++) {
      dns_cache_node *n;
      if(NULL != (n = noit_skiplist_find(&nc_dns_cache, argv[i], NULL))) {
        if(n->lookup_inflight_v4 || n->lookup_inflight_v6)
          nc_printf(ncct, "%s is currently resolving and cannot be removed.\n");
        else {
          noit_skiplist_remove(&nc_dns_cache, argv[i], dns_cache_node_free);
          nc_printf(ncct, "%s removed.\n", argv[i]);
        }
      }
      else nc_printf(ncct, "%s not in system.\n", argv[i]);
    }
  }
  return 0;
}

static void
register_console_dns_cache_commands() {
  noit_console_state_t *tl;
  cmd_info_t *showcmd, *nocmd;

  tl = noit_console_state_initial();
  showcmd = noit_console_state_get_cmd(tl, "show");
  assert(showcmd && showcmd->dstate);

  nocmd = noit_console_state_get_cmd(tl, "no");
  assert(nocmd && nocmd->dstate);

  noit_console_state_add_cmd(showcmd->dstate,
    NCSCMD("dns_cache", noit_console_show_dns_cache, NULL, NULL, NULL));

  noit_console_state_add_cmd(tl,
    NCSCMD("dns_cache", noit_console_manip_dns_cache, NULL, NULL, NULL));

  noit_console_state_add_cmd(nocmd->dstate,
    NCSCMD("dns_cache", noit_console_manip_dns_cache, NULL, NULL, (void *)0x1));
}

int
noit_check_etc_hosts_cache_refresh(eventer_t e, int mask, void *closure,
                                   struct timeval *now) {
  static struct stat last_stat;
  struct stat sb;
  struct hostent *ent;
  int reload = 0;

  memset(&sb, 0, sizeof(sb));
  stat("/etc/hosts", &sb);
#define CSTAT(f) (sb.f == last_stat.f)
  reload = ! (CSTAT(st_dev) && CSTAT(st_ino) && CSTAT(st_mode) && CSTAT(st_uid) &&
              CSTAT(st_gid) && CSTAT(st_size) && CSTAT(st_mtime));
  memcpy(&last_stat, &sb, sizeof(sb));

  if(reload) {
    noit_hash_delete_all(&etc_hosts_cache, free, free);
    while(NULL != (ent = gethostent())) {
      int i = 0;
      char *name = ent->h_name;
      while(name) {
        void *vnode;
        static_host_node *node;
        if(!noit_hash_retrieve(&etc_hosts_cache, name, strlen(name), &vnode)) {
          vnode = node = calloc(1, sizeof(*node));
          node->target = strdup(name);
          noit_hash_store(&etc_hosts_cache, node->target, strlen(node->target), node);
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
    noitL(noit_debug, "reloaded %d /etc/hosts targets\n", noit_hash_size(&etc_hosts_cache));
  }

  eventer_add_in_s_us(noit_check_etc_hosts_cache_refresh, NULL, 1, 0);
  return 0;
}

void noit_check_resolver_init() {
  eventer_t e;
  if(dns_init(NULL, 0) < 0)
    noitL(noit_error, "dns initialization failed.\n");
  dns_ctx = dns_new(NULL);
  if(dns_init(dns_ctx, 0) != 0 ||
     dns_open(dns_ctx) < 0) {
    noitL(noit_error, "dns initialization failed.\n");
  }
  eventer_name_callback("dns_cache_callback", dns_cache_callback);
  dns_set_tmcbck(dns_ctx, dns_cache_utm_fn, dns_ctx);
  e = eventer_alloc();
  e->mask = EVENTER_READ | EVENTER_EXCEPTION;
  e->closure = dns_ctx;
  e->callback = dns_cache_callback;
  e->fd = dns_sock(dns_ctx);
  eventer_add(e);

  noit_skiplist_init(&nc_dns_cache);
  noit_skiplist_set_compare(&nc_dns_cache, name_lookup, name_lookup_k);
  noit_skiplist_add_index(&nc_dns_cache, refresh_idx, refresh_idx_k);
  noit_check_resolver_loop(NULL, 0, NULL, NULL);
  register_console_dns_cache_commands();

  noit_hash_init(&etc_hosts_cache);
  noit_check_etc_hosts_cache_refresh(NULL, 0, NULL, NULL);
}
