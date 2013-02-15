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

#define DEFAULT_MAX_POST_SIZE 1024*1024
#define lua_web_driver_xml_description  ""
static const char *script_dir = "";

typedef struct lua_web_conf {
  lua_module_closure_t lmc;
  const char *script_dir;
  const char *dispatch;
  int max_post_size;
  lua_State *L;
} lua_web_conf_t;

typedef struct lua_resume_rest_info {
  noit_http_rest_closure_t *restc;
  char *err;
  int httpcode;
} lua_resume_rest_info_t;

static lua_web_conf_t *the_one_conf = NULL;
static lua_web_conf_t *get_config(noit_module_generic_t *self) {
  if(the_one_conf) return the_one_conf;
  the_one_conf = noit_image_get_userdata(&self->hdr);
  if(the_one_conf) return the_one_conf;
  the_one_conf = calloc(1, sizeof(*the_one_conf));
  noit_image_set_userdata(&self->hdr, the_one_conf);
  return the_one_conf;
}

static void
rest_lua_ctx_free(void *cl) {
  noit_lua_resume_info_t *ri = cl;
  if(ri) {
    noit_lua_cancel_coro(ri);
    noit_lua_resume_clean_events(ri);
    if(ri->context_data) {
      lua_resume_rest_info_t *ctx = ri->context_data;
      if(ctx->err) free(ctx->err);
    }
    free(ri);
  }
}

static int
lua_web_restc_fastpath(noit_http_rest_closure_t *restc,
                       int npats, char **pats) {
  noit_lua_resume_info_t *ri = restc->call_closure;
  noit_http_response *res = noit_http_session_response(restc->http_ctx);
  lua_resume_rest_info_t *ctx = ri->context_data;

  if(noit_http_response_complete(res) != noit_true) {
    noit_http_response_standard(restc->http_ctx,
                                (ctx && ctx->httpcode) ? ctx->httpcode : 500,
                                "ERROR", "text/html");
    if(ctx->err) noit_http_response_append(restc->http_ctx, ctx->err, strlen(ctx->err));
    noit_http_response_end(restc->http_ctx);
  }

  noit_http_rest_clean_request(restc);
  return 0;
}

