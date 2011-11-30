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

#include "utils/noit_hash.h"
#include "utils/noit_atomic.h"
#include "noit_conf.h"
#include "noit_acl.h"
#include "btrie.h"

#include <assert.h>

typedef struct _acl_btrie_t {
  int family;
  aclaccess_t allow_deny;
  char *module;
  btrie tree;
  struct _acl_btrie_t *next;
} acl_btrie_t;

typedef struct {
  char *name;
  acl_btrie_t *cidrs;
} aclset_t;

static noit_hash_table *aclsets = NULL;

static void
acl_btrie_free(void *vp) {
  acl_btrie_t *cidr = vp;
  free(cidr->module);
  free(cidr);
}

static int
noit_btrie_create(acl_btrie_t **b, const char *range, aclaccess_t allow_deny, const char *module) {
  char *ip, *prefix;
  int prefix_len;
  int family, rv, rc = 0;
  union {
    struct in_addr addr4;
    struct in6_addr addr6;
  } a;

  ip = strdup(range);
  prefix = strrchr(ip, '/');
  if (prefix == NULL) {
    rc = -1;
    goto done;
  }

  *(prefix++) = '\0';
  prefix_len = strtoul(prefix, NULL, 10);

  family = AF_INET;
  rv = inet_pton(family, ip, &a);
  if(rv != 1) {
    family = AF_INET6;
    rv = inet_pton(family, ip, &a);
    if(rv != 1) {
      rc = -1;
      goto done;
    }
  }

  *b = calloc(1, sizeof(*b));
  (*b)->allow_deny = allow_deny;
  (*b)->family = family;
  (*b)->module = strdup(module);

  if(family == AF_INET)
    add_route_ipv4(&(*b)->tree, &a.addr4, prefix_len, (void*)1);
  else
    add_route_ipv6(&(*b)->tree, &a.addr6, prefix_len, (void*)1);

done:
  free(ip);
  return rc;
}

static void
aclset_free(void *vp) {
  aclset_t *as = vp;
  acl_btrie_t *n;
  while(as->cidrs) {
    n = as->cidrs->next;
    acl_btrie_free(as->cidrs);
    as->cidrs = n;
  }
  if(as->name) free(as->name);
  free(as);
}

static aclset_t*
noit_aclset_create(const char *name) {
  aclset_t *set = calloc(1, sizeof(*set));
  set->name = strdup(name);
  return set;
}

static void
noit_acl_from_conf() {
  noit_conf_section_t *sets;
  int i, cnt;

  sets = noit_conf_get_sections(NULL, "/noit/acl", &cnt);
  for(i=0; i<cnt; i++) {
    noit_acl_add(sets[i]);
  }
  free(sets);
}
void
noit_acl_add_cidr(aclset_t *set, const char *range, aclaccess_t allow_deny, const char *module) {
  acl_btrie_t *cidr;
  if (!noit_btrie_create(&cidr, range, allow_deny, module)) {
    cidr->next = set->cidrs;
    set->cidrs = cidr;
  }
}
void
noit_acl_init() {
  aclsets = calloc(1, sizeof(*aclsets));
  noit_acl_from_conf();
}
void
noit_refresh_acl() {
}
void
noit_acl_add(noit_conf_section_t setinfo) {
  noit_conf_section_t *networks;
  char acl_name[256];
  aclset_t *set;
  int fcnt, j;

  if(!noit_conf_get_stringbuf(setinfo, "@name",
                              acl_name, sizeof(acl_name))) {
    noitL(noit_error,
          "acl with no name, skipping as it cannot be referenced.\n");
    return;
  }

  noitL(noit_debug, "loaded ACL (name=%s)\n", acl_name);

  set = noit_aclset_create(acl_name);
  networks = noit_conf_get_sections(setinfo, "network", &fcnt);

  for(j=fcnt-1; j>=0; j--) {
    char range_buffer[256];
    char type_buffer[256];
    char module_buffer[256];
    aclaccess_t type;

    if(!noit_conf_get_stringbuf(networks[j], "@ip", range_buffer, sizeof(range_buffer))) {
      noitL(noit_error, "ip or ip range not specified\n");
      continue;
    }
    if(!noit_conf_get_stringbuf(networks[j], "@type", type_buffer, sizeof(type_buffer))) {
      noitL(noit_error, "type not specified\n");
      continue;
    }
    if(!noit_conf_get_stringbuf(networks[j], "@module", module_buffer, sizeof(module_buffer))) {
      module_buffer[0] = '\0';
    }

    noitL(noit_debug, "  range=%s type=%s module=%s\n", range_buffer, type_buffer, module_buffer);
    type = strcasecmp(type_buffer, "allow") == 0 ? NOIT_IP_ACL_ALLOW : NOIT_IP_ACL_DENY;
    noit_acl_add_cidr(set, range_buffer, type, module_buffer);
  }

  noit_hash_replace(aclsets, set->name, strlen(set->name), (void *)set,
                    NULL, aclset_free);
}
int
noit_acl_check_ip(noit_check_t *check, const char *ip, aclaccess_t *access) {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *k;
  void *data;
  int family, rv, klen;
  unsigned char pl;
  union {
    struct in_addr addr4;
    struct in6_addr addr6;
  } a;

  assert(ip != NULL);
  assert(access != NULL);

  *access = NOIT_IP_ACL_DENY;

  family = AF_INET;
  rv = inet_pton(family, ip, &a);
  if(rv != 1) {
    family = AF_INET6;
    rv = inet_pton(family, ip, &a);
    if(rv != 1) {
      return -1;
    }
  }

  while(noit_hash_next(aclsets, &iter, &k, &klen, &data)) {
    aclset_t *set = data;
    acl_btrie_t *cidr = set->cidrs;

    while(cidr) {
      if(family == AF_INET && cidr->family == AF_INET) {
        if (find_bpm_route_ipv4(&cidr->tree, &a.addr4, &pl)) {
          if (cidr->module[0]) {
            if (strcasecmp(check->module, cidr->module) == 0) {
              *access = cidr->allow_deny;
              break;
            }
          } else {
            *access = cidr->allow_deny;
            break;
          }
        }
      }
      else if(family == AF_INET6 && cidr->family == AF_INET6) {
        if (find_bpm_route_ipv6(&cidr->tree, &a.addr6, &pl)) {
          if (cidr->module[0]) {
            if (strcasecmp(check->module, cidr->module) == 0) {
              *access = cidr->allow_deny;
              break;
            }
          } else {
            *access = cidr->allow_deny;
            break;
          }
        }
      }
      cidr = cidr->next;
    }
  }

  return 0;
}
