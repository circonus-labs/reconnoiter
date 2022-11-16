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


#include <stdio.h>

#include "lua.h"
#include <mtev_log.h>
#include <mtev_defines.h>
#include "lua_mtev.h"
#include "noit_metric_rollup.h"
#include "noit_metric_director.h"
#include "noit_metric_tag_search.h"

void noit_lua_libnoit_init();
static ck_spinlock_t noit_lua_libnoit_init_lock = CK_SPINLOCK_INITIALIZER;

// We need to hold-on to a reference to the metric name as long as we make use of the tagset
// we also need to hold onto any dynamically constructed tag strings we add.
typedef struct noit_lua_tagset_allocation {
  struct noit_lua_tagset_allocation *next;
  char data[0];
} noit_lua_tagset_allocation_t;

typedef struct {
  noit_metric_tagset_t tagset;
  int lua_name_ref;
  noit_lua_tagset_allocation_t *allocations;
} noit_lua_tagset_t;

static inline char *noit_lua_tagset_alloc(noit_lua_tagset_t *tagset, size_t len) {
  noit_lua_tagset_allocation_t *na = malloc(offsetof(noit_lua_tagset_allocation_t, data) + len);
  na->next = tagset->allocations;
  tagset->allocations = na;
  return na->data;
}

// account_set is a map: account_id => account_interests
// account_interests is an array of integers, such that account_interests[lane] > 0
//   if we should deliver messages for the given account_id to the lane.
static mtev_hash_table *account_set = NULL;

static int
noit_lua_tagset_copy_setup(lua_State *, noit_lua_tagset_t *);

static int
lua_noit_metric_adjustsubscribe(lua_State *L, short bump) {
  const char* uuid = luaL_checkstring(L, 1);
  const char* metric_name = luaL_checkstring(L, 2);
  uuid_t id;
  if(mtev_uuid_parse(uuid, id)) {
    return luaL_error(L, "(un)subscribe expects a uuid as the first parameter");
  }
  caql_cnt_t cnt = noit_metric_director_adjust_metric_interest(id, metric_name, bump);
  lua_pushnumber(L, cnt);
  return 1;
}

static int
lua_noit_metric_register_search(lua_State *L) {
  uuid_t uuid = { 0 };
  int account_id = luaL_checkinteger(L,1);
  const char *uuid_str = NULL;
  if(!lua_isnil(L,2)) {
    uuid_str = luaL_checkstring(L,2);
    if(mtev_uuid_parse(uuid_str, uuid)) {
      return luaL_error(L, "invalid UUID");
    }
  }
  noit_metric_tag_search_ast_t **udata = (noit_metric_tag_search_ast_t **)
    luaL_checkudata(L, 3, "noit_metric_tag_search_ast_t");
  lua_pushinteger(L, noit_metric_director_register_search(account_id, uuid, NULL, *udata));
  return 1;
}

static int
lua_noit_metric_deregister_search(lua_State *L) {
  uuid_t uuid = { 0 };
  int account_id = luaL_checkinteger(L,1);
  const char *uuid_str = NULL;
  if(!lua_isnil(L,2)) {
    uuid_str = luaL_checkstring(L,2);
    if(mtev_uuid_parse(uuid_str, uuid)) {
      return luaL_error(L, "invalid UUID");
    }
  }
  uint32_t ref = luaL_checkinteger(L,3);
  noit_metric_director_deregister_search(account_id, uuid, NULL, ref);
  return 0;
}

static int
lua_noit_metric_subscribe(lua_State *L) {
  return lua_noit_metric_adjustsubscribe(L, 1);
}

static int
lua_noit_metric_unsubscribe(lua_State *L) {
  return lua_noit_metric_adjustsubscribe(L, -1);
}

static int
lua_noit_checks_adjustsubscribe(lua_State *L, short bump) {
  caql_cnt_t cnt = noit_metric_director_adjust_checks_interest(bump);
  lua_pushnumber(L, cnt);
  return 1;
}

static int
lua_noit_checks_subscribe(lua_State *L) {
  return lua_noit_checks_adjustsubscribe(L, 1);
}

