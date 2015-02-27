/*
 * Copyright (c) 2007-2010, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2010-2015, Circonus, Inc. All rights reserved.
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

#include <mtev_defines.h>

#include <unistd.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif
#include <assert.h>

#include <mtev_conf.h>
#include <mtev_dso.h>
#include <mtev_log.h>

#include "lua_mtev.h"

static mtev_log_stream_t nlerr = NULL;
static mtev_log_stream_t nldeb = NULL;
static mtev_hash_table mtev_lua_states = MTEV_HASH_EMPTY;
static pthread_mutex_t mtev_lua_states_lock = PTHREAD_MUTEX_INITIALIZER;
static mtev_hash_table mtev_coros = MTEV_HASH_EMPTY;
static pthread_mutex_t coro_lock = PTHREAD_MUTEX_INITIALIZER;

void
mtev_lua_cancel_coro(mtev_lua_resume_info_t *ci) {
  lua_getglobal(ci->lmc->lua_state, "mtev_coros");
  luaL_unref(ci->lmc->lua_state, -1, ci->coro_state_ref);
  lua_pop(ci->lmc->lua_state, 1);
  lua_gc(ci->lmc->lua_state, LUA_GCCOLLECT, 0);
  mtevL(nldeb, "coro_store <- %p\n", ci->coro_state);
  pthread_mutex_lock(&coro_lock);
  assert(mtev_hash_delete(&mtev_coros,
                          (const char *)&ci->coro_state, sizeof(ci->coro_state),
                          NULL, NULL));
  pthread_mutex_unlock(&coro_lock);
}

void
mtev_lua_set_resume_info(lua_State *L, mtev_lua_resume_info_t *ri) {
  lua_getglobal(L, "mtev_internal_lmc");
  ri->lmc = lua_touserdata(L, lua_gettop(L));
  mtevL(nldeb, "coro_store -> %p\n", ri->coro_state);
  pthread_mutex_lock(&coro_lock);
  mtev_hash_store(&mtev_coros,
                  (const char *)&ri->coro_state, sizeof(ri->coro_state),
                  ri); 
  pthread_mutex_unlock(&coro_lock);
}

struct lua_context_describer {
  int context_magic;
  void (*describe)(mtev_console_closure_t, mtev_lua_resume_info_t *);
  struct lua_context_describer *next;
};

static struct lua_context_describer *context_describers = NULL;
void
mtev_lua_context_describe(int magic,
                          void (*f)(mtev_console_closure_t,
                                    mtev_lua_resume_info_t *)) {
  struct lua_context_describer *desc = malloc(sizeof(*desc));
  desc->context_magic = magic;
  desc->describe = f;
  desc->next = context_describers;
  context_describers = desc;
}

static void
describe_lua_context(mtev_console_closure_t ncct,
                     mtev_lua_resume_info_t *ri) {
  struct lua_context_describer *desc;
  for(desc = context_describers; desc; desc = desc->next) {
    if(desc->context_magic == ri->context_magic) {
      desc->describe(ncct,ri);
      return;
    }
  }
  if(ri->context_magic == 0) {
    nc_printf(ncct, "lua_native(state:%p, parent:%p)\n",
              ri->coro_state, ri->lmc->lua_state);
    return;
  }
  nc_printf(ncct, "Unknown lua context(state:%p, parent:%p)\n",
            ri->coro_state, ri->lmc->lua_state);
}

struct lua_reporter {
  pthread_mutex_t lock;
  mtev_console_closure_t ncct;
  mtev_atomic32_t outstanding;
};

static int
mtev_console_lua_thread_reporter(eventer_t e, int mask, void *closure,
                                 struct timeval *now) {
  struct lua_reporter *reporter = closure;
  mtev_console_closure_t ncct = reporter->ncct;
  mtev_hash_iter zero = MTEV_HASH_ITER_ZERO, iter;
  const char *key;
  int klen;
  void *vri;
  pthread_t me, *tgt;
  me = pthread_self();

  pthread_mutex_lock(&reporter->lock);
  nc_printf(ncct, "== Thread %x ==\n", me);

  memcpy(&iter, &zero, sizeof(zero));
  pthread_mutex_lock(&mtev_lua_states_lock);
  while(mtev_hash_next(&mtev_lua_states, &iter, &key, &klen, &vri)) {
    lua_State **Lptr = (lua_State **)key;
    pthread_t tgt = (pthread_t)(vpsized_int)vri;
    if(!pthread_equal(me, tgt)) continue;
    nc_printf(ncct, "master (state:%p)\n", *Lptr);
    nc_printf(ncct, "\tmemory: %d kb\n", lua_gc(*Lptr, LUA_GCCOUNT, 0));
    nc_printf(ncct, "\n");
  }
  pthread_mutex_unlock(&mtev_lua_states_lock);

  memcpy(&iter, &zero, sizeof(zero));
  pthread_mutex_lock(&coro_lock);
  while(mtev_hash_next(&mtev_coros, &iter, &key, &klen, &vri)) {
    mtev_lua_resume_info_t *ri;
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
        while(cp >= ar.source && *cp != '/' && *cp != '\n') cp--;
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
  mtev_atomic_dec32(&reporter->outstanding);
  pthread_mutex_unlock(&reporter->lock);
  return 0;
}
static int
mtev_console_show_lua(mtev_console_closure_t ncct,
                      int argc, char **argv,
                      mtev_console_state_t *dstate,
                      void *closure) {
  int i = 0;
  pthread_t me, tgt, first;
  struct lua_reporter crutch;
  struct timeval old = { 1ULL, 0ULL };

  crutch.outstanding = 1; /* me */
  crutch.ncct = ncct;
  pthread_mutex_init(&crutch.lock, NULL);

  me = pthread_self();
  mtev_console_lua_thread_reporter(NULL, 0, &crutch, NULL);
  first = eventer_choose_owner(i++);
  if(!pthread_equal(first, me)) {
    do {
      eventer_t e;
      tgt = eventer_choose_owner(i++);
      e = eventer_alloc();
      memcpy(&e->whence, &old, sizeof(old));
      e->thr_owner = tgt;
      e->mask = EVENTER_TIMER;
      e->callback = mtev_console_lua_thread_reporter;
      e->closure = &crutch;
      mtev_atomic_inc32(&crutch.outstanding);
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

void
register_console_lua_commands() {
  mtev_console_state_t *tl;
  cmd_info_t *showcmd;

  tl = mtev_console_state_initial();
  showcmd = mtev_console_state_get_cmd(tl, "show");
  assert(showcmd && showcmd->dstate);
  mtev_console_state_add_cmd(showcmd->dstate,
    NCSCMD("lua", mtev_console_show_lua, NULL, NULL, NULL));
}

void 
mtev_lua_new_coro(mtev_lua_resume_info_t *ri) {
  lua_module_closure_t *lmc = ri->lmc;
  lua_State *L = lmc->lua_state;
  lua_getglobal(L, "mtev_coros");
  ri->coro_state = lua_newthread(L);
  ri->coro_state_ref = luaL_ref(L, -2);
  lua_pop(L, 1); /* pops mtev_coros */
  mtevL(nldeb, "coro_store -> %p\n", ri->coro_state);
  pthread_mutex_lock(&coro_lock);
  mtev_hash_store(&mtev_coros,
                  (const char *)&ri->coro_state, sizeof(ri->coro_state),
                  ri);
  pthread_mutex_unlock(&coro_lock);
  return;
}
mtev_lua_resume_info_t *
mtev_lua_get_resume_info(lua_State *L) {
  mtev_lua_resume_info_t *ri;
  lua_module_closure_t *lmc;
  void *v = NULL;
  pthread_mutex_lock(&coro_lock);
  if(mtev_hash_retrieve(&mtev_coros, (const char *)&L, sizeof(L), &v)) {
    pthread_mutex_unlock(&coro_lock);
    ri = v;
    assert(pthread_equal(pthread_self(), ri->bound_thread));
    return ri;
  }
  ri = calloc(1, sizeof(*ri));
  ri->bound_thread = pthread_self();
  ri->coro_state = L;
  lua_getglobal(L, "mtev_internal_lmc");;
  ri->lmc = lua_touserdata(L, lua_gettop(L));
  lua_pop(L, 1);
  mtevL(nldeb, "coro_store -> %p\n", ri->coro_state);
  lua_getglobal(L, "mtev_coros");
  lua_pushthread(L);
  ri->coro_state_ref = luaL_ref(L, -2);
  lua_pop(L, 1); /* pops mtev_coros */
  mtev_hash_store(&mtev_coros,
                  (const char *)&ri->coro_state, sizeof(ri->coro_state),
                  ri);
  pthread_mutex_unlock(&coro_lock);
  return ri;
}

static void
mtev_event_dispose(void *ev) {
  int mask;
  eventer_t *value = ev;
  eventer_t removed, e = *value;
  mtevL(nldeb, "lua check cleanup: dropping (%p)->fd (%d)\n", e, e->fd);
  removed = eventer_remove(e);
  mtevL(nldeb, "    remove from eventer system %s\n",
        removed ? "succeeded" : "failed");
  if(e->mask & (EVENTER_READ|EVENTER_WRITE|EVENTER_EXCEPTION)) {
    mtevL(nldeb, "    closing down fd %d\n", e->fd);
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
mtev_lua_register_event(mtev_lua_resume_info_t *ci, eventer_t e) {
  eventer_t *eptr;
  eptr = calloc(1, sizeof(*eptr));
  memcpy(eptr, &e, sizeof(*eptr));
  if(!ci->events) {
    ci->events = calloc(1, sizeof(*ci->events));
    mtev_hash_init(ci->events);
  }
  assert(mtev_hash_store(ci->events, (const char *)eptr, sizeof(*eptr), eptr));
}
void
mtev_lua_deregister_event(mtev_lua_resume_info_t *ci, eventer_t e,
                                int tofree) {
  assert(ci->events);
  assert(mtev_hash_delete(ci->events, (const char *)&e, sizeof(e),
                          NULL, tofree ? mtev_event_dispose : free));
}
void
mtev_lua_resume_clean_events(mtev_lua_resume_info_t *ci) {
  if(ci->events == NULL) return;
  mtev_hash_destroy(ci->events, NULL, mtev_event_dispose);
  free(ci->events);
  ci->events = NULL;
}

void
mtev_lua_pushmodule(lua_State *L, const char *m) {
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
mtev_hash_table *
mtev_lua_table_to_hash(lua_State *L, int idx) {
  mtev_hash_table *t;
  if(lua_gettop(L) < idx || !lua_istable(L,idx))
    luaL_error(L, "table_to_hash: not a table");

  t = calloc(1, sizeof(*t));
  lua_pushnil(L);  /* first key */
  while (lua_next(L, idx) != 0) {
    const char *key, *value;
    size_t klen;
    key = lua_tolstring(L, -2, &klen);
    value = lua_tostring(L, -1);
    mtev_hash_store(t, key, klen, (void *)value);
    lua_pop(L, 1);
  }
  return t;
}
void
mtev_lua_hash_to_table(lua_State *L,
                       mtev_hash_table *t) {
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  const char *key, *value;
  int klen, kcnt;
  kcnt = t ? mtev_hash_size(t) : 0;
  lua_createtable(L, 0, kcnt);
  if(t) {
    while(mtev_hash_next_str(t, &iter, &key, &klen, &value)) {
      lua_pushlstring(L, value, strlen(value));
      lua_setfield(L, -2, key);
    }
  }
  return;
}

const char *
mtev_lua_type_name(int t) {
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

int
mtev_lua_yield(mtev_lua_resume_info_t *ci, int nargs) {
  mtevL(nldeb, "lua: %p yielding\n", ci->coro_state);
  return lua_yield(ci->coro_state, nargs);
}

static int mtev_lua_panic(lua_State *L) {
  if(L) {
    int level = 0;
    lua_Debug ar;
    const char *err = lua_tostring(L,2);
    
    while (lua_getstack(L, level++, &ar));
    mtevL(mtev_error, "lua panic[top:%d]: %s\n", lua_gettop(L), err);
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
        mtevL(mtev_error, "\t%s:%s(%s):%d\n", cp, ar.namewhat, ar.name, ar.currentline);
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
mtev_lua_open(const char *module_name, void *lmc,
              const char *script_dir, const char *cpath) {
  int rv;
  char cpath_lua[PATH_MAX];
  lua_State *L = luaL_newstate(), **Lptr;
  lua_atpanic(L, &mtev_lua_panic);

  lua_gc(L, LUA_GCSTOP, 0);  /* stop collector during initialization */
  luaL_openlibs(L);  /* open libraries */

  lua_newtable(L);
  lua_setglobal(L, "mtev_coros");

  if(lmc) {
    lua_pushlightuserdata(L, lmc);
    lua_setglobal(L, "mtev_internal_lmc");
  }

  lua_getglobal(L, "package");
  lua_pushfstring(L, "%s", script_dir);
  lua_setfield(L, -2, "path");

  if(!cpath)  {
    char *base = NULL;
    (void)mtev_conf_get_string(NULL, "//modules/@directory", &base);
    if(base) {
      snprintf(cpath_lua, sizeof(cpath_lua),
               "./?.so;%s/?.so;%s/?.so", MTEV_MODULES_DIR, base);
      cpath = cpath_lua;
      free(base);
    }
    else cpath = "./?.so;" MTEV_MODULES_DIR "/?.so";
  }
  lua_pushfstring(L, "%s", cpath);
  lua_setfield(L, -2, "cpath");
  lua_pop(L, 1);

  luaopen_pack(L);
  luaopen_bit(L);
  require(L, rv, mtev_lua);
  require(L, rv, mtev.timeval);
  require(L, rv, mtev.extras);

  lua_gc(L, LUA_GCRESTART, 0);

  Lptr = malloc(sizeof(*Lptr));
  *Lptr = L;
  pthread_mutex_lock(&mtev_lua_states_lock);
  mtev_hash_store(&mtev_lua_states,
                  (const char *)Lptr, sizeof(*Lptr),
                  (void *)(vpsized_int)pthread_self());
  pthread_mutex_unlock(&mtev_lua_states_lock);

  return L;
}
