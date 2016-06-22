/*
 * Copyright (c) 2015, Circonus, Inc.  All rights reserved.
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

#include <unistd.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#include <assert.h>

#include <mtev_conf.h>
#include <mtev_dso.h>
#include <mtev_log.h>

#include "noit_config.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_mtev_bridge.h"

#include <lua_mtev.h>
#include "lua_check.h"

static pthread_t loader_main_thread;
static mtev_log_stream_t nlerr = NULL;
static mtev_log_stream_t nldeb = NULL;

#define LMC_DECL(L, mod, object) \
  lua_State *L; \
  lua_module_closure_t *lmc; \
  const char *object; \
  lmc = noit_lua_setup_lmc(mod, &object); \
  L = lmc->lua_state

struct loader_conf {
  pthread_key_t key;
  char *script_dir;
  char *cpath;
};
struct module_conf {
  struct loader_conf *c;
  char *object;
  mtev_hash_table *options;
  pthread_key_t key;
};
struct module_tls_conf {
  int loaded;
  int configured;
  int configured_return;
  int initialized;
  int initialized_return;
};

static struct module_tls_conf *__get_module_tls_conf(mtev_image_t *img) {
  struct module_conf *mc;
  struct module_tls_conf *mtlsc;
  mc = mtev_image_get_userdata(img);
  mtlsc = pthread_getspecific(mc->key);
  if(mtlsc == NULL) {
    mtlsc = calloc(1, sizeof(*mtlsc));
    pthread_setspecific(mc->key, mtlsc);
  }
  return mtlsc;
}
static struct loader_conf *__get_loader_conf(mtev_dso_loader_t *self) {
  struct loader_conf *c;
  c = mtev_image_get_userdata(&self->hdr);
  if(!c) {
    c = calloc(1, sizeof(*c));
    pthread_key_create(&c->key, NULL);
    mtev_image_set_userdata(&self->hdr, c);
  }
  return c;
}
static lua_module_closure_t *lmc_tls_get(mtev_image_t *mod) {
  lua_module_closure_t *lmc;
  struct loader_conf *c;
  if(mod->magic == MTEV_LOADER_MAGIC) {
    c = mtev_image_get_userdata(mod);
  }
  else {
    struct module_conf *mc;
    mc = mtev_image_get_userdata(mod);
    c = mc->c;
  }
  lmc = pthread_getspecific(c->key);
  return lmc;
}

static lua_module_closure_t *noit_lua_setup_lmc(noit_module_t *mod, const char **obj) {
  lua_module_closure_t *lmc;
  struct module_conf *mc;
  struct module_tls_conf *mtlsc;
  mc = mtev_image_get_userdata(&mod->hdr);
  if(obj) *obj = mc->object;
  lmc = pthread_getspecific(mc->c->key);
  if(lmc == NULL) {
    int rv;
    lmc = calloc(1, sizeof(*lmc));
    lmc->pending = calloc(1, sizeof(*lmc->pending));
    lmc->owner = pthread_self();
    lmc->resume = noit_lua_check_resume;
    pthread_setspecific(mc->c->key, lmc);
    mtevL(nldeb, "lua_state[%s]: new state\n", mod->hdr.name);
    lmc->lua_state = mtev_lua_open(mod->hdr.name, lmc,
                                   mc->c->script_dir, mc->c->cpath);
    require(lmc->lua_state, rv, noit);
  }
  mtlsc = __get_module_tls_conf(&mod->hdr);
  if(!mtlsc->loaded) {
    if(mod->hdr.onload(&mod->hdr) == -1) {
      return NULL;
    }
    mtlsc->loaded = 1;
  }
  mtevL(nldeb, "lua_state[%s]: %p\n", mod->hdr.name, lmc->lua_state);
  return lmc;
}

static int
noit_lua_module_set_description(lua_State *L) {
  noit_module_t *module;
  module = lua_touserdata(L, lua_upvalueindex(1));
  if(lua_gettop(L) == 1) {
    if(pthread_equal(pthread_self(), loader_main_thread)) {
      if(module->hdr.description) free(module->hdr.description);
      module->hdr.description = strdup(lua_tostring(L, 1));
    }
  }
  else if(lua_gettop(L) > 1)
    luaL_error(L, "wrong number of arguments");
  lua_pushstring(L, module->hdr.description);
  return 1;
}
static int
noit_lua_module_set_name(lua_State *L) {
  noit_module_t *module;
  module = lua_touserdata(L, lua_upvalueindex(1));
  if(lua_gettop(L) == 1) {
    if(pthread_equal(pthread_self(), loader_main_thread)) {
      if(module->hdr.name) free(module->hdr.name);
      module->hdr.name = strdup(lua_tostring(L, 1));
    }
  }
  else if(lua_gettop(L) > 1)
    luaL_error(L, "wrong number of arguments");
  lua_pushstring(L, module->hdr.name);
  return 1;
}
static int
noit_lua_module_set_xml_description(lua_State *L) {
  noit_module_t *module;
  module = lua_touserdata(L, lua_upvalueindex(1));
  if(lua_gettop(L) == 1) {
    if(pthread_equal(pthread_self(), loader_main_thread)) {
      if(module->hdr.xml_description) free(module->hdr.xml_description);
      module->hdr.xml_description = strdup(lua_tostring(L, 1));
    }
  }
  else if(lua_gettop(L) > 1)
    luaL_error(L, "wrong number of arguments");
  lua_pushstring(L, module->hdr.xml_description);
  return 1;
}
static int
noit_module_index_func(lua_State *L) {
  noit_module_t **udata, *module;
  const char *k;
  int n;
  n = lua_gettop(L);    /* number of arguments */
  assert(n == 2);
  if(!luaL_checkudata(L, 1, "noit_module_t")) {
    luaL_error(L, "metatable error, arg1 not a noit_module_t!");
  }
  udata = lua_touserdata(L, 1);
  module = *udata;
  if(!lua_isstring(L, 2)) {
    luaL_error(L, "metatable error, arg2 not a string!");
  }
  k = lua_tostring(L, 2);
  switch(*k) {
    case 'd':
      if(!strcmp(k, "description")) {
        lua_pushlightuserdata(L, module);
        lua_pushcclosure(L, noit_lua_module_set_description, 1);
      }
      else break;
      return 1;
    case 'n':
      if(!strcmp(k, "name")) {
        lua_pushlightuserdata(L, module);
        lua_pushcclosure(L, noit_lua_module_set_name, 1);
      }
      else break;
      return 1;
    case 'x':
      if(!strcmp(k, "xml_description")) {
        lua_pushlightuserdata(L, module);
        lua_pushcclosure(L, noit_lua_module_set_xml_description, 1);
      }
      else break;
      return 1;
    default:
      break;
  }
  luaL_error(L, "noit_module_t no such element: %s", k);
  return 0;
}