static int
lua_noit_checks_unsubscribe(lua_State *L) {
  return lua_noit_checks_adjustsubscribe(L, -1);
}

mtev_hook_return_t
hook_metric_subscribe_all(void *closure, noit_metric_message_t *m, int *w, int wlen) {
  int *lane = (int*) closure;
  assert(*lane < wlen);
  w[*lane] = 1;
  return MTEV_HOOK_CONTINUE;
}

static int
lua_noit_metric_subscribe_all(lua_State *L) {
  int *lane = malloc(sizeof(int));
  *lane = noit_metric_director_my_lane();
  metric_director_want_hook_register("metrics_select", hook_metric_subscribe_all, lane);
  return 0;
}

mtev_hook_return_t
hook_metric_subscribe_account(void *closure, noit_metric_message_t *m, int *w, int wlen) {
  if(account_set == NULL) {
    return MTEV_HOOK_ABORT;
  }
  int account_id = m->id.account_id;
  int *account_interests;
  if(mtev_hash_retrieve(account_set, (const char *)&account_id, sizeof(account_id), (void **)&account_interests)){
    for(int i=0; i<wlen; i++){
      w[i] = account_interests[i];
    }
  }
  return MTEV_HOOK_CONTINUE;
}

static int
lua_noit_metric_subscribe_account(lua_State *L) {
  if(account_set == NULL) {
    noit_lua_libnoit_init();
  }
  int account_id = luaL_checkint(L, 1);
  int *account_interests = NULL;
  while(!mtev_hash_retrieve(account_set, (const char *)&account_id, sizeof(account_id), (void **)&account_interests)){
    int nthreads = eventer_loop_concurrency();
    account_interests = calloc(nthreads, (sizeof(*account_interests)));
    int *account_id_copy = calloc(1, sizeof(int));
    *account_id_copy = account_id;
    int rc = mtev_hash_store(account_set, (const char *)account_id_copy, sizeof(*account_id_copy), (void **)account_interests);
    if(!rc) {
      // We lost the race. Try again
      free(account_id_copy);
      free(account_interests);
    }
    else {
      break;
    }
  }
  assert(account_interests);
  account_interests[noit_metric_director_my_lane()] += 1;
  return 0;
}

static int
nl_push_tagset(lua_State *L, noit_metric_tagset_t const *tagset) {
  char tag_buf[NOIT_TAG_MAX_PAIR_LEN];
  lua_newtable(L);
  if (tagset != NULL) {
    for (int i = 0; i < tagset->tag_count; i++) {
      noit_metric_tag_t tag = tagset->tags[i];
      int decoded_total_size = noit_metric_tagset_decode_tag(tag_buf, NOIT_TAG_MAX_PAIR_LEN, tag.tag, tag.total_size);
      if(decoded_total_size <= 0) {
        return luaL_error(L, "Error decoding tag");
      }
      // tagset decoded replaces the : with the ASCII unit separator 0x1f
      char *sep = (char *) memchr(tag_buf, 0x1f, NOIT_TAG_MAX_PAIR_LEN);
      int decoded_category_size = sep - tag_buf;
      if(decoded_category_size <= 0) {
        return luaL_error(L, "Error decoding tag");
      }
      lua_pushlstring(L, tag_buf, decoded_category_size);
      lua_pushlstring(L, tag_buf + decoded_category_size + 1, decoded_total_size - decoded_category_size - 1);
      lua_settable(L, -3);
    }
  }
  return 0;
}

