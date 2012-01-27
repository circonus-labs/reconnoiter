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

#include "noit_defines.h"

#include "noit_conf.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "utils/noit_log.h"
#include "lua_noit.h"

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#include <assert.h>

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;
static noit_hash_table noit_coros = NOIT_HASH_EMPTY;

struct loader_conf {
  char *script_dir;
};
static struct loader_conf *__get_loader_conf(noit_module_loader_t *self) {
  struct loader_conf *c;
  c = noit_image_get_userdata(&self->hdr);
  if(!c) {
    c = calloc(1, sizeof(*c));
    noit_image_set_userdata(&self->hdr, c);
  }
  return c;
}
static void
noit_lua_loader_set_directory(noit_module_loader_t *self, const char *dir) {
  struct loader_conf *c = __get_loader_conf(self);
  if(c->script_dir) free(c->script_dir);
  c->script_dir = strdup(dir);
}
static const char *
noit_lua_loader_get_directory(noit_module_loader_t *self) {
  struct loader_conf *c = __get_loader_conf(self);
  return c->script_dir;
}

void
cancel_coro(noit_lua_check_info_t *ci) {
  lua_getglobal(ci->lmc->lua_state, "noit_coros");
  luaL_unref(ci->lmc->lua_state, -1, ci->coro_state_ref);
  lua_pop(ci->lmc->lua_state, 1);
  lua_gc(ci->lmc->lua_state, LUA_GCCOLLECT, 0);
  noit_hash_delete(&noit_coros,
                   (const char *)&ci->coro_state, sizeof(ci->coro_state),
                   NULL, NULL);
}

noit_lua_check_info_t *
get_ci(lua_State *L) {
  void *v = NULL;
  if(noit_hash_retrieve(&noit_coros, (const char *)&L, sizeof(L), &v))
    return (noit_lua_check_info_t *)v;
  return NULL;
}
static void
int_cl_free(void *vcl) {
  free(vcl);
}

static void
noit_event_dispose(void *ev) {
  int mask;
  eventer_t *value = ev;
  eventer_t removed, e = *value;
  noitL(nldeb, "lua check cleanup: dropping (%p)->fd (%d)\n", e, e->fd);
  removed = eventer_remove(e);
  noitL(nldeb, "    remove from eventer system %s\n",
        removed ? "succeeded" : "failed");
  if(e->mask & (EVENTER_READ|EVENTER_WRITE|EVENTER_EXCEPTION)) {
    noitL(nldeb, "    closing down fd %d\n", e->fd);
    e->opset->close(e->fd, &mask, e);
  }
  if(e->closure) {
    struct nl_generic_cl *cl;
    cl = e->closure;
    if(cl->free) cl->free(cl);
  }
  eventer_free(e);
  free(ev);
}
void
noit_lua_check_register_event(noit_lua_check_info_t *ci, eventer_t e) {
  eventer_t *eptr;
  eptr = calloc(1, sizeof(*eptr));
  memcpy(eptr, &e, sizeof(*eptr));
  if(!ci->events) ci->events = calloc(1, sizeof(*ci->events));
  assert(noit_hash_store(ci->events, (const char *)eptr, sizeof(*eptr), eptr));
}
void
noit_lua_check_deregister_event(noit_lua_check_info_t *ci, eventer_t e,
                                int tofree) {
  assert(ci->events);
  assert(noit_hash_delete(ci->events, (const char *)&e, sizeof(e),
                          NULL, tofree ? noit_event_dispose : free));
}
void
noit_lua_check_clean_events(noit_lua_check_info_t *ci) {
  if(ci->events == NULL) return;
  noit_hash_destroy(ci->events, NULL, noit_event_dispose);
  free(ci->events);
  ci->events = NULL;
}