static int
noit_lua_get_available(lua_State *L) {
  char av[2] = { '\0', '\0' };
  noit_check_t *check;
  stats_t *current;
  if(lua_gettop(L)) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  current = noit_check_get_stats_current(check);
  av[0] = (char)noit_check_stats_available(current, NULL);
  lua_pushstring(L, av);
  return 1;
}
static int
noit_lua_set_available(lua_State *L) {
  noit_check_t *check;
  if(lua_gettop(L)) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  noit_stats_set_available(check, lua_tointeger(L, lua_upvalueindex(2)));
  return 0;
}
static int
noit_lua_get_state(lua_State *L) {
  char status[2] = { '\0', '\0' };
  noit_check_t *check;
  stats_t *current;
  if(lua_gettop(L)) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  current = noit_check_get_stats_current(check);
  status[0] = (char)noit_check_stats_state(current, NULL);
  lua_pushstring(L, status);
  return 1;
}

static int
noit_lua_get_flags(lua_State *L) {
  noit_check_t *check;
  u_int32_t andset = ~0;
  int narg;

  if(lua_gettop(L) > 0) andset = 0;
  for(narg = 0; narg < lua_gettop(L); narg++) {
    const char *freq;
    freq = lua_tostring(L, narg+1);
    if(!freq) luaL_error(L, "argument must be a string");
#define NP_F(flag) if(!strcmp(freq, #flag)) { andset |= flag; }
    NP_F(NP_RUNNING)
    else NP_F(NP_KILLED)
    else NP_F(NP_DISABLED)
    else NP_F(NP_UNCONFIG)
    else NP_F(NP_TRANSIENT)
    else NP_F(NP_RESOLVE)
    else NP_F(NP_RESOLVED)
    else NP_F(NP_SUPPRESS_STATUS)
    else NP_F(NP_SUPPRESS_METRICS)
    else NP_F(NP_PREFER_IPV6)
    else NP_F(NP_SINGLE_RESOLVE)
    else NP_F(NP_PASSIVE_COLLECTION)
#undef NP_F
    else luaL_error(L, "unknown flag specified");
  }
  check = lua_touserdata(L, lua_upvalueindex(1));
  lua_pushinteger(L, (check->flags & andset));
  return 1;
}
static int
noit_lua_unset_flags(lua_State *L) {
  noit_check_t *check;
  int narg;

  check = lua_touserdata(L, lua_upvalueindex(1));
  for(narg = 0; narg < lua_gettop(L); narg++) {
    const char *freq;
    freq = lua_tostring(L, narg+1);
    if(!freq) luaL_error(L, "argument must be a string");
#define NP_F(flag) if(!strcmp(freq, #flag)) { check->flags &= ~(flag); }
    NP_F(NP_RUNNING)
    else NP_F(NP_KILLED)
    else NP_F(NP_DISABLED)
    else NP_F(NP_UNCONFIG)
    else NP_F(NP_TRANSIENT)
    else NP_F(NP_RESOLVE)
    else NP_F(NP_RESOLVED)
    else NP_F(NP_SUPPRESS_STATUS)
    else NP_F(NP_SUPPRESS_METRICS)
    else NP_F(NP_PREFER_IPV6)
    else NP_F(NP_SINGLE_RESOLVE)
    else NP_F(NP_PASSIVE_COLLECTION)
#undef NP_F
    else luaL_error(L, "unknown flag specified");
  }
  lua_pushinteger(L, check->flags);
  return 1;
}
static int
noit_lua_set_flags(lua_State *L) {
  noit_check_t *check;
  int narg;

  check = lua_touserdata(L, lua_upvalueindex(1));
  for(narg = 0; narg < lua_gettop(L); narg++) {
    const char *freq;
    freq = lua_tostring(L, narg+1);
    if(!freq) luaL_error(L, "argument must be a string");
#define NP_F(flag) if(!strcmp(freq, #flag)) { check->flags |= flag; }
    NP_F(NP_RUNNING)
    else NP_F(NP_KILLED)
    else NP_F(NP_DISABLED)
    else NP_F(NP_UNCONFIG)
    else NP_F(NP_TRANSIENT)
    else NP_F(NP_RESOLVE)
    else NP_F(NP_RESOLVED)
    else NP_F(NP_SUPPRESS_STATUS)
    else NP_F(NP_SUPPRESS_METRICS)
    else NP_F(NP_PREFER_IPV6)
    else NP_F(NP_SINGLE_RESOLVE)
    else NP_F(NP_PASSIVE_COLLECTION)
#undef NP_F
    else luaL_error(L, "unknown flag specified");
  }
  lua_pushinteger(L, check->flags);
  return 1;
}
static int
noit_lua_set_state(lua_State *L) {
  noit_check_t *check;
  if(lua_gettop(L)) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  noit_stats_set_state(check, lua_tointeger(L, lua_upvalueindex(2)));
  return 0;
}
static int
noit_lua_set_status(lua_State *L) {
  const char *ns;
  noit_check_t *check;
  if(lua_gettop(L) != 1) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  ns = lua_tostring(L, 1);
  noit_stats_set_status(check, ns);
  return 0;
}
static int
noit_lua_set_metric_json(lua_State *L) {
  noit_check_t *check;
  const char *json;
  size_t jsonlen;
  int rv;

  if(lua_gettop(L) != 1) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  if(!lua_isstring(L, 1)) luaL_error(L, "argument #1 must be a string");
  json = lua_tolstring(L, 1, &jsonlen);
  rv = noit_check_stats_from_json_str(check, json, (int)jsonlen);
  lua_pushinteger(L, rv);
  return 1;
}

