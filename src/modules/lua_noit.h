/*
 * Copyright (c) 2007-2010, OmniTI Computer Consulting, Inc.
 * All rights reserved.
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

#ifndef LUA_NOIT_H
#define LUA_NOIT_H

#include <mtev_defines.h>

#include <assert.h>
#include <openssl/x509.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <mtev_conf.h>
#include <mtev_rest.h>
#include <mtev_log.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"

typedef struct noit_lua_resume_info noit_lua_resume_info_t;

typedef struct lua_module_closure {
  lua_State *lua_state;
  mtev_hash_table *pending;
  int (*resume)(noit_lua_resume_info_t *ci, int nargs);
  pthread_t owner;
} lua_module_closure_t;

struct noit_lua_resume_info {
  pthread_t bound_thread;
  lua_module_closure_t *lmc;
  lua_State *coro_state;
  int coro_state_ref;
  mtev_hash_table *events; /* Any eventers we need to cleanup */
  int context_magic;
  void *context_data;
  noit_check_t *check; /* If this is null, we're in a daemonized coroutine */
};

#define LUA_GENERAL_INFO_MAGIC 0x918243fa

typedef struct noit_lua_resume_check_info {
  noit_module_t *self;
  noit_check_t *check;
  noit_check_t *cause;
  int timed_out;
  eventer_t timeout_event;
  struct timeval finish_time;
} noit_lua_resume_check_info_t;
#define LUA_CHECK_INFO_MAGIC 0x22113322

typedef struct noit_lua_resume_rest_info {
  mtev_http_rest_closure_t *restc;
  char *err;
  int httpcode;
} noit_lua_resume_rest_info_t;
#define LUA_REST_INFO_MAGIC 0x80443000

struct nl_generic_cl {
  void (*free)(void *);
};

struct nl_intcl {
  void (*free)(void *);
  noit_lua_resume_info_t *ri;
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

void noit_lua_init();
lua_State *noit_lua_open(const char *module_name, void *lmc,
                         const char *script_dir);
void noit_lua_cancel_coro(noit_lua_resume_info_t *ci);
void noit_lua_resume_clean_events(noit_lua_resume_info_t *ci);
void noit_lua_pushmodule(lua_State *L, const char *m);
int noit_lua_check_resume(noit_lua_resume_info_t *ri, int nargs);
void noit_lua_init_dns();
mtev_hash_table *noit_lua_table_to_hash(lua_State *L, int idx);
void noit_lua_hash_to_table(lua_State *L, mtev_hash_table *t);
int noit_lua_dns_gc(lua_State *L);
int noit_lua_dns_index_func(lua_State *L);
int nl_dns_lookup(lua_State *L);
int luaopen_noit(lua_State *L);
int noit_lua_crypto_newx509(lua_State *L, X509 *x509);
int noit_lua_crypto_new_ssl_session(lua_State *L, SSL_SESSION *sess);
int luaopen_crypto(lua_State *L);
int luaopen_pack(lua_State *L); /* from lua_lpack.c */
int luaopen_bit(lua_State *L); /* from lua_bit.c */
int luaopen_snmp(lua_State *L); /* from lua_snmp.c */
void noit_lua_setup_check(lua_State *L, noit_check_t *check);
noit_lua_resume_info_t *noit_lua_get_resume_info(lua_State *L);
void noit_lua_set_resume_info(lua_State *L, noit_lua_resume_info_t *ri);
int noit_lua_yield(noit_lua_resume_info_t *ci, int nargs);
void noit_lua_check_register_event(noit_lua_resume_info_t *ci, eventer_t e);
void noit_lua_check_deregister_event(noit_lua_resume_info_t *ci, eventer_t e,
                                     int tofree);

#endif
