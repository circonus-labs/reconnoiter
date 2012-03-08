/*
 * Copyright (c) 2011, Circonus, Inc.
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
 *     * Neither the name Circonus, Inc. nor the names
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
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"
#include "utils/noit_btrie.h"
#include "ip_acl.xmlh"
#include <assert.h>

#define DENY_PTR (void *)-1
#define ALLOW_PTR (void *)1

static int ip_acl_module_id = -1;

static noit_hash_table acls = NOIT_HASH_EMPTY;

static void
free_btrie(void *vb) {
  btrie *acl = vb;
  if(acl) {
    noit_drop_tree(acl, NULL);
    free(acl);
  }
}
static int
ip_acl_onload(noit_image_t *self) {
  int i, cnt;
  noit_conf_section_t *acl_c;
  ip_acl_module_id = noit_check_register_module("ip_acl");
  if(ip_acl_module_id < 0) return -1;

  acl_c = noit_conf_get_sections(NULL, "/noit/acls//acl", &cnt);
  if(acl_c) {
    for(i=0; i<cnt; i++) {
      char *name;
      int j, rcnt, arcnt = 0;
      noit_conf_section_t *rule_c;
      if(noit_conf_get_string(acl_c[i], "@name", &name)) {
        rule_c = noit_conf_get_sections(acl_c[i], "rule", &rcnt);
        if(rule_c) {
          btrie *acl = calloc(1, sizeof(*acl));
          for(j=0; j<rcnt; j++) {
            int mask = -1, rv;
            char dirstr[16] = "unspecified";
            char *cp, target[256] = "";
            union {
              struct in_addr addr4;
              struct in6_addr addr6;
            } a;

            noit_conf_get_stringbuf(rule_c[j], "self::node()", target, sizeof(target));
            if(NULL != (cp = strchr(target, '/'))) {
              *cp++ = '\0';
              mask = atoi(cp);
            }
            if(!noit_conf_get_stringbuf(rule_c[j], "@type", dirstr, sizeof(dirstr)) ||
               (strcmp(dirstr, "deny") && strcmp(dirstr, "allow"))) {
              noitL(noit_error, "Unknown acl rule type \"%s\" in acl \"%s\"\n",
                    dirstr, name);
            }
            else if(inet_pton(AF_INET, target, &a) == 1) {
              if(mask == -1) mask = 32;
              noit_add_route_ipv4(acl, &a.addr4, mask, strcmp(dirstr, "allow") ? DENY_PTR : ALLOW_PTR);
              arcnt++;
            }
            else if(inet_pton(AF_INET6, target, &a) == 1) {
              if(mask == -1) mask = 128;
              noit_add_route_ipv6(acl, &a.addr6, mask, strcmp(dirstr, "allow") ? DENY_PTR : ALLOW_PTR);
              arcnt++;
            }
          }
          noitL(noit_error, "ACL %s/%p -> %d/%d rules\n", name, acl, arcnt, rcnt);
          noit_hash_replace(&acls, name, strlen(name), acl, free, free_btrie);
          free(rule_c);
        }
      }
    }
    free(acl_c);
  }
  return 0;
}

static int
ip_acl_config(noit_module_generic_t *self, noit_hash_table *o) {
  return 0;
}

static noit_hook_return_t
ip_acl_hook_impl(void *closure, noit_module_t *self,
                 noit_check_t *check, noit_check_t *cause) {
  char deny_msg[128];
  stats_t current;
  noit_hash_table *config;
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *k = NULL;
  int klen;
  void *data;
  config = noit_check_get_module_config(check, ip_acl_module_id);
  if(!config || config->size == 0) return NOIT_HOOK_CONTINUE;
  while(noit_hash_next(config, &iter, &k, &klen, &data)) {
    if(k) {
      void *dir = NULL;
      unsigned char mask;
      if(noit_hash_retrieve(&acls, k, strlen(k), &data)) {
        btrie *acl = data;
        if(check->target_family == AF_INET) {
          dir = noit_find_bpm_route_ipv4(acl, &check->target_addr.addr, &mask);
          if(dir == DENY_PTR) goto prevent;
          else if(dir == ALLOW_PTR) return NOIT_HOOK_CONTINUE;
        }
        else if(check->target_family == AF_INET6) {
          dir = noit_find_bpm_route_ipv6(acl, &check->target_addr.addr6, &mask);
          if(dir == DENY_PTR) goto prevent;
          else if(dir == ALLOW_PTR) return NOIT_HOOK_CONTINUE;
        }
      }
    }
  }
  return NOIT_HOOK_CONTINUE;

 prevent:
  memset(&current, 0, sizeof(current));
  current.available = NP_UNAVAILABLE;
  current.state = NP_BAD;
  gettimeofday(&current.whence, NULL);
  snprintf(deny_msg, sizeof(deny_msg), "prevented by ACL '%s'", k ? k : "unknown");
  current.status = deny_msg;
  noit_check_set_stats(check, &current);
  return NOIT_HOOK_DONE;
}
static int
ip_acl_init(noit_module_generic_t *self) {
  check_preflight_hook_register("ip_acl", ip_acl_hook_impl, NULL);
  return 0;
}

noit_module_generic_t ip_acl = {
  {
    NOIT_GENERIC_MAGIC,
    NOIT_GENERIC_ABI_VERSION,
    "ip_acl",
    "IP Access Controls for Checks",
    ip_acl_xml_description,
    ip_acl_onload
  },
  ip_acl_config,
  ip_acl_init
};