static int
noit_lua_set_metric_f(lua_State *L,
                      void(*set)(noit_check_t *,
                                 const char *, metric_type_t,
                                 const void *)) {
  noit_check_t *check;
  const char *metric_name;
  metric_type_t metric_type;

  double __n = 0.0;
  int32_t __i = 0;
  u_int32_t __I = 0;
  int64_t __l = 0;
  u_int64_t __L = 0;

  if(lua_gettop(L) != 2) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  if(!lua_isstring(L, 1)) luaL_error(L, "argument #1 must be a string");
  metric_name = lua_tostring(L, 1);
  metric_type = lua_tointeger(L, lua_upvalueindex(2));
  if(lua_isnil(L, 2)) {
    set(check, metric_name, metric_type, NULL);
    lua_pushboolean(L, 1);
    return 1;
  }
  switch(metric_type) {
    case METRIC_INT32:
    case METRIC_UINT32:
    case METRIC_INT64:
    case METRIC_UINT64:
    case METRIC_DOUBLE:
      if(!lua_isnumber(L, 2)) {
        set(check, metric_name, metric_type, NULL);
        lua_pushboolean(L, 0);
        return 1;
      }
    default:
    break;
  }
  switch(metric_type) {
    case METRIC_GUESS:
    case METRIC_STRING:
      set(check, metric_name, metric_type,
                            (void *)lua_tostring(L, 2));
      break;
    case METRIC_INT32:
      __i = strtol(lua_tostring(L, 2), NULL, 10);
      set(check, metric_name, metric_type, &__i);
      break;
    case METRIC_UINT32:
      __I = strtoul(lua_tostring(L, 2), NULL, 10);
      set(check, metric_name, metric_type, &__I);
      break;
    case METRIC_INT64:
      __l = strtoll(lua_tostring(L, 2), NULL, 10);
      set(check, metric_name, metric_type, &__l);
      break;
    case METRIC_UINT64:
      __L = strtoull(lua_tostring(L, 2), NULL, 10);
      set(check, metric_name, metric_type, &__L);
      break;
    case METRIC_DOUBLE:
      __n = luaL_optnumber(L, 2, 0);
      set(check, metric_name, metric_type, &__n);
      break;
  }
  lua_pushboolean(L, 1);
  return 1;
}

static int
noit_lua_set_metric(lua_State *L) {
  return noit_lua_set_metric_f(L, noit_stats_set_metric);
}

static int
noit_lua_log_immediate_metric(lua_State *L) {
  return noit_lua_set_metric_f(L, noit_stats_log_immediate_metric);
}

