/*
 * Copyright (c) 2013-2015, Circonus, Inc. All rights reserved.
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
 *     * Neither the name Circonus, Inc. nor the names of its contributors
 *       may be used to endorse or promote products derived from this
 *       software without specific prior written permission.
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

#include <errno.h>
#include <sys/mman.h>
#include <assert.h>
#include <lauxlib.h>

#include <mtev_dso.h>

#include "lua_http.h"
#include "lua_noit.h"

#define OO_LUA_DECL(L, type, var, methodvar) \
  type **udata, *var; \
  const char *methodvar; \
  int n; \
  n = lua_gettop(L);    /* number of arguments */ \
  assert(n == 2); \
  if(!luaL_checkudata(L, 1, #type)) { \
    luaL_error(L, "metatable error, arg1 not " #type "!"); \
  } \
  udata = lua_touserdata(L, 1); \
  var = *udata; \
  if(!lua_isstring(L, 2)) { \
    luaL_error(L, "metatable error, arg2 not a string!"); \
  } \
  methodvar = lua_tostring(L, 2)

#define CCALL_DECL(L, type, var, nargs) \
  type *var; \
  var = lua_touserdata(L, lua_upvalueindex(1)); \
  if(nargs && lua_gettop(L) != (nargs)) \
    luaL_error(L, "wrong number of arguments")

#define CCALL_NARGS(L, nargs) \
  if(nargs && lua_gettop(L) != (nargs)) \
    luaL_error(L, "wrong number of arguments")

static int
noit_lua_http_request_headers(lua_State *L) {
  mtev_hash_table *h;
  CCALL_DECL(L, mtev_http_request, req, 0);
  h = mtev_http_request_headers_table(req);
  if(lua_gettop(L) == 1)
    noit_lua_hash_to_table(L, h);
  else if(lua_gettop(L) == 2) {
    const char *hdr = lua_tostring(L,2);
    if(hdr == NULL) lua_pushnil(L);
    else {
      char *cp, *lower = alloca(strlen(hdr) + 1);
      memcpy(lower, hdr, strlen(hdr)+1);
      for(cp=lower; *cp; cp++) *cp = tolower(*cp);
      if(mtev_hash_retr_str(h, lower, strlen(lower), &hdr))
        lua_pushstring(L, hdr);
      else
        lua_pushnil(L);
    }
  }
  else luaL_error(L, "invalid arguments to mtev_http_request:headers()");
  return 1;
}
static int
noit_lua_http_request_querystring(lua_State *L) {
  mtev_hash_table *h;
  CCALL_DECL(L, mtev_http_request, req, 0);
  h = mtev_http_request_querystring_table(req);
  if(lua_gettop(L) == 1)
    noit_lua_hash_to_table(L, h);
  else if(lua_gettop(L) == 2) {
    const char *key = lua_tostring(L,2), *value;
    if(key == NULL) lua_pushnil(L);
    else {
      if(mtev_hash_retr_str(h, key, strlen(key), &value))
        lua_pushstring(L, value);
      else
        lua_pushnil(L);
    }
  }
  else luaL_error(L, "invalid arguments to mtev_http_request:querystring()");
  return 1;
}
static int
noit_lua_http_request_uri(lua_State *L) {
  CCALL_DECL(L, mtev_http_request, req, 1);
  lua_pushstring(L, mtev_http_request_uri_str(req));
  return 1;
}
static int
noit_lua_http_request_method(lua_State *L) {
  CCALL_DECL(L, mtev_http_request, req, 1);
  lua_pushstring(L, mtev_http_request_method_str(req));
  return 1;
}
static int
noit_lua_http_request_form(lua_State *L) {
  int has_arg;
  CCALL_NARGS(L, 0);
  has_arg = (lua_gettop(L) == 2);
  if(lua_gettop(L) > 2)
    luaL_error(L, "invalid arguments to mtev_http_request:form()");

  lua_getglobal(L, "noit");
  lua_getfield(L, -1, "extras");
  lua_remove(L, -2);  /* pop noit */
  lua_getfield(L, -1, "decode_form");
  lua_remove(L, -2);  /* pop extras */
  lua_pushvalue(L, 1);
  if(has_arg) lua_pushvalue(L, 2);
  lua_call(L, has_arg ? 2 : 1, 1);
  return 1;
}
static int
noit_lua_http_request_cookie(lua_State *L) {
  int has_arg;
  CCALL_NARGS(L, 0);
  has_arg = (lua_gettop(L) == 2);
  if(lua_gettop(L) > 2)
    luaL_error(L, "invalid arguments to mtev_http_request:cookie()");

  lua_getglobal(L, "noit");
  lua_getfield(L, -1, "extras");
  lua_remove(L, -2);  /* pop noit */
  lua_getfield(L, -1, "decode_cookie");
  lua_remove(L, -2);  /* pop extras */
  lua_pushvalue(L, 1);
  if(has_arg) lua_pushvalue(L, 2);
  lua_call(L, has_arg ? 2 : 1, 1);
  return 1;
}
static int
noit_lua_http_request_payload(lua_State *L) {
  const void *payload = NULL;
  int64_t size;
  CCALL_DECL(L, mtev_http_request, req, 1);
  payload = mtev_http_request_get_upload(req, &size);
  if(payload)
    lua_pushlstring(L, (char *)payload, size);
  else
    lua_pushnil(L);
  return 1;
}

#define REQ_DISPATCH(name) do { \
  if(!strcmp(k, #name)) { \
    lua_pushlightuserdata(L, req); \
    lua_pushcclosure(L, noit_lua_http_request_##name, 1); \
    return 1; \
  } \
} while(0)

static int
mtev_http_request_index_func(lua_State *L) {
  OO_LUA_DECL(L, mtev_http_request, req, k);
  switch(*k) {
    case 'c':
      REQ_DISPATCH(cookie);
      break;
    case 'f':
      REQ_DISPATCH(form);
      break;
    case 'h':
      REQ_DISPATCH(headers);
      break;
    case 'm':
      REQ_DISPATCH(method);
      break;
    case 'p':
      REQ_DISPATCH(payload);
      break;
    case 'u':
      REQ_DISPATCH(uri);
      break;
    case 'q':
      REQ_DISPATCH(querystring);
      break;
    default:
      break;
  }
  luaL_error(L, "mtev_http_request no such element: %s", k);
  return 0;
}
void
noit_lua_setup_http_request(lua_State *L,
                            mtev_http_request *req) {
  mtev_http_request **addr;
  addr = (mtev_http_request **)lua_newuserdata(L, sizeof(req));
  *addr = req;
  if(luaL_newmetatable(L, "mtev_http_request") == 1) {
    lua_pushcclosure(L, mtev_http_request_index_func, 0);
    lua_setfield(L, -2, "__index");
  }
  lua_setmetatable(L, -2);
}

static int
noit_lua_http_option_set(lua_State *L) {
  CCALL_DECL(L, mtev_http_session_ctx, http_ctx, 2);
  u_int32_t opt = lua_tointeger(L,2);
  lua_pushboolean(L, mtev_http_response_option_set(http_ctx, opt));
  return 1;
}
static int
noit_lua_http_status_set(lua_State *L) {
  const char *val;
  CCALL_DECL(L, mtev_http_session_ctx, http_ctx, 3);
  val = lua_tostring(L,3);
  val = val ? val : "YeeHaw";
  mtev_http_response_status_set(http_ctx, lua_tointeger(L,2), val);
  return 0;
}
static int
noit_lua_http_header_set(lua_State *L) {
  const char *hdr, *val;
  CCALL_DECL(L, mtev_http_session_ctx, http_ctx, 3);
  hdr = lua_tostring(L,2);
  val = lua_tostring(L,3);
  if(!hdr || !val) luaL_error(L, "invalid header or headervalue");
  mtev_http_response_header_set(http_ctx, hdr, val);
  return 0;
}
static int
noit_lua_http_flush_and_end(lua_State *L) {
  CCALL_DECL(L, mtev_http_session_ctx, http_ctx, 1);
  mtev_http_response_end(http_ctx);
  return 0;
}
static int
noit_lua_http_flush(lua_State *L) {
  CCALL_DECL(L, mtev_http_session_ctx, http_ctx, 1);
  mtev_http_response_flush(http_ctx, mtev_false);
  return 0;
}
static int
noit_lua_http_write(lua_State *L) {
  mtev_boolean status = mtev_false;
  size_t inlen;
  const char *message;
  CCALL_DECL(L, mtev_http_session_ctx, http_ctx, 2);

  message = lua_tolstring(L, 2, &inlen);
  if(message)
    status = mtev_http_response_append(http_ctx, message, inlen);
  lua_pushboolean(L, status);
  return 1;
}

static int
noit_lua_http_write_fd(lua_State *L) {
  mtev_boolean status = mtev_false;
  int fd;
  size_t size = 0;
  off_t offset = 0;
  mtev_http_session_ctx *http_ctx;
  http_ctx = lua_touserdata(L, lua_upvalueindex(1));
  if(lua_gettop(L) < 2 || lua_gettop(L) > 4)
    luaL_error(L, "wrong number of arguments");

  fd = lua_tointeger(L, 2);
  if(fd >= 0) {
    if(lua_gettop(L) >= 3) size = lua_tointeger(L, 3);
    else {
      int rv;
      struct stat st;
      while((rv = fstat(fd, &st)) == -1 && errno == EINTR);
      if(rv < 0) goto leave;
      size = st.st_size;
    }
    if(lua_gettop(L) == 4) offset = (off_t)lua_tointeger(L, 4);
    status = mtev_http_response_append_mmap(http_ctx, fd, size, MAP_SHARED, offset);
  }

 leave:
  lua_pushboolean(L, status);
  return 1;
}

static int
noit_lua_http_set_cookie(lua_State *L) {
  int i, n;
  CCALL_NARGS(L, 0);
  n = lua_gettop(L);
  if(n < 3 || n > 4)
    luaL_error(L, "invalid arguments to mtev_http_session:set_cookie()");

  lua_getglobal(L, "noit");
  lua_getfield(L, -1, "extras");
  lua_remove(L, -2);  /* pop noit */
  lua_getfield(L, -1, "set_cookie");
  lua_remove(L, -2);  /* pop extras */
  lua_pushvalue(L, 1);
  for(i = 2; i <= n; i++) lua_pushvalue(L, i);
  lua_call(L, n, 0);
  return 0;
}

static int
noit_lua_http_request_func(lua_State *L) {
  CCALL_DECL(L, mtev_http_request, req, 1);
  noit_lua_setup_http_request(L, req);
  return 1;
}

static int
noit_lua_http_url_encode(lua_State *L) {
  if(lua_gettop(L) != 2)
    luaL_error(L, "mtev_http_session:url_encode bad arguments");
  lua_getglobal(L, "noit");
  lua_getfield(L, -1, "extras");
  lua_remove(L, -2);  /* pop noit */
  lua_getfield(L, -1, "url_encode");
  lua_remove(L, -2);  /* pop extras */
  lua_pushvalue(L,2);
  lua_call(L,1,1);
  return 1;
}
static int
noit_lua_http_url_decode(lua_State *L) {
  if(lua_gettop(L) != 2)
    luaL_error(L, "mtev_http_session:url_decode bad arguments");
  lua_getglobal(L, "noit");
  lua_getfield(L, -1, "extras");
  lua_remove(L, -2);  /* pop noit */
  lua_getfield(L, -1, "url_decode");
  lua_remove(L, -2);  /* pop extras */
  lua_pushvalue(L,2);
  lua_call(L,1,1);
  return 1;
}
static int
mtev_http_ctx_index_func(lua_State *L) {
  OO_LUA_DECL(L, mtev_http_session_ctx, http_ctx, k);
  switch(*k) {
    case 'C':
      if(!strcmp(k, "CHUNKED")) {
        lua_pushinteger(L, MTEV_HTTP_CHUNKED);
        return 1;
      }
      if(!strcmp(k, "CLOSE")) {
        lua_pushinteger(L, MTEV_HTTP_CLOSE);
        return 1;
      }
      break;
    case 'D':
      if(!strcmp(k, "DEFATE")) {
        lua_pushinteger(L, MTEV_HTTP_DEFLATE);
        return 1;
      }
      break;
    case 'G':
      if(!strcmp(k, "GZIP")) {
        lua_pushinteger(L, MTEV_HTTP_GZIP);
        return 1;
      }
      break;
    case 'f':
      if(!strcmp(k, "flush")) {
        lua_pushlightuserdata(L, http_ctx);
        lua_pushcclosure(L, noit_lua_http_flush, 1);
        return 1;
      }
      if(!strcmp(k, "flush_end")) {
        lua_pushlightuserdata(L, http_ctx);
        lua_pushcclosure(L, noit_lua_http_flush_and_end, 1);
        return 1;
      }
      break;
    case 'h':
      if(!strcmp(k, "htmlentities")) {
        lua_getglobal(L, "noit");
        lua_getfield(L, -1, "utf8tohtml");
        lua_remove(L, -2);  /* pop noit */
        return 1;
      }
      if(!strcmp(k, "header")) {
        lua_pushlightuserdata(L, http_ctx);
        lua_pushcclosure(L, noit_lua_http_header_set, 1);
        return 1;
      }
      break;
    case 'o':
      if(!strcmp(k, "option")) {
        lua_pushlightuserdata(L, http_ctx);
        lua_pushcclosure(L, noit_lua_http_option_set, 1);
        return 1;
      }
      break;
    case 'r':
      if(!strcmp(k, "request")) {
        lua_pushlightuserdata(L, mtev_http_session_request(http_ctx));
        lua_pushcclosure(L, noit_lua_http_request_func, 1);
        return 1;
      }
      if(!strcmp(k, "response")) {
        return 0;
      }
      break;
    case 's':
      if(!strcmp(k, "status")) {
        lua_pushlightuserdata(L, http_ctx);
        lua_pushcclosure(L, noit_lua_http_status_set, 1);
        return 1;
      }
      if(!strcmp(k, "set_cookie")) {
        lua_pushlightuserdata(L, http_ctx);
        lua_pushcclosure(L, noit_lua_http_set_cookie, 1);
        return 1;
      }
      break;
    case 'u':
      if(!strcmp(k, "url_decode")) {
        lua_pushcclosure(L, noit_lua_http_url_decode, 0);
        return 1;
      }
      if(!strcmp(k, "url_encode")) {
        lua_pushcclosure(L, noit_lua_http_url_encode, 0);
        return 1;
      }
      break;
    case 'w':
      if(!strcmp(k, "write")) {
        lua_pushlightuserdata(L, http_ctx);
        lua_pushcclosure(L, noit_lua_http_write, 1);
        return 1;
      }
      if(!strcmp(k, "write_fd")) {
        lua_pushlightuserdata(L, http_ctx);
        lua_pushcclosure(L, noit_lua_http_write_fd, 1);
        return 1;
      }
      break;
    default:
      break;
  }
  luaL_error(L, "mtev_http_session_ctx no such element: %s", k);
  return 0;
}

void
noit_lua_setup_http_ctx(lua_State *L,
                        mtev_http_session_ctx *http_ctx) {
  mtev_http_session_ctx **addr;
  addr = (mtev_http_session_ctx **)lua_newuserdata(L, sizeof(http_ctx));
  *addr = http_ctx;
  if(luaL_newmetatable(L, "mtev_http_session_ctx") == 1) {
    lua_pushcclosure(L, mtev_http_ctx_index_func, 0);
    lua_setfield(L, -2, "__index");
  }
  lua_setmetatable(L, -2);
}
static int
noit_lua_rest_acl_func(lua_State *L) {
  const char *acl_type;
  CCALL_DECL(L, mtev_http_rest_closure_t, restc, 1);
  lua_pushboolean(L, mtev_http_rest_client_cert_auth(restc, 0, NULL));
  return 1;
}
static int
noit_lua_http_ctx_func(lua_State *L) {
  CCALL_DECL(L, mtev_http_session_ctx, http_ctx, 1);
  noit_lua_setup_http_ctx(L, http_ctx);
  return 1;
}
static int
noit_restc_index_func(lua_State *L) {
  OO_LUA_DECL(L, mtev_http_rest_closure_t, restc, k);
  switch(*k) {
    case 'a':
      if(!strcmp(k, "apply_acl")) {
        lua_pushlightuserdata(L, restc);
        lua_pushcclosure(L, noit_lua_rest_acl_func, 1);
        return 1;
      }
    case 'h':
      if(!strcmp(k, "http")) {
        lua_pushlightuserdata(L, restc->http_ctx);
        lua_pushcclosure(L, noit_lua_http_ctx_func, 1);
        return 1;
      }
      break;
    default:
      break;
  }
  luaL_error(L, "mtev_http_rest_closure_t no such element: %s", k);
  return 0;
}
void
noit_lua_setup_restc(lua_State *L,
                     mtev_http_rest_closure_t *restc) {
  mtev_http_rest_closure_t **addr;
  addr = (mtev_http_rest_closure_t **)lua_newuserdata(L, sizeof(restc));
  *addr = restc;
  if(luaL_newmetatable(L, "mtev_http_rest_closure_t") == 1) {
    lua_pushcclosure(L, noit_restc_index_func, 0);
    lua_setfield(L, -2, "__index");
  }
  lua_setmetatable(L, -2);
}

