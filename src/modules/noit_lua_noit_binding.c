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
#include "noit_metric_rollup.h"
#include "noit_filters.h"
#include "lua_mtev.h"
#include "lua_check.h"
#include "noit_metric_director.h"
#include "lua.h"

typedef struct lua_callback_ref{
  lua_State *L;
  int callback_reference;
} lua_callback_ref;

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
    check = noit_poller_lookup(id);
    if(check) {
      noit_lua_setup_check(L, check);
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}

static int
nl_check_ud(lua_State *L) {
  uuid_t *uuid = lua_touserdata(L, 1);

  noit_check_t *check = noit_poller_lookup(*uuid);
  if(check) {
    noit_lua_setup_check(L, check);
    return 1;
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

static int
lua_noit_metric_adjustsubscribe(lua_State *L, short bump) {
  uuid_t id;
  if(uuid_parse(lua_tostring(L,1), id)) {
    luaL_error(L, "(un)subscribe expects a uuid as the first parameter");
  }
  noit_adjust_metric_interest(id, lua_tostring(L,2), bump);
  return 0;
}

static int
lua_noit_metric_subscribe(lua_State *L) {
  return lua_noit_metric_adjustsubscribe(L, 1);
}
static int
lua_noit_metric_unsubscribe(lua_State *L) {
  return lua_noit_metric_adjustsubscribe(L, -1);
}

static int
lua_noit_checks_adjustsubscribe(lua_State *L, short bump) {
  noit_adjust_checks_interest(bump);
  return 0;
}

static int
lua_noit_checks_subscribe(lua_State *L) {
  return lua_noit_checks_adjustsubscribe(L, 1);
}
static int
lua_noit_checks_unsubscribe(lua_State *L) {
  return lua_noit_checks_adjustsubscribe(L, -1);
}

static int noit_metric_id_index_func(lua_State *L) {
  int n;
  const char *k;
  noit_metric_id_t **udata, *metric_id;
  n = lua_gettop(L); /* number of arguments */
  mtevAssert(n == 2);
  if(!luaL_checkudata(L, 1, "metric_id_t")) {
    luaL_error(L, "metatable error, arg1 not a metric_id_t!");
    return 1;
  }
  udata = lua_touserdata(L, 1);
  metric_id = *udata;
  if(!lua_isstring(L, 2)) {
    luaL_error(L, "metatable error, arg2 not a string!");
    return 1;
  }

  k = lua_tostring(L, 2);
  switch (*k) {
    case 'i':
      if(!strcmp(k, "id")) {
        char uuid_str[UUID_PRINTABLE_STRING_LENGTH];
        uuid_unparse_lower(metric_id->id, uuid_str);
        lua_pushstring(L, uuid_str);
      } else if(!strcmp(k, "id_ud")) {
        lua_pushlightuserdata(L, (void*)&metric_id->id);
      } else {
        break;
      }
      return 1;
    case 'n':
      if(!strcmp(k, "name")) {
        lua_pushlstring(L, metric_id->name, metric_id->name_len);
      } else if(!strcmp(k, "name_len")) {
        lua_pushinteger(L, metric_id->name_len);
      } else {
        break;
      }
      return 1;
    default:
      break;
  }
  luaL_error(L, "metric_id_t no such element: %s", k);
  return 0;
}

static void
noit_lua_setup_metric_id(lua_State *L,
    noit_metric_id_t *msg) {
  noit_metric_id_t **addr;
  addr = (noit_metric_id_t **)lua_newuserdata(L, sizeof(msg));
  *addr = msg;
  if(luaL_newmetatable(L, "metric_id_t") == 1) {
    lua_pushcclosure(L, noit_metric_id_index_func, 0);
    lua_setfield(L, -2, "__index");
  }
  lua_setmetatable(L, -2);
}

static int
noit_message_index_func(lua_State *L) {
  int n;
  const char *k;
  noit_metric_message_t **udata, *msg;
  n = lua_gettop(L);    /* number of arguments */
  mtevAssert(n == 2);
  if(!luaL_checkudata(L, 1, "metric_message_t")) {
    luaL_error(L, "metatable error, arg1 not a metric_message_t!");
    return 1;
  }
  udata = lua_touserdata(L, 1);
  msg = *udata;
  if(!lua_isstring(L, 2)) {
    luaL_error(L, "metatable error, arg2 not a string!");
    return 1;
  }

  k = lua_tostring(L, 2);
  switch (*k) {
    case 'i':
      if(!strcmp(k, "id")) {
        noit_lua_setup_metric_id(L, &msg->id);
      } else {
        break;
      }
      return 1;
    case 'w':
      if(!strcmp(k, "whence_ms")) {
        lua_pushinteger(L, msg->value.whence_ms);
      } else {
        break;
      }
      return 1;
    case 'v':
      if(!strcmp(k, "value")) {
        lua_pushlightuserdata(L, (void*)&msg->value);
      } else {
        break;
      }
      return 1;
    case 't':
      if(!strcmp(k, "type")) {
        lua_pushinteger(L, msg->type);
      } else {
        break;
      }
      return 1;
    case 'o':
      if(!strcmp(k, "original_message")) {
        lua_pushstring(L, msg->original_message);
      } else {
        break;
      }
      return 1;
    default:
      break;
  }
  luaL_error(L, "noit_check_t no such element: %s", k);
  return 0;
}

static int
noit_lua_free_message(lua_State *L) {
  const char *k;
  noit_metric_message_t **udata;
  if(!luaL_checkudata(L, 1, "metric_message_t")) {
    luaL_error(L, "metatable error, arg1 not a metric_message_t!");
    return 1;
  }
  udata = lua_touserdata(L, 1);

  noit_metric_director_message_deref(*udata);
  return 0;
}

static void
noit_lua_setup_message(lua_State *L,
    noit_metric_message_t *msg) {
  noit_metric_message_t **addr;
  addr = (noit_metric_message_t **)lua_newuserdata(L, sizeof(msg));
  *addr = msg;
  if(luaL_newmetatable(L, "metric_message_t") == 1) {
    lua_pushcclosure(L, noit_message_index_func, 0);
    lua_setfield(L, -2, "__index");

    lua_pushcclosure(L, noit_lua_free_message, 0);
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);
}

static int
lua_noit_metric_next(lua_State *L) {
  noit_metric_message_t *msg;
  msg = noit_metric_director_lane_next();
  if(msg) {
    noit_lua_setup_message(L, msg);
    return 1;
  }
  return 0;
}

static int
lua_noit_poller_cb(noit_check_t * check, void *closure) {
  lua_callback_ref *cb_ref = closure;

  lua_rawgeti( cb_ref->L, LUA_REGISTRYINDEX, cb_ref->callback_reference );

  noit_lua_setup_check(cb_ref->L, check);
  lua_call(cb_ref->L, 1, 0);

  return 1;
}

static int
lua_noit_check_do(lua_State *L) {
  if (lua_gettop(L) == 1 && // make sure exactly one argument is passed
     lua_isfunction(L, -1)) // and that argument (which is on top of the stack) is a function
  {
    lua_callback_ref cb_ref;
    cb_ref.L = L;
    cb_ref.callback_reference = luaL_ref( L, LUA_REGISTRYINDEX );

    noit_poller_do(lua_noit_poller_cb, &cb_ref);
    luaL_unref(L, LUA_REGISTRYINDEX, cb_ref.callback_reference);
  } else {
    luaL_error(L, "bad parameters to noit.metric_poller_do");
  }
  return 0;
}

static const luaL_Reg noit_binding[] = {
  { "register_dns_ignore_domain", nl_register_dns_ignore_domain },
  { "valid_ip", nl_valid_ip },
  { "check", nl_check },
  { "check_ud", nl_check_ud },
  { "filtersets_cull", lua_general_filtersets_cull },
  { "metric_director_subscribe_checks", lua_noit_checks_subscribe },
  { "metric_director_unsubscribe_checks", lua_noit_checks_unsubscribe },
  { "metric_director_subscribe", lua_noit_metric_subscribe },
  { "metric_director_unsubscribe", lua_noit_metric_unsubscribe },
  { "metric_director_next", lua_noit_metric_next },
  { "checks_do", lua_noit_check_do },
  { NULL, NULL }
};

LUALIB_API int luaopen_noit_binding(lua_State *L)
{
  luaL_openlib(L, "noit_binding", noit_binding, 0);
  return 1;
}

