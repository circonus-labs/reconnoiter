/*
 * Copyright (c) 2015, Circonus, Inc. All rights reserved.
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
#include <mtev_hash.h>
#include <mtev_reverse_socket.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_mtev_bridge.h"

#include "reverse_check.xmlh"

static int reverse_check_module_id = -1;

static int
reverse_check_onload(mtev_image_t *self) {
  reverse_check_module_id = noit_check_register_module("reverse");
  if(reverse_check_module_id < 0) return -1;
  return 0;
}

static int
reverse_check_config(mtev_dso_generic_t *self, mtev_hash_table *o) {
  return 0;
}

static mtev_hook_return_t
reverse_check_hook_impl(void *closure, noit_check_t *check) {
  mtev_hash_table *config;
  config = noit_check_get_module_config(check, reverse_check_module_id);
  if(config && mtev_hash_size(config)) {
    mtev_hash_merge_as_dict(check->config, config);
  }
  return MTEV_HOOK_CONTINUE;
}

mtev_reverse_acl_decision_t
reverse_check_allow(const char *id, mtev_acceptor_closure_t *ac) {
  mtev_hash_table *config;
  noit_check_t *check;
  const char *key;
  uuid_t uuid;
  char uuid_str[UUID_STR_LEN+1];
  char expected_id[256];

  if(strncmp(id, "check/", 6)) return MTEV_ACL_ABSTAIN;

  strlcpy(uuid_str, id + 6, sizeof(uuid_str));
  if(mtev_uuid_parse(uuid_str, uuid) != 0) return MTEV_ACL_DENY;
  mtev_uuid_unparse_lower(uuid, uuid_str);

  check = noit_poller_lookup(uuid);
  if(!check) return MTEV_ACL_DENY;

  config = noit_check_get_module_config(check, reverse_check_module_id);
  if(config && mtev_hash_retr_str(config, "secret_key", strlen("secret_key"), &key)) {
    snprintf(expected_id, sizeof(expected_id), "check/%s#%s", uuid_str, key);
  }
  else {
    snprintf(expected_id, sizeof(expected_id), "check/%s", uuid_str);
  }
  noit_check_deref(check);
  if(!strncmp(id, expected_id, strlen(id))) return MTEV_ACL_ALLOW;
  return MTEV_ACL_DENY;
}

static mtev_hook_return_t
reverse_check_post_init(void *unused) {
  mtev_reverse_socket_acl(reverse_check_allow);
  return MTEV_HOOK_CONTINUE;
}
static int
reverse_check_init(mtev_dso_generic_t *self) {
  dso_post_init_hook_register("reverse_check", reverse_check_post_init, NULL);
  check_config_fixup_hook_register("reverse_check", reverse_check_hook_impl, NULL);
  return 0;
}

mtev_dso_generic_t reverse_check = {
  {
    .magic = MTEV_GENERIC_MAGIC,
    .version = MTEV_GENERIC_ABI_VERSION,
    .name = "reverse_check",
    .description = "config namespaces",
    .xml_description = reverse_check_xml_description,
    .onload = reverse_check_onload
  },
  reverse_check_config,
  reverse_check_init
};