static int noit_metric_id_index_func(lua_State *L) {
  int n;
  const char *k;
  noit_metric_id_t **udata, *metric_id;
  n = lua_gettop(L); /* number of arguments */
  mtevAssert(n == 2);
  if(!luaL_checkudata(L, 1, "metric_id_t")) {
    luaL_error(L, "metatable error, arg1 not a metric_id_t!");
    return 1;
  }
  udata = lua_touserdata(L, 1);
  metric_id = *udata;
  if(!lua_isstring(L, 2)) {
    luaL_error(L, "metatable error, arg2 not a string!");
    return 1;
  }

  k = lua_tostring(L, 2);
  switch (*k) {
    case 'a':
      if(!strcmp(k, "account_id")){
        lua_pushinteger(L, metric_id->account_id);
      } else {
        break;
      }
      return 1;
    case 'c':
      if(!strcmp(k, "canonical_name")) {
        lua_pushlstring(L, metric_id->name, metric_id->name_len_with_tags);
      }
      else if(!strcmp(k, "ctags") || !strcmp(k, "check_tags")) {
        /* Before passing along the check tags, we need to
           1. Augment the tag-set by the check uuid we get from the message
           2. Invoke the fixup hook, that will add replicated check tags.
         */
        if ( metric_id->check.tag_count > MAX_TAGS - 1 ) { return 0; }
        /* Step 0: Copy everything to the stack, so we can mutate */
        noit_metric_tag_t check_tags[MAX_TAGS];
        memcpy(&check_tags, metric_id->check.tags, metric_id->check.tag_count * sizeof(noit_metric_tag_t));
        noit_metric_tagset_t check_tagset = {0};
        memcpy(&check_tagset, &metric_id->check, sizeof(noit_metric_tagset_t));
        check_tagset.tags = check_tags;

        /* Step 1: Add uuid tag */
        char uuid_str[13 + UUID_STR_LEN + 1];
        strcpy(uuid_str, "__check_uuid:");
        mtev_uuid_unparse_lower(metric_id->id, uuid_str + 13);
        noit_metric_tag_t uuid_tag = { .tag = uuid_str, .total_size = strlen(uuid_str), .category_size = 13 };
        check_tagset.tags[check_tagset.tag_count] = uuid_tag;
        check_tagset.tag_count++;

        /* Step 2: Fixup check tags */
        if(noit_metric_tagset_fixup_hook_invoke(NOIT_METRIC_TAGSET_CHECK, &check_tagset) == MTEV_HOOK_ABORT) {
          return 0;
        };

        if(!strcmp(k, "check_tags")) {
          nl_push_tagset(L, &check_tagset);
        }
        else if(!strcmp(k, "ctags")) {
        /* wrap into lua_tagset */
          lua_pushvalue(L, 2);
          int ref = luaL_ref(L, LUA_REGISTRYINDEX);
          noit_lua_tagset_t check_lua_tagset = { .tagset = check_tagset, .lua_name_ref = ref };

          for(int i=0; i<check_tagset.tag_count; i++) {
            if(check_tagset.tags[i].tag == uuid_str) {  /* This is on stack, move it somewhere safe */
              char *copy = noit_lua_tagset_alloc(&check_lua_tagset, check_tagset.tags[i].total_size+1);
              memcpy(copy, check_tagset.tags[i].tag, check_tagset.tags[i].total_size);
              copy[check_tagset.tags[i].total_size] = '\0';
              check_tagset.tags[i].tag = copy;
            }
          }
          noit_lua_tagset_copy_setup(L, &check_lua_tagset);
        }
      } else {
        break;
      }
      return 1;
    case 'i':
      if(!strcmp(k, "id")) {
        char uuid_str[UUID_PRINTABLE_STRING_LENGTH];
        mtev_uuid_unparse_lower(metric_id->id, uuid_str);
        lua_pushstring(L, uuid_str);
      } else if(!strcmp(k, "id_ud")) {
        lua_pushlightuserdata(L, (void*)&metric_id->id);
      } else {
        break;
      }
      return 1;
    case 'n':
      if(!strcmp(k, "name")) {
        lua_pushlstring(L, metric_id->name, metric_id->name_len_with_tags);
      } else if(!strcmp(k, "name_len")) {
        lua_pushinteger(L, metric_id->name_len_with_tags);
      } else if(!strcmp(k, "name_without_tags")) {
        lua_pushlstring(L, metric_id->name, metric_id->name_len);
      } else if(!strcmp(k, "name_len_without_tags")) {
        lua_pushinteger(L, metric_id->name_len);
      } else {
        break;
      }
      return 1;
    case 'm':
      if(!strcmp(k, "mtags")) {
        lua_pushvalue(L, 2);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        noit_lua_tagset_t set = { .tagset = metric_id->measurement, .lua_name_ref = ref };
        noit_lua_tagset_copy_setup(L, &set);
      } else if (!strcmp(k, "measurement_tags")) {
        nl_push_tagset(L, &metric_id->measurement);
      } else {
        break;
      }
      return 1;
    case 's':
      if(!strcmp(k, "stags")) {
        lua_pushvalue(L, 2);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        noit_lua_tagset_t set = { .tagset = metric_id->stream, .lua_name_ref = ref };
        noit_lua_tagset_copy_setup(L, &set);
      } else if (!strcmp(k, "stream_tags")) {
        nl_push_tagset(L, &metric_id->stream);
      } else {
        break;
      }
      return 1;
    case 't':
      if(!strcmp(k, "tagless_name")) {
        lua_pushlstring(L, metric_id->name, metric_id->name_len);
      }
      return 1;
    default:
      break;
  }
  luaL_error(L, "metric_id_t no such element: %s", k);
  return 0;
}

