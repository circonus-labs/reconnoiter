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
#include "noit_metric_rollup.h"
#include "lua_mtev.h"
#include "noit_metric_director.h"
#include "lua.h"
#include "noit_metric_tag_search.h"


static int
lua_noit_metric_adjustsubscribe(lua_State *L, short bump) {
  uuid_t id;
  if(mtev_uuid_parse(lua_tostring(L,1), id)) {
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
        mtev_uuid_unparse_lower(metric_id->id, uuid_str);
        lua_pushstring(L, uuid_str);
      } else if(!strcmp(k, "id_ud")) {
        lua_pushlightuserdata(L, (void*)&metric_id->id);
      } else {
        break;
      }
      return 1;
    case 'n':
      if(!strcmp(k, "name")) {
        lua_pushlstring(L, metric_id->name, metric_id->name_len_with_tags);
      } else if(!strcmp(k, "name_len")) {
        lua_pushinteger(L, metric_id->name_len_with_tags);
      } else if(!strcmp(k, "name_without_tags")) {
        lua_pushlstring(L, metric_id->name, metric_id->name_len);
      } else if(!strcmp(k, "name_len_without_tags")) {
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
  luaL_error(L, "metric_message_t no such element: %s", k);
  return 0;
}

static int
noit_lua_free_message(lua_State *L) {
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
lua_noit_metric_messages_received(lua_State *L) {
  lua_pushinteger(L, noit_metric_director_get_messages_received());
  return 1;
}

static int
lua_noit_metric_messages_distributed(lua_State *L) {
  lua_pushinteger(L, noit_metric_director_get_messages_distributed());
  return 1;
}

static int
noit_lua_tag_search_ast_free(lua_State *L) {
  noit_metric_tag_search_ast_t **udata = (noit_metric_tag_search_ast_t **)
    luaL_checkudata(L, 1, "noit_metric_tag_search_ast_t");
  noit_metric_tag_search_free(*udata);
  return 0;
}

static int
noit_lua_tag_search_ast_setup(lua_State *L, noit_metric_tag_search_ast_t *ast) {
  noit_metric_tag_search_ast_t **udata = (noit_metric_tag_search_ast_t **) lua_newuserdata(L, sizeof(ast));
  *udata = ast;
  if(luaL_newmetatable(L, "noit_metric_tag_search_ast_t") == 1){
    lua_pushcclosure(L, noit_lua_tag_search_ast_free, 0);
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);
  return 0;
}

// Parse tag search query string into a tag-search ast
// param: query
// retuns: ast
static int
lua_noit_tag_search_parse(lua_State *L) {
  const char *query = luaL_checkstring(L, 1);
  noit_metric_tag_search_ast_t *ast;
  int erroroff;
  ast = noit_metric_tag_search_parse(query, &erroroff);
  if (ast == NULL) {
    luaL_error(L, "Error parsing tag_search query (%s) at position %d", query, erroroff);
    return 0;
  }
  noit_lua_tag_search_ast_setup(L, ast);
  return 1;
}

static int
noit_lua_tagset_copy_free(lua_State *L) {
  noit_metric_tagset_t **udata = (noit_metric_tagset_t **)
    luaL_checkudata(L, 1, "noit_metric_tagset_t");
  noit_metric_tagset_t *set = *udata;
  free(set->tags);
  free(set);
  return 0;
}

static int
noit_lua_tagset_copy_setup(lua_State *L, noit_metric_tagset_t *src_set) {
  noit_metric_tagset_t *dst_set = calloc(1, sizeof(*src_set));
  memcpy(dst_set, src_set, sizeof(*dst_set));
  noit_metric_tag_t *dst_tags = calloc(src_set->tag_count, sizeof(noit_metric_tag_t));
  memcpy(dst_tags, src_set->tags, src_set->tag_count*sizeof(noit_metric_tag_t));
  dst_set->tags = dst_tags;
  noit_metric_tagset_t **udata = (noit_metric_tagset_t **) lua_newuserdata(L, sizeof(dst_set));
  *udata = dst_set;
  if(luaL_newmetatable(L, "noit_metric_tagset_t") == 1){
    lua_pushcclosure(L, noit_lua_tagset_copy_free, 0);
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);
  return 0;
}

// Convert a tagset to strings
// param: tag
// returns: tag1, tag2, ...
static int
lua_noit_tag_tostring(lua_State *L) {
  noit_metric_tagset_t **udata = (noit_metric_tagset_t **)
    luaL_checkudata(L, 1, "noit_metric_tagset_t");
  noit_metric_tagset_t *set = *udata;
  int cnt = set->tag_count;
  for (int i=0; i<cnt; i++) {
    noit_metric_tag_t tag = set->tags[i];
    lua_pushlstring(L, tag.tag, tag.total_size);
  }
  return cnt;
}

// Parse a metric name to a tagset
// param: name
// returns: stags, mtags (userdata)
static int
lua_noit_tag_parse(lua_State *L) {
  const char* name = luaL_checkstring(L, 1);
  noit_metric_tag_t stags[MAX_TAGS], mtags[MAX_TAGS];
  noit_metric_tagset_t stset = { .tags = stags, .tag_count = MAX_TAGS };
  noit_metric_tagset_t mtset = { .tags = mtags, .tag_count = MAX_TAGS };
  int rc = noit_metric_parse_tags(name, strlen(name), &stset, &mtset);
  if (rc < 0) {
    luaL_error(L, "Error parsing tags for metric %s", name);
    return 0;
  }
  noit_lua_tagset_copy_setup(L, &stset);
  noit_lua_tagset_copy_setup(L, &mtset);
  return 2;
}

// Eval tag search query against tagset
// param: ast (userdata)
// param: tagset (userdata)
// returns: matches (boolean)
static int
lua_noit_tag_search_eval(lua_State *L) {
  noit_metric_tag_search_ast_t **ast_ud = (noit_metric_tag_search_ast_t **)
    luaL_checkudata(L, 1, "noit_metric_tag_search_ast_t");
  noit_metric_tagset_t **set_ud = (noit_metric_tagset_t **)
    luaL_checkudata(L, 2, "noit_metric_tagset_t");
  mtev_boolean ok = noit_metric_tag_search_evaluate_against_tags(*ast_ud, *set_ud);
  lua_pushboolean(L, ok);
  return 1;
}

// Eval tag search query against string
// param: ast (userdata)
// param: name (string)
// returns: matches (boolean)
static int
lua_noit_tag_search_eval_string(lua_State *L) {
  noit_metric_tag_search_ast_t **ast_ud = (noit_metric_tag_search_ast_t **)
    luaL_checkudata(L, 1, "noit_metric_tag_search_ast_t");
  const char* name = luaL_checkstring(L, 2);
  noit_metric_tag_t stags[MAX_TAGS], mtags[MAX_TAGS];
  noit_metric_tagset_t stset = { .tags = stags, .tag_count = MAX_TAGS };
  noit_metric_tagset_t mtset = { .tags = mtags, .tag_count = MAX_TAGS };
  int rc = noit_metric_parse_tags(name, strlen(name), &stset, &mtset);
  if (rc < 0) {
    luaL_error(L, "Error parsing tagset for metric %s", name);
    return 0;
  }
  mtev_boolean ok = noit_metric_tag_search_evaluate_against_tags(*ast_ud, &mtset);
  lua_pushboolean(L, ok);
  return 1;
}

#ifndef NO_LUAOPEN_LIBNOIT
static const luaL_Reg libnoit_binding[] = {
  { "metric_director_subscribe_checks", lua_noit_checks_subscribe },
  { "metric_director_unsubscribe_checks", lua_noit_checks_unsubscribe },
  { "metric_director_subscribe", lua_noit_metric_subscribe },
  { "metric_director_unsubscribe", lua_noit_metric_unsubscribe },
  { "metric_director_next", lua_noit_metric_next },
  { "metric_director_get_messages_received", lua_noit_metric_messages_received },
  { "metric_director_get_messages_distributed", lua_noit_metric_messages_distributed},
  { "tag_parse", lua_noit_tag_parse},
  { "tag_tostring", lua_noit_tag_tostring},
  { "tag_search_parse", lua_noit_tag_search_parse},
  { "tag_search_eval", lua_noit_tag_search_eval},
  { "tag_search_eval_string", lua_noit_tag_search_eval_string},
  { NULL, NULL }
};

LUALIB_API int luaopen_libnoit_binding(lua_State *L)
{
  luaL_openlib(L, "libnoit_binding", libnoit_binding, 0);
  return 1;
}
#endif
