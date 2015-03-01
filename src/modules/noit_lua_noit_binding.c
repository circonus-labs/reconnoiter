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
#include "noit_check.h"
#include "noit_filters.h"
#include "lua_mtev.h"
#include "lua_check.h"
#include "lua.h"

/* noit specific stuff */
static int
nl_valid_ip(lua_State *L) {
  if(lua_gettop(L) != 1 || !lua_isstring(L,1))
    luaL_error(L, "bad parameters to noit.valid_ip");
  lua_pushboolean(L, noit_check_is_valid_target(lua_tostring(L,1)));
  return 1;
}

static int
nl_check(lua_State *L) {
  noit_check_t *check;
  uuid_t id;
  const char *id_str = lua_tostring(L,1);
  if(id_str && uuid_parse((char *)id_str, id) == 0) {
    noit_check_t *check = noit_poller_lookup(id);
    if(check) {
      noit_lua_setup_check(L, check);
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}

static int
nl_register_dns_ignore_domain(lua_State *L) {
  size_t extension_len, ignore_len;
  const char *extension, *ignore;
  if(lua_gettop(L) != 2) luaL_error(L, "bad call to noit.nl_register_dns_ignore_domain");

  extension = (const char *)lua_tolstring(L, 1, &extension_len);
  ignore = (const char *)lua_tolstring(L, 2, &ignore_len);

  noit_check_dns_ignore_tld(extension, ignore);

  return 1;
}

static int
lua_general_filtersets_cull(lua_State *L) {
  int rv;
  rv = noit_filtersets_cull_unused();
  if(rv > 0) mtev_conf_mark_changed();
  lua_pushinteger(L, rv);
  return 1;
}

static const luaL_Reg noit_binding[] = {
  { "register_dns_ignore_domain", nl_register_dns_ignore_domain },
  { "valid_ip", nl_valid_ip },
  { "check", nl_check },
  { "filtersets_cull", lua_general_filtersets_cull },
  { NULL, NULL }
};

LUALIB_API int luaopen_noit_binding(lua_State *L)
{
  luaL_openlib(L, "noit_binding", noit_binding, 0);
  return 1;
}