static int
noit_lua_interpolate(lua_State *L) {
  noit_check_t *check;
  mtev_hash_table check_attrs_hash = MTEV_HASH_EMPTY;
  char buff[8192];

  if(lua_gettop(L) != 1) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  if(!lua_isstring(L,1) && !lua_istable(L,1)) {
    luaL_error(L, "noit.check.interpolate(<string|table>)");
  }

  noit_check_make_attrs(check, &check_attrs_hash);
  if(lua_isstring(L,1)) {
    const char *ns = lua_tostring(L, 1);
    noit_check_interpolate(buff, sizeof(buff), ns,
                           &check_attrs_hash, check->config);
    lua_pushstring(L, buff);
  }
  else {
    /* We have a table */
         /* And we need a new table to return */
    lua_createtable(L, 0, 0);

    /* push a blank key to prep for lua_next calls */
    lua_pushnil(L);
    while(lua_next(L, -3)) { /* src table is -3 */
      const char *key = lua_tostring(L, -2);
      if(lua_isstring(L, -1)) {
                               const char *ns = lua_tostring(L,-1);
        noit_check_interpolate(buff, sizeof(buff), ns,
                               &check_attrs_hash, check->config);
                               lua_pop(L,1);
        lua_pushstring(L, buff);
                       }
                       lua_setfield(L, -3, key); /* tgt table is -3 */
    }
  }
  mtev_hash_destroy(&check_attrs_hash, NULL, NULL);
  return 1;
}

