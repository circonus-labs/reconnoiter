/*
 * Copyright (c) 2007-2010, OmniTI Computer Consulting, Inc.
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

#ifndef LUA_NOIT_H
#define LUA_NOIT_H

#include "noit_defines.h"

#include "noit_conf.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "utils/noit_log.h"

#include <assert.h>

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

typedef struct lua_module_closure {
  char *object;
  lua_State *lua_state;
  int object_ref;
} lua_module_closure_t;

typedef struct noit_lua_check_info {
  noit_module_t *self;
  noit_check_t *check;
  int timed_out;
  eventer_t timeout_event;
  lua_module_closure_t *lmc;
  lua_State *coro_state;
  int coro_state_ref;
  struct timeval finish_time;
  stats_t current;
  noit_hash_table *events; /* Any eventers we need to cleanup */
} noit_lua_check_info_t;

struct nl_generic_cl {
  void (*free)(void *);
};

struct nl_intcl {
  void (*free)(void *);
  noit_lua_check_info_t *ci;
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

  int sendto; /* whether this send is a sendto call */
  union {
    struct sockaddr_in sin4;
    struct sockaddr_in6 sin6;
  } address;

  lua_State *L;
};

void noit_lua_init();
void noit_lua_init_dns();
int noit_lua_dns_gc(lua_State *L);
int noit_lua_dns_index_func(lua_State *L);
int nl_dns_lookup(lua_State *L);
int luaopen_noit(lua_State *L);
int luaopen_pack(lua_State *L); /* from lua_lpack.c */
noit_lua_check_info_t *get_ci(lua_State *L);
int noit_lua_yield(noit_lua_check_info_t *ci, int nargs);
int noit_lua_resume(noit_lua_check_info_t *ci, int nargs);
void noit_lua_check_register_event(noit_lua_check_info_t *ci, eventer_t e);
void noit_lua_check_deregister_event(noit_lua_check_info_t *ci, eventer_t e,
                                     int tofree);

#endif
