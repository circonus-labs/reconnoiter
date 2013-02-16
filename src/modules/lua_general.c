/*
 * Copyright (c) 2013, Circonus, Inc.
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

#include "noit_defines.h"
#include "noit_module.h"
#include "noit_http.h"
#include "noit_rest.h"
#include "lua_noit.h"
#include "lua_http.h"
#include <assert.h>

#define lua_general_xml_description  ""
static const char *script_dir = "";

typedef struct lua_general_conf {
  lua_module_closure_t lmc;
  const char *script_dir;
  const char *module;
  const char *function;
  lua_State *L;
} lua_general_conf_t;

static lua_general_conf_t *get_config(noit_module_generic_t *self) {
  lua_general_conf_t *conf = noit_image_get_userdata(&self->hdr);
  if(conf) return conf;
  conf = calloc(1, sizeof(*conf));
  noit_image_set_userdata(&self->hdr, conf);
  return conf;
}

static void
lua_general_ctx_free(void *cl) {
  noit_lua_resume_info_t *ri = cl;
  if(ri) {
    noitL(noit_error, "lua_general(%p) -> stopping job (%p)\n",
          ri->lmc->lua_state, ri->coro_state);
    noit_lua_cancel_coro(ri);
    noit_lua_resume_clean_events(ri);
    free(ri);
  }
}

static int
lua_general_resume(noit_lua_resume_info_t *ri, int nargs) {
  const char *err = NULL;
  int status, base, rv = 0;

  status = lua_resume(ri->coro_state, nargs);

  switch(status) {
    case 0: break;
    case LUA_YIELD:
      lua_gc(ri->coro_state, LUA_GCCOLLECT, 0);
      return 0;
    default: /* The complicated case */
      base = lua_gettop(ri->coro_state);
      if(base>=0) {
        if(lua_isstring(ri->coro_state, base-1)) {
          err = lua_tostring(ri->coro_state, base-1);
          noitL(noit_error, "err -> %s\n", err);
        }
      }
      rv = -1;
  }

  lua_general_ctx_free(ri);
  return rv;
}

static noit_lua_resume_info_t *
lua_general_new_resume_info(lua_module_closure_t *lmc) {
  noit_lua_resume_info_t *ri;
  ri = calloc(1, sizeof(*ri));
  ri->context_magic = 0;
  ri->lmc = lmc;
  lua_getglobal(lmc->lua_state, "noit_coros");
  ri->coro_state = lua_newthread(lmc->lua_state);
  ri->coro_state_ref = luaL_ref(lmc->lua_state, -2);
  noit_lua_set_resume_info(lmc->lua_state, ri);
  lua_pop(lmc->lua_state, 1); /* pops noit_coros */
  noitL(noit_error, "lua_general(%p) -> starting new job (%p)\n",
        lmc->lua_state, ri->coro_state);
  return ri;
}

static int
lua_general_handler(noit_module_generic_t *self) {
  int status, rv;
  lua_general_conf_t *conf = get_config(self);
  lua_module_closure_t *lmc = &conf->lmc;
  noit_lua_resume_info_t *ri = NULL;;
  const char *err = NULL;
  char errbuf[128];
  lua_State *L;

  if(!lmc || !conf || !conf->module || !conf->function) {
    goto boom;
  }
  ri = lua_general_new_resume_info(lmc);
  L = ri->coro_state;

  lua_getglobal(L, "require");
  lua_pushstring(L, conf->module);
  rv = lua_pcall(L, 1, 1, 0);
  if(rv) {
    int i;
    noitL(noit_error, "lua: require %s failed\n", conf->module);
    i = lua_gettop(L);
    if(i>0) {
      if(lua_isstring(L, i)) {
        const char *err;
        size_t len;
        err = lua_tolstring(L, i, &len);
        noitL(noit_error, "lua: %s\n", err);
      }
    }
    lua_pop(L,i);
    goto boom;
  }
  lua_pop(L, lua_gettop(L));

  noit_lua_pushmodule(L, conf->module);
  if(lua_isnil(L, -1)) {
    lua_pop(L, 1);
    snprintf(errbuf, sizeof(errbuf), "no such module: '%s'", conf->module);
    err = errbuf;
    goto boom;
  }
  lua_getfield(L, -1, conf->function);
  lua_remove(L, -2);
  if(!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    snprintf(errbuf, sizeof(errbuf), "no function '%s' in module '%s'",
             conf->function, conf->module);
    err = errbuf;
    goto boom;
  }

  status = lmc->resume(ri, 0);
  if(status == 0) return 0;

 boom:
  if(ri) lua_general_ctx_free(ri);
  if(err) noitL(noit_error, "lua dispatch error: %s\n", err);
  return 0;
}