static int
noit_check_index_func(lua_State *L) {
  int n;
  const char *k;
  noit_check_t **udata, *check;
  n = lua_gettop(L);    /* number of arguments */
  assert(n == 2);
  if(!luaL_checkudata(L, 1, "noit_check_t")) {
    luaL_error(L, "metatable error, arg1 not a noit_check_t!");
  }
  udata = lua_touserdata(L, 1);
  check = *udata;
  if(!lua_isstring(L, 2)) {
    luaL_error(L, "metatable error, arg2 not a string!");
  }
  k = lua_tostring(L, 2);
  switch(*k) {
    case 'a':
      if(!strcmp(k, "available")) {
        lua_pushlightuserdata(L, check);
        lua_pushinteger(L, NP_AVAILABLE);
        lua_pushcclosure(L, noit_lua_set_available, 2);
      }
      else if(!strcmp(k, "availability")) {
        lua_pushlightuserdata(L, check);
        lua_pushcclosure(L, noit_lua_get_available, 1);
      }
      else break;
      return 1;
    case 'b':
      if(!strcmp(k, "bad")) {
        lua_pushlightuserdata(L, check);
        lua_pushinteger(L, NP_BAD);
        lua_pushcclosure(L, noit_lua_set_state, 2);
      }
      else break;
      return 1;
    case 'c':
      if(!strcmp(k, "config")) mtev_lua_hash_to_table(L, check->config);
      else if(!strcmp(k, "checkid")) {
        char uuid_str[UUID_STR_LEN + 1];
        uuid_unparse_lower(check->checkid, uuid_str);
        lua_pushstring(L, uuid_str);
      }
      else break;
      return 1;
    case 'f':
      if(!strcmp(k, "flags")) {
        lua_pushlightuserdata(L, check);
        lua_pushcclosure(L, noit_lua_get_flags, 1);
      }
      else break;
      return 1;
    case 'g':
      if(!strcmp(k, "good")) {
        lua_pushlightuserdata(L, check);
        lua_pushinteger(L, NP_GOOD);
        lua_pushcclosure(L, noit_lua_set_state, 2);
      }
      else break;
      return 1;
    case 'i':
      if(!strcmp(k, "interpolate")) {
        lua_pushlightuserdata(L, check);
        lua_pushcclosure(L, noit_lua_interpolate, 1);
      }
      else if(!strcmp(k, "is_thread_local")) {
        if(check->fire_event &&
           pthread_equal(pthread_self(), check->fire_event->thr_owner)) {
          lua_pushboolean(L, 1);
        }
        else {
          lua_pushboolean(L, 0);
        }
        return 1;
      }
      else break;
                       return 1;
    case 'm':
      if(!strcmp(k, "module")) lua_pushstring(L, check->module);

#define IF_METRIC_BLOCK(name,type) \
      if(!strcmp(k, name)) { \
        lua_pushlightuserdata(L, check); \
        lua_pushinteger(L, type); \
        lua_pushcclosure(L, noit_lua_set_metric, 2); \
      }

      else IF_METRIC_BLOCK("metric", METRIC_GUESS)
      else IF_METRIC_BLOCK("metric_string", METRIC_STRING)
      else IF_METRIC_BLOCK("metric_int32", METRIC_INT32)
      else IF_METRIC_BLOCK("metric_uint32", METRIC_UINT32)
      else IF_METRIC_BLOCK("metric_int64", METRIC_INT64)
      else IF_METRIC_BLOCK("metric_uint64", METRIC_UINT64)
      else IF_METRIC_BLOCK("metric_double", METRIC_DOUBLE)
      else if(!strcmp(k, "metric_json")) {
        lua_pushlightuserdata(L, check);
        lua_pushcclosure(L, noit_lua_set_metric_json, 1);
      }

#define IF_METRIC_IMMEDIATE_BLOCK(name,type) \
      if(!strcmp(k, "immediate_" name)) { \
        lua_pushlightuserdata(L, check); \
        lua_pushinteger(L, type); \
        lua_pushcclosure(L, noit_lua_log_immediate_metric, 2); \
      }
      else IF_METRIC_IMMEDIATE_BLOCK("metric", METRIC_GUESS)
      else IF_METRIC_IMMEDIATE_BLOCK("metric_string", METRIC_STRING)
      else IF_METRIC_IMMEDIATE_BLOCK("metric_int32", METRIC_INT32)
      else IF_METRIC_IMMEDIATE_BLOCK("metric_uint32", METRIC_UINT32)
      else IF_METRIC_IMMEDIATE_BLOCK("metric_int64", METRIC_INT64)
      else IF_METRIC_IMMEDIATE_BLOCK("metric_uint64", METRIC_UINT64)
      else IF_METRIC_BLOCK("metric_double", METRIC_DOUBLE)

      else break;
      return 1;
    case 'n':
      if(!strcmp(k, "name")) lua_pushstring(L, check->name);
      else break;
      return 1;
    case 'p':
      if(!strcmp(k, "period")) lua_pushinteger(L, check->period);
      else break;
      return 1;
    case 's':
      if(!strcmp(k, "set_flags")) {
        lua_pushlightuserdata(L, check);
        lua_pushcclosure(L, noit_lua_set_flags, 1);
      }
      else if(!strcmp(k, "state")) {
        lua_pushlightuserdata(L, check);
        lua_pushcclosure(L, noit_lua_get_state, 1);
      }
      else if(!strcmp(k, "status")) {
        lua_pushlightuserdata(L, check);
        lua_pushcclosure(L, noit_lua_set_status, 1);
      }
      else break;
      return 1;
   case 't':
      if(!strcmp(k, "target")) lua_pushstring(L, check->target);
      else if(!strcmp(k, "target_ip")) {
        if(check->target_ip[0] == '\0') lua_pushnil(L);
        else lua_pushstring(L, check->target_ip);
      }
      else if(!strcmp(k, "timeout")) lua_pushinteger(L, check->timeout);
      else break;
      return 1;
    case 'u':
      if(!strcmp(k, "unavailable")) {
        lua_pushlightuserdata(L, check);
        lua_pushinteger(L, NP_UNAVAILABLE);
        lua_pushcclosure(L, noit_lua_set_available, 2);
      }
      else if(!strcmp(k, "unset_flags")) {
        lua_pushlightuserdata(L, check);
        lua_pushcclosure(L, noit_lua_unset_flags, 1);
      }
      else if(!strcmp(k, "uuid")) {
        char uuid_str[UUID_STR_LEN+1];
        uuid_unparse_lower(check->checkid, uuid_str);
        lua_pushstring(L, uuid_str);
        return 1;
      }
      else break;
      return 1;
    default:
      break;
  }
  luaL_error(L, "noit_check_t no such element: %s", k);
  return 0;
}
static void
noit_lua_setup_module(lua_State *L,
                      noit_module_t *mod) {
  noit_module_t **addr;
  addr = (noit_module_t **)lua_newuserdata(L, sizeof(mod));
  *addr = mod;
  if(luaL_newmetatable(L, "noit_module_t") == 1) {
    lua_pushcclosure(L, noit_module_index_func, 0);
    lua_setfield(L, -2, "__index");
  }
  lua_setmetatable(L, -2);
}
void
noit_lua_setup_check(lua_State *L,
                     noit_check_t *check) {
  noit_check_t **addr;
  addr = (noit_check_t **)lua_newuserdata(L, sizeof(check));
  *addr = check;
  if(luaL_newmetatable(L, "noit_check_t") == 1) {
    lua_pushcclosure(L, noit_check_index_func, 0);
    lua_setfield(L, -2, "__index");
  }
  lua_setmetatable(L, -2);
}
static int
noit_lua_module_onload(mtev_image_t *img) {
  int rv;
  lua_State *L;
  lua_module_closure_t *lmc;
  struct module_conf *mc;

  mc = mtev_image_get_userdata(img);

  lmc = lmc_tls_get(img);
  L = lmc->lua_state;
  if(!L) return -1;
  lua_getglobal(L, "require");
  lua_pushstring(L, mc->object);
  rv = lua_pcall(L, 1, 1, 0);
  if(rv) {
    int i;
    mtevL(nlerr, "lua: %s.onload failed\n", mc->object);
    i = lua_gettop(L);
    if(i>0) {
      if(lua_isstring(L, i)) {
        const char *err;
        size_t len;
        err = lua_tolstring(L, i, &len);
        mtevL(nlerr, "lua: %s\n", err);
      }
    }
    lua_pop(L,i);
    return -1;
  }
  lua_pop(L, lua_gettop(L));

  mtev_lua_pushmodule(L, mc->object);
  if(lua_isnil(L, -1)) {
    lua_pop(L, 1);
    mtevL(nlerr, "lua: no such object %s\n", mc->object);
    return -1;
  }
  lua_getfield(L, -1, "onload");
  lua_remove(L, -2);
  if(!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    /* No onload */
    return 0;
  }
  noit_lua_setup_module(L, (noit_module_t *)img);
  lua_pcall(L, 1, 1, 0);
  if(lua_isnumber(L, -1)) {
    int rv;
    rv = lua_tointeger(L, -1);
    lua_pop(L, 1);
    return rv;
  }
  mtevL(nlerr, "%s.onload must return a integer not %s (%s)\n", mc->object, mtev_lua_type_name(lua_type(L,-1)), lua_tostring(L,-1));
  lua_pop(L,1);
  return -1;
}

