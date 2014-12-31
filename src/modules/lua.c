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

#include <unistd.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#include <assert.h>

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;
static noit_hash_table noit_lua_states = NOIT_HASH_EMPTY;
static pthread_mutex_t noit_lua_states_lock = PTHREAD_MUTEX_INITIALIZER;
static noit_hash_table noit_coros = NOIT_HASH_EMPTY;
static pthread_mutex_t coro_lock = PTHREAD_MUTEX_INITIALIZER;

struct loader_conf {
  pthread_key_t key;
  char *script_dir;
};
struct module_conf {
  struct loader_conf *c;
  char *object;
  noit_hash_table *options;
  pthread_key_t key;
};
struct module_tls_conf {
  int loaded;
  int configured;
  int initialized;
};
static struct module_tls_conf *__get_module_tls_conf(noit_image_t *img) {
  struct module_conf *mc;
  struct module_tls_conf *mtlsc;
  mc = noit_image_get_userdata(img);
  mtlsc = pthread_getspecific(mc->key);
  if(mtlsc == NULL) {
    mtlsc = calloc(1, sizeof(*mtlsc));
    pthread_setspecific(mc->key, mtlsc);
  }
  return mtlsc;
}
static struct loader_conf *__get_loader_conf(noit_module_loader_t *self) {
  struct loader_conf *c;
  c = noit_image_get_userdata(&self->hdr);
  if(!c) {
    c = calloc(1, sizeof(*c));
    pthread_key_create(&c->key, NULL);
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
noit_lua_cancel_coro(noit_lua_resume_info_t *ci) {
  lua_getglobal(ci->lmc->lua_state, "noit_coros");
  luaL_unref(ci->lmc->lua_state, -1, ci->coro_state_ref);
  lua_pop(ci->lmc->lua_state, 1);
  lua_gc(ci->lmc->lua_state, LUA_GCCOLLECT, 0);
  noitL(nldeb, "coro_store <- %p\n", ci->coro_state);
  pthread_mutex_lock(&coro_lock);
  assert(noit_hash_delete(&noit_coros,
                          (const char *)&ci->coro_state, sizeof(ci->coro_state),
                          NULL, NULL));
  pthread_mutex_unlock(&coro_lock);
}

void
noit_lua_set_resume_info(lua_State *L, noit_lua_resume_info_t *ri) {
  lua_getglobal(L, "noit_internal_lmc");
  ri->lmc = lua_touserdata(L, lua_gettop(L));
  noitL(nldeb, "coro_store -> %p\n", ri->coro_state);
  pthread_mutex_lock(&coro_lock);
  noit_hash_store(&noit_coros,
                  (const char *)&ri->coro_state, sizeof(ri->coro_state),
                  ri); 
  pthread_mutex_unlock(&coro_lock);
}
static void
describe_lua_context(noit_console_closure_t ncct,
                     noit_lua_resume_info_t *ri) {
  switch(ri->context_magic) {
    case LUA_CHECK_INFO_MAGIC:
    {
      char uuid_str[UUID_STR_LEN+1];
      noit_lua_resume_check_info_t *ci = ri->context_data;
      nc_printf(ncct, "lua_check(state:%p, parent:%p)\n",
                ri->coro_state, ri->lmc->lua_state);
      uuid_unparse_lower(ci->check->checkid, uuid_str);
      nc_printf(ncct, "\tcheck: %s\n", uuid_str);
      nc_printf(ncct, "\tname: %s\n", ci->check->name);
      nc_printf(ncct, "\tmodule: %s\n", ci->check->module);
      nc_printf(ncct, "\ttarget: %s\n", ci->check->target);
      break;
    }
    case LUA_GENERAL_INFO_MAGIC:
      nc_printf(ncct, "lua_general(state:%p, parent:%p)\n",
                ri->coro_state, ri->lmc->lua_state);
      break;
    case 0:
      nc_printf(ncct, "lua_native(state:%p, parent:%p)\n",
                ri->coro_state, ri->lmc->lua_state);
      break;
    default:
      nc_printf(ncct, "Unknown lua context(state:%p, parent:%p)\n",
                ri->coro_state, ri->lmc->lua_state);
  }
}

struct lua_reporter {
  pthread_mutex_t lock;
  noit_console_closure_t ncct;
  noit_atomic32_t outstanding;
};

static int
noit_console_lua_thread_reporter(eventer_t e, int mask, void *closure,
                                 struct timeval *now) {
  struct lua_reporter *reporter = closure;
  noit_console_closure_t ncct = reporter->ncct;
  noit_hash_iter zero = NOIT_HASH_ITER_ZERO, iter;
  const char *key;
  int klen;
  void *vri;
  pthread_t me, *tgt;
  me = pthread_self();

  pthread_mutex_lock(&reporter->lock);
  nc_printf(ncct, "== Thread %x ==\n", me);

  memcpy(&iter, &zero, sizeof(zero));
  pthread_mutex_lock(&noit_lua_states_lock);
  while(noit_hash_next(&noit_lua_states, &iter, &key, &klen, &vri)) {
    lua_State **Lptr = (lua_State **)key;
    pthread_t tgt = (pthread_t)(vpsized_int)vri;
    if(!pthread_equal(me, tgt)) continue;
    nc_printf(ncct, "master (state:%p)\n", *Lptr);
    nc_printf(ncct, "\tmemory: %d kb\n", lua_gc(*Lptr, LUA_GCCOUNT, 0));
    nc_printf(ncct, "\n");
  }
  pthread_mutex_unlock(&noit_lua_states_lock);

  memcpy(&iter, &zero, sizeof(zero));
  pthread_mutex_lock(&coro_lock);
  while(noit_hash_next(&noit_coros, &iter, &key, &klen, &vri)) {
    noit_lua_resume_info_t *ri;
    int level = 1;
    lua_Debug ar;
    lua_State *L;
    assert(klen == sizeof(L));
    L = *((lua_State **)key);
    ri = vri;
    if(!pthread_equal(me, ri->lmc->owner)) continue;
    if(ri) describe_lua_context(ncct, ri);
    nc_printf(ncct, "\tstack:\n");
    while (lua_getstack(L, level++, &ar));
    level--;
    while (level > 0 && lua_getstack(L, --level, &ar)) {
      const char *name, *cp;
      lua_getinfo(L, "n", &ar);
      name = ar.name;
      lua_getinfo(L, "Snlf", &ar);
      cp = ar.source;
      if(cp) {
        cp = cp + strlen(cp) - 1;
        while(cp >= ar.source && *cp != '/') cp--;
        cp++;
      }
      else cp = "???";
      if(ar.name == NULL) ar.name = name;
      if(ar.name == NULL) ar.name = "???";
      if (ar.currentline > 0) {
        if(*ar.namewhat) {
          nc_printf(ncct, "\t\t%s:%s(%s):%d\n", cp, ar.namewhat, ar.name, ar.currentline);
        } else {
          nc_printf(ncct, "\t\t%s:%d\n", cp, ar.currentline);
        }
      } else {
        nc_printf(ncct, "\t\t%s:%s(%s)\n", cp, ar.namewhat, ar.name);
      }
    }
    nc_printf(ncct, "\n");
  }
  pthread_mutex_unlock(&coro_lock);
  noit_atomic_dec32(&reporter->outstanding);
  pthread_mutex_unlock(&reporter->lock);
  return 0;
}
static int
noit_console_show_lua(noit_console_closure_t ncct,
                      int argc, char **argv,
                      noit_console_state_t *dstate,
                      void *closure) {
  int i = 0;
  pthread_t me, tgt, first;
  struct lua_reporter crutch;
  struct timeval old = { 1ULL, 0ULL };

  crutch.outstanding = 1; /* me */
  crutch.ncct = ncct;
  pthread_mutex_init(&crutch.lock, NULL);

  me = pthread_self();
  noit_console_lua_thread_reporter(NULL, 0, &crutch, NULL);
  first = eventer_choose_owner(i++);
  if(!pthread_equal(first, me)) {
    do {
      eventer_t e;
      tgt = eventer_choose_owner(i++);
      e = eventer_alloc();
      memcpy(&e->whence, &old, sizeof(old));
      e->thr_owner = tgt;
      e->mask = EVENTER_TIMER;
      e->callback = noit_console_lua_thread_reporter;
      e->closure = &crutch;
      noit_atomic_inc32(&crutch.outstanding);
      eventer_add(e);
    } while(!pthread_equal(first, tgt));
  }

  /* Wait for completion */
  while(crutch.outstanding > 0) {
    usleep(500);
  }

  pthread_mutex_destroy(&crutch.lock);
  return 0;
}

static void
register_console_lua_commands() {
  noit_console_state_t *tl;
  cmd_info_t *showcmd;

  tl = noit_console_state_initial();
  showcmd = noit_console_state_get_cmd(tl, "show");
  assert(showcmd && showcmd->dstate);
  noit_console_state_add_cmd(showcmd->dstate,
    NCSCMD("lua", noit_console_show_lua, NULL, NULL, NULL));
}

noit_lua_resume_info_t *
noit_lua_get_resume_info(lua_State *L) {
  noit_lua_resume_info_t *ri;
  lua_module_closure_t *lmc;
  void *v = NULL;
  pthread_mutex_lock(&coro_lock);
  if(noit_hash_retrieve(&noit_coros, (const char *)&L, sizeof(L), &v)) {
    pthread_mutex_unlock(&coro_lock);
    ri = v;
    assert(pthread_equal(pthread_self(), ri->bound_thread));
    return ri;
  }
  ri = calloc(1, sizeof(*ri));
  ri->bound_thread = pthread_self();
  ri->coro_state = L;
  lua_getglobal(L, "noit_internal_lmc");;
  ri->lmc = lua_touserdata(L, lua_gettop(L));
  lua_pop(L, 1);
  noitL(nldeb, "coro_store -> %p\n", ri->coro_state);
  lua_getglobal(L, "noit_coros");
  lua_pushthread(L);
  ri->coro_state_ref = luaL_ref(L, -2);
  lua_pop(L, 1); /* pops noit_coros */
  noit_hash_store(&noit_coros,
                  (const char *)&ri->coro_state, sizeof(ri->coro_state),
                  ri);
  pthread_mutex_unlock(&coro_lock);
  return ri;
}
static void
int_cl_free(void *vcl) {
  free(vcl);
}

static lua_module_closure_t *lmc_tls_get(noit_image_t *mod) {
  lua_module_closure_t *lmc;
  struct loader_conf *c;
  if(mod->magic == NOIT_LOADER_MAGIC) {
    c = noit_image_get_userdata(mod);
  }
  else {
    struct module_conf *mc;
    mc = noit_image_get_userdata(mod);
    c = mc->c;
  }
  lmc = pthread_getspecific(c->key);
  return lmc;
}
static lua_module_closure_t *noit_lua_setup_lmc(noit_image_t *mod, const char **obj) {
  lua_module_closure_t *lmc;
  struct module_conf *mc;
  struct module_tls_conf *mtlsc;
  mc = noit_image_get_userdata(mod);
  if(obj) *obj = mc->object;
  lmc = pthread_getspecific(mc->c->key);
  if(lmc == NULL) {
    lmc = calloc(1, sizeof(*lmc));
    lmc->pending = calloc(1, sizeof(*lmc->pending));
    lmc->owner = pthread_self();
    lmc->resume = noit_lua_check_resume;
    pthread_setspecific(mc->c->key, lmc);
    noitL(nldeb, "lua_state[%s]: new state\n", mod->name);
    lmc->lua_state = noit_lua_open(mod->name, lmc, mc->c->script_dir);
  }
  mtlsc = __get_module_tls_conf(mod);
  if(!mtlsc->loaded) {
    if(mod->onload(mod) == -1) {
      return NULL;
    }
    mtlsc->loaded = 1;
  }
  noitL(nldeb, "lua_state[%s]: %p\n", mod->name, lmc->lua_state);
  return lmc;
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
noit_lua_check_register_event(noit_lua_resume_info_t *ci, eventer_t e) {
  eventer_t *eptr;
  eptr = calloc(1, sizeof(*eptr));
  memcpy(eptr, &e, sizeof(*eptr));
  if(!ci->events) {
    ci->events = calloc(1, sizeof(*ci->events));
    noit_hash_init(ci->events);
  }
  assert(noit_hash_store(ci->events, (const char *)eptr, sizeof(*eptr), eptr));
}
void
noit_lua_check_deregister_event(noit_lua_resume_info_t *ci, eventer_t e,
                                int tofree) {
  assert(ci->events);
  assert(noit_hash_delete(ci->events, (const char *)&e, sizeof(e),
                          NULL, tofree ? noit_event_dispose : free));
}
void
noit_lua_resume_clean_events(noit_lua_resume_info_t *ci) {
  if(ci->events == NULL) return;
  noit_hash_destroy(ci->events, NULL, noit_event_dispose);
  free(ci->events);
  ci->events = NULL;
}

void
noit_lua_pushmodule(lua_State *L, const char *m) {
  int stack_pos = 0;
  char *copy, *part, *brkt;
  copy = alloca(strlen(m)+1);
  assert(copy);
  memcpy(copy,m,strlen(m)+1);

  for(part = strtok_r(copy, ".", &brkt);
      part;
      part = strtok_r(NULL, ".", &brkt)) {
    if(stack_pos) lua_getfield(L, stack_pos, part);
    else lua_getglobal(L, part);
    if(stack_pos == -1) lua_remove(L, -2);
    else stack_pos = -1;
  }
}
noit_hash_table *
noit_lua_table_to_hash(lua_State *L, int idx) {
  noit_hash_table *t;
  if(lua_gettop(L) < idx || !lua_istable(L,idx))
    luaL_error(L, "table_to_hash: not a table");

  t = calloc(1, sizeof(*t));
  lua_pushnil(L);  /* first key */
  while (lua_next(L, idx) != 0) {
    const char *key, *value;
    size_t klen;
    key = lua_tolstring(L, -2, &klen);
    value = lua_tostring(L, -1);
    noit_hash_store(t, key, klen, (void *)value);
    lua_pop(L, 1);
  }
  return t;
}
void
noit_lua_hash_to_table(lua_State *L,
                       noit_hash_table *t) {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *key, *value;
  int klen, kcnt;
  kcnt = t ? noit_hash_size(t) : 0;
  lua_createtable(L, 0, kcnt);
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
  if(lua_gettop(L) == 1) {
    if(module->hdr.xml_description) free(module->hdr.xml_description);
    module->hdr.xml_description = strdup(lua_tostring(L, 1));
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
  if(lua_gettop(L)) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  av[0] = (char)check->stats.current.available;
  lua_pushstring(L, av);
  return 1;
}
static int
noit_lua_set_available(lua_State *L) {
  noit_check_t *check;
  if(lua_gettop(L)) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  check->stats.inprogress.available = lua_tointeger(L, lua_upvalueindex(2));
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
noit_lua_get_flags(lua_State *L) {
  noit_check_t *check;
  noit_lua_resume_info_t *ri;
  noit_lua_resume_check_info_t *ci;
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
    else NP_F(NP_PREFER_IPV6)
    else NP_F(NP_SINGLE_RESOLVE)
    else NP_F(NP_PASSIVE_COLLECTION)
    else luaL_error(L, "unknown flag specified");
  }
  check = lua_touserdata(L, lua_upvalueindex(1));
  lua_pushinteger(L, (check->flags & andset));
  return 1;
}
static int
noit_lua_set_state(lua_State *L) {
  noit_check_t *check;
  if(lua_gettop(L)) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  check->stats.inprogress.state = lua_tointeger(L, lua_upvalueindex(2));
  return 0;
}
static int
noit_lua_set_status(lua_State *L) {
  const char *ns;
  noit_check_t *check;
  if(lua_gettop(L) != 1) luaL_error(L, "wrong number of arguments");
  check = lua_touserdata(L, lua_upvalueindex(1));
  /* strdup here... but free later */
  if(check->stats.inprogress.status) free(check->stats.inprogress.status);
  ns = lua_tostring(L, 1);
  check->stats.inprogress.status = ns ? strdup(ns) : NULL;
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
  rv = noit_check_stats_from_json_str(check, &check->stats.inprogress, json, (int)jsonlen);
  lua_pushinteger(L, rv);
  return 1;
}
static int
noit_lua_set_metric(lua_State *L) {
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
    noit_stats_set_metric(check, &check->stats.inprogress, metric_name, metric_type, NULL);
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
        noit_stats_set_metric(check, &check->stats.inprogress, metric_name, metric_type, NULL);
        lua_pushboolean(L, 0);
        return 1;
      }
    default:
    break;
  }
  switch(metric_type) {
    case METRIC_GUESS:
    case METRIC_STRING:
      noit_stats_set_metric(check, &check->stats.inprogress, metric_name, metric_type,
                            (void *)lua_tostring(L, 2));
      break;
    case METRIC_INT32:
      __i = strtol(lua_tostring(L, 2), NULL, 10);
      noit_stats_set_metric(check, &check->stats.inprogress, metric_name, metric_type, &__i);
      break;
    case METRIC_UINT32:
      __I = strtoul(lua_tostring(L, 2), NULL, 10);
      noit_stats_set_metric(check, &check->stats.inprogress, metric_name, metric_type, &__I);
      break;
    case METRIC_INT64:
      __l = strtoll(lua_tostring(L, 2), NULL, 10);
      noit_stats_set_metric(check, &check->stats.inprogress, metric_name, metric_type, &__l);
      break;
    case METRIC_UINT64:
      __L = strtoull(lua_tostring(L, 2), NULL, 10);
      noit_stats_set_metric(check, &check->stats.inprogress, metric_name, metric_type, &__L);
      break;
    case METRIC_DOUBLE:
      __n = luaL_optnumber(L, 2, 0);
      noit_stats_set_metric(check, &check->stats.inprogress, metric_name, metric_type, &__n);
      break;
  }
  lua_pushboolean(L, 1);
  return 1;
}
static int
noit_lua_interpolate(lua_State *L) {
  noit_check_t *check;
  noit_hash_table check_attrs_hash = NOIT_HASH_EMPTY;
  char buff[2048];

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
  noit_hash_destroy(&check_attrs_hash, NULL, NULL);
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

static const char *
noit_lua_type_name(int t) {
  switch(t) {
    case LUA_TNIL: return "nil";
    case LUA_TNUMBER: return "number";
    case LUA_TBOOLEAN: return "boolean";
    case LUA_TSTRING: return "string";
    case LUA_TTABLE: return "table";
    case LUA_TFUNCTION: return "function";
    case LUA_TUSERDATA: return "userdata";
    case LUA_TTHREAD: return "thread";
    case LUA_TLIGHTUSERDATA: return "lightuserdata";
    default: return "unknown";
  }
}
static int
noit_lua_module_onload(noit_image_t *img) {
  int rv;
  lua_State *L;
  lua_module_closure_t *lmc;
  struct module_conf *mc;

  mc = noit_image_get_userdata(img);
  noit_lua_init();

  lmc = lmc_tls_get(img);
  L = lmc->lua_state;
  lua_getglobal(L, "require");
  lua_pushstring(L, mc->object);
  rv = lua_pcall(L, 1, 1, 0);
  if(rv) {
    int i;
    noitL(nlerr, "lua: %s.onload failed\n", mc->object);
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

  noit_lua_pushmodule(L, mc->object);
  if(lua_isnil(L, -1)) {
    lua_pop(L, 1);
    noitL(nlerr, "lua: no such object %s\n", mc->object);
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
  noitL(nlerr, "%s.onload must return a integer not %s (%s)\n", mc->object, noit_lua_type_name(lua_type(L,-1)), lua_tostring(L,-1));
  lua_pop(L,1);
  return -1;
}

#define LMC_DECL(L, mod, object) \
  lua_State *L; \
  lua_module_closure_t *lmc; \
  const char *object; \
  lmc = noit_lua_setup_lmc(&mod->hdr, &object); \
  L = lmc->lua_state
#define SETUP_CALL(L, object, func, failure) do { \
  noitL(nldeb, "lua calling %s->%s\n", object, func); \
  noit_lua_pushmodule(L, object); \
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
  noitL(nlerr, "%s.%s must return a integer not %s (%s)\n", object, func, noit_lua_type_name(lua_type(L,-1)), lua_tostring(L,-1)); \
  lua_pop(L,1); \
} while(0)

static int 
noit_lua_module_config(noit_module_t *mod,
                       noit_hash_table *options) {
  struct module_conf *mc;
  struct module_tls_conf *mtlsc;
  LMC_DECL(L, mod, object);

  mc = noit_module_get_userdata(mod);
  if(options) mc->options = options;
  else options = mc->options;
  mtlsc = __get_module_tls_conf(&mod->hdr);
  if(mtlsc->configured) return mtlsc->configured;

  SETUP_CALL(L, object, "config", return 0);

  noit_lua_setup_module(L, mod);
  noit_lua_hash_to_table(L, options);
  lua_pcall(L, 2, 1, 0);

  /* If rv == 0, the caller will free options. We've
   * already freed options, that would be bad. fudge -> 1 */
  RETURN_INT(L, object, "config",
             { mtlsc->configured = rv = (rv == 0) ? 1 : rv; });
  mtlsc->configured = -1;
  return -1;
}
static int
noit_lua_module_init(noit_module_t *mod) {
  LMC_DECL(L, mod, object);
  SETUP_CALL(L, object, "init", return 0);

  noit_lua_setup_module(L, mod);
  lua_pcall(L, 1, 1, 0);

  RETURN_INT(L, object, "init", );
  return -1;
}
static void
noit_lua_module_cleanup(noit_module_t *mod, noit_check_t *check) {
  noit_lua_resume_info_t *ri = check->closure;
  LMC_DECL(L, mod, object);
  SETUP_CALL(L, object, "cleanup", goto clean);

  noit_lua_setup_module(L, mod);
  noit_lua_setup_check(L, check);
  lua_pcall(L, 2, 0, 0);

 clean:
  if(ri) { 
    noit_lua_resume_clean_events(ri);
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
  noit_lua_resume_info_t *ri = check->closure;
  noit_lua_resume_check_info_t *ci = ri->context_data;
  struct timeval duration;

  gettimeofday(&ci->finish_time, NULL);
  sub_timeval(ci->finish_time, check->last_fire_time, &duration);

  memcpy(&check->stats.inprogress.whence, &ci->finish_time, sizeof(check->stats.inprogress.whence));
  check->stats.inprogress.duration = duration.tv_sec * 1000 + duration.tv_usec / 1000;

  /* Only set out stats/log if someone has actually performed a check */
  if(check->stats.inprogress.state != NP_UNKNOWN ||
     check->stats.inprogress.available != NP_UNKNOWN)
    noit_check_set_stats(check, &check->stats.inprogress);
  free(check->stats.inprogress.status);
  noit_check_stats_clear(check, &check->stats.inprogress);
}
int
noit_lua_yield(noit_lua_resume_info_t *ci, int nargs) {
  noitL(nldeb, "lua: %p yielding\n", ci->coro_state);
  return lua_yield(ci->coro_state, nargs);
}
int
noit_lua_check_resume(noit_lua_resume_info_t *ri, int nargs) {
  int result = -1, base;
  noit_module_t *self = NULL;
  noit_check_t *check = NULL;
  noit_lua_resume_check_info_t *ci = ri->context_data;

  assert(pthread_equal(pthread_self(), ri->bound_thread));

  noitL(nldeb, "lua: %p resuming(%d)\n", ri->coro_state, nargs);
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
        noitL(nldeb, "lua_State(%p) -> %d [check: %p]\n", ri->coro_state,
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
      noitL(nldeb, "lua resume returned: %d\n", result);
      if(check)  {
        if(check->stats.inprogress.status) free(check->stats.inprogress.status);
        check->stats.inprogress.status = strdup(ci->timed_out ? "timeout" : "unknown error from lua");
        check->stats.inprogress.available = NP_UNAVAILABLE;
        check->stats.inprogress.state = NP_BAD;
      }
      base = lua_gettop(ri->coro_state);
      if(base>0) {
        if(lua_isstring(ri->coro_state, base)) {
          const char *err, *nerr;
          err = lua_tostring(ri->coro_state, base);
          nerr = lua_tostring(ri->coro_state, base - 2);
          if(err) noitL(nldeb, "err -> %s\n", err);
          if(nerr) noitL(nldeb, "nerr -> %s\n", nerr);
          if(nerr && *nerr == 31) nerr = NULL; // 31? WTF lua?
          if(err) {
            nerr = strchr(err, ' '); /* advance past the file */
            if(nerr) nerr += 1;
          }
          if(nerr) {
            if(check) {
              if(check->stats.inprogress.status) free(check->stats.inprogress.status);
              check->stats.inprogress.status = strdup(nerr);
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
  noit_lua_cancel_coro(ri);
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
  noit_lua_resume_info_t *ri = int_cl->ri;
  noit_lua_resume_check_info_t *ci = ri->context_data;
  noitL(nldeb, "lua: %p ->check_timeout\n", ri->coro_state);
  ci->timed_out = 1;
  noit_lua_check_deregister_event(ri, e, 0);

  self = ci->self;
  check = ci->check;

  if(ri->coro_state) {
    /* Our coro is still "in-flight". To fix this we will unreference
     * it, garbage collect it and then ensure that it failes a resume
     */
    noit_lua_cancel_coro(ri);
  }
  if(check) {
    if(check->stats.inprogress.status) free(check->stats.inprogress.status);
    check->stats.inprogress.status = strdup("timeout");
    check->stats.inprogress.available = NP_UNAVAILABLE;
    check->stats.inprogress.state = NP_BAD;
  }

  noit_lua_log_results(self, check);
  noit_lua_module_cleanup(self, check);
  check->flags &= ~NP_RUNNING;

  if(int_cl->free) int_cl->free(int_cl);
  return 0;
}
static int
noit_lua_initiate(noit_module_t *self, noit_check_t *check,
                  noit_check_t *cause) {
  LMC_DECL(L, self, object);
  struct nl_intcl *int_cl;
  noit_lua_resume_info_t *ri;
  noit_lua_resume_check_info_t *ci;
  struct timeval p_int, __now;
  eventer_t e;

  if(!check->closure) {
    check->closure = calloc(1, sizeof(noit_lua_resume_info_t));
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

  /* this will config if it hasn't happened yet */
  noit_lua_module_config(self, NULL);

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
  noit_lua_check_register_event(ri, e);
  eventer_add(e);

  ri->lmc = lmc;
  lua_getglobal(L, "noit_coros");
  ri->coro_state = lua_newthread(L);
  ri->check = check; /* This is the coroutine from which the check is run */
  ri->coro_state_ref = luaL_ref(L, -2);
  lua_pop(L, 1); /* pops noit_coros */
  noitL(nldeb, "coro_store -> %p\n", ri->coro_state);
  pthread_mutex_lock(&coro_lock);
  noit_hash_store(&noit_coros,
                  (const char *)&ri->coro_state, sizeof(ri->coro_state),
                  ri);
  pthread_mutex_unlock(&coro_lock);

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
static int noit_lua_panic(lua_State *L) {
  if(L) {
    int level = 0;
    lua_Debug ar;
    const char *err = lua_tostring(L,2);
    
    while (lua_getstack(L, level++, &ar));
    noitL(noit_error, "lua panic[top:%d]: %s\n", lua_gettop(L), err);
    while (level > 0 && lua_getstack(L, --level, &ar)) {
      lua_getinfo(L, "Sl", &ar);
      lua_getinfo(L, "n", &ar);
      if (ar.currentline > 0) {
        const char *cp = ar.source;
        if(cp) {
          cp = cp + strlen(cp) - 1;
          while(cp >= ar.source && *cp != '/') cp--;
          cp++;
        }
        else cp = "???";
        if(ar.name == NULL) ar.name = "???";
        noitL(noit_error, "\t%s:%s(%s):%d\n", cp, ar.namewhat, ar.name, ar.currentline);
      }
    }
  }
  assert(L == NULL);
  return 0;
}

static void *l_alloc (void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud; (void)osize;  /* not used */
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  else
    return realloc(ptr, nsize);
}

lua_State *
noit_lua_open(const char *module_name, void *lmc, const char *script_dir) {
  int rv;
  lua_State *L = luaL_newstate(), **Lptr;
  lua_atpanic(L, &noit_lua_panic);

  lua_gc(L, LUA_GCSTOP, 0);  /* stop collector during initialization */
  luaL_openlibs(L);  /* open libraries */
  luaopen_snmp(L);
  luaopen_pack(L);
  luaopen_bit(L);
  luaopen_noit(L);
  luaopen_crypto(L);

  lua_newtable(L);
  lua_setglobal(L, "noit_coros");

  if(lmc) {
    lua_pushlightuserdata(L, lmc);
    lua_setglobal(L, "noit_internal_lmc");
  }

  lua_getglobal(L, "package");
  lua_pushfstring(L, "%s", script_dir);
  lua_setfield(L, -2, "path");
  lua_pop(L, 1);

#define require(a) do { \
  lua_getglobal(L, "require"); \
  lua_pushstring(L, #a); \
  rv = lua_pcall(L, 1, 1, 0); \
  if(rv != 0) { \
    noitL(noit_stderr, "Loading %s: %d (%s)\n", #a, rv, lua_tostring(L,-1)); \
    lua_close(L); \
    return NULL; \
  } \
  lua_pop(L, 1); \
} while(0)

  require(noit.timeval);
  require(noit.extras);

  lua_gc(L, LUA_GCRESTART, 0);

  Lptr = malloc(sizeof(*Lptr));
  *Lptr = L;
  pthread_mutex_lock(&noit_lua_states_lock);
  noit_hash_store(&noit_lua_states,
                  (const char *)Lptr, sizeof(*Lptr),
                  (void *)(vpsized_int)pthread_self());
  pthread_mutex_unlock(&noit_lua_states_lock);

  return L;
}
static noit_module_t *
noit_lua_loader_load(noit_module_loader_t *loader,
                     char *module_name,
                     noit_conf_section_t section) {
  int rv;
  noit_module_t *m;
  lua_State *L = NULL;
  lua_module_closure_t *lmc;
  struct module_conf *mc;
  struct loader_conf *c;
  char *object;

  noitL(nldeb, "Loading lua module: %s\n", module_name);
  if(noit_conf_get_string(section, "@object", &object) == 0) {
    noitL(nlerr, "Lua module %s require object attribute.\n", module_name);
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

  lmc = noit_lua_setup_lmc(&m->hdr, NULL);
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
    noitL(nlerr, "lua failed to register '%s' as a module\n", m->hdr.name);
    goto load_failed;
  }
  return m;
}

static int
noit_lua_loader_config(noit_module_loader_t *self, noit_hash_table *o) {
  const char *dir = ".";
  (void)noit_hash_retr_str(o, "directory", strlen("directory"), &dir);
  noit_lua_loader_set_directory(self, dir);
  return 0;
}
static int
noit_lua_loader_onload(noit_image_t *self) {
  pthread_key_t *key;
  nlerr = noit_log_stream_find("error/lua");
  nldeb = noit_log_stream_find("debug/lua");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;
  eventer_name_callback("lua/check_timeout", noit_lua_check_timeout);
  register_console_lua_commands();
  return 0;
}

#include "lua.xmlh"
noit_module_loader_t lua = {
  {
    .magic = NOIT_LOADER_MAGIC,
    .version = NOIT_LOADER_ABI_VERSION,
    .name = "lua",
    .description = "Lua check loader",
    .xml_description = lua_xml_description,
    .onload = noit_lua_loader_onload,
  },
  noit_lua_loader_config,
  NULL,
  noit_lua_loader_load
};
