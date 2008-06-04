/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include "noit_conf.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "utils/noit_log.h"
#include "eventer/eventer.h"
#include "lua_noit.h"

#include <assert.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

struct nl_slcl {
  struct timeval start;
  luaL_Buffer inbuff;
  int read_sofar;
  int read_goal;
  const char *read_terminator;
  const char *outbuff;
  int write_sofar;
  int write_goal;
  eventer_t *eptr;
  lua_State *L;
};

static int
noit_lua_socket_connect_complete(eventer_t e, int mask, void *vcl,
                                 struct timeval *now) {
  noit_lua_check_info_t *ci;
  struct nl_slcl *cl = vcl;
  int args = 0;

  ci = get_ci(cl->L);
  assert(ci);
  noit_lua_check_deregister_event(ci, e, 0);

  *(cl->eptr) = eventer_alloc();
  memcpy(*cl->eptr, e, sizeof(*e));
  noit_lua_check_register_event(ci, *cl->eptr);

  if(!(mask & EVENTER_EXCEPTION) &&
     mask & EVENTER_WRITE) {
    /* Connect completed successfully */
    lua_pushinteger(cl->L, 0);
    args = 1;
  }
  else {
    lua_pushinteger(cl->L, -1);
    lua_pushstring(cl->L, strerror(errno));
    args = 2;
  }
  noit_lua_resume(ci, args);
  return 0;
}
static int
noit_lua_socket_connect(lua_State *L) {
  noit_lua_check_info_t *ci;
  eventer_t e, *eptr;
  const char *target;
  unsigned short port;
  int8_t family;
  int rv;
  union {
    struct sockaddr_in sin4;
    struct sockaddr_in6 sin6;
  } a;

  ci = get_ci(L);
  assert(ci);

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  e = *eptr;
  target = lua_tostring(L, 1);
  port = lua_tointeger(L, 2);

  family = AF_INET;
  rv = inet_pton(family, target, &a.sin4.sin_addr);
  if(rv != 1) {
    family = AF_INET6;
    rv = inet_pton(family, target, &a.sin6.sin6_addr);
    if(rv != 1) {
      noitL(noit_stderr, "Cannot translate '%s' to IP\n", target);
      memset(&a, 0, sizeof(a));
      lua_pushinteger(L, -1);
      lua_pushfstring(L, "Cannot translate '%s' to IP\n", target);
      return 2;
    }
    else {
      /* We've IPv6 */
      a.sin6.sin6_family = AF_INET6;
      a.sin6.sin6_port = htons(port);
    }
  }
  else {
    a.sin4.sin_family = family;
    a.sin4.sin_port = htons(port);
  }

  rv = connect(e->fd, (struct sockaddr *)&a,
               family==AF_INET ? sizeof(a.sin4) : sizeof(a.sin6));
  if(rv == 0) {
    lua_pushinteger(L, 0);
    return 1;
  }
  if(rv == -1 && errno == EINPROGRESS) {
    /* Need completion */
    e->callback = noit_lua_socket_connect_complete;
    e->mask = EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
    eventer_add(e);
    return noit_lua_yield(ci, 0);
  }
  lua_pushinteger(L, -1);
  lua_pushstring(L, strerror(errno));
  return 2;
}