static void
noit_lua_setup_metric_id(lua_State *L,
    noit_metric_id_t *msg) {
  noit_metric_id_t **addr;
  addr = (noit_metric_id_t **)lua_newuserdata(L, sizeof(msg));
  *addr = msg;
  if(luaL_newmetatable(L, "metric_id_t") == 1) {
    lua_pushcclosure(L, noit_metric_id_index_func, 0);
    lua_setfield(L, -2, "__index");
  }
  lua_setmetatable(L, -2);
}

struct id_in_one {
  noit_metric_id_t *addr;
  noit_metric_id_t impl;
  noit_metric_tag_t stream_tags[MAX_TAGS];
  noit_metric_tag_t measurement_tags[MAX_TAGS];
  char canonical_name[0];
};

static int
noit_lua_new_metric_id(lua_State *L) {
  struct id_in_one *addr;
  int account_id = luaL_checkinteger(L,1);
  const char *uuid_str = luaL_checkstring(L,2);
  size_t canonical_name_len;
  const char *canonical_name = luaL_checklstring(L,3,&canonical_name_len);
  addr = (struct id_in_one *)lua_newuserdata(L, sizeof(*addr) + canonical_name_len + 1);
  memset(addr, 0, sizeof(*addr));
  memcpy(addr->canonical_name, canonical_name, canonical_name_len + 1);
  addr->addr = &addr->impl;
  addr->impl.account_id = account_id;
  if(mtev_uuid_parse(uuid_str, addr->impl.id) != 0) luaL_error(L, "invalid uuid");
  addr->impl.name = addr->canonical_name;
  addr->impl.name_len_with_tags = canonical_name_len;
  addr->impl.stream.tags = addr->stream_tags;
  addr->impl.stream.tag_count = MAX_TAGS;
  addr->impl.measurement.tags = addr->measurement_tags;
  addr->impl.measurement.tag_count = MAX_TAGS;
  addr->impl.name_len = noit_metric_parse_tags(addr->impl.name, addr->impl.name_len_with_tags,
                                               &addr->impl.stream, &addr->impl.measurement);
  if(addr->impl.name_len < 0) return 0;

  if(luaL_newmetatable(L, "metric_id_t") == 1) {
    lua_pushcclosure(L, noit_metric_id_index_func, 0);
    lua_setfield(L, -2, "__index");
  }
  lua_setmetatable(L, -2);
  return 1;
}

