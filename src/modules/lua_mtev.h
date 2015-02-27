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

#ifndef LUA_MTEV_H
#define LUA_MTEV_H

#include <mtev_defines.h>

#include <assert.h>
#include <openssl/x509.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <mtev_conf.h>
#include <mtev_rest.h>
#include <mtev_log.h>

typedef struct mtev_lua_resume_info mtev_lua_resume_info_t;

typedef struct lua_module_closure {
  lua_State *lua_state;
  mtev_hash_table *pending;
  int (*resume)(mtev_lua_resume_info_t *ci, int nargs);
  pthread_t owner;
} lua_module_closure_t;

struct mtev_lua_resume_info {
  pthread_t bound_thread;
  lua_module_closure_t *lmc;
  lua_State *coro_state;
  int coro_state_ref;
  mtev_hash_table *events; /* Any eventers we need to cleanup */
  int context_magic;
  void *context_data;
};
#define LUA_GENERAL_INFO_MAGIC 0x918243fa

typedef struct mtev_lua_resume_rest_info {
  mtev_http_rest_closure_t *restc;
  char *err;
  int httpcode;
} mtev_lua_resume_rest_info_t;
#define LUA_REST_INFO_MAGIC 0x80443000

struct nl_generic_cl {
  void (*free)(void *);
};

struct nl_intcl {
  void (*free)(void *);
  mtev_lua_resume_info_t *ri;
};

struct nl_slcl {
  void (*free)(void *);
  int send_size;
  struct timeval start;
  char *inbuff;
  int   inbuff_allocd;
  int   inbuff_len;
  size_t read_sofar;
  size_t read_goal;
  const char *read_terminator;
  const char *outbuff;
  size_t write_sofar;
  size_t write_goal;
  eventer_t *eptr;
  eventer_t pending_event;

  int sendto; /* whether this send is a sendto call */
  union {
    struct sockaddr_in sin4;
    struct sockaddr_in6 sin6;
  } address;

  lua_State *L;
};

void mtev_lua_context_describe(int magic,
                               void (*f)(mtev_console_closure_t,
                                         mtev_lua_resume_info_t *));
const char *mtev_lua_type_name(int);
lua_State *mtev_lua_open(const char *module_name, void *lmc,
                         const char *script_dir, const char *cpath);
void register_console_lua_commands();
void mtev_lua_new_coro(mtev_lua_resume_info_t *);
void mtev_lua_cancel_coro(mtev_lua_resume_info_t *ci);
void mtev_lua_resume_clean_events(mtev_lua_resume_info_t *ci);
void mtev_lua_pushmodule(lua_State *L, const char *m);
void mtev_lua_init_dns();
mtev_hash_table *mtev_lua_table_to_hash(lua_State *L, int idx);
void mtev_lua_hash_to_table(lua_State *L, mtev_hash_table *t);
int mtev_lua_dns_gc(lua_State *L);
int mtev_lua_dns_index_func(lua_State *L);
int nl_dns_lookup(lua_State *L);
int luaopen_mtev(lua_State *L);
int mtev_lua_crypto_newx509(lua_State *L, X509 *x509);
int mtev_lua_crypto_new_ssl_session(lua_State *L, SSL_SESSION *sess);
int luaopen_crypto(lua_State *L);
int luaopen_pack(lua_State *L); /* from lua_lpack.c */
int luaopen_bit(lua_State *L); /* from lua_bit.c */
mtev_lua_resume_info_t *mtev_lua_get_resume_info(lua_State *L);
void mtev_lua_set_resume_info(lua_State *L, mtev_lua_resume_info_t *ri);
int mtev_lua_yield(mtev_lua_resume_info_t *ci, int nargs);
void mtev_lua_register_event(mtev_lua_resume_info_t *ci, eventer_t e);
void mtev_lua_deregister_event(mtev_lua_resume_info_t *ci, eventer_t e,
                                     int tofree);

#define require(L, rv, a) do { \
  lua_getglobal(L, "require"); \
  lua_pushstring(L, #a); \
  rv = lua_pcall(L, 1, 1, 0); \
  if(rv != 0) { \
    mtevL(mtev_stderr, "Loading %s: %d (%s)\n", #a, rv, lua_tostring(L,-1)); \
    lua_close(L); \
    return NULL; \
  } \
  lua_pop(L, 1); \
} while(0)

#define SETUP_CALL(L, object, func, failure) do { \
  mtevL(nldeb, "lua calling %s->%s\n", object, func); \
  mtev_lua_pushmodule(L, object); \
  lua_getfield(L, -1, func); \
  lua_remove(L, -2); \
  if(!lua_isfunction(L, -1)) { \
    lua_pop(L, 1); \
    failure; \
  } \
} while(0)

#define RETURN_INT(L, object, func, expr) do { \
  int base = lua_gettop(L); \
  assert(base == 1); \
  if(lua_isnumber(L, -1)) { \
    int rv; \
    rv = lua_tointeger(L, -1); \
    lua_pop(L, 1); \
    expr \
    return rv; \
  } \
  mtevL(nlerr, "%s.%s must return a integer not %s (%s)\n", object, func, mtev_lua_type_name(lua_type(L,-1)), lua_tostring(L,-1)); \
  lua_pop(L,1); \
} while(0)

#endif