static int
noit_lua_socket_read_complete(eventer_t e, int mask, void *vcl,
                              struct timeval *now) {
  char buff[4096];
  noit_lua_check_info_t *ci;
  struct nl_slcl *cl = vcl;
  int len;
  int args = 0;

  ci = get_ci(cl->L);
  assert(ci);

  if(mask & EVENTER_EXCEPTION) {
    lua_pushnil(cl->L);
    args = 1;
    goto alldone;
  }

  while((len = e->opset->read(e->fd, buff, sizeof(buff), &mask, e)) > 0) {
    if(cl->read_goal) {
      int remaining = cl->read_goal - cl->read_sofar;
      /* copy up to the goal into the inbuff */
      luaL_addlstring(&cl->inbuff, buff, MIN(len, remaining));
      cl->read_sofar += len;
      if(cl->read_sofar >= cl->read_goal) { /* We're done */
        luaL_pushresult(&cl->inbuff);
        cl->read_sofar -= cl->read_goal;
        if(cl->read_sofar > 0) { /* We have to buffer this for next read */
          luaL_buffinit(cl->L, &cl->inbuff);
          luaL_addlstring(&cl->inbuff,
                          buff + remaining,
                          cl->read_sofar);
        }
        args = 1;
        break;
      }
    }
    else if(cl->read_terminator) {
      char *cp;
      int remaining = len;
      cp = strnstr(buff, cl->read_terminator, len);
      if(cp) remaining = cp - buff + strlen(cl->read_terminator);
      luaL_addlstring(&cl->inbuff, buff, MIN(len, remaining));
      cl->read_sofar += len;
      if(cp) {
        luaL_pushresult(&cl->inbuff);
        cl->read_sofar = len - remaining;
        if(cl->read_sofar > 0) { /* We have to buffer this for next read */
          luaL_buffinit(cl->L, &cl->inbuff);
          luaL_addlstring(&cl->inbuff, buff + remaining, cl->read_sofar);
        }
        args = 1;
        break;
      }
    }
  }
  if(len >= 0) {
    /* We broke out, cause we read enough... */
  }
  else if(len == -1 && errno == EAGAIN) {
    return EVENTER_READ | EVENTER_EXCEPTION;
  }
  else {
    lua_pushnil(cl->L);
    args = 1;
  }
 alldone:
  noit_lua_check_deregister_event(ci, e, 0);
  *(cl->eptr) = eventer_alloc();
  memcpy(*cl->eptr, e, sizeof(*e));
  noit_lua_check_register_event(ci, *cl->eptr);
  noit_lua_resume(ci, args);
  return 0;
}

static int
noit_lua_socket_read(lua_State *L) {
  struct nl_slcl *cl;
  noit_lua_check_info_t *ci;
  eventer_t e, *eptr;

  ci = get_ci(L);
  assert(ci);

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  e = *eptr;
  cl = e->closure;
  if(cl->read_sofar == 0) luaL_buffinit(L, &cl->inbuff);
  cl->read_goal = 0;
  cl->read_terminator = NULL;
  fprintf(stderr, "initiating read... (%d bytes buffered)\n", cl->read_sofar);

  if(lua_isnumber(L, 1)) {
    cl->read_goal = lua_tointeger(L, 1);
    fprintf(stderr, " read wants %d bytes\n", cl->read_goal);
    if(cl->read_goal <= cl->read_sofar) {
      const char *current_buff;
      int base;
      size_t len;
     i_know_better:
      base = lua_gettop(L);
      /* We have enough, we can service this right here */
      luaL_pushresult(&cl->inbuff);
      current_buff = lua_tolstring(L, base + 1, &len);
      assert(len == cl->read_sofar);
      lua_pop(L, 1);
      lua_pushlstring(L, current_buff, cl->read_goal);
      cl->read_sofar -= cl->read_goal;
      if(cl->read_sofar) {
        luaL_buffinit(L, &cl->inbuff);
        luaL_addlstring(&cl->inbuff, current_buff + cl->read_goal,
                        cl->read_sofar);
      }
      return 1;
    }
  }
  else {
    cl->read_terminator = lua_tostring(L, 1);
    fprintf(stderr, " read wants up to [%s]\n", cl->read_terminator);
    if(cl->read_sofar) {
      const char *cp;
      /* Ugh... inernalism */
      cp = strnstr(cl->inbuff.buffer, cl->read_terminator, cl->read_sofar);
      if(cp) {
        /* Here we matched... and we _know_ that someone actually wants:
         * strlen(cl->read_terminator) + cp - cl->inbuff.buffer bytes...
         * give it to them.
         */
        cl->read_goal = strlen(cl->read_terminator) + cp - cl->inbuff.buffer;
        cl->read_terminator = NULL;
        assert(cl->read_goal <= cl->read_sofar);
        goto i_know_better;
      }
    }
  }

  e->callback = noit_lua_socket_read_complete;
  e->mask = EVENTER_READ | EVENTER_EXCEPTION;
  eventer_add(e);
  return noit_lua_yield(ci, 0);
}
static int
noit_eventer_index_func(lua_State *L) {
  int n;
  const char *k;
  eventer_t *udata, e;
  n = lua_gettop(L);    /* number of arguments */
  assert(n == 2);
  if(!luaL_checkudata(L, 1, "eventer_t")) {
    luaL_error(L, "metatable error, arg1 not a eventer_t!");
  }
  udata = lua_touserdata(L, 1);
  e = *udata;
  if(!lua_isstring(L, 2)) {
    luaL_error(L, "metatable error, arg2 not a string!");
  }
  k = lua_tostring(L, 2);
  switch(*k) {
    case 'c':
     if(!strcmp(k, "connect")) {
       lua_pushlightuserdata(L, udata);
       lua_pushcclosure(L, noit_lua_socket_connect, 1);
       return 1;
     }
     break;
    case 'r':
     if(!strcmp(k, "read")) {
       lua_pushlightuserdata(L, udata);
       lua_pushcclosure(L, noit_lua_socket_read, 1);
       return 1;
     }
     break;
    default:
      break;
  }
  luaL_error(L, "eventer_t no such element: %s", k);
  return 0;
}