static int
noit_lua_module_config(noit_module_t *mod,
                       mtev_hash_table *options) {
  struct module_conf *mc;
  struct module_tls_conf *mtlsc;
  LMC_DECL(L, mod, object);

  mc = noit_module_get_userdata(mod);
  if(options) {
    assert(mc->options == NULL);
    mc->options = calloc(1, sizeof(*mc->options));
    mtev_hash_init(mc->options);
    mtev_hash_merge_as_dict(mc->options, options);
  }
  else options = mc->options;
  mtlsc = __get_module_tls_conf(&mod->hdr);
  if(mtlsc->configured) return mtlsc->configured_return;

  SETUP_CALL(L, object, "config", return 0);

  noit_lua_setup_module(L, mod);
  mtev_lua_hash_to_table(L, options);
  lua_pcall(L, 2, 1, 0);

  /* If rv == 0, the caller will free options. We've
   * already freed options, that would be bad. fudge -> 1 */
  RETURN_INT(L, object, "config",
             { mtlsc->configured = 1; mtlsc->configured_return = rv; });
  mtlsc->configured = 1;
  mtlsc->configured_return = -1;
  return -1;
}

static int
noit_lua_module_init(noit_module_t *mod) {
  struct module_tls_conf *mtlsc;
  LMC_DECL(L, mod, object);

  mtlsc = __get_module_tls_conf(&mod->hdr);
  if(mtlsc->initialized) return mtlsc->initialized_return;

  SETUP_CALL(L, object, "init", return 0);

  noit_lua_setup_module(L, mod);
  lua_pcall(L, 1, 1, 0);

  RETURN_INT(L, object, "init",
             { mtlsc->initialized = 1; mtlsc->initialized_return = rv; });
  mtlsc->initialized = 1;
  mtlsc->initialized_return = -1;
  return -1;
}

static void
noit_lua_module_cleanup(noit_module_t *mod, noit_check_t *check) {
  mtev_lua_resume_info_t *ri = check->closure;
  LMC_DECL(L, mod, object);
  SETUP_CALL(L, object, "cleanup", goto clean);

  noit_lua_setup_module(L, mod);
  noit_lua_setup_check(L, check);
  lua_pcall(L, 2, 0, 0);

 clean:
  if(ri) {
    mtev_lua_resume_clean_events(ri);
    if(ri->context_data) {
      free(ri->context_data);
    }
    free(ri);
    check->closure = NULL;
  }
}

/* Here is where the magic starts */
static void
noit_lua_log_results(noit_module_t *self, noit_check_t *check) {
  mtev_lua_resume_info_t *ri = check->closure;
  noit_lua_resume_check_info_t *ci = ri->context_data;
  struct timeval duration;
  stats_t *inprogress;

  gettimeofday(&ci->finish_time, NULL);
  sub_timeval(ci->finish_time, check->last_fire_time, &duration);

  noit_stats_set_whence(check, &ci->finish_time);
  noit_stats_set_duration(check, duration.tv_sec * 1000 + duration.tv_usec / 1000);

  /* Only set out stats/log if someone has actually performed a check */
  inprogress = noit_check_get_stats_inprogress(check);
  if(noit_check_stats_state(inprogress,NULL) != NP_UNKNOWN ||
     noit_check_stats_available(inprogress,NULL) != NP_UNKNOWN)
    noit_check_set_stats(check);
}

int
noit_lua_check_resume(mtev_lua_resume_info_t *ri, int nargs) {
  int result = -1, base;
  noit_module_t *self = NULL;
  noit_check_t *check = NULL;
  noit_lua_resume_check_info_t *ci = ri->context_data;

  assert(pthread_equal(pthread_self(), ri->bound_thread));

  mtevL(nldeb, "lua: %p resuming(%d)\n", ri->coro_state, nargs);
#if LUA_VERSION_NUM >= 502
  result = lua_resume(ri->coro_state, ri->lmc->lua_state, nargs);
#else
  result = lua_resume(ri->coro_state, nargs);
#endif
  if(ci) {
    self = ci->self;
    check = ci->check;
  }
  switch(result) {
    case 0: /* success */
      if(lua_status(ri->coro_state) == 0 && lua_gettop(ri->coro_state) == 0) {
        /* LUA_OK status with no stack means we're dead (good or bad) */
        mtevL(nldeb, "lua_State(%p) -> %d [check: %p]\n", ri->coro_state,
              lua_status(ri->coro_state), ci ? ci->check: NULL);
      }
      break;
    case LUA_YIELD: /* The complicated case */
      /* The person yielding had better setup an event
       * to wake up the coro...
       */
      lua_gc(ri->lmc->lua_state, LUA_GCCOLLECT, 0);
      goto done;
    default: /* Errors */
      mtevL(nldeb, "lua resume returned: %d\n", result);
      if(check) {
        noit_stats_set_status(check, ci->timed_out ? "timeout" : "unknown error from lua");
        noit_stats_set_available(check, NP_UNAVAILABLE);
        noit_stats_set_state(check, NP_BAD);
      }
      base = lua_gettop(ri->coro_state);
      if(base>0) {
        int errbase;
        mtev_lua_traceback(ri->coro_state);
        errbase = lua_gettop(ri->coro_state);
        if(lua_isstring(ri->coro_state, errbase)) {
          const char *err, *nerr;
          nerr = err = lua_tostring(ri->coro_state, errbase);
          if(errbase != base)
            nerr = lua_tostring(ri->coro_state, base);
          if(err) mtevL(nldeb, "lua error: %s\n", err);
          if(nerr) {
            nerr = strchr(nerr, ' '); /* advance past the file */
            if(nerr) nerr += 1;
          }
          if(nerr) {
            if(check) {
              noit_stats_set_status(check, nerr);
            }
          }
        }
      }
      break;
  }

  if(ci) {
    self = ci->self;
    check = ci->check;
  }
  mtev_lua_cancel_coro(ri);
  if(check) {
    noit_lua_log_results(self, check);
    noit_lua_module_cleanup(self, check);
    ri = NULL; /* we freed it... explode if someone uses it before we return */
    check->flags &= ~NP_RUNNING;
  }

 done:
  return result;
}