static int
noit_message_index_func(lua_State *L) {
  int n;
  const char *k;
  noit_metric_message_t **udata, *msg;
  n = lua_gettop(L);    /* number of arguments */
  mtevAssert(n == 2);
  if(!luaL_checkudata(L, 1, "metric_message_t")) {
    luaL_error(L, "metatable error, arg1 not a metric_message_t!");
    return 1;
  }
  udata = lua_touserdata(L, 1);
  msg = *udata;
  if(!lua_isstring(L, 2)) {
    luaL_error(L, "metatable error, arg2 not a string!");
    return 1;
  }

  k = lua_tostring(L, 2);
  switch (*k) {
    case 'i':
      if(!strcmp(k, "id")) {
        noit_lua_setup_metric_id(L, &msg->id);
      } else {
        break;
      }
      return 1;
    case 'w':
      if(!strcmp(k, "whence_ms")) {
        lua_pushinteger(L, msg->value.whence_ms);
      } else {
        break;
      }
      return 1;
    case 'v':
      if(!strcmp(k, "value")) {
        lua_pushlightuserdata(L, (void*)&msg->value);
      } else {
        break;
      }
      return 1;
    case 't':
      if(!strcmp(k, "type")) {
        lua_pushinteger(L, msg->type);
      } else {
        break;
      }
      return 1;
    case 'o':
      if(!strcmp(k, "original_message")) {
        lua_pushstring(L, msg->original_message);
      } else {
        break;
      }
      return 1;
    default:
      break;
  }
  luaL_error(L, "metric_message_t no such element: %s", k);
  return 0;
}

static int
noit_lua_free_message(lua_State *L) {
  noit_metric_message_t **udata;
  if(!luaL_checkudata(L, 1, "metric_message_t")) {
    luaL_error(L, "metatable error, arg1 not a metric_message_t!");
    return 1;
  }
  udata = lua_touserdata(L, 1);

  noit_metric_director_message_deref(*udata);
  return 0;
}

static void
noit_lua_setup_message(lua_State *L,
    noit_metric_message_t *msg) {
  noit_metric_message_t **addr;
  addr = (noit_metric_message_t **)lua_newuserdata(L, sizeof(msg));
  *addr = msg;
  if(luaL_newmetatable(L, "metric_message_t") == 1) {
    lua_pushcclosure(L, noit_message_index_func, 0);
    lua_setfield(L, -2, "__index");

    lua_pushcclosure(L, noit_lua_free_message, 0);
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);
}

static int
lua_noit_metric_next(lua_State *L) {
  noit_metric_message_t *msg;
  uint32_t backlog = 0;
  msg = noit_metric_director_lane_next_backlog(&backlog);
  if(msg) {
    noit_lua_setup_message(L, msg);
    lua_pushinteger(L, backlog);
    return 2;
  }
  return 0;
}

static int
lua_noit_metric_drop_backlogged(lua_State *L) {
  uint32_t t = luaL_checknumber(L, 1);
  noit_metric_director_drop_backlogged(t);
  return 0;
}

static int
lua_noit_metric_drop_before(lua_State *L) {
  double t = luaL_checknumber(L, 1);
  noit_metric_director_drop_before(t);
  return 0;
}

static int
noit_lua_tag_search_ast_free(lua_State *L) {
  noit_metric_tag_search_ast_t **udata = (noit_metric_tag_search_ast_t **)
    luaL_checkudata(L, 1, "noit_metric_tag_search_ast_t");
  noit_metric_tag_search_free(*udata);
  return 0;
}

static int
noit_lua_tag_search_ast_setup(lua_State *L, noit_metric_tag_search_ast_t *ast) {
  noit_metric_tag_search_ast_t **udata = (noit_metric_tag_search_ast_t **) lua_newuserdata(L, sizeof(ast));
  *udata = ast;
  if(luaL_newmetatable(L, "noit_metric_tag_search_ast_t") == 1){
    lua_pushcclosure(L, noit_lua_tag_search_ast_free, 0);
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);
  return 0;
}

// Parse tag search query string into a tag-search ast
// param: query
// retuns: ast
static int
lua_noit_tag_search_parse(lua_State *L) {
  const char *query = luaL_checkstring(L, 1);
  noit_metric_tag_search_ast_t *ast;
  int erroroff;
  ast = noit_metric_tag_search_parse(query, &erroroff);
  if (ast == NULL) {
    luaL_error(L, "Error parsing tag_search query (%s) at position %d", query, erroroff);
    return 0;
  }
  noit_lua_tag_search_ast_setup(L, ast);
  return 1;
}

