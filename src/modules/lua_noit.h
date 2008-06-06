/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
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

int luaopen_noit(lua_State *L);
noit_lua_check_info_t *get_ci(lua_State *L);
int noit_lua_yield(noit_lua_check_info_t *ci, int nargs);
int noit_lua_resume(noit_lua_check_info_t *ci, int nargs);
void noit_lua_check_register_event(noit_lua_check_info_t *ci, eventer_t e);
void noit_lua_check_deregister_event(noit_lua_check_info_t *ci, eventer_t e,
                                     int tofree);

#endif