static int
noit_lua_check_timeout(eventer_t e, int mask, void *closure,
                       struct timeval *now) {
  noit_module_t *self;
  noit_check_t *check;
  struct nl_intcl *int_cl = closure;
  mtev_lua_resume_info_t *ri = int_cl->ri;
  noit_lua_resume_check_info_t *ci = ri->context_data;
  mtevL(nldeb, "lua: %p ->check_timeout\n", ri->coro_state);
  ci->timed_out = 1;
  mtev_lua_deregister_event(ri, e, 0);

  self = ci->self;
  check = ci->check;

  if(ri->coro_state) {
    /* Our coro is still "in-flight". To fix this we will unreference
     * it, garbage collect it and then ensure that it failes a resume
     */
    mtev_lua_cancel_coro(ri);
  }
  if(check) {
    noit_stats_set_status(check, "timeout");
    noit_stats_set_available(check, NP_UNAVAILABLE);
    noit_stats_set_state(check, NP_BAD);
  }

  noit_lua_log_results(self, check);
  noit_lua_module_cleanup(self, check);
  check->flags &= ~NP_RUNNING;

  if(int_cl->free) int_cl->free(int_cl);
  return 0;
}

static void
int_cl_free(void *vcl) {
  free(vcl);
}

static int
noit_lua_initiate(noit_module_t *self, noit_check_t *check,
                  noit_check_t *cause) {
  LMC_DECL(L, self, object);
  /* deal with unused warning */
  (void)L;

  struct nl_intcl *int_cl;
  mtev_lua_resume_info_t *ri;
  noit_lua_resume_check_info_t *ci;
  struct timeval p_int, __now;
  eventer_t e;

  if(!check->closure) {
    check->closure = calloc(1, sizeof(mtev_lua_resume_info_t));
    ri = check->closure;
    ri->bound_thread = pthread_self();
  }
  else {
    ri = check->closure;
  }
  ci = ri->context_data;
  if(!ci) {
    ri->context_magic = LUA_CHECK_INFO_MAGIC;
    ri->context_data = calloc(1, sizeof(noit_lua_resume_check_info_t));
    ci = ri->context_data;
  }

  /* We cannot be running */
  BAIL_ON_RUNNING_CHECK(check);
  check->flags |= NP_RUNNING;

  /* this will config & init if it hasn't happened yet */
  noit_lua_module_config(self, NULL);
  noit_lua_module_init(self);

  ci->self = self;
  ci->check = check;
  ci->cause = cause;

  gettimeofday(&__now, NULL);
  memcpy(&check->last_fire_time, &__now, sizeof(__now));

  e = eventer_alloc();
  e->mask = EVENTER_TIMER;
  e->callback = noit_lua_check_timeout;
  /* We wrap this in an alloc so we can blindly free it later */
  int_cl = calloc(1, sizeof(*int_cl));
  int_cl->ri = ri;
  int_cl->free = int_cl_free;
  e->closure = int_cl;
  memcpy(&e->whence, &__now, sizeof(__now));
  p_int.tv_sec = check->timeout / 1000;
  p_int.tv_usec = (check->timeout % 1000) * 1000;
  add_timeval(e->whence, p_int, &e->whence);
  mtev_lua_register_event(ri, e);
  eventer_add(e);

  ri->lmc = lmc;
  // ri->check = check; /* This is the coroutine from which the check is run */
  mtev_lua_new_coro(ri);

  SETUP_CALL(ri->coro_state, object, "initiate", goto fail);
  noit_lua_setup_module(ri->coro_state, ci->self);
  noit_lua_setup_check(ri->coro_state, ci->check);
  if(cause)
    noit_lua_setup_check(ri->coro_state, ci->cause);
  else
    lua_pushnil(ri->coro_state);
  lmc->resume(ri, 3);
  return 0;

 fail:
  noit_lua_log_results(ci->self, ci->check);
  noit_lua_module_cleanup(ci->self, ci->check);
  check->flags &= ~NP_RUNNING;
  return -1;
}

/* This is the standard wrapper */
static int
noit_lua_module_initiate_check(noit_module_t *self, noit_check_t *check,
                               int once, noit_check_t *cause) {
  INITIATE_CHECK(noit_lua_initiate, self, check, cause);
  return 0;
}