static int
noit_lua_tagset_copy_free(lua_State *L) {
  noit_lua_tagset_t **udata = (noit_lua_tagset_t **)
    luaL_checkudata(L, 1, "noit_lua_tagset_t");
  noit_lua_tagset_t *set = *udata;
  luaL_unref(L, LUA_REGISTRYINDEX, set->lua_name_ref);
  free(set->tagset.tags);
  noit_lua_tagset_allocation_t *tofree;
  while(NULL != (tofree = set->allocations)) {
    set->allocations = set->allocations->next;
    free(tofree);
  }
  free(set);
  return 0;
}

static int
noit_lua_tagset_copy_setup(lua_State *L, noit_lua_tagset_t *src_set) {
  noit_lua_tagset_t *dst_set = calloc(1, sizeof(*src_set));
  memcpy(dst_set, src_set, sizeof(*dst_set));
  noit_metric_tag_t *dst_tags = calloc(src_set->tagset.tag_count, sizeof(noit_metric_tag_t));
  memcpy(dst_tags, src_set->tagset.tags, src_set->tagset.tag_count*sizeof(noit_metric_tag_t));
  dst_set->tagset.tags = dst_tags;
  noit_lua_tagset_t **udata = (noit_lua_tagset_t **) lua_newuserdata(L, sizeof(dst_set));
  *udata = dst_set;
  if(luaL_newmetatable(L, "noit_lua_tagset_t") == 1) {
    lua_pushcclosure(L, noit_lua_tagset_copy_free, 0);
    lua_setfield(L, -2, "__gc");
  }
  lua_setmetatable(L, -2);
  return 0;
}

// Convert a tagset to strings
// param: tag
// returns: tag1, tag2, ...
static int
lua_noit_tag_tostring(lua_State *L) {
  noit_lua_tagset_t **udata = (noit_lua_tagset_t **) luaL_checkudata(L, 1, "noit_lua_tagset_t");
  noit_lua_tagset_t *lset = *udata;
  noit_metric_tagset_t *set = &(lset->tagset);
  int cnt = set->tag_count;
  for (int i=0; i<cnt; i++) {
    noit_metric_tag_t tag = set->tags[i];
    lua_pushlstring(L, tag.tag, tag.total_size);
  }
  return cnt;
}

// Parse a metric name to a tagset
// param: name
// returns: stags, mtags (userdata)
static int
lua_noit_tag_parse(lua_State *L) {
  const char* name = luaL_checkstring(L, 1);
  noit_metric_tag_t stags[MAX_TAGS], mtags[MAX_TAGS];
  noit_metric_tagset_t stset = { .tags = stags, .tag_count = MAX_TAGS };
  noit_metric_tagset_t mtset = { .tags = mtags, .tag_count = MAX_TAGS };
  int rc = noit_metric_parse_tags(name, strlen(name), &stset, &mtset);
  if (rc < 0) {
    luaL_error(L, "Error parsing tags for metric %s", name);
    return 0;
  }
  int ref;
  lua_pushvalue(L, 1);
  ref = luaL_ref(L, LUA_REGISTRYINDEX);
  noit_lua_tagset_t l_stset = { .tagset = stset, .lua_name_ref = ref };
  lua_pushvalue(L, 1);
  ref = luaL_ref(L, LUA_REGISTRYINDEX);
  noit_lua_tagset_t l_mtset = { .tagset = mtset, .lua_name_ref = ref };
  noit_lua_tagset_copy_setup(L, &l_stset);
  noit_lua_tagset_copy_setup(L, &l_mtset);
  return 2;
}

// Eval tag search query against tagset
// param: ast (userdata)
// param: tagset (userdata)
// returns: matches (boolean)
static int
lua_noit_tag_search_eval(lua_State *L) {
  noit_metric_tag_search_ast_t **ast_ud = (noit_metric_tag_search_ast_t **)
    luaL_checkudata(L, 1, "noit_metric_tag_search_ast_t");
  noit_metric_tagset_t **set_ud = (noit_metric_tagset_t **)
    luaL_checkudata(L, 2, "noit_lua_tagset_t");
  mtev_boolean ok = noit_metric_tag_search_evaluate_against_tags(*ast_ud, *set_ud);
  lua_pushboolean(L, ok);
  return 1;
}