static void
noit_lua_pushmodule(lua_State *L, const char *m) {
  int stack_pos = LUA_GLOBALSINDEX;
  char *copy, *part, *brkt;
  copy = alloca(strlen(m)+1);
  assert(copy);
  memcpy(copy,m,strlen(m)+1);

  for(part = strtok_r(copy, ".", &brkt);
      part;
      part = strtok_r(NULL, ".", &brkt)) {
    lua_getfield(L, stack_pos, part);
    if(stack_pos == -1) lua_remove(L, -2);
    else stack_pos = -1;
  }
}
static void
noit_lua_hash_to_table(lua_State *L,
                       noit_hash_table *t) {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *key, *value;
  int klen;
  lua_createtable(L, 0, t ? t->size : 0);
  if(t) {
    while(noit_hash_next_str(t, &iter, &key, &klen, &value)) {
      lua_pushlstring(L, value, strlen(value));
      lua_setfield(L, -2, key);
    }
  }
  return;
}
static int
noit_lua_module_set_description(lua_State *L) {
  noit_module_t *module;
  module = lua_touserdata(L, lua_upvalueindex(1));
  if(lua_gettop(L) == 1)
    module->hdr.description = strdup(lua_tostring(L, 1));
  else if(lua_gettop(L) > 1)
    luaL_error(L, "wrong number of arguments");
  lua_pushstring(L, module->hdr.description);
  return 1;
}
static int
noit_lua_module_set_name(lua_State *L) {
  noit_module_t *module;
  module = lua_touserdata(L, lua_upvalueindex(1));
  if(lua_gettop(L) == 1)
    module->hdr.name = strdup(lua_tostring(L, 1));
  else if(lua_gettop(L) > 1)
    luaL_error(L, "wrong number of arguments");
  lua_pushstring(L, module->hdr.name);
  return 1;
}
static int
noit_lua_module_set_xml_description(lua_State *L) {
  noit_module_t *module;
  module = lua_touserdata(L, lua_upvalueindex(1));
  if(lua_gettop(L) == 1)
    module->hdr.xml_description = strdup(lua_tostring(L, 1));
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
  if(lua_gettop(L)) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  av[0] = (char)check->stats.current.available;
  lua_pushstring(L, av);
  return 1;
}
static int
noit_lua_set_available(lua_State *L) {
  noit_check_t *check;
  noit_lua_check_info_t *ci;
  if(lua_gettop(L)) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  ci = check->closure;
  ci->current.available = lua_tointeger(L, lua_upvalueindex(2));
  return 0;
}
static int
noit_lua_get_state(lua_State *L) {
  char status[2] = { '\0', '\0' };
  noit_check_t *check;
  if(lua_gettop(L)) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  status[0] = (char)check->stats.current.state;
  lua_pushstring(L, status);
  return 1;
}
static int
noit_lua_set_state(lua_State *L) {
  noit_check_t *check;
  noit_lua_check_info_t *ci;
  if(lua_gettop(L)) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  ci = check->closure;
  ci->current.state = lua_tointeger(L, lua_upvalueindex(2));
  return 0;
}
static int
noit_lua_set_status(lua_State *L) {
  const char *ns;
  noit_check_t *check;
  noit_lua_check_info_t *ci;
  if(lua_gettop(L) != 1) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  ci = check->closure;
  /* strdup here... but free later */
  if(ci->current.status) free(ci->current.status);
  ns = lua_tostring(L, 1);
  ci->current.status = ns ? strdup(ns) : NULL;
  return 0;
}
static int
noit_lua_set_metric_json(lua_State *L) {
  noit_check_t *check;
  noit_lua_check_info_t *ci;
  const char *json;
  size_t jsonlen;
  int rv;

  if(lua_gettop(L) != 1) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  ci = check->closure;
  if(!lua_isstring(L, 1)) luaL_error(L, "argument #1 must be a string");
  json = lua_tolstring(L, 1, &jsonlen);
  rv = noit_check_stats_from_json_str(&ci->current, json, (int)jsonlen);
  lua_pushinteger(L, rv);
  return 1;
}
static int
noit_lua_set_metric(lua_State *L) {
  noit_check_t *check;
  noit_lua_check_info_t *ci;
  const char *metric_name;
  metric_type_t metric_type;

  double __n = 0.0;
  int32_t __i = 0;
  u_int32_t __I = 0;
  int64_t __l = 0;
  u_int64_t __L = 0;

  if(lua_gettop(L) != 2) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  ci = check->closure;
  if(!lua_isstring(L, 1)) luaL_error(L, "argument #1 must be a string");
  metric_name = lua_tostring(L, 1);
  metric_type = lua_tointeger(L, lua_upvalueindex(2));
  if(lua_isnil(L, 2)) {
    noit_stats_set_metric(&ci->current, metric_name, metric_type, NULL);
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
        noit_stats_set_metric(&ci->current, metric_name, metric_type, NULL);
        lua_pushboolean(L, 0);
        return 1;
      }
    default:
    break;
  }
  switch(metric_type) {
    case METRIC_GUESS:
    case METRIC_STRING:
      noit_stats_set_metric(&ci->current, metric_name, metric_type,
                            (void *)lua_tostring(L, 2));
      break;
    case METRIC_INT32:
      __i = strtol(lua_tostring(L, 2), NULL, 10);
      noit_stats_set_metric(&ci->current, metric_name, metric_type, &__i);
      break;
    case METRIC_UINT32:
      __I = strtoul(lua_tostring(L, 2), NULL, 10);
      noit_stats_set_metric(&ci->current, metric_name, metric_type, &__I);
      break;
    case METRIC_INT64:
      __l = strtoll(lua_tostring(L, 2), NULL, 10);
      noit_stats_set_metric(&ci->current, metric_name, metric_type, &__l);
      break;
    case METRIC_UINT64:
      __L = strtoull(lua_tostring(L, 2), NULL, 10);
      noit_stats_set_metric(&ci->current, metric_name, metric_type, &__L);
      break;
    case METRIC_DOUBLE:
      __n = luaL_optnumber(L, 2, 0);
      noit_stats_set_metric(&ci->current, metric_name, metric_type, &__n);
      break;
  }
  lua_pushboolean(L, 1);
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
      if(!strcmp(k, "config")) noit_lua_hash_to_table(L, check->config);
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
      if(!strcmp(k, "state")) {
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
static void
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
noit_lua_module_onload(noit_image_t *i) {
  int rv;
  lua_State *L;
  lua_module_closure_t *lmc;

  noit_lua_init();

  lmc = noit_image_get_userdata(i);
  L = lmc->lua_state;
  lua_getglobal(L, "require");
  lua_pushstring(L, lmc->object);
  rv = lua_pcall(L, 1, 1, 0);
  if(rv) {
    int i;
    noitL(nlerr, "lua: %s.onload failed\n", lmc->object);
    i = lua_gettop(L);
    if(i>0) {
      if(lua_isstring(L, i)) {
        const char *err;
        size_t len;
        err = lua_tolstring(L, i, &len);
        noitL(nlerr, "lua: %s\n", err);
      }
    }
    lua_pop(L,i);
    return -1;
  }
  lua_pop(L, lua_gettop(L));

  noit_lua_pushmodule(L, lmc->object);
  if(lua_isnil(L, -1)) {
    lua_pop(L, 1);
    noitL(nlerr, "lua: no such object %s\n", lmc->object);
    return -1;
  }
  lua_getfield(L, -1, "onload");
  lua_remove(L, -2);
  if(!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    /* No onload */
    return 0;
  }
  noit_lua_setup_module(L, (noit_module_t *)i);
  lua_pcall(L, 1, 1, 0);
  if(lua_isnumber(L, -1)) {
    int rv;
    rv = lua_tointeger(L, -1);
    lua_pop(L, 1);
    return rv;
  }
  lua_pop(L,1);
  noitL(nlerr, "%s.onload must return a integer\n", lmc->object);
  return -1;
}
#define LMC_DECL(L, mod) \
  lua_State *L; \
  lua_module_closure_t *lmc; \
  lmc = noit_module_get_userdata(mod); \
  L = lmc->lua_state
#define SETUP_CALL(L, func, failure) do { \
  noit_lua_pushmodule(L, lmc->object); \
  lua_getfield(L, -1, func); \
  lua_remove(L, -2); \
  if(!lua_isfunction(L, -1)) { \
    lua_pop(L, 1); \
    failure; \
  } \
} while(0)
#define RETURN_INT(L, func, expr) do { \
  int base = lua_gettop(L); \
  assert(base == 1); \
  if(lua_isnumber(L, -1)) { \
    int rv; \
    rv = lua_tointeger(L, -1); \
    lua_pop(L, 1); \
    expr \
    return rv; \
  } \
  lua_pop(L,1); \
  noitL(nlerr, "%s.%s must return a integer\n", lmc->object, func); \
} while(0)