static mtev_image_t *
noit_lua_loader_load(mtev_dso_loader_t *loader,
                     char *module_name,
                     mtev_conf_section_t section) {
  noit_module_t *m;
  lua_State *L = NULL;
  lua_module_closure_t *lmc;
  struct module_conf *mc;
  struct loader_conf *c;
  char *object;

  mtevL(nldeb, "Loading lua module: %s\n", module_name);
  if(mtev_conf_get_string(section, "@object", &object) == 0) {
    mtevL(nlerr, "Lua module %s require object attribute.\n", module_name);
    return NULL;
  }

  c = __get_loader_conf(loader);

  m = noit_blank_module();
  m->hdr.magic = NOIT_MODULE_MAGIC;
  m->hdr.version = NOIT_MODULE_ABI_VERSION;
  m->hdr.name = strdup(module_name);
  m->hdr.description = strdup("Lua module");
  m->hdr.onload = noit_lua_module_onload;
  mc = calloc(1, sizeof(*mc));
  mc->c = c;
  mc->object = strdup(object);
  pthread_key_create(&mc->key, NULL);
  noit_module_set_userdata(m, mc);

  lmc = noit_lua_setup_lmc(m, NULL);
  if(lmc != NULL) L = lmc->lua_state;
  if(L == NULL) {
   load_failed:
    if(L) lua_close(L);
    free(m->hdr.name);
    free(m->hdr.description);
    if(lmc) {
      free(lmc->pending);
      free(lmc);
    }
    /* FIXME: We leak the opaque_handler in the module here... */
    free(m);
    return NULL;
  }

  m->config = noit_lua_module_config;
  m->init = noit_lua_module_init;
  m->initiate_check = noit_lua_module_initiate_check;
  m->cleanup = noit_lua_module_cleanup;

  if(noit_register_module(m)) {
    mtevL(nlerr, "lua failed to register '%s' as a module\n", m->hdr.name);
    goto load_failed;
  }
  return (mtev_image_t *)m;
}

static int
noit_lua_loader_config(mtev_dso_loader_t *self, mtev_hash_table *o) {
  struct loader_conf *c = __get_loader_conf(self);
  const char *dir = ".";
  (void)mtev_hash_retr_str(o, "directory", strlen("directory"), &dir);
  c->script_dir = strdup(dir);
 
  dir = NULL; 
  (void)mtev_hash_retr_str(o, "cpath", strlen("cpath"), &dir);
  if(dir) c->cpath = strdup(dir);

  if(!c->cpath) {
    char *basepath = NULL;
    char cpath_lua[PATH_MAX];
    /* Set it to something reasonable.... we need the MTEV path and ours */
    (void)mtev_conf_get_string(NULL, "//modules/@directory", &basepath);
    if(basepath) {
      char *base, *brk;
      snprintf(cpath_lua, sizeof(cpath_lua),
               "./?.so;%s/noit_lua/?.so;%s/mtev_lua/?.so",
               LIB_DIR, MTEV_LIB_DIR);
      for(base = strtok_r(basepath, ":;", &brk); base;
          base = strtok_r(NULL, ":;", &brk)) {
        strlcat(cpath_lua, ";", sizeof(cpath_lua));
        strlcat(cpath_lua, base, sizeof(cpath_lua));
        strlcat(cpath_lua, "/?.so", sizeof(cpath_lua));
      }
      free(basepath);
    }
    else
      strlcpy(cpath_lua,
              "./?.so;" LIB_DIR "/noit_lua/?.so;" MTEV_LIB_DIR "/mtev_lua/?.so",
              sizeof(cpath_lua));
    c->cpath = strdup(cpath_lua);
  }
  return 0;
}

static void
describe_lua_check_context(mtev_console_closure_t ncct,
                           mtev_lua_resume_info_t *ri) {
  char uuid_str[UUID_STR_LEN+1];
  noit_lua_resume_check_info_t *ci = ri->context_data;
  nc_printf(ncct, "lua_check(state:%p, parent:%p)\n",
            ri->coro_state, ri->lmc->lua_state);
  uuid_unparse_lower(ci->check->checkid, uuid_str);
  nc_printf(ncct, "\tcheck: %s\n", uuid_str);
  nc_printf(ncct, "\tname: %s\n", ci->check->name);
  nc_printf(ncct, "\tmodule: %s\n", ci->check->module);
  nc_printf(ncct, "\ttarget: %s\n", ci->check->target);
}

static int
noit_lua_loader_onload(mtev_image_t *self) {
  loader_main_thread = pthread_self();
  nlerr = mtev_log_stream_find("error/lua");
  nldeb = mtev_log_stream_find("debug/lua");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;
  eventer_name_callback("lua/check_timeout", noit_lua_check_timeout);
  mtev_lua_context_describe(LUA_CHECK_INFO_MAGIC, describe_lua_check_context);
  register_console_lua_commands();
  return 0;
}

#include "lua.xmlh"
mtev_dso_loader_t lua = {
  {
    .magic = MTEV_LOADER_MAGIC,
    .version = MTEV_LOADER_ABI_VERSION,
    .name = "lua",
    .description = "Lua check loader",
    .xml_description = lua_xml_description,
    .onload = noit_lua_loader_onload,
  },
  noit_lua_loader_config,
  NULL,
  noit_lua_loader_load
};