static eventer_t *
noit_lua_event(lua_State *L, eventer_t e) {
  eventer_t *addr;
  addr = (eventer_t *)lua_newuserdata(L, sizeof(e));
  *addr = e;
  if(luaL_newmetatable(L, "eventer_t") == 1) {
    lua_pushcclosure(L, noit_eventer_index_func, 0);
    lua_setfield(L, -2, "__index");
  }
  lua_setmetatable(L, -2);
  return addr;
}

static int
nl_sleep_complete(eventer_t e, int mask, void *vcl, struct timeval *now) {
  noit_lua_check_info_t *ci;
  struct nl_slcl *cl = vcl;
  struct timeval diff;
  double p_int;

  ci = get_ci(cl->L);
  assert(ci);
  noit_lua_check_deregister_event(ci, e, 0);

  sub_timeval(*now, cl->start, &diff);
  p_int = diff.tv_sec + diff.tv_usec / 1000000.0;
  lua_pushnumber(cl->L, p_int);
  free(cl);
  noit_lua_resume(ci, 1);
  return 0;
}

static int
nl_sleep(lua_State *L) {
  noit_lua_check_info_t *ci;
  struct nl_slcl *cl;
  struct timeval diff;
  eventer_t e;
  double p_int;

  ci = get_ci(L);
  assert(ci);

  p_int = lua_tonumber(L, 1);
  cl = calloc(1, sizeof(*cl));
  cl->L = L;
  gettimeofday(&cl->start, NULL);

  e = eventer_alloc();
  e->mask = EVENTER_TIMER;
  e->callback = nl_sleep_complete;
  e->closure = cl;
  memcpy(&e->whence, &cl->start, sizeof(cl->start));
  diff.tv_sec = floor(p_int);
  diff.tv_usec = (p_int - floor(p_int)) * 1000000;
  add_timeval(e->whence, diff, &e->whence);
  noit_lua_check_register_event(ci, e);
  eventer_add(e);
  return noit_lua_yield(ci, 0);
}

static int
nl_socket_tcp(lua_State *L, int family) {
  struct nl_slcl *cl;
  noit_lua_check_info_t *ci;
  socklen_t on;
  int fd;
  eventer_t e;

  fd = socket(family, SOCK_STREAM, 0);
  if(fd < 0) {
    lua_pushnil(L);
    return 1;
  }
  on = 1;
  if(ioctl(fd, FIONBIO, &on)) {
    close(fd);
    lua_pushnil(L);
    return 1;
  }

  ci = get_ci(L);
  assert(ci);

  cl = calloc(1, sizeof(*cl));
  cl->L = L;

  e = eventer_alloc();
  e->fd = fd;
  e->mask = 0;
  e->callback = NULL;
  e->closure = cl;
  cl->eptr = noit_lua_event(L, e);

  noit_lua_check_register_event(ci, e);
  return 1;
}
static int
nl_socket(lua_State *L) {
  return nl_socket_tcp(L, AF_INET);
}
static int
nl_socket_ipv6(lua_State *L) {
  return nl_socket_tcp(L, AF_INET6);
}

static const luaL_Reg noitlib[] = {
  { "sleep", nl_sleep },
  { "socket", nl_socket },
  { "socket_ipv6", nl_socket_ipv6 },
  { NULL, NULL }
};

int luaopen_noit(lua_State *L) {
  luaL_register(L, "noit", noitlib);
  return 0;
}
