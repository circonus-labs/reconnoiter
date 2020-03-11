/*
 * Copyright (c) 2011,2015, Circonus, Inc. All rights reserved.
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

#include <mtev_defines.h>

#include <assert.h>

#include <mtev_hash.h>
#include <mtev_btrie.h>
#include <mtev_conf.h>

#include "noit_mtev_bridge.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "ip_acl.xmlh"

#ifndef MTEV_FEATURE_CONF_RW
#define mtev_conf_get_sections_read mtev_conf_get_sections
#define mtev_conf_relase_sections_read mtev_conf_relase_sections
#endif

#define DENY_PTR (void *)-1
#define ALLOW_PTR (void *)1

static int ip_acl_module_id = -1;

static mtev_hash_table acls;

static void
free_btrie(void *vb) {
  mtev_btrie *acl = vb;
  if(acl) {
    mtev_btrie_drop_tree(acl, NULL);
    free(acl);
  }
}
static int
ip_acl_onload(mtev_image_t *self) {
  int i, cnt;
  mtev_conf_section_t *acl_c;
  mtev_hash_init(&acls);
  ip_acl_module_id = noit_check_register_module("ip_acl");
  if(ip_acl_module_id < 0) return -1;

  acl_c = mtev_conf_get_sections_read(MTEV_CONF_ROOT, "/noit/acls//acl", &cnt);
  for(i=0; i<cnt; i++) {
    char *name;
    int j, rcnt, arcnt = 0;
    mtev_conf_section_t *rule_c;
    if(mtev_conf_env_off(acl_c[i], NULL)) continue;
    if(mtev_conf_get_string(acl_c[i], "@name", &name)) {
      rule_c = mtev_conf_get_sections_read(acl_c[i], "rule", &rcnt);
      if(rule_c) {
        mtev_btrie *acl = calloc(1, sizeof(*acl));
        for(j=0; j<rcnt; j++) {
          int mask = -1;
          char dirstr[16] = "unspecified";
          char *cp, target[256] = "";
          union {
            struct in_addr addr4;
            struct in6_addr addr6;
          } a;

          if(mtev_conf_env_off(rule_c[j], NULL)) continue;
          if(!mtev_conf_get_stringbuf(rule_c[j], "self::node()", target, sizeof(target)))
            target[0] = '\0';
          if(NULL != (cp = strchr(target, '/'))) {
            *cp++ = '\0';
            mask = atoi(cp);
          }
          if(!mtev_conf_get_stringbuf(rule_c[j], "@type", dirstr, sizeof(dirstr)) ||
             (strcmp(dirstr, "deny") && strcmp(dirstr, "allow"))) {
            mtevL(noit_error, "Unknown acl rule type \"%s\" in acl \"%s\"\n",
                  dirstr, name);
          }
          else if(inet_pton(AF_INET, target, &a) == 1) {
            if(mask == -1) mask = 32;
            mtev_btrie_add_route_ipv4(acl, &a.addr4, mask, strcmp(dirstr, "allow") ? DENY_PTR : ALLOW_PTR);
            arcnt++;
          }
          else if(inet_pton(AF_INET6, target, &a) == 1) {
            if(mask == -1) mask = 128;
            mtev_btrie_add_route_ipv6(acl, &a.addr6, mask, strcmp(dirstr, "allow") ? DENY_PTR : ALLOW_PTR);
            arcnt++;
          }
        }
        mtevL(noit_debug, "ACL %s/%p -> %d/%d rules\n", name, acl, arcnt, rcnt);
        mtev_hash_replace(&acls, name, strlen(name), acl, free, free_btrie);
      }
      mtev_conf_release_sections_read(rule_c, rcnt);
    }
  }
  mtev_conf_release_sections_read(acl_c, cnt);
  
  return 0;
}

static int
ip_acl_config(mtev_dso_generic_t *self, mtev_hash_table *o) {
  return 0;
}

static mtev_hook_return_t
ip_acl_hook_impl(void *closure, noit_module_t *self,
                 noit_check_t *check, noit_check_t *cause) {
  char deny_msg[128];
  struct timeval now;
  mtev_hash_table *config;
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  const char *k = NULL;
  int klen;
  void *data;
  config = noit_check_get_module_config(check, ip_acl_module_id);
  if(!config || mtev_hash_size(config) == 0) return MTEV_HOOK_CONTINUE;
  while(mtev_hash_next(config, &iter, &k, &klen, &data)) {
    if(k) {
      void *dir = NULL;
      unsigned char mask;
      if(mtev_hash_retrieve(&acls, k, strlen(k), &data)) {
        mtev_btrie *acl = data;
        if(check->target_family == AF_INET) {
          dir = mtev_btrie_find_bpm_route_ipv4(acl, &check->target_addr.addr, &mask);
          if(dir == DENY_PTR) goto prevent;
          else if(dir == ALLOW_PTR) return MTEV_HOOK_CONTINUE;
        }
        else if(check->target_family == AF_INET6) {
          dir = mtev_btrie_find_bpm_route_ipv6(acl, &check->target_addr.addr6, &mask);
          if(dir == DENY_PTR) goto prevent;
          else if(dir == ALLOW_PTR) return MTEV_HOOK_CONTINUE;
        }
      }
    }
  }
  return MTEV_HOOK_CONTINUE;

 prevent:
  mtev_gettimeofday(&now, NULL);
  noit_stats_set_whence(check, &now);
  noit_stats_set_available(check, NP_UNAVAILABLE);
  noit_stats_set_state(check, NP_BAD);
  snprintf(deny_msg, sizeof(deny_msg), "prevented by ACL '%s'", k);
  noit_stats_set_status(check, deny_msg);
  noit_check_set_stats(check);
  return MTEV_HOOK_DONE;
}
static int
ip_acl_init(mtev_dso_generic_t *self) {
  check_preflight_hook_register("ip_acl", ip_acl_hook_impl, NULL);
  return 0;
}

mtev_dso_generic_t ip_acl = {
  {
    .magic = MTEV_GENERIC_MAGIC,
    .version = MTEV_GENERIC_ABI_VERSION,
    .name = "ip_acl",
    .description = "IP Access Controls for Checks",
    .xml_description = ip_acl_xml_description,
    .onload = ip_acl_onload
  },
  ip_acl_config,
  ip_acl_init
};