static int
lua_general_coroutine_spawn(lua_State *Lp) {
  int nargs;
  lua_State *L;
  noit_lua_resume_info_t *ri_parent = NULL, *ri = NULL;

  nargs = lua_gettop(Lp);
  if(nargs < 1 || !lua_isfunction(Lp,1))
    luaL_error(Lp, "noit.coroutine_spawn(func, ...): bad arguments");
  ri_parent = noit_lua_get_resume_info(Lp);
  assert(ri_parent);

  ri = lua_general_new_resume_info(ri_parent->lmc);
  L = ri->coro_state;
  lua_xmove(Lp, L, nargs);
  lua_setlevel(Lp, L);
  ri->lmc->resume(ri, nargs-1);
  return 0;
}

int
dispatch_general(eventer_t e, int mask, void *cl, struct timeval *now) {
  return lua_general_handler((noit_module_generic_t *)cl);
}

static int
noit_lua_general_config(noit_module_generic_t *self, noit_hash_table *o) {
  lua_general_conf_t *conf = get_config(self);
  conf->script_dir = "";
  conf->module = NULL;
  conf->function = NULL;
  noit_hash_retr_str(o, "directory", strlen("directory"), &conf->script_dir);
  if(conf->script_dir) conf->script_dir = strdup(conf->script_dir);
  noit_hash_retr_str(o, "lua_module", strlen("lua_module"), &conf->module);
  if(conf->module) conf->module = strdup(conf->module);
  noit_hash_retr_str(o, "lua_function", strlen("lua_function"), &conf->function);
  if(conf->function) conf->function = strdup(conf->function);
  return 0;
}

static const luaL_reg general_lua_funcs[] =
{
  {"coroutine_spawn", lua_general_coroutine_spawn },
  {NULL,  NULL}
};


static int
noit_lua_general_init(noit_module_generic_t *self) {
  lua_general_conf_t *conf = get_config(self);
  lua_module_closure_t *lmc = &conf->lmc;

  if(!conf->module || !conf->function) {
    noitL(noit_error, "lua_general cannot be used without module and function config\n");
    return -1;
  }

  lmc->resume = lua_general_resume;
  lmc->lua_state = noit_lua_open(self->hdr.name, lmc, conf->script_dir);
  noitL(noit_error, "lua_general opening state -> %p\n", lmc->lua_state);
  luaL_openlib(lmc->lua_state, "noit", general_lua_funcs, 0);
  if(lmc->lua_state == NULL) {
   noitL(noit_error, "lua_general could not add general functions\n");
    return -1;
  }
  lmc->pending = calloc(1, sizeof(*lmc->pending));
  eventer_add_in_s_us(dispatch_general, self, 0, 0);
  return 0;
}

static int
noit_lua_general_onload(noit_image_t *self) {
  return 0;
}

noit_module_generic_t lua_general = {
  {
    NOIT_GENERIC_MAGIC,
    NOIT_GENERIC_ABI_VERSION,
    "lua_general",
    "general services in lua",
    lua_general_xml_description,
    noit_lua_general_onload
  },
  noit_lua_general_config,
  noit_lua_general_init
};