// Eval tag search query against string
// param: ast (userdata)
// param: name (string)
// returns: matches (boolean)
static int
lua_noit_tag_search_eval_string(lua_State *L) {
  noit_metric_tag_search_ast_t **ast_ud = (noit_metric_tag_search_ast_t **)
    luaL_checkudata(L, 1, "noit_metric_tag_search_ast_t");
  const char* name = luaL_checkstring(L, 2);
  noit_metric_tag_t stags[MAX_TAGS], mtags[MAX_TAGS];
  noit_metric_tagset_t stset = { .tags = stags, .tag_count = MAX_TAGS };
  noit_metric_tagset_t mtset = { .tags = mtags, .tag_count = MAX_TAGS };
  int rc = noit_metric_parse_tags(name, strlen(name), &stset, &mtset);
  if (rc < 0) {
    luaL_error(L, "Error parsing tagset for metric %s", name);
    return 0;
  }
  mtev_boolean ok = noit_metric_tag_search_evaluate_against_tags(*ast_ud, &mtset);
  lua_pushboolean(L, ok);
  return 1;
}

// Eval tag search query against a metric_message_t
// param: ast (userdata)
// param: message (userdata)
// returns: match (boolean)
static int
lua_noit_tag_search_eval_message(lua_State *L) {
  noit_metric_tag_search_ast_t **ast_ud = (noit_metric_tag_search_ast_t **)
    luaL_checkudata(L, 1, "noit_metric_tag_search_ast_t");
  noit_metric_message_t **msg_ud = (noit_metric_message_t **)
    luaL_checkudata(L, 2, "metric_message_t");
  noit_metric_message_t *msg = *msg_ud;

  mtev_boolean ok = noit_metric_tag_search_evaluate_against_metric_id(*ast_ud, &msg->id);
  lua_pushboolean(L, ok);
  return 1;
}


void
noit_lua_libnoit_init() {
  // It would be better to call this function once during startup.
  // However, I did not find a good place to put this, so we are using locks instead,
  // to ensure this get's called only once.
  ck_spinlock_lock(&noit_lua_libnoit_init_lock);
  if (account_set == NULL) {
    mtev_hash_table *tmp = calloc(1, sizeof(*account_set));
    mtev_hash_init(tmp);
    ck_pr_store_ptr(&account_set, tmp);
    metric_director_want_hook_register("metrics_select", hook_metric_subscribe_account, NULL);
  }
  ck_spinlock_unlock(&noit_lua_libnoit_init_lock);
}

#ifndef NO_LUAOPEN_LIBNOIT
static const luaL_Reg libnoit_binding[] = {
  { "metric_director_register_search", lua_noit_metric_register_search },
  { "metric_director_deregister_search", lua_noit_metric_deregister_search },
  { "metric_director_subscribe_checks", lua_noit_checks_subscribe },
  { "metric_director_unsubscribe_checks", lua_noit_checks_unsubscribe },
  { "metric_director_subscribe", lua_noit_metric_subscribe },
  { "metric_director_unsubscribe", lua_noit_metric_unsubscribe },
  { "metric_director_next", lua_noit_metric_next },
  { "metric_director_subscribe_all", lua_noit_metric_subscribe_all},
  { "metric_director_subscribe_account", lua_noit_metric_subscribe_account},
  { "metric_director_drop_backlogged", lua_noit_metric_drop_backlogged},
  { "metric_director_drop_before", lua_noit_metric_drop_before},
  { "metric_id_new", noit_lua_new_metric_id},
  { "tag_parse", lua_noit_tag_parse},
  { "tag_tostring", lua_noit_tag_tostring},
  { "tag_search_parse", lua_noit_tag_search_parse},
  { "tag_search_eval", lua_noit_tag_search_eval},
  { "tag_search_eval_string", lua_noit_tag_search_eval_string},
  { "tag_search_eval_message", lua_noit_tag_search_eval_message},
  { NULL, NULL }
};

LUALIB_API int luaopen_libnoit_binding(lua_State *L)
{
  luaL_openlib(L, "libnoit_binding", libnoit_binding, 0);
  return 1;
}
#endif