static int 
noit_lua_module_config(noit_module_t *mod,
                       noit_hash_table *options) {
  LMC_DECL(L, mod);
  SETUP_CALL(L, "config", return 0);

  noit_lua_setup_module(L, mod);
  noit_lua_hash_to_table(L, options);
  noit_hash_destroy(options, free, free);
  free(options);
  lua_pcall(L, 2, 1, 0);

  /* If rv == 0, the caller will free options. We've
   * already freed options, that would be bad. fudge -> 1 */
  RETURN_INT(L, "config", { rv = (rv == 0) ? 1 : rv; });
  return -1;
}
static int
noit_lua_module_init(noit_module_t *mod) {
  LMC_DECL(L, mod);
  SETUP_CALL(L, "init", return 0);

  noit_lua_setup_module(L, mod);
  lua_pcall(L, 1, 1, 0);

  RETURN_INT(L, "init", );
  return -1;
}
static void
noit_lua_module_cleanup(noit_module_t *mod, noit_check_t *check) {
  noit_lua_check_info_t *ci = check->closure;
  LMC_DECL(L, mod);
  SETUP_CALL(L, "cleanup", goto clean);

  noit_lua_setup_module(L, mod);
  noit_lua_setup_check(L, check);
  lua_pcall(L, 2, 0, 0);

 clean:
  if(ci) { 
    noit_lua_check_clean_events(ci);
    free(ci);
    check->closure = NULL;
  }
}