static int
lua_web_resume(noit_lua_resume_info_t *ri, int nargs) {
  const char *err = NULL;
  int status, base, rv = 0;
  lua_resume_rest_info_t *ctx = ri->context_data;
  noit_http_rest_closure_t *restc = ctx->restc;
  noit_http_response *res = noit_http_session_response(restc->http_ctx);
  eventer_t conne = noit_http_connection_event(noit_http_session_connection(restc->http_ctx));

  status = lua_resume(ri->coro_state, nargs);

  switch(status) {
    case 0: break;
    case LUA_YIELD:
      lua_gc(ri->coro_state, LUA_GCCOLLECT, 0);
      return 0;
    default: /* The complicated case */
      ctx->httpcode = 500;
      base = lua_gettop(ri->coro_state);
      if(base>=0) {
        if(lua_isstring(ri->coro_state, base-1)) {
          err = lua_tostring(ri->coro_state, base-1);
          noitL(noit_error, "err -> %s\n", err);
          if(!ctx->err) ctx->err = strdup(err);
        }
      }
      rv = -1;
  }

  lua_web_restc_fastpath(restc, 0, NULL);
  eventer_add(conne);
  eventer_trigger(conne, EVENTER_READ|EVENTER_WRITE);
  return rv;
}
static void req_payload_free(void *d, int64_t s, void *c) {
  (void)s;
  (void)c;
  if(d) free(d);
}
static int
lua_web_handler(noit_http_rest_closure_t *restc,
                int npats, char **pats) {
  int status, base, rv, mask = 0;
  int complete = 0;
  lua_web_conf_t *conf = the_one_conf;
  lua_module_closure_t *lmc = &conf->lmc;
  noit_lua_resume_info_t *ri;
  lua_resume_rest_info_t *ctx = NULL;
  lua_State *L;
  eventer_t conne;
  noit_http_request *req = noit_http_session_request(restc->http_ctx);
  noit_http_response *res = noit_http_session_response(restc->http_ctx);

  if(!lmc || !conf || !conf->dispatch) {
    goto boom;
  }

  if(noit_http_request_get_upload(req, NULL) == NULL &&
     noit_http_request_has_payload(req)) {
    const void *payload = NULL;
    int payload_len = 0;
    payload = rest_get_raw_upload(restc, &mask, &complete, &payload_len);
    if(!complete) return mask;
    noit_http_request_set_upload(req, (char *)payload, (int64_t)payload_len,
                                 req_payload_free, NULL);
    restc->call_closure_free(restc->call_closure);
    restc->call_closure = NULL;
  }

  if(restc->call_closure == NULL) {
    ri = calloc(1, sizeof(*ri));
    ri->context_magic = LUA_REST_INFO_MAGIC;
    ctx = ri->context_data = calloc(1, sizeof(lua_resume_rest_info_t));
    ctx->restc = restc;
    ri->lmc = lmc;
    lua_getglobal(lmc->lua_state, "noit_coros");
    ri->coro_state = lua_newthread(lmc->lua_state);
    ri->coro_state_ref = luaL_ref(lmc->lua_state, -2);

    noit_lua_set_resume_info(lmc->lua_state, ri);

    lua_pop(lmc->lua_state, 1); /* pops noit_coros */

    restc->call_closure = ri;
    restc->call_closure_free = rest_lua_ctx_free;
  }
  ri = restc->call_closure;
  ctx = ri->context_data;
  ctx->httpcode = 404;

  L = ri->coro_state;

  lua_getglobal(L, "require");
  lua_pushstring(L, conf->dispatch);
  rv = lua_pcall(L, 1, 1, 0);
  if(rv) {
    int i;
    noitL(noit_error, "lua: require %s failed\n", conf->dispatch);
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

  noit_lua_pushmodule(L, conf->dispatch);
  if(lua_isnil(L, -1)) {
    lua_pop(L, 1);
    ctx->err = strdup("no such module");
    goto boom;
  }
  lua_getfield(L, -1, "handler");
  lua_remove(L, -2);
  if(!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    ctx->err = strdup("no 'handler' function in module");
    goto boom;
  }
  noit_lua_setup_restc(L, restc);
  noit_lua_hash_to_table(L, restc->ac->config);

  conne = noit_http_connection_event(noit_http_session_connection(restc->http_ctx));
  eventer_remove(conne);
  restc->fastpath = lua_web_restc_fastpath;

  status = lmc->resume(ri, 2);
  if(status == 0) return 0;

  if(noit_http_response_complete(res) != noit_true) {
 boom:
    noit_http_response_standard(restc->http_ctx,
                                (ctx && ctx->httpcode) ? ctx->httpcode : 500,
                                "ERROR", "text/plain");
    if(ctx->err) noit_http_response_append(restc->http_ctx, ctx->err, strlen(ctx->err));
    noit_http_response_end(restc->http_ctx);
  }
  return 0;
}

static int
noit_lua_web_driver_onload(noit_image_t *self) {
  return 0;
}

static int
noit_lua_web_driver_config(noit_module_generic_t *self, noit_hash_table *o) {
  lua_web_conf_t *conf = get_config(self);
  conf->script_dir = "";
  conf->dispatch = NULL;
  noit_hash_retr_str(o, "directory", strlen("directory"), &conf->script_dir);
  if(conf->script_dir) conf->script_dir = strdup(conf->script_dir);
  noit_hash_retr_str(o, "dispatch", strlen("dispatch"), &conf->dispatch);
  if(conf->dispatch) conf->dispatch = strdup(conf->dispatch);
  conf->max_post_size = DEFAULT_MAX_POST_SIZE;
  return 0;
}

static noit_hook_return_t late_stage_rest_register(void *cl) {
  assert(noit_http_rest_register("GET", "/", "(.*)$", lua_web_handler) == 0);
  assert(noit_http_rest_register("POST", "/", "(.*)$", lua_web_handler) == 0);
  return NOIT_HOOK_CONTINUE;
}
static int
noit_lua_web_driver_init(noit_module_generic_t *self) {
  lua_web_conf_t *conf = get_config(self);
  lua_module_closure_t *lmc = &conf->lmc;
  lmc->resume = lua_web_resume;
  lmc->lua_state = noit_lua_open(self->hdr.name, lmc, conf->script_dir);
  if(lmc->lua_state == NULL) return -1;
  lmc->pending = calloc(1, sizeof(*lmc->pending));
  module_post_init_hook_register("web_lua", late_stage_rest_register, NULL);
  return 0;
}

noit_module_generic_t lua_web = {
  {
    NOIT_GENERIC_MAGIC,
    NOIT_GENERIC_ABI_VERSION,
    "lua_web",
    "web services in lua",
    lua_web_driver_xml_description,
    noit_lua_web_driver_onload
  },
  noit_lua_web_driver_config,
  noit_lua_web_driver_init
};
