/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
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
#include "utils/noit_str.h"
#include "eventer/eventer.h"
#include "lua_noit.h"

#include <assert.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
#include <zlib.h>

#define DEFLATE_CHUNK_SIZE 32768

static void
nl_extended_free(void *vcl) {
  struct nl_slcl *cl = vcl;
  if(cl->inbuff) free(cl->inbuff);
  free(cl);
}
static void
inbuff_addlstring(struct nl_slcl *cl, const char *b, int l) {
  int newsize = 0;
  char *newbuf;
  if(cl->inbuff_len + l > cl->inbuff_allocd)
    newsize = cl->inbuff_len + l;
  if(newsize) {
    newbuf = cl->inbuff_allocd ? realloc(cl->inbuff, newsize) : malloc(newsize);
    assert(newbuf);
    cl->inbuff = newbuf;
    cl->inbuff_allocd = newsize;
  }
  memcpy(cl->inbuff + cl->inbuff_len, b, l);
  cl->inbuff_len += l;
}

static int
noit_lua_socket_connect_complete(eventer_t e, int mask, void *vcl,
                                 struct timeval *now) {
  noit_lua_check_info_t *ci;
  struct nl_slcl *cl = vcl;
  int args = 0, aerrno;
  socklen_t aerrno_len = sizeof(aerrno);

  ci = get_ci(cl->L);
  assert(ci);
  noit_lua_check_deregister_event(ci, e, 0);

  *(cl->eptr) = eventer_alloc();
  memcpy(*cl->eptr, e, sizeof(*e));
  noit_lua_check_register_event(ci, *cl->eptr);

  if(getsockopt(e->fd,SOL_SOCKET,SO_ERROR, &aerrno, &aerrno_len) == 0)
    if(aerrno != 0) goto connerr;

  if(!(mask & EVENTER_EXCEPTION) &&
     mask & EVENTER_WRITE) {
    /* Connect completed successfully */
    lua_pushinteger(cl->L, 0);
    args = 1;
  }
  else {
    aerrno = errno;
   connerr:
    lua_pushinteger(cl->L, -1);
    lua_pushstring(cl->L, strerror(aerrno));
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
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;
  target = lua_tostring(L, 2);
  port = lua_tointeger(L, 3);

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
noit_lua_ssl_upgrade(eventer_t e, int mask, void *vcl,
                     struct timeval *now) {
  noit_lua_check_info_t *ci;
  struct nl_slcl *cl = vcl;
  int rv;
  
  rv = eventer_SSL_connect(e, &mask);
  if(rv <= 0 && errno == EAGAIN) return mask | EVENTER_EXCEPTION;

  ci = get_ci(cl->L);
  assert(ci);
  noit_lua_check_deregister_event(ci, e, 0);
  
  *(cl->eptr) = eventer_alloc();
  memcpy(*cl->eptr, e, sizeof(*e));
  noit_lua_check_register_event(ci, *cl->eptr);

  /* Upgrade completed successfully */
  lua_pushinteger(cl->L, (rv > 0) ? 0 : -1);
  noit_lua_resume(ci, 1);
  return 0;
}
static int
noit_lua_socket_connect_ssl(lua_State *L) {
  const char *ca, *ciphers, *cert, *key;
  eventer_ssl_ctx_t *sslctx;
  noit_lua_check_info_t *ci;
  eventer_t e, *eptr;
  struct timeval now;

  ci = get_ci(L);
  assert(ci);

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;
  cert = lua_tostring(L, 2);
  key = lua_tostring(L, 3);
  ca = lua_tostring(L, 4);
  ciphers = lua_tostring(L, 5);

  sslctx = eventer_ssl_ctx_new(SSL_CLIENT, cert, key, ca, ciphers);
  if(!sslctx) {
    lua_pushinteger(L, -1);
    return 1;
  }

  eventer_ssl_ctx_set_verify(sslctx, eventer_ssl_verify_cert, NULL);
  EVENTER_ATTACH_SSL(e, sslctx);
  e->callback = noit_lua_ssl_upgrade;
  gettimeofday(&now, NULL);
  e->mask = e->callback(e, EVENTER_READ|EVENTER_WRITE, e->closure, &now);
  if(e->mask & (EVENTER_READ|EVENTER_WRITE)) {
    /* Need completion */
    eventer_add(e);
    return noit_lua_yield(ci, 0);
  }
  lua_pushinteger(L, 0);
  return 1;
}

static int
noit_lua_socket_do_read(eventer_t e, int *mask, struct nl_slcl *cl,
                        int *read_complete) {
  char buff[4096];
  int len;
  *read_complete = 0;
  while((len = e->opset->read(e->fd, buff, sizeof(buff), mask, e)) > 0) {
    if(cl->read_goal) {
      int remaining = cl->read_goal - cl->read_sofar;
      /* copy up to the goal into the inbuff */
      inbuff_addlstring(cl, buff, MIN(len, remaining));
      cl->read_sofar += len;
      if(cl->read_sofar >= cl->read_goal) { /* We're done */
        lua_pushlstring(cl->L, cl->inbuff, cl->read_goal);
        *read_complete = 1;
        cl->read_sofar -= cl->read_goal;
        if(cl->read_sofar > 0) {  /* We have to buffer this for next read */
          cl->inbuff_len = 0;
          inbuff_addlstring(cl, buff + remaining, cl->read_sofar);
        }
        break;
      }
    }
    else if(cl->read_terminator) {
      const char *cp;
      int remaining = len;
      cp = strnstrn(cl->read_terminator, strlen(cl->read_terminator),
                    buff, len);
      if(cp) remaining = cp - buff + strlen(cl->read_terminator);
      inbuff_addlstring(cl, buff, MIN(len, remaining));
      cl->read_sofar += len;
      if(cp) {
        lua_pushlstring(cl->L, cl->inbuff, cl->inbuff_len);
        *read_complete = 1;
        
        cl->read_sofar = len - remaining;
        cl->inbuff_len = 0;
        if(cl->read_sofar > 0) { /* We have to buffer this for next read */
          inbuff_addlstring(cl, buff + remaining, cl->read_sofar);
        }
        break;
      }
    }
  }
  return len;
}
static int
noit_lua_socket_read_complete(eventer_t e, int mask, void *vcl,
                              struct timeval *now) {
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

  len = noit_lua_socket_do_read(e, &mask, cl, &args);
  if(len >= 0) {
    /* We broke out, cause we read enough... */
  }
  else if(len == -1 && errno == EAGAIN) {
    return mask | EVENTER_EXCEPTION;
  }
  else {
    lua_pushnil(cl->L);
    args = 1;
  }
 alldone:
  eventer_remove_fd(e->fd);
  noit_lua_check_deregister_event(ci, e, 0);
  *(cl->eptr) = eventer_alloc();
  memcpy(*cl->eptr, e, sizeof(*e));
  noit_lua_check_register_event(ci, *cl->eptr);
  noit_lua_resume(ci, args);
  return 0;
}

static int
noit_lua_socket_read(lua_State *L) {
  int args, mask, len;
  struct nl_slcl *cl;
  noit_lua_check_info_t *ci;
  eventer_t e, *eptr;

  ci = get_ci(L);
  assert(ci);

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;
  cl = e->closure;
  cl->read_goal = 0;
  cl->read_terminator = NULL;

  if(lua_isnumber(L, 2)) {
    cl->read_goal = lua_tointeger(L, 2);
    if(cl->read_goal <= cl->read_sofar) {
      int base;
     i_know_better:
      base = lua_gettop(L);
      /* We have enough, we can service this right here */
      lua_pushlstring(L, cl->inbuff, cl->read_goal);
      cl->read_sofar -= cl->read_goal;
      if(cl->read_sofar) {
        memmove(cl->inbuff, cl->inbuff + cl->read_goal, cl->read_sofar);
      }
      cl->inbuff_len = cl->read_sofar;
      return 1;
    }
  }
  else {
    cl->read_terminator = lua_tostring(L, 2);
    if(cl->read_sofar) {
      const char *cp;
      /* Ugh... inernalism */
      cp = strnstrn(cl->read_terminator, strlen(cl->read_terminator),
                    cl->inbuff, cl->read_sofar);
      if(cp) {
        /* Here we matched... and we _know_ that someone actually wants:
         * strlen(cl->read_terminator) + cp - cl->inbuff.buffer bytes...
         * give it to them.
         */
        cl->read_goal = strlen(cl->read_terminator) + cp - cl->inbuff;
        cl->read_terminator = NULL;
        assert(cl->read_goal <= cl->read_sofar);
        goto i_know_better;
      }
    }
  }

  len = noit_lua_socket_do_read(e, &mask, cl, &args);
  if(args == 1) return 1; /* completed read, return result */
  if(len == -1 && errno == EAGAIN) {
    /* we need to drop into eventer */
  }
  else {
    lua_pushnil(cl->L);
    args = 1;
    return args;
  }

  e->callback = noit_lua_socket_read_complete;
  e->mask = mask | EVENTER_EXCEPTION;
  eventer_add(e);
  return noit_lua_yield(ci, 0);
}
static int
noit_lua_socket_write_complete(eventer_t e, int mask, void *vcl,
                               struct timeval *now) {
  noit_lua_check_info_t *ci;
  struct nl_slcl *cl = vcl;
  int rv;
  int args = 0;

  ci = get_ci(cl->L);
  assert(ci);

  if(mask & EVENTER_EXCEPTION) {
    lua_pushinteger(cl->L, -1);
    args = 1;
    goto alldone;
  }
  while((rv = e->opset->write(e->fd,
                              cl->outbuff + cl->write_sofar,
                              MIN(cl->send_size, cl->write_goal),
                              &mask, e)) > 0) {
    cl->write_sofar += rv;
    assert(cl->write_sofar <= cl->write_goal);
    if(cl->write_sofar == cl->write_goal) break;
  }
  if(rv > 0) {
    lua_pushinteger(cl->L, cl->write_goal);
    args = 1;
  }
  else if(rv == -1 && errno == EAGAIN) {
    return mask | EVENTER_EXCEPTION;
  }
  else {
    lua_pushinteger(cl->L, -1);
    args = 1;
    if(rv == -1) {
      lua_pushstring(cl->L, strerror(errno));
      args++;
    }
  }

 alldone:
  eventer_remove_fd(e->fd);
  noit_lua_check_deregister_event(ci, e, 0);
  *(cl->eptr) = eventer_alloc();
  memcpy(*cl->eptr, e, sizeof(*e));
  noit_lua_check_register_event(ci, *cl->eptr);
  noit_lua_resume(ci, args);
  return 0;
}
static int
noit_lua_socket_write(lua_State *L) {
  int rv, mask;
  struct nl_slcl *cl;
  noit_lua_check_info_t *ci;
  eventer_t e, *eptr;

  ci = get_ci(L);
  assert(ci);

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;
  cl = e->closure;
  cl->write_sofar = 0;
  cl->outbuff = lua_tolstring(L, 2, &cl->write_goal);

  while((rv = e->opset->write(e->fd,
                              cl->outbuff + cl->write_sofar,
                              MIN(cl->send_size, cl->write_goal),
                              &mask, e)) > 0) {
    cl->write_sofar += rv;
    assert(cl->write_sofar <= cl->write_goal);
    if(cl->write_sofar == cl->write_goal) break;
  }
  if(rv > 0) {
    lua_pushinteger(L, cl->write_goal);
    return 1;
  }
  if(rv == -1 && errno == EAGAIN) {
    e->callback = noit_lua_socket_write_complete;
    e->mask = mask | EVENTER_EXCEPTION;
    eventer_add(e);
    return noit_lua_yield(ci, 0);
  }
  lua_pushinteger(L, -1);
  return 1;
}
static int
noit_lua_socket_ssl_ctx(lua_State *L) {
  eventer_t *eptr, e;
  eventer_ssl_ctx_t **ssl_ctx_holder, *ssl_ctx;

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;

  ssl_ctx = eventer_get_eventer_ssl_ctx(e);
  if(!ssl_ctx) {
    lua_pushnil(L);
    return 1;
  }

  ssl_ctx_holder = (eventer_ssl_ctx_t **)lua_newuserdata(L, sizeof(ssl_ctx));
  *ssl_ctx_holder = ssl_ctx;
  luaL_getmetatable(L, "noit.eventer.ssl_ctx");
  lua_setmetatable(L, -2);
  return 1;
}
static int
noit_eventer_index_func(lua_State *L) {
  int n;
  const char *k;
  eventer_t *udata, e;
  n = lua_gettop(L); /* number of arguments */
  assert(n == 2);
  if(!luaL_checkudata(L, 1, "noit.eventer")) {
    luaL_error(L, "metatable error, arg1 not a noit.eventer!");
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
    case 's':
     if(!strcmp(k, "ssl_upgrade_socket")) {
       lua_pushlightuserdata(L, udata);
       lua_pushcclosure(L, noit_lua_socket_connect_ssl, 1);
       return 1;
     }
     if(!strcmp(k, "ssl_ctx")) {
       lua_pushlightuserdata(L, udata);
       lua_pushcclosure(L, noit_lua_socket_ssl_ctx, 1);
       return 1;
     }
     break;
    case 'w':
     if(!strcmp(k, "write")) {
       lua_pushlightuserdata(L, udata);
       lua_pushcclosure(L, noit_lua_socket_write, 1);
       return 1;
     }
     break;
    default:
      break;
  }
  luaL_error(L, "noit.eventer no such element: %s", k);
  return 0;
}

static eventer_t *
noit_lua_event(lua_State *L, eventer_t e) {
  eventer_t *addr;
  addr = (eventer_t *)lua_newuserdata(L, sizeof(e));
  *addr = e;
  luaL_getmetatable(L, "noit.eventer");
  lua_setmetatable(L, -2);
  return addr;
}

static int
noit_ssl_ctx_index_func(lua_State *L) {
  int n;
  const char *k;
  eventer_ssl_ctx_t **udata, *ssl_ctx;
  n = lua_gettop(L); /* number of arguments */
  assert(n == 2);
  if(!luaL_checkudata(L, 1, "noit.eventer.ssl_ctx")) {
    luaL_error(L, "metatable error, arg1 not a noit.eventer.ssl_ctx!");
  }
  udata = lua_touserdata(L, 1);
  ssl_ctx = *udata;
  if(!lua_isstring(L, 2)) {
    luaL_error(L, "metatable error, arg2 not a string!");
  }
  k = lua_tostring(L, 2);
  switch(*k) {
    case 'e':
      if(!strcmp(k,"error")) {
        lua_pushstring(L,eventer_ssl_get_peer_error(ssl_ctx));
        return 1;
      }
      if(!strcmp(k,"end_time")) {
        lua_pushinteger(L,eventer_ssl_get_peer_end_time(ssl_ctx));
        return 1;
      }
      break;
    case 'i':
      if(!strcmp(k,"issuer")) {
        lua_pushstring(L,eventer_ssl_get_peer_issuer(ssl_ctx));
        return 1;
      }
      break;
    case 's':
      if(!strcmp(k,"subject")) {
        lua_pushstring(L,eventer_ssl_get_peer_subject(ssl_ctx));
        return 1;
      }
      if(!strcmp(k,"start_time")) {
        lua_pushinteger(L,eventer_ssl_get_peer_start_time(ssl_ctx));
        return 1;
      }
      break;
    default:
      break;
  }
  luaL_error(L, "noit.eventer.ssl_ctx no such element: %s", k);
  return 0;
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
  cl->free = nl_extended_free;
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
nl_log(lua_State *L) {
  int i, n;
  const char *log_dest, *message;
  noit_log_stream_t ls;

  if(lua_gettop(L) < 2) luaL_error(L, "bad call to noit.log");

  log_dest = lua_tostring(L, 1);
  ls = noit_log_stream_find(log_dest);
  if(!ls) {
    noitL(noit_stderr, "Cannot find log stream: '%s'\n", log_dest);
    return 0;
  }

  n = lua_gettop(L);
  lua_pushstring(L, "string");
  lua_gettable(L, LUA_GLOBALSINDEX);
  lua_pushstring(L, "format");
  lua_gettable(L, -1);
  for(i=2;i<=n;i++)
    lua_pushvalue(L, i);
  lua_call(L, n-1, 1);
  message = lua_tostring(L, -1);
  noitL(ls, "%s", message);
  lua_pop(L, 1); /* formatted string */
  lua_pop(L, 1); /* "string" table */
  return 0;
}
static int
nl_gettimeofday(lua_State *L) {
  struct timeval now;
  gettimeofday(&now, NULL);
  lua_pushinteger(L, now.tv_sec);
  lua_pushinteger(L, now.tv_usec);
  return 2;
}
static int
nl_socket_tcp(lua_State *L, int family) {
  struct nl_slcl *cl;
  noit_lua_check_info_t *ci;
  socklen_t optlen;
  int fd;
  eventer_t e;

  fd = socket(family, SOCK_STREAM, 0);
  if(fd < 0) {
    lua_pushnil(L);
    return 1;
  }
  if(eventer_set_fd_nonblocking(fd)) {
    close(fd);
    lua_pushnil(L);
    return 1;
  }

  ci = get_ci(L);
  assert(ci);

  cl = calloc(1, sizeof(*cl));
  cl->free = nl_extended_free;
  cl->L = L;

  optlen = sizeof(cl->send_size);
  if(getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &cl->send_size, &optlen) != 0)
    cl->send_size = 4096;

  e = eventer_alloc();
  e->fd = fd;
  e->mask = EVENTER_EXCEPTION;
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

static int
nl_gunzip_deflate(lua_State *L) {
  const char *input;
  size_t inlen;
  z_stream *stream;
  Bytef *data = NULL;
  uLong outlen = 0;
  int limit = 1024*1024;
  int err, n = lua_gettop(L);

  if(n < 1 || n > 2) {
    lua_pushnil(L);
    return 1;
  }

  stream = lua_touserdata(L, lua_upvalueindex(1));

  input = lua_tolstring(L, 1, &inlen);
  if(!input) {
    lua_pushnil(L);
    return 1;
  }
  if(n == 2)
    limit = lua_tointeger(L, 2);

  stream->next_in = (Bytef *)input;
  stream->avail_in = inlen;
  while(1) {
    err = inflate(stream, Z_FULL_FLUSH);
    if(err == Z_OK || err == Z_STREAM_END) {
      /* got some data */
      int size_read = DEFLATE_CHUNK_SIZE - stream->avail_out;
      uLong newoutlen = outlen + size_read;
      if(newoutlen > limit) {
        err = Z_MEM_ERROR;
        break;
      }
      if(newoutlen > outlen) {
        Bytef *newdata;
        if(data) newdata = realloc(data, newoutlen);
        else newdata = malloc(newoutlen);
        if(!newdata) {
          err = Z_MEM_ERROR;
          break;
        }
        data = newdata;
        memcpy(data + outlen, stream->next_out - size_read, size_read);
        outlen += size_read;
        stream->next_out -= size_read;
        stream->avail_out += size_read;
      }
      if(err == Z_STREAM_END) {
        /* Good to go */
        break;
      }
    }
    else break;
    if(stream->avail_in == 0) break;
  }
  if(err == Z_OK || err == Z_STREAM_END) {
    if(outlen > 0) lua_pushlstring(L, (char *)data, outlen);
    else lua_pushstring(L, "");
    if(data) free(data);
    return 1;
  }
  lua_pushnil(L);
  return 1;
}
static int
nl_gunzip(lua_State *L) {
  z_stream *stream;
  
  stream = (z_stream *)lua_newuserdata(L, sizeof(*stream));
  memset(stream, 0, sizeof(*stream));
  luaL_getmetatable(L, "noit.gunzip");
  lua_setmetatable(L, -2);

  stream->next_in = NULL;
  stream->avail_in = 0;
  stream->next_out = malloc(DEFLATE_CHUNK_SIZE);
  stream->avail_out = stream->next_out ? DEFLATE_CHUNK_SIZE : 0;
  inflateInit2(stream, MAX_WBITS+32);

  lua_pushcclosure(L, nl_gunzip_deflate, 1);
  return 1;
}
static int
noit_lua_gunzip_gc(lua_State *L) {
  z_stream *stream;
  stream = (z_stream *)lua_touserdata(L,1);
  if(stream->next_out) free(stream->next_out);
  inflateEnd(stream);
  return 0;
}

static int
noit_lua_pcre_match(lua_State *L) {
  const char *subject;
  pcre **re;
  int i, cnt, ovector[30];
  size_t inlen;

  re = (pcre **)lua_touserdata(L, lua_upvalueindex(1));
  subject = lua_tolstring(L,1,&inlen);
  if(!subject) {
    lua_pushboolean(L,0);
    return 1;
  }
  cnt = pcre_exec(*re, NULL, subject, inlen, 0, 0,
                  ovector, sizeof(ovector)/sizeof(*ovector));
  if(cnt <= 0) {
    lua_pushboolean(L,0);
    return 1;
  }
  lua_pushboolean(L,1);
  for(i = 0; i < cnt; i++) {
    int start = ovector[i*2];
    int end = ovector[i*2+1];
    lua_pushlstring(L, subject+start, end-start);
  }
  return cnt+1;
}
static int
nl_pcre(lua_State *L) {
  pcre *re, **holder;
  const char *expr;
  const char *errstr;
  int erroff;

  expr = lua_tostring(L,1); 
  re = pcre_compile(expr, 0, &errstr, &erroff, NULL);
  if(!re) {
    lua_pushnil(L);
    lua_pushstring(L, errstr);
    lua_pushinteger(L, erroff);
    return 3;
  }
  holder = (pcre **)lua_newuserdata(L, sizeof(*holder));
  *holder = re;
  luaL_getmetatable(L, "noit.pcre");
  lua_setmetatable(L, -2);
  lua_pushcclosure(L, noit_lua_pcre_match, 1);
  return 1;
}
static int
noit_lua_pcre_gc(lua_State *L) {
  pcre **holder;
  holder = (pcre **)lua_touserdata(L,1);
  pcre_free(*holder);
  return 0;
}

static int
nl_conf_get_string(lua_State *L) {
  char *val;
  const char *path = lua_tostring(L,1);
  if(path &&
     noit_conf_get_string(NULL, path, &val)) {
    lua_pushstring(L,val);
    free(val);
  }
  else lua_pushnil(L);
  return 1;
}
static int
nl_conf_get_integer(lua_State *L) {
  int val;
  const char *path = lua_tostring(L,1);
  if(path &&
     noit_conf_get_int(NULL, path, &val)) {
    lua_pushinteger(L,val);
  }
  else lua_pushnil(L);
  return 1;
}
static int
nl_conf_get_boolean(lua_State *L) {
  noit_boolean val;
  const char *path = lua_tostring(L,1);
  if(path &&
     noit_conf_get_boolean(NULL, path, &val)) {
    lua_pushboolean(L,val);
  }
  else lua_pushnil(L);
  return 1;
}
static int
nl_conf_get_float(lua_State *L) {
  float val;
  const char *path = lua_tostring(L,1);
  if(path &&
     noit_conf_get_float(NULL, path, &val)) {
    lua_pushnumber(L,val);
  }
  else lua_pushnil(L);
  return 1;
}
static const luaL_Reg noitlib[] = {
  { "sleep", nl_sleep },
  { "gettimeofday", nl_gettimeofday },
  { "socket", nl_socket },
  { "log", nl_log },
  { "pcre", nl_pcre },
  { "socket_ipv6", nl_socket_ipv6 },
  { "gunzip", nl_gunzip },
  { "conf_get", nl_conf_get_string },
  { "conf_get_string", nl_conf_get_string },
  { "conf_get_integer", nl_conf_get_integer },
  { "conf_get_boolean", nl_conf_get_boolean },
  { "conf_get_number", nl_conf_get_float },
  { NULL, NULL }
};

int luaopen_noit(lua_State *L) {
  luaL_newmetatable(L, "noit.eventer");
  lua_pushcclosure(L, noit_eventer_index_func, 0);
  lua_setfield(L, -2, "__index");

  luaL_newmetatable(L, "noit.eventer.ssl_ctx");
  lua_pushcclosure(L, noit_ssl_ctx_index_func, 0);
  lua_setfield(L, -2, "__index");

  luaL_newmetatable(L, "noit.gunzip");
  lua_pushcfunction(L, noit_lua_gunzip_gc);
  lua_setfield(L, -2, "__gc");

  luaL_newmetatable(L, "noit.pcre");
  lua_pushcfunction(L, noit_lua_pcre_gc);
  lua_setfield(L, -2, "__gc");

  luaL_register(L, "noit", noitlib);
  return 0;
}

void noit_lua_init() {
  eventer_name_callback("lua/sleep", nl_sleep_complete);
  eventer_name_callback("lua/socket_read",
                        noit_lua_socket_read_complete);
  eventer_name_callback("lua/socket_write",
                        noit_lua_socket_write_complete);
  eventer_name_callback("lua/socket_connect",
                        noit_lua_socket_connect_complete);
  eventer_name_callback("lua/ssl_upgrade", noit_lua_ssl_upgrade);
}