/* Here is where the magic starts */
static void
noit_lua_log_results(noit_module_t *self, noit_check_t *check) {
  noit_lua_check_info_t *ci = check->closure;
  struct timeval duration;

  gettimeofday(&ci->finish_time, NULL);
  sub_timeval(ci->finish_time, check->last_fire_time, &duration);

  memcpy(&ci->current.whence, &ci->finish_time, sizeof(ci->current.whence));
  ci->current.duration = duration.tv_sec * 1000 + duration.tv_usec / 1000;

  /* Only set out stats/log if someone has actually performed a check */
  if(ci->current.state != NP_UNKNOWN ||
     ci->current.available != NP_UNKNOWN)
    noit_check_set_stats(self, check, &ci->current);
  free(ci->current.status);
}
int
noit_lua_yield(noit_lua_check_info_t *ci, int nargs) {
  noitL(nldeb, "lua: %p yielding\n", ci->coro_state);
  return lua_yield(ci->coro_state, nargs);
}
int
noit_lua_resume(noit_lua_check_info_t *ci, int nargs) {
  int result = -1, base;
  noit_module_t *self;
  noit_check_t *check;

  noitL(nldeb, "lua: %p resuming(%d)\n", ci->coro_state, nargs);
  result = lua_resume(ci->coro_state, nargs);
  switch(result) {
    case 0: /* success */
      break;
    case LUA_YIELD: /* The complicated case */
      /* The person yielding had better setup an event
       * to wake up the coro...
       */
      lua_gc(ci->lmc->lua_state, LUA_GCCOLLECT, 0);
      goto done;
    default: /* Errors */
      noitL(nldeb, "lua resume returned: %d\n", result);
      ci->current.status = strdup(ci->timed_out ? "timeout" : "unknown error");
      ci->current.available = NP_UNAVAILABLE;
      ci->current.state = NP_BAD;
      base = lua_gettop(ci->coro_state);
      if(base>0) {
        if(lua_isstring(ci->coro_state, base)) {
          const char *err, *nerr;
          err = lua_tostring(ci->coro_state, base);
          nerr = lua_tostring(ci->coro_state, base - 2);
          if(err) noitL(nldeb, "err -> %s\n", err);
          if(nerr) noitL(nldeb, "nerr -> %s\n", nerr);
          if(nerr && *nerr == 31) nerr = NULL; // 31? WTF lua?
          if(!nerr && err) {
            nerr = strchr(err, ' '); /* advance past the file */
            if(nerr) nerr += 1;
          }
          if(nerr) {
            free(ci->current.status);
            ci->current.status = strdup(nerr);
          }
        }
      }
      break;
  }

  self = ci->self;
  check = ci->check;
  cancel_coro(ci);
  noit_lua_log_results(self, check);
  noit_lua_module_cleanup(self, check);
  ci = NULL; /* we freed it... explode if someone uses it before we return */
  check->flags &= ~NP_RUNNING;

 done:
  return result;
}
static int
noit_lua_check_timeout(eventer_t e, int mask, void *closure,
                       struct timeval *now) {
  noit_module_t *self;
  noit_check_t *check;
  struct nl_intcl *int_cl = closure;
  noit_lua_check_info_t *ci = int_cl->ci;
  noitL(nldeb, "lua: %p ->check_timeout\n", ci->coro_state);
  ci->timed_out = 1;
  noit_lua_check_deregister_event(ci, e, 0);

  self = ci->self;
  check = ci->check;

  if(ci->coro_state) {
    /* Our coro is still "in-flight". To fix this we will unreference
     * it, garbage collect it and then ensure that it failes a resume
     */
    cancel_coro(ci);
  }
  ci->current.status = strdup("timeout");
  ci->current.available = NP_UNAVAILABLE;
  ci->current.state = NP_BAD;

  noit_lua_log_results(self, check);
  noit_lua_module_cleanup(self, check);
  check->flags &= ~NP_RUNNING;

  if(int_cl->free) int_cl->free(int_cl);
  return 0;
}
static int
noit_lua_initiate(noit_module_t *self, noit_check_t *check,
                  noit_check_t *cause) {
  LMC_DECL(L, self);
  struct nl_intcl *int_cl;
  noit_lua_check_info_t *ci;
  struct timeval p_int, __now;
  eventer_t e;

  if(!check->closure) check->closure = calloc(1, sizeof(noit_lua_check_info_t));
  ci = check->closure;

  /* We cannot be running */
  assert(!(check->flags & NP_RUNNING));
  check->flags |= NP_RUNNING;

  ci->self = self;
  ci->check = check;
  ci->cause = cause;
  noit_check_stats_clear(&ci->current);

  gettimeofday(&__now, NULL);
  memcpy(&check->last_fire_time, &__now, sizeof(__now));

  e = eventer_alloc();
  e->mask = EVENTER_TIMER;
  e->callback = noit_lua_check_timeout;
  /* We wrap this in an alloc so we can blindly free it later */
  int_cl = calloc(1, sizeof(*int_cl));
  int_cl->ci = ci;
  int_cl->free = int_cl_free;
  e->closure = int_cl;
  memcpy(&e->whence, &__now, sizeof(__now));
  p_int.tv_sec = check->timeout / 1000;
  p_int.tv_usec = (check->timeout % 1000) * 1000;
  add_timeval(e->whence, p_int, &e->whence);
  noit_lua_check_register_event(ci, e);
  eventer_add(e);

  ci->lmc = lmc;
  lua_getglobal(L, "noit_coros");
  ci->coro_state = lua_newthread(L);
  ci->coro_state_ref = luaL_ref(L, -2);
  lua_pop(L, 1); /* pops noit_coros */
  noit_hash_store(&noit_coros,
                  (const char *)&ci->coro_state, sizeof(ci->coro_state),
                  ci);

  SETUP_CALL(ci->coro_state, "initiate", goto fail);
  noit_lua_setup_module(ci->coro_state, ci->self);
  noit_lua_setup_check(ci->coro_state, ci->check);
  if(cause)
    noit_lua_setup_check(ci->coro_state, ci->cause);
  else
    lua_pushnil(ci->coro_state);
  noit_lua_resume(ci, 3);
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
  if(!check->closure) check->closure = calloc(1, sizeof(noit_lua_check_info_t));
  INITIATE_CHECK(noit_lua_initiate, self, check, cause);
  return 0;
}
static int noit_lua_panic(lua_State *L) {
  assert(L == NULL);
  return 0;
}
static noit_module_t *
noit_lua_loader_load(noit_module_loader_t *loader,
                     char *module_name,
                     noit_conf_section_t section) {
  int rv;
  noit_module_t *m;
  lua_State *L;
  lua_module_closure_t *lmc;
  char *object;
  
  noitL(nldeb, "Loading lua module: %s\n", module_name);
  if(noit_conf_get_string(section, "@object", &object) == 0) {
    noitL(nlerr, "Lua module %s require object attribute.\n", module_name);
    return NULL;
  }

  m = noit_blank_module();
  m->hdr.magic = NOIT_MODULE_MAGIC;
  m->hdr.version = NOIT_MODULE_ABI_VERSION;
  m->hdr.name = strdup(module_name);
  m->hdr.description = strdup("Lua module");
  m->hdr.onload = noit_lua_module_onload;
  lmc = calloc(1, sizeof(*lmc));
  lmc->object = object;

  L = lmc->lua_state = lua_open();
  lua_atpanic(L, &noit_lua_panic);

  lua_gc(L, LUA_GCSTOP, 0);  /* stop collector during initialization */
  luaL_openlibs(L);  /* open libraries */
  luaopen_pack(L);
  luaopen_noit(L);

  lua_newtable(L);
  lua_setglobal(L, "noit_coros");

  lua_getfield(L, LUA_GLOBALSINDEX, "package");
  lua_pushfstring(L, "%s", noit_lua_loader_get_directory(loader));
  lua_setfield(L, -2, "path");
  lua_pop(L, 1);

#define require(a) do { \
  lua_getglobal(L, "require"); \
  lua_pushstring(L, #a); \
  rv = lua_pcall(L, 1, 1, 0); \
  if(rv != 0) { \
    noitL(noit_stderr, "Loading: %d\n", rv); \
    goto load_failed; \
  } \
  lua_pop(L, 1); \
} while(0)

  require(noit.timeval);
  require(noit.extras);

  lua_gc(L, LUA_GCRESTART, 0);

  noit_image_set_userdata(&m->hdr, lmc);
  if(m->hdr.onload(&m->hdr) == -1) {
   load_failed:
    lua_close(L);
    free(m->hdr.name);
    free(m->hdr.description);
    free(lmc->object);
    free(lmc);
    /* FIXME: We leak the opaque_handler in the module here... */
    free(m);
    return NULL;
  }

  m->config = noit_lua_module_config;
  m->init = noit_lua_module_init;
  m->initiate_check = noit_lua_module_initiate_check;
  m->cleanup = noit_lua_module_cleanup;

  noit_register_module(m);
  return m;
}

static int
noit_lua_loader_config(noit_module_loader_t *self, noit_hash_table *o) {
  const char *dir = ".";
  noit_hash_retr_str(o, "directory", strlen("directory"), &dir);
  noit_lua_loader_set_directory(self, dir);
  return 0;
}
static int
noit_lua_loader_onload(noit_image_t *self) {
  nlerr = noit_log_stream_find("error/lua");
  nldeb = noit_log_stream_find("debug/lua");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;
  eventer_name_callback("lua/check_timeout", noit_lua_check_timeout);
  return 0;
}

#include "lua.xmlh"
noit_module_loader_t lua = {
  {
    NOIT_LOADER_MAGIC,
    NOIT_LOADER_ABI_VERSION,
    "lua",
    "Lua check loader",
    lua_xml_description,
    noit_lua_loader_onload,
  },
  noit_lua_loader_config,
  NULL,
  noit_lua_loader_load
};
