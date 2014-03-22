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

#include <assert.h>
#include <math.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <dirent.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
#include <sys/wait.h>
#include <zlib.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/tree.h>
#include <libxml/HTMLparser.h>
#include <openssl/md5.h>
#include <openssl/hmac.h>

#include "noit_conf.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_xml.h"
#include "utils/noit_log.h"
#include "utils/noit_str.h"
#include "utils/noit_b32.h"
#include "utils/noit_b64.h"
#include "eventer/eventer.h"
#include "json-lib/json.h"
#define LUA_COMPAT_MODULE
#include "lua_noit.h"

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;

#define DEFLATE_CHUNK_SIZE 32768
#define ON_STACK_LUA_STRLEN 2048

#define LUA_DISPATCH(n, f) \
     if(!strcmp(k, #n)) { \
       lua_pushlightuserdata(L, udata); \
       lua_pushcclosure(L, f, 1); \
       return 1; \
     }
#define LUA_RETSTRING(n, g) \
     if(!strcmp(k, #n)) { \
       lua_pushstring(L, g); \
       return 1; \
     }
#define LUA_RETINTEGER(n, g) \
     if(!strcmp(k, #n)) { \
       lua_pushinteger(L, g); \
       return 1; \
     }

typedef struct {
  struct json_tokener *tok;
  struct json_object *root;
} json_crutch;

static void
nl_extended_free(void *vcl) {
  struct nl_slcl *cl = vcl;
  if(cl->inbuff) free(cl->inbuff);
  free(cl);
}
static int
lua_push_inet_ntop(lua_State *L, struct sockaddr *r) {
  char remote_str[128];
  int len;
  switch(r->sa_family) {
    case AF_INET:
      len = sizeof(struct sockaddr_in);
      inet_ntop(AF_INET, &((struct sockaddr_in *)r)->sin_addr,
                remote_str, len);
      lua_pushstring(L, remote_str);
      lua_pushinteger(L, ntohs(((struct sockaddr_in *)r)->sin_port));
      break;
    case AF_INET6:
      len = sizeof(struct sockaddr_in6);
      inet_ntop(AF_INET6, &((struct sockaddr_in6 *)r)->sin6_addr,
                remote_str, len);
      lua_pushstring(L, remote_str);
      lua_pushinteger(L, ntohs(((struct sockaddr_in6 *)r)->sin6_port));
      break;
    default:
      lua_pushnil(L);
      lua_pushnil(L);
  }
  return 2;
}
static void
inbuff_addlstring(struct nl_slcl *cl, const char *b, int l) {
  int newsize = 0;
  char *newbuf;

  if (cl->inbuff_len < 0 || l < 0) {
    noitL(nldeb, "Invalid Argument: An argument was negative");
    abort();
  }
  if (cl->inbuff_len + l < 0) {
    noitL(nldeb, "Error: Addition Overflow");
    abort();
  }

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
noit_lua_socket_close(lua_State *L) {
  noit_lua_resume_info_t *ci;
  eventer_t *eptr, e;
  struct nl_slcl *cl;
  int newmask;

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;
  if (e == NULL) return 0;

  ci = noit_lua_get_resume_info(L);
  assert(ci);

  noit_lua_check_deregister_event(ci, e, 0);
  eventer_remove_fd(e->fd);
  e->opset->close(e->fd, &newmask, e);
  cl = e->closure;
  eventer_free(e);
  if(cl && cl->free) cl->free(cl);
  return 0;
}

static int
noit_lua_socket_connect_complete(eventer_t e, int mask, void *vcl,
                                 struct timeval *now) {
  noit_lua_resume_info_t *ci;
  struct nl_slcl *cl = vcl;
  int args = 0, aerrno;
  socklen_t aerrno_len = sizeof(aerrno);

  /* If the pointer has been cleared, this has been gc'd */
  if(!*(cl->eptr)) {
    noitL(nlerr, "noit.eventer callback post GC\n");
    return 0;
  }

  ci = noit_lua_get_resume_info(cl->L);
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
  ci->lmc->resume(ci, args);
  return 0;
}
static int
noit_lua_socket_recv_complete(eventer_t e, int mask, void *vcl,
                              struct timeval *now) {
  noit_lua_resume_info_t *ci;
  struct nl_slcl *cl = vcl;
  int rv, args = 0;
  void *inbuff = NULL;
  socklen_t alen;

  /* If the pointer has been cleared, this has been gc'd */
  if(!*(cl->eptr)) {
    noitL(nlerr, "noit.eventer callback post GC\n");
    return 0;
  }

  ci = noit_lua_get_resume_info(cl->L);
  assert(ci);

  if(mask & EVENTER_EXCEPTION) {
    lua_pushinteger(cl->L, -1);
    args = 1;
    goto alldone;
  }

  inbuff = malloc(cl->read_goal);
  if(!inbuff) {
    lua_pushinteger(cl->L, -1);
    args = 1;
    goto alldone;
  }

  alen = sizeof(cl->address);
  while((rv = recvfrom(e->fd, inbuff, cl->read_goal, 0,
                       (struct sockaddr *)&cl->address, &alen)) == -1 &&
        errno == EINTR);
  if(rv < 0) {
    if(errno == EAGAIN) {
      free(inbuff);
      return EVENTER_READ | EVENTER_EXCEPTION;
    }
    lua_pushinteger(cl->L, rv);
    lua_pushstring(cl->L, strerror(errno));
    args = 2;
  }
  else {
    lua_pushinteger(cl->L, rv);
    lua_pushlstring(cl->L, inbuff, rv);
    args = 2;
    args += lua_push_inet_ntop(cl->L, (struct sockaddr *)&cl->address);
  }

 alldone:
  if(inbuff) free(inbuff);
  eventer_remove_fd(e->fd);
  noit_lua_check_deregister_event(ci, e, 0);
  *(cl->eptr) = eventer_alloc();
  memcpy(*cl->eptr, e, sizeof(*e));
  noit_lua_check_register_event(ci, *cl->eptr);
  ci->lmc->resume(ci, args);
  return 0;
}
static int
noit_lua_socket_recv(lua_State *L) {
  int args, rv;
  struct nl_slcl *cl;
  noit_lua_resume_info_t *ci;
  eventer_t e, *eptr;
  void *inbuff;
  socklen_t alen;

  ci = noit_lua_get_resume_info(L);
  assert(ci);

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;
  cl = e->closure;
  cl->read_goal = lua_tointeger(L, 2);
  inbuff = malloc(cl->read_goal);

  alen = sizeof(cl->address);
  while((rv = recvfrom(e->fd, inbuff, cl->read_goal, 0,
                       (struct sockaddr *)&cl->address, &alen)) == -1 &&
        errno == EINTR);
  if(rv < 0) {
    if(errno == EAGAIN) {
      e->callback = noit_lua_socket_recv_complete;
      e->mask = EVENTER_READ | EVENTER_EXCEPTION;
      eventer_add(e);
      free(inbuff);
      return noit_lua_yield(ci, 0);
    }
    lua_pushinteger(cl->L, rv);
    lua_pushstring(cl->L, strerror(errno));
    args = 2;
  }
  else {
    lua_pushinteger(cl->L, rv);
    lua_pushlstring(cl->L, inbuff, rv);
    args = 2;
    args += lua_push_inet_ntop(cl->L, (struct sockaddr *)&cl->address);
  }
  free(inbuff);
  return args;
}
static int
noit_lua_socket_send_complete(eventer_t e, int mask, void *vcl,
                              struct timeval *now) {
  noit_lua_resume_info_t *ci;
  struct nl_slcl *cl = vcl;
  int sbytes;
  int args = 0;

  /* If the pointer has been cleared, this has been gc'd */
  if(!*(cl->eptr)) {
    noitL(nlerr, "noit.eventer callback post GC\n");
    return 0;
  }

  ci = noit_lua_get_resume_info(cl->L);
  assert(ci);

  if(mask & EVENTER_EXCEPTION) {
    lua_pushinteger(cl->L, -1);
    args = 1;
    goto alldone;
  }
  if(cl->sendto) {
    while((sbytes = sendto(e->fd, cl->outbuff, cl->write_goal, 0,
                           (struct sockaddr *)&cl->address,
                           cl->address.sin4.sin_family==AF_INET ?
                               sizeof(cl->address.sin4) :
                               sizeof(cl->address.sin6))) == -1 &&
          errno == EINTR);
  }
  else {
    while((sbytes = send(e->fd, cl->outbuff, cl->write_goal, 0)) == -1 &&
          errno == EINTR);
  }
  if(sbytes > 0) {
    lua_pushinteger(cl->L, sbytes);
    args = 1;
  }
  else if(sbytes == -1 && errno == EAGAIN) {
    return EVENTER_WRITE | EVENTER_EXCEPTION;
  }
  else {
    lua_pushinteger(cl->L, sbytes);
    args = 1;
    if(sbytes == -1) {
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
  ci->lmc->resume(ci, args);
  return 0;
}
static int
noit_lua_socket_send(lua_State *L) {
  noit_lua_resume_info_t *ci;
  eventer_t e, *eptr;
  const void *bytes;
  size_t nbytes;
  ssize_t sbytes;

  ci = noit_lua_get_resume_info(L);
  assert(ci);

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;
  if(lua_gettop(L) != 2)
    luaL_error(L, "noit.socket.send with bad arguments");
  bytes = lua_tolstring(L, 2, &nbytes);

  while((sbytes = send(e->fd, bytes, nbytes, 0)) == -1 && errno == EINTR);
  if(sbytes < 0 && errno == EAGAIN) {
    struct nl_slcl *cl;
    /* continuation */
    cl = e->closure;
    cl->write_sofar = 0;
    cl->outbuff = bytes;
    cl->write_goal = nbytes;
    cl->sendto = 0;
    e->callback = noit_lua_socket_send_complete;
    e->mask = EVENTER_WRITE | EVENTER_EXCEPTION;
    eventer_add(e);
    return noit_lua_yield(ci, 0);
  }
  lua_pushinteger(L, sbytes);
  if(sbytes < 0) {
    lua_pushstring(L, strerror(errno));
    return 2;
  }
  return 1;
}

static int
noit_lua_socket_sendto(lua_State *L) {
  noit_lua_resume_info_t *ci;
  eventer_t e, *eptr;
  const char *target;
  unsigned short port;
  int8_t family;
  int rv;
  const void *bytes;
  size_t nbytes;
  ssize_t sbytes;
  union {
    struct sockaddr_in sin4;
    struct sockaddr_in6 sin6;
  } a;

  ci = noit_lua_get_resume_info(L);
  assert(ci);

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;
  if(lua_gettop(L) != 4)
    luaL_error(L, "noit.socket.sendto with bad arguments");
  bytes = lua_tolstring(L, 2, &nbytes);
  target = lua_tostring(L, 3);
  if(!target) target = "";
  port = lua_tointeger(L, 4);

  family = AF_INET;
  rv = inet_pton(family, target, &a.sin4.sin_addr);
  if(rv != 1) {
    family = AF_INET6;
    rv = inet_pton(family, target, &a.sin6.sin6_addr);
    if(rv != 1) {
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

  while((sbytes = sendto(e->fd, bytes, nbytes, 0, (struct sockaddr *)&a,
                         family==AF_INET ? sizeof(a.sin4)
                                         : sizeof(a.sin6))) == -1 &&
        errno == EINTR);
  if(sbytes < 0 && errno == EAGAIN) {
    struct nl_slcl *cl;
    /* continuation */
    cl = e->closure;
    cl->write_sofar = 0;
    cl->outbuff = bytes;
    cl->write_goal = nbytes;
    cl->sendto = 1;
    e->callback = noit_lua_socket_send_complete;
    e->mask = EVENTER_WRITE | EVENTER_EXCEPTION;
    eventer_add(e);
    return noit_lua_yield(ci, 0);
  }
  lua_pushinteger(L, sbytes);
  if(sbytes < 0) {
    lua_pushstring(L, strerror(errno));
    return 2;
  }
  return 1;
}
static int
noit_lua_socket_bind(lua_State *L) {
  noit_lua_resume_info_t *ci;
  eventer_t e, *eptr;
  const char *target;
  unsigned short port;
  int8_t family;
  int rv;
  int flag;
  union {
    struct sockaddr_in sin4;
    struct sockaddr_in6 sin6;
  } a;

  ci = noit_lua_get_resume_info(L);
  assert(ci);

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;
  target = lua_tostring(L, 2);
  if(!target) target = "";
  port = lua_tointeger(L, 3);
  memset(&a, 0, sizeof(a));

  family = AF_INET;
  rv = inet_pton(family, target, &a.sin4.sin_addr);
  if(rv != 1) {
    family = AF_INET6;
    rv = inet_pton(family, target, &a.sin6.sin6_addr);
    if(rv != 1) {
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
    a.sin4.sin_addr.s_addr = INADDR_ANY;
    memset (a.sin4.sin_zero, 0, sizeof (a.sin4.sin_zero));
  }

  rv = bind(e->fd, (struct sockaddr *)&a,
            family==AF_INET ? sizeof(a.sin4) : sizeof(a.sin6));
  if(rv == 0) {
    lua_pushinteger(L, 0);
    return 1;
  }
  lua_pushinteger(L, -1);
  lua_pushstring(L, strerror(errno));
  return 2;
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
noit_lua_socket_accept_complete(eventer_t e, int mask, void *vcl,
                                struct timeval *now) {
  noit_lua_resume_info_t *ci;
  struct nl_slcl *cl = vcl, *newcl;
  eventer_t newe;
  int fd, nargs = 0, newmask;
  union {
    struct sockaddr in;
    struct sockaddr_in in4;
    struct sockaddr_in6 in6;
  } addr;
  socklen_t inlen, optlen;

  /* If the pointer has been cleared, this has been gc'd */
  if(!*(cl->eptr)) {
    noitL(nlerr, "noit.eventer callback post GC\n");
    return 0;
  }

  ci = noit_lua_get_resume_info(cl->L);
  assert(ci);

  inlen = sizeof(addr.in);
  fd = e->opset->accept(e->fd, &addr.in, &inlen, &newmask, e);
  if(fd <= 0 && errno == EAGAIN) return newmask | EVENTER_EXCEPTION;
  if(fd < 0) {
    lua_pushnil(cl->L);
    goto alldone;
  }
  
  if(eventer_set_fd_nonblocking(fd)) {
    close(fd);
    lua_pushnil(cl->L);
    goto alldone;
  }

  newcl = calloc(1, sizeof(*cl));
  newcl->free = nl_extended_free;
  newcl->L = cl->L;

  optlen = sizeof(newcl->send_size);
  if(getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &newcl->send_size, &optlen) != 0)
    newcl->send_size = 4096;

  newe = eventer_alloc();
  newe->fd = fd;
  newe->mask = EVENTER_EXCEPTION;
  newe->callback = NULL;
  newe->closure = newcl;
  newcl->eptr = noit_lua_event(cl->L, newe);

  noit_lua_check_register_event(ci, newe);
  nargs = 1;

 alldone:
  eventer_remove_fd(e->fd);
  noit_lua_check_deregister_event(ci, e, 0);
  *(cl->eptr) = eventer_alloc();
  memcpy(*cl->eptr, e, sizeof(*e));
  noit_lua_check_register_event(ci, *cl->eptr);
  ci->lmc->resume(ci, nargs);
  return 0;
}

static int
noit_lua_socket_listen(lua_State *L) {
  noit_lua_resume_info_t *ci;
  eventer_t e, *eptr;
  int rv;

  ci = noit_lua_get_resume_info(L);
  assert(ci);

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;

  if((rv = listen(e->fd, lua_tointeger(L, 2))) < 0) {
    lua_pushinteger(L, rv);
    lua_pushinteger(L, errno);
    lua_pushstring(L, strerror(errno));
    return 3;
  }
  lua_pushinteger(L, rv);
  return 1;
}

static int
noit_lua_socket_own(lua_State *L) {
  noit_lua_resume_info_t *ci;
  struct nl_slcl *cl;
  eventer_t e, *eptr;

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;
  cl = e->closure;
  if(cl->L == L) return 0;
  
  ci = noit_lua_get_resume_info(cl->L);
  assert(ci);
  noit_lua_check_deregister_event(ci, e, 0);
  ci = noit_lua_get_resume_info(L);
  assert(ci);
  cl->L = L;
  noit_lua_check_register_event(ci, e);
  return 0;
}

static int
noit_lua_socket_accept(lua_State *L) {
  noit_lua_resume_info_t *ci;
  struct nl_slcl *cl;
  eventer_t e, *eptr;
  socklen_t optlen;
  union {
    struct sockaddr in;
    struct sockaddr_in in4;
    struct sockaddr_in6 in6;
  } addr;
  socklen_t inlen;
  int fd, newmask;

  ci = noit_lua_get_resume_info(L);
  assert(ci);

  noitL(nldeb, "accept starting\n");
  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;
  cl = e->closure;

  if(cl->L != L) {
    noitL(nlerr, "cross-coroutine socket call: use event:own()\n");
    luaL_error(L, "cross-coroutine socket call: use event:own()");
  }

  inlen = sizeof(addr.in);
  fd = e->opset->accept(e->fd, &addr.in, &inlen, &newmask, e);
  if(fd < 0) {
    if(errno == EAGAIN) {
      /* Need completion */
      eventer_remove_fd(e->fd);
      e->callback = noit_lua_socket_accept_complete;
      e->mask = newmask | EVENTER_EXCEPTION;
      eventer_add(e);
      noitL(nldeb, "accept rescheduled\n");
      return noit_lua_yield(ci, 0);
    }
    noitL(nldeb, "accept error: %s\n", strerror(errno));
    lua_pushnil(L);
    return 1;
  }
  if(eventer_set_fd_nonblocking(fd)) {
    close(fd);
    lua_pushnil(L);
    return 1;
  }

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
noit_lua_socket_setsockopt(lua_State *L) {
  noit_lua_resume_info_t *ci;
  eventer_t e, *eptr;
  int rv;
  const char *type;
  int type_val;
  int value;

  ci = noit_lua_get_resume_info(L);
  assert(ci);

  if(lua_gettop(L) != 3) {
    lua_pushinteger(L, -1);
    lua_pushfstring(L, "setsockopt(type, value) wrong arguments");
    return 2;
  }
  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;
  type = lua_tostring(L, 2);
  value = lua_tointeger(L, 3);

  if (strcmp(type, "SO_BROADCAST") == 0)
    type_val = SO_BROADCAST;
  else if (strcmp(type, "SO_REUSEADDR") == 0)
    type_val = SO_REUSEADDR;
  else if (strcmp(type, "SO_KEEPALIVE") == 0)
    type_val = SO_KEEPALIVE;
  else if (strcmp(type, "SO_LINGER") == 0)
    type_val = SO_LINGER;
  else if (strcmp(type, "SO_OOBINLINE") == 0)
    type_val = SO_OOBINLINE;
  else if (strcmp(type, "SO_SNDBUF") == 0)
    type_val = SO_SNDBUF;
  else if (strcmp(type, "SO_RCVBUF") == 0)
    type_val = SO_RCVBUF;
  else if (strcmp(type, "SO_DONTROUTE") == 0)
    type_val = SO_DONTROUTE;
  else if (strcmp(type, "SO_RCVLOWAT") == 0)
    type_val = SO_RCVLOWAT;
  else if (strcmp(type, "SO_RCVTIMEO") == 0)
    type_val = SO_RCVTIMEO;
  else if (strcmp(type, "SO_SNDLOWAT") == 0)
    type_val = SO_SNDLOWAT;
  else if (strcmp(type, "SO_SNDTIMEO") == 0)
    type_val = SO_SNDTIMEO;
  else {
    lua_pushinteger(L, -1);
    lua_pushfstring(L, "Socket  operation '%s' not supported\n", type);
    return 2;
  }

  if (setsockopt(e->fd, SOL_SOCKET, type_val, (char*)&value, sizeof(value)) < 0) {
    lua_pushinteger(L, -1);
    lua_pushfstring(L, strerror(errno));
    return 2;
  }
  lua_pushinteger(L, 0);
  return 1;
}
static int
noit_lua_socket_connect(lua_State *L) {
  noit_lua_resume_info_t *ci;
  eventer_t e, *eptr;
  const char *target;
  unsigned short port;
  int8_t family;
  int rv;
  union {
    struct sockaddr_in sin4;
    struct sockaddr_in6 sin6;
  } a;

  ci = noit_lua_get_resume_info(L);
  assert(ci);

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;
  target = lua_tostring(L, 2);
  if(!target) target = "";
  port = lua_tointeger(L, 3);

  family = AF_INET;
  rv = inet_pton(family, target, &a.sin4.sin_addr);
  if(rv != 1) {
    family = AF_INET6;
    rv = inet_pton(family, target, &a.sin6.sin6_addr);
    if(rv != 1) {
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
  noit_lua_resume_info_t *ci;
  struct nl_slcl *cl = vcl;
  int rv, nargs;

  rv = eventer_SSL_connect(e, &mask);
  if(rv <= 0 && errno == EAGAIN) return mask | EVENTER_EXCEPTION;

  ci = noit_lua_get_resume_info(cl->L);
  assert(ci);
  noit_lua_check_deregister_event(ci, e, 0);

  *(cl->eptr) = eventer_alloc();
  memcpy(*cl->eptr, e, sizeof(*e));
  noit_lua_check_register_event(ci, *cl->eptr);

  /* Upgrade completed (successfully???) */
  nargs = 1;
  lua_pushinteger(cl->L, (rv > 0) ? 0 : -1);
  if(rv <= 0) {
    eventer_ssl_ctx_t *ctx;
    const char *err = NULL;
    ctx = eventer_get_eventer_ssl_ctx(e);
    if(ctx) err = eventer_ssl_get_last_error(ctx);
    lua_pushinteger(cl->L, -1);
    if(err) {
      lua_pushlstring(cl->L, err, strlen(err));
      nargs++;
    }
  }
  ci->lmc->resume(ci, nargs);
  return 0;
}
static int
noit_lua_socket_connect_ssl(lua_State *L) {
  const char *layer, *ca, *ciphers, *cert, *key, *snihost, *err;
  eventer_ssl_ctx_t *sslctx;
  noit_lua_resume_info_t *ci;
  eventer_t e, *eptr;
  int tmpmask, rv;

  ci = noit_lua_get_resume_info(L);
  assert(ci);

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;
  cert = lua_tostring(L, 2);
  key = lua_tostring(L, 3);
  ca = lua_tostring(L, 4);
  ciphers = lua_tostring(L, 5);
  snihost = lua_tostring(L, 6);
  layer = lua_tostring(L, 7);

  sslctx = eventer_ssl_ctx_new(SSL_CLIENT, layer, cert, key, ca, ciphers);
  if(!sslctx) {
    lua_pushinteger(L, -1);
    lua_pushstring(L, "ssl_client context creation failed");
    return 2;
  }

  if (snihost != NULL && strlen(snihost) >= 1) {
    eventer_ssl_ctx_set_sni(sslctx, snihost);
  }

  eventer_ssl_ctx_set_verify(sslctx, eventer_ssl_verify_cert, NULL);
  EVENTER_ATTACH_SSL(e, sslctx);

  /* We need do the ssl connect and register a completion if
   * it comes back with an EAGAIN.
   */
  tmpmask = EVENTER_READ|EVENTER_WRITE;
  rv = eventer_SSL_connect(e, &tmpmask);
  if(rv <= 0 && errno == EAGAIN) {
    /* Need completion */
    eventer_remove_fd(e->fd);
    e->mask = tmpmask | EVENTER_EXCEPTION;
    e->callback = noit_lua_ssl_upgrade;
    eventer_add(e);
    return noit_lua_yield(ci, 0);
  }
  lua_pushinteger(L, (rv > 0) ? 0 : -1);
  if(rv <= 0) {
    err = eventer_ssl_get_last_error(sslctx);
    if(err) {
      lua_pushstring(L, err);
      return 2;
    }
  }
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
        cl->inbuff_len = 0;
        if(cl->read_sofar > 0) {  /* We have to buffer this for next read */
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
  if((((len < 0) && (errno != EAGAIN)) || (len == 0)) && cl->inbuff_len) {
    /* EOF */
    *read_complete = 1;
    lua_pushlstring(cl->L, cl->inbuff, cl->inbuff_len);
    cl->inbuff_len = 0;
    return 0;
  }
  return len;
}
static int
noit_lua_socket_read_complete(eventer_t e, int mask, void *vcl,
                              struct timeval *now) {
  noit_lua_resume_info_t *ci;
  struct nl_slcl *cl = vcl;
  int len;
  int args = 0;

  /* If the pointer has been cleared, this has been gc'd */
  if(!*(cl->eptr)) {
    noitL(nlerr, "noit.eventer callback post GC\n");
    return 0;
  }

  ci = noit_lua_get_resume_info(cl->L);
  assert(ci);

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
  eventer_remove_fd(e->fd);
  noit_lua_check_deregister_event(ci, e, 0);
  *(cl->eptr) = eventer_alloc();
  memcpy(*cl->eptr, e, sizeof(*e));
  noit_lua_check_register_event(ci, *cl->eptr);
  ci->lmc->resume(ci, args);
  return 0;
}

static int
noit_lua_socket_read(lua_State *L) {
  int args, mask, len;
  struct nl_slcl *cl;
  noit_lua_resume_info_t *ci;
  eventer_t e, *eptr;

  ci = noit_lua_get_resume_info(L);
  assert(ci);

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;
  cl = e->closure;
  cl->read_goal = 0;
  cl->read_terminator = NULL;

  if(cl->L != L) {
    noitL(nlerr, "cross-coroutine socket call: use event:own()\n");
    luaL_error(L, "cross-coroutine socket call: use event:own()");
  }

  if(lua_isnumber(L, 2)) {
    cl->read_goal = lua_tointeger(L, 2);
    if(cl->read_goal <= cl->read_sofar) {
     i_know_better:
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

  eventer_remove_fd(e->fd);
  e->callback = noit_lua_socket_read_complete;
  e->mask = mask | EVENTER_EXCEPTION;
  eventer_add(e);
  return noit_lua_yield(ci, 0);
}
static int
noit_lua_socket_write_complete(eventer_t e, int mask, void *vcl,
                               struct timeval *now) {
  noit_lua_resume_info_t *ci;
  struct nl_slcl *cl = vcl;
  int rv;
  int args = 0;

  /* If the pointer has been cleared, this has been gc'd */
  if(!*(cl->eptr)) {
    noitL(nlerr, "noit.eventer callback post GC\n");
    return 0;
  }

  ci = noit_lua_get_resume_info(cl->L);
  assert(ci);

  if(mask & EVENTER_EXCEPTION) {
    lua_pushinteger(cl->L, -1);
    args = 1;
    goto alldone;
  }
  while((rv = e->opset->write(e->fd,
                              cl->outbuff + cl->write_sofar,
                              MIN(cl->send_size,
                                  (cl->write_goal - cl->write_sofar)),
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
  ci->lmc->resume(ci, args);
  return 0;
}
static int
noit_lua_socket_write(lua_State *L) {
  int rv, mask;
  struct nl_slcl *cl;
  noit_lua_resume_info_t *ci;
  eventer_t e, *eptr;

  ci = noit_lua_get_resume_info(L);
  assert(ci);

  eptr = lua_touserdata(L, lua_upvalueindex(1));
  if(eptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  e = *eptr;
  cl = e->closure;
  cl->write_sofar = 0;
  cl->outbuff = lua_tolstring(L, 2, &cl->write_goal);

  if(cl->L != L) {
    noitL(nlerr, "cross-coroutine socket call: use event:own()\n");
    luaL_error(L, "cross-coroutine socket call: use event:own()");
  }

  while((rv = e->opset->write(e->fd,
                              cl->outbuff + cl->write_sofar,
                              MIN(cl->send_size,
                                  (cl->write_goal - cl->write_sofar)),
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
    eventer_remove_fd(e->fd);
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
  eventer_t *udata;
  n = lua_gettop(L); /* number of arguments */
  assert(n == 2);
  if(!luaL_checkudata(L, 1, "noit.eventer")) {
    luaL_error(L, "metatable error, arg1 not a noit.eventer!");
  }
  udata = lua_touserdata(L, 1);
  if(!lua_isstring(L, 2)) {
    luaL_error(L, "metatable error, arg2 not a string!");
  }
  k = lua_tostring(L, 2);
  switch(*k) {
    case 'a':
     LUA_DISPATCH(accept, noit_lua_socket_accept);
     break;
    case 'b':
     LUA_DISPATCH(bind, noit_lua_socket_bind);
     break;
    case 'c':
     LUA_DISPATCH(close, noit_lua_socket_close);
     LUA_DISPATCH(connect, noit_lua_socket_connect);
     break;
    case 'l':
     LUA_DISPATCH(listen, noit_lua_socket_listen);
     break;
    case 'o':
     LUA_DISPATCH(own, noit_lua_socket_own);
     break;
    case 'r':
     LUA_DISPATCH(read, noit_lua_socket_read);
     LUA_DISPATCH(recv, noit_lua_socket_recv);
     break;
    case 's':
     LUA_DISPATCH(send, noit_lua_socket_send);
     LUA_DISPATCH(sendto, noit_lua_socket_sendto);
     LUA_DISPATCH(setsockopt, noit_lua_socket_setsockopt);
     LUA_DISPATCH(ssl_upgrade_socket, noit_lua_socket_connect_ssl);
     LUA_DISPATCH(ssl_ctx, noit_lua_socket_ssl_ctx);
     break;
    case 'w':
     LUA_DISPATCH(write, noit_lua_socket_write);
     break;
    default:
      break;
  }
  luaL_error(L, "noit.eventer no such element: %s", k);
  return 0;
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
    case 'c':
      if(!strcmp(k,"ciphers")) {
        int i = 0;
        const char *ciphername;
        lua_newtable(L);
        while(NULL != (ciphername = eventer_ssl_get_cipher_list(ssl_ctx,i))) {
          if(!ciphername) break;
          lua_pushnumber(L, ++i);
          lua_pushstring(L, ciphername);
          lua_settable(L,-3);
        }
        return 1;
      }
      LUA_RETSTRING(current_cipher, eventer_ssl_get_current_cipher(ssl_ctx));
      break;
    case 'e':
      LUA_RETSTRING(error, eventer_ssl_get_peer_error(ssl_ctx));
      LUA_RETINTEGER(end_time, eventer_ssl_get_peer_end_time(ssl_ctx));
      break;
    case 'i':
      LUA_RETSTRING(issuer, eventer_ssl_get_peer_issuer(ssl_ctx));
      break;
    case 'm':
      LUA_RETINTEGER(method, eventer_ssl_get_method(ssl_ctx));
      break;
    case 's':
      LUA_RETSTRING(san_list, eventer_ssl_get_peer_san_list(ssl_ctx));
      LUA_RETSTRING(subject, eventer_ssl_get_peer_subject(ssl_ctx));
      LUA_RETINTEGER(start_time, eventer_ssl_get_peer_start_time(ssl_ctx));
      break;
    default:
      break;
  }
  luaL_error(L, "noit.eventer.ssl_ctx no such element: %s", k);
  return 0;
}

static int
nl_waitfor_notify(lua_State *L) {
  noit_lua_resume_info_t *ci;
  struct nl_slcl *cl;
  void *vptr;
  const char *key;
  int nargs;

  ci = noit_lua_get_resume_info(L);
  assert(ci);
  nargs = lua_gettop(L);
  if(nargs < 1) {
    return 0;
  }
  key = lua_tostring(L, 1);
  if(!noit_hash_retrieve(ci->lmc->pending, key, strlen(key), &vptr)) {
    return 0;
  }
  noit_hash_delete(ci->lmc->pending, key, strlen(key), free, NULL);
  cl = vptr;
  lua_xmove(L, cl->L, nargs);

  ci = noit_lua_get_resume_info(cl->L);
  assert(ci);
  eventer_remove(cl->pending_event);
  noit_lua_check_deregister_event(ci, cl->pending_event, 0);
  ci->lmc->resume(ci, nargs);
  lua_pushinteger(L, nargs);
  return 0;
}

static int
nl_waitfor_timeout(eventer_t e, int mask, void *vcl, struct timeval *now) {
  noit_lua_resume_info_t *ci;
  struct nl_slcl *cl = vcl;
  struct timeval diff;
  double p_int;

  ci = noit_lua_get_resume_info(cl->L);
  assert(ci);
  noit_lua_check_deregister_event(ci, e, 0);
  noit_hash_delete(ci->lmc->pending, cl->inbuff, strlen(cl->inbuff), free, NULL);
  free(cl);
  ci->lmc->resume(ci, 0);
  return 0;
}

static int
nl_waitfor(lua_State *L) {
  noit_lua_resume_info_t *ci;
  const char *key;
  struct nl_slcl *cl;
  struct timeval diff;
  eventer_t e;
  double p_int;

  ci = noit_lua_get_resume_info(L);
  assert(ci);
  if(lua_gettop(L) != 2) {
    luaL_error(L, "waitfor(key, timeout) wrong arguments");
  }
  p_int = lua_tonumber(L, 2);
  cl = calloc(1, sizeof(*cl));
  cl->free = nl_extended_free;
  cl->L = L;
  gettimeofday(&cl->start, NULL);

  key = lua_tostring(L, 1);
  if(!key) luaL_error(L, "waitfor called without key");
  cl->inbuff = strdup(key);
  if(!noit_hash_store(ci->lmc->pending, cl->inbuff, strlen(cl->inbuff), cl)) {
    nl_extended_free(cl);
    luaL_error(L, "waitfor called with duplicate key");
  }

  cl->pending_event = e = eventer_alloc();
  e->mask = EVENTER_TIMER;
  e->callback = nl_waitfor_timeout;
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
nl_sleep_complete(eventer_t e, int mask, void *vcl, struct timeval *now) {
  noit_lua_resume_info_t *ci;
  struct nl_slcl *cl = vcl;
  struct timeval diff;
  double p_int;

  ci = noit_lua_get_resume_info(cl->L);
  assert(ci);
  noit_lua_check_deregister_event(ci, e, 0);

  sub_timeval(*now, cl->start, &diff);
  p_int = diff.tv_sec + diff.tv_usec / 1000000.0;
  lua_pushnumber(cl->L, p_int);
  free(cl);
  ci->lmc->resume(ci, 1);
  return 0;
}

static int
nl_sleep(lua_State *L) {
  noit_lua_resume_info_t *ci;
  struct nl_slcl *cl;
  struct timeval diff;
  eventer_t e;
  double p_int;

  ci = noit_lua_get_resume_info(L);
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
nl_open(lua_State *L) {
  const char *file;
  int fd, flags;
  if(lua_gettop(L) < 2 || lua_gettop(L) > 3)
    luaL_error(L, "bad call to noit.open");
  file = lua_tostring(L, 1);
  flags = lua_tointeger(L, 2);
  if(lua_gettop(L) == 2)
    fd = open(file, flags);
  else
    fd = open(file, flags, lua_tointeger(L, 3));
  lua_pushinteger(L, fd);
  if(fd >= 0) return 1;
  lua_pushinteger(L, errno);
  lua_pushstring(L, strerror(errno));
  return 3;
}

static int
nl_write(lua_State *L) {
  int fd, rv;
  size_t len;
  const char *str;
  if(lua_gettop(L) != 2 || !lua_isnumber(L,1) || !lua_isstring(L,2))
    luaL_error(L, "bad parameters to noit.write(fd, str)");
  fd = lua_tointeger(L,1);
  str = lua_tolstring(L,2,&len);
  rv = write(fd, str, len);
  lua_pushinteger(L,rv);
  if(rv < 0) lua_pushinteger(L,errno);
  else lua_pushnil(L);
  return 2;
}

static int
nl_close(lua_State *L) {
  if(lua_gettop(L) != 1 || !lua_isnumber(L, 1))
    luaL_error(L, "bad call to noit.close");
  close(lua_tointeger(L,1));
  return 0;
}

static int
nl_chmod(lua_State *L) {
  int rv;
  if(lua_gettop(L) != 2 || !lua_isstring(L, 1) || !lua_isnumber(L, 2))
    luaL_error(L, "bad call to noit.chmod(file, mode)");
  rv = chmod(lua_tostring(L,1), lua_tointeger(L,2));
  lua_pushinteger(L, rv);
  if(rv<0) lua_pushinteger(L, errno);
  else lua_pushnil(L);
  return 2;
}

static int
nl_stat(lua_State *L) {
  struct stat st;
  int err;
  if(lua_gettop(L) != 1) luaL_error(L, "bad call to noit.stat");
  if(lua_isstring(L,1)) err = lstat(lua_tostring(L,1), &st);
  else if(lua_isnumber(L,1)) err = fstat(lua_tointeger(L,1), &st);
  else if(lua_isnil(L,1)) {
    lua_pushnil(L);
    return 1;
  }
  else luaL_error(L, "noit.stat expects a filename or descriptor");
  if(err < 0) {
    lua_pushnil(L);
    lua_pushinteger(L, errno);
    lua_pushstring(L, strerror(errno));
    return 3;
  }
#define SET_STAT(attr) do { \
  lua_pushinteger(L, (int)st.st_##attr); \
  lua_setfield(L, -2, #attr); \
} while(0)
  lua_createtable(L, 0, 9);
  SET_STAT(dev);
  SET_STAT(ino);
  SET_STAT(mode);
  SET_STAT(nlink);
  SET_STAT(uid);
  SET_STAT(gid);
  SET_STAT(rdev);
  SET_STAT(size);
  return 1;
}

static int
nl_readdir(lua_State *L) {
  const char *path;
  DIR *root;
  struct dirent *de, *entry;
  int size = 0, cnt = 1;
  int use_filter = 1;
  if(lua_gettop(L) < 1 || lua_gettop(L) < 2)
    luaL_error(L, "bad call to noit.readdir");
  path = lua_tostring(L, 1);
  if(lua_gettop(L) == 2) {
    if(!lua_isfunction(L, 2))
      luaL_error(L, "noit.readdir second argument must be a function");
    use_filter = 1;
  }

#ifdef _PC_NAME_MAX
  size = pathconf(path, _PC_NAME_MAX);
#endif
  size = MAX(size, PATH_MAX + 128);
  de = alloca(size);
  root = opendir(path);
  if(!root) {
    lua_pushnil(L);
    return 1;
  }
  lua_newtable(L);
  while(portable_readdir_r(root, de, &entry) == 0 && entry != NULL) {
    int use_value = 1;
    lua_pushstring(L, entry->d_name);
    if(use_filter) {
      lua_pushvalue(L,2);   /* func */
      lua_pushvalue(L,-2);  /* arg  */
      if(lua_pcall(L,1,1,0) != 0) {
        closedir(root);
        luaL_error(L, lua_tostring(L,-1));
      }
      use_value = lua_toboolean(L,-1);
      lua_pop(L, 1);
    }
    if(use_value) {
      lua_pushinteger(L, cnt++);
      lua_insert(L, -2);
      lua_settable(L, -3);
    }
    else {
      lua_pop(L,1);
    }
  }
  closedir(root);

  return 1;
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
  lua_getglobal(L, "string");
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
nl_crc32(lua_State *L) {
  size_t inlen;
  const char *input;
  uLong start = 0, inputidx = 1;
  if(lua_isnil(L,1)) start = crc32(0, NULL, 0);
  if(lua_gettop(L) == 2) {
    start = lua_tointeger(L, 2);
    inputidx = 2;
  }
  input = lua_tolstring(L, inputidx, &inlen);
  lua_pushnumber(L, (double)crc32(start, (Bytef *)input, inlen));
  return 1;
}
static int
nl_base32_decode(lua_State *L) {
  size_t inlen, decoded_len;
  const char *message;
  unsigned char *decoded;
  int needs_free = 0;

  if(lua_gettop(L) != 1) luaL_error(L, "bad call to noit.decode");

  message = lua_tolstring(L, 1, &inlen);
  if(MAX(1,inlen) <= ON_STACK_LUA_STRLEN) {
    decoded = alloca(MAX(1,inlen));
  }
  else {
    decoded = malloc(MAX(1,inlen));
    needs_free = 1;
  }
  if(!decoded) luaL_error(L, "out-of-memory");
  decoded_len = noit_b32_decode(message, inlen, decoded, MAX(1,inlen));
  lua_pushlstring(L, (char *)decoded, decoded_len);
  if(needs_free) free(decoded);
  return 1;
}
static int
nl_base32_encode(lua_State *L) {
  size_t inlen, encoded_len;
  const unsigned char *message;
  char *encoded;
  int needs_free = 0;

  if(lua_gettop(L) != 1) luaL_error(L, "bad call to noit.encode");

  message = (const unsigned char *)lua_tolstring(L, 1, &inlen);
  encoded_len = (((inlen + 7) / 5) * 8) + 1;
  if(encoded_len <= ON_STACK_LUA_STRLEN) {
    encoded = alloca(encoded_len);
  }
  else {
    encoded = malloc(encoded_len);
    needs_free = 1;
  }
  if(!encoded) luaL_error(L, "out-of-memory");
  encoded_len = noit_b32_encode(message, inlen, encoded, encoded_len);
  lua_pushlstring(L, (char *)encoded, encoded_len);
  if(needs_free) free(encoded);
  return 1;
}
static int
nl_base64_decode(lua_State *L) {
  size_t inlen, decoded_len;
  const char *message;
  unsigned char *decoded;
  int needs_free = 0;

  if(lua_gettop(L) != 1) luaL_error(L, "bad call to noit.decode");

  message = lua_tolstring(L, 1, &inlen);
  if(MAX(1,inlen) <= ON_STACK_LUA_STRLEN) {
    decoded = alloca(MAX(1,inlen));
  }
  else {
    decoded = malloc(MAX(1,inlen));
    needs_free = 1;
  }
  if(!decoded) luaL_error(L, "out-of-memory");
  decoded_len = noit_b64_decode(message, inlen, decoded, MAX(1,inlen));
  lua_pushlstring(L, (char *)decoded, decoded_len);
  if(needs_free) free(decoded);
  return 1;
}
static int
nl_base64_encode(lua_State *L) {
  size_t inlen, encoded_len;
  const unsigned char *message;
  char *encoded;
  int needs_free = 0;

  if(lua_gettop(L) != 1) luaL_error(L, "bad call to noit.encode");

  message = (const unsigned char *)lua_tolstring(L, 1, &inlen);
  encoded_len = (((inlen + 2) / 3) * 4) + 1;
  if(encoded_len <= ON_STACK_LUA_STRLEN) {
    encoded = alloca(encoded_len);
  }
  else {
    encoded = malloc(encoded_len);
    needs_free = 1;
  }
  if(!encoded) luaL_error(L, "out-of-memory");
  encoded_len = noit_b64_encode(message, inlen, encoded, encoded_len);
  lua_pushlstring(L, (char *)encoded, encoded_len);
  if(needs_free) free(encoded);
  return 1;
}
static int
nl_utf8tohtml(lua_State *L) {
  int in_idx = 1, tags_idx = 2;
  const unsigned char *in;
  unsigned char *out;
  size_t in_len_size_t;
  int rv, out_alloc, out_len, in_len, needs_free = 0;

  if(lua_gettop(L) < 1)
    luaL_error(L, "bad arguments to noit.utf8tohtml");
  /* We might be called a method. cope. */
  if(lua_isuserdata(L,1)) {
    in_idx++; tags_idx++;
  }
  if(lua_gettop(L) < in_idx || lua_gettop(L) > tags_idx ||
     !lua_isstring(L,in_idx))
    luaL_error(L, "bad arguments to noit.utf8tohtml");

  in = (const unsigned char *)lua_tolstring(L, in_idx, &in_len_size_t);
  in_len = (int)in_len_size_t;
  if((size_t)in_len != (size_t)in_len_size_t)
    luaL_error(L, "overflow");
  out_alloc = out_len = in_len * 6 + 1;
  if(out_len <= ON_STACK_LUA_STRLEN) {
    out = alloca(out_len);
  }
  else {
    out = malloc(out_len);
    needs_free = 1;
  }
  if(!out) luaL_error(L, "out-of-memory");
  rv = UTF8ToHtml(out, &out_len, in, &in_len);
  if(rv >= 0) {
    if(lua_toboolean(L,tags_idx)) {
      int i = 0, tagcnt = 0;
      for(i=0;i<out_len;i++) if(out[i] == '<' || out[i] == '>') tagcnt++;
      if(tagcnt) {
        unsigned char *newout, *outcp;
        /* each tag goes from 1 char to 4, (+3) */
        outcp = newout = malloc(out_len + (tagcnt * 3) + 1);
        if(!newout) {
          if(needs_free) free(out);
          luaL_error(L, "out-of-memory");
        }
        for(i=0;i<out_len;i++) {
          if(out[i] == '<') {
            memcpy(outcp, "&lt;", 4);
            outcp += 4;
          }
          else if(out[i] == '>') {
            memcpy(outcp, "&gt;", 4);
            outcp += 4;
          }
          else {
            *outcp++ = out[i];
          }
        }
        if(needs_free) free(out);
        out = newout;
        out_len += tagcnt * 3;
        needs_free = 1;
      }
    }
    lua_pushlstring(L, (const char *)out, out_len);
    if(needs_free) free(out);
    return 1;
  }
  if(needs_free) free(out);
  if(rv == -2) luaL_error(L, "utf8tohtml transcoding failure");
  luaL_error(L, "utf8tohtml failure");
  return 0;
}
static int
nl_hmac_sha1_encode(lua_State *L) {
  size_t messagelen, keylen, encoded_len;
  const unsigned char *message, *key;
  unsigned char result[EVP_MAX_MD_SIZE+1];
  unsigned int md_len;
  char encoded[29];

  if(lua_gettop(L) != 2) luaL_error(L, "bad call to noit.hmac_sha1_encode");
  encoded_len = 28; /* the length of the base64 encoded HMAC-SHA1 result will always be 28 */

  message = (const unsigned char *)lua_tolstring(L, 1, &messagelen);
  key = (const unsigned char *)lua_tolstring(L, 2, &keylen);

  HMAC(EVP_sha1(), key, keylen, message, messagelen, result, &md_len);
  encoded_len = noit_b64_encode(result, md_len, encoded, encoded_len);

  lua_pushlstring(L, (char *)encoded, encoded_len);

  return 1;
}
static int
nl_hmac_sha256_encode(lua_State *L) {
  size_t messagelen, keylen, encoded_len;
  const unsigned char *message, *key;
  unsigned char result[EVP_MAX_MD_SIZE+1];
  unsigned int md_len;
  char encoded[45];

  if(lua_gettop(L) != 2) luaL_error(L, "bad call to noit.hmac_sha256_encode");
  encoded_len = 44; /* the length of the base64 encoded HMAC-SHA256 result will always be 44 */

  message = (const unsigned char *)lua_tolstring(L, 1, &messagelen);
  key = (const unsigned char *)lua_tolstring(L, 2, &keylen);

  HMAC(EVP_sha256(), key, keylen, message, messagelen, result, &md_len);
  encoded_len = noit_b64_encode(result, md_len, encoded, encoded_len);

  lua_pushlstring(L, (char *)encoded, encoded_len);

  return 1;
}
static const char _hexchars[16] =
  {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
static int
nl_md5_hex(lua_State *L) {
  int i;
  MD5_CTX ctx;
  size_t inlen;
  const char *in;
  unsigned char md5[MD5_DIGEST_LENGTH];
  char md5_hex[MD5_DIGEST_LENGTH * 2 + 1];

  if(lua_gettop(L) != 1) luaL_error(L, "bad call to noit.md5_hex");
  MD5_Init(&ctx);
  in = lua_tolstring(L, 1, &inlen);
  MD5_Update(&ctx, (const void *)in, (unsigned long)inlen);
  MD5_Final(md5, &ctx);
  for(i=0;i<MD5_DIGEST_LENGTH;i++) {
    md5_hex[i*2] = _hexchars[(md5[i] >> 4) & 0xf];
    md5_hex[i*2+1] = _hexchars[md5[i] & 0xf];
  }
  md5_hex[i*2] = '\0';
  lua_pushstring(L, md5_hex);
  return 1;
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
nl_uuid(lua_State *L) {
  uuid_t out;
  char uuid_str[UUID_STR_LEN+1];
  uuid_generate(out);
  uuid_unparse_lower(out, uuid_str);
  lua_pushstring(L, uuid_str);
  return 1;
}
static int
nl_socket_internal(lua_State *L, int family, int proto) {
  struct nl_slcl *cl;
  noit_lua_resume_info_t *ci;
  socklen_t optlen;
  int fd;
  eventer_t e;

  fd = socket(family, proto, 0);
  if(fd < 0) {
    lua_pushnil(L);
    return 1;
  }
  if(eventer_set_fd_nonblocking(fd)) {
    close(fd);
    lua_pushnil(L);
    return 1;
  }

  ci = noit_lua_get_resume_info(L);
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
  int n = lua_gettop(L);
  uint8_t family = AF_INET;
  union {
    struct in_addr addr4;
    struct in6_addr addr6;
  } a;

  if(n > 0 && lua_isstring(L,1)) {
    const char *fam = lua_tostring(L,1);
    if(!fam) fam = "";
    if(!strcmp(fam, "inet")) family = AF_INET;
    else if(!strcmp(fam, "inet6")) family = AF_INET6;
    else if(inet_pton(AF_INET, fam, &a) == 1) family = AF_INET;
    else if(inet_pton(AF_INET6, fam, &a) == 1) family = AF_INET6;
    else luaL_error(L, "noit.socket family for %s unknown", fam);
  }

  if(n <= 1) return nl_socket_internal(L, family, SOCK_STREAM);
  if(n == 2 && lua_isstring(L,2)) {
    const char *type = lua_tostring(L,2);
    if(!strcmp(type, "tcp"))
      return nl_socket_internal(L, family, SOCK_STREAM);
    else if(!strcmp(type, "udp"))
      return nl_socket_internal(L, family, SOCK_DGRAM);
  }
  luaL_error(L, "noit.socket called with invalid arguments");
  return 0;
}

static int
nl_valid_ip(lua_State *L) {
  if(lua_gettop(L) != 1 || !lua_isstring(L,1))
    luaL_error(L, "bad parameters to noit.valid_ip");
  lua_pushboolean(L, noit_check_is_valid_target(lua_tostring(L,1)));
  return 1;
}

struct gunzip_crutch {
  z_stream *stream;
  void *scratch_buffer;
};
static int
nl_gunzip_deflate(lua_State *L) {
  struct gunzip_crutch *crutch;
  const char *input;
  size_t inlen;
  z_stream *stream;
  Bytef *data = NULL;
  uLong outlen = 0;
  int limit = 1024*1024;
  int allow_restart = 1;
  int err, n = lua_gettop(L);

  if(n < 1 || n > 2) {
    lua_pushnil(L);
    return 1;
  }

  crutch = lua_touserdata(L, lua_upvalueindex(1));
  stream = crutch->stream;

  input = lua_tolstring(L, 1, &inlen);
  if(!input) {
    lua_pushnil(L);
    return 1;
  }
  if(n == 2 && !lua_isnil(L, 2))
    limit = lua_tointeger(L, 2);

  stream->next_in = (Bytef *)input;
  stream->avail_in = inlen;
  while(1) {
    err = inflate(stream, Z_FULL_FLUSH);
    if(err == Z_OK || err == Z_STREAM_END) {
      /* got some data */
      int size_read = DEFLATE_CHUNK_SIZE - stream->avail_out;
      allow_restart = 0;
      uLong newoutlen = outlen + size_read;
      if(limit && newoutlen > limit) {
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
    else if(allow_restart && err == Z_DATA_ERROR) {
      /* Rarely seen, but on the internet, some IIS servers seem
       * to not generate 'correct' deflate streams, so we use
       * inflateInit2 here to manually configure the stream.
       */
      inflateEnd(stream);
      err = inflateInit2(stream, -MAX_WBITS);
      if (err != Z_OK) {
        break;
      }
      stream->next_in = (Bytef *)input;
      stream->avail_in = inlen;
      allow_restart = 0;
      continue;
    }
    else {
      break;
    }

    if(stream->avail_in == 0) break;
  }
  if(err == Z_OK || err == Z_STREAM_END) {
    if(outlen > 0) lua_pushlstring(L, (char *)data, outlen);
    else lua_pushstring(L, "");
    if(data) free(data);
    return 1;
  }
  if(data) free(data);
  switch(err) {
    case Z_NEED_DICT: luaL_error(L, "zlib: dictionary error"); break;
    case Z_STREAM_ERROR: luaL_error(L, "zlib: stream error"); break;
    case Z_DATA_ERROR: luaL_error(L, "zlib: data error"); break;
    case Z_MEM_ERROR: luaL_error(L, "zlib: out-of-memory"); break;
    case Z_BUF_ERROR: luaL_error(L, "zlib: buffer error"); break;
    case Z_VERSION_ERROR: luaL_error(L, "zlib: version mismatch"); break;
    case Z_ERRNO: luaL_error(L, strerror(errno)); break;
  }
  lua_pushnil(L);
  return 1;
}
static int
nl_gunzip(lua_State *L) {
  struct gunzip_crutch *crutch;
  z_stream *stream;
  
  crutch = (struct gunzip_crutch *)lua_newuserdata(L, sizeof(*crutch));
  crutch->stream = malloc(sizeof(*stream));
  memset(crutch->stream, 0, sizeof(*crutch->stream));
  luaL_getmetatable(L, "noit.gunzip");
  lua_setmetatable(L, -2);

  crutch->stream->next_in = NULL;
  crutch->stream->avail_in = 0;
  crutch->scratch_buffer =
    crutch->stream->next_out = malloc(DEFLATE_CHUNK_SIZE);
  crutch->stream->avail_out = crutch->stream->next_out ? DEFLATE_CHUNK_SIZE : 0;
  inflateInit2(crutch->stream, MAX_WBITS+32);

  lua_pushcclosure(L, nl_gunzip_deflate, 1);
  return 1;
}
static int
noit_lua_gunzip_gc(lua_State *L) {
  struct gunzip_crutch *crutch;
  crutch = (struct gunzip_crutch *)lua_touserdata(L,1);
  if(crutch->scratch_buffer) free(crutch->scratch_buffer);
  inflateEnd(crutch->stream);
  free(crutch->stream);
  return 0;
}

struct pcre_global_info {
  pcre *re;
  int offset;
};
static int
noit_lua_pcre_match(lua_State *L) {
  const char *subject;
  struct pcre_global_info *pgi;
  int i, cnt, ovector[30];
  size_t inlen;
  struct pcre_extra e = { 0 };

  pgi = (struct pcre_global_info *)lua_touserdata(L, lua_upvalueindex(1));
  subject = lua_tolstring(L,1,&inlen);
  if(!subject) {
    lua_pushboolean(L,0);
    return 1;
  }
  if(lua_gettop(L) > 1) {
    if(!lua_istable(L, 2)) {
      noitL(nldeb, "pcre match called with second argument that is not a table\n");
    }
    else {
      lua_pushstring(L, "limit");
      lua_gettable(L, -2);
      if(lua_isnumber(L, -1)) {
        e.flags |= PCRE_EXTRA_MATCH_LIMIT;
        e.match_limit = (int)lua_tonumber(L, -1);
      }
      lua_pop(L, 1);
      lua_pushstring(L, "limit_recurse");
      lua_gettable(L, -2);
      if(lua_isnumber(L, -1)) {
        e.flags |= PCRE_EXTRA_MATCH_LIMIT_RECURSION;
        e.match_limit_recursion = (int)lua_tonumber(L, -1);
      }
      lua_pop(L, 1);
    }
  }
  if (pgi->offset >= inlen) {
    lua_pushboolean(L,0);
    return 1;
  }
  cnt = pcre_exec(pgi->re, &e, subject + pgi->offset,
                  inlen - pgi->offset, 0, 0,
                  ovector, sizeof(ovector)/sizeof(*ovector));
  if(cnt <= 0) {
    lua_pushboolean(L,0);
    return 1;
  }
  lua_pushboolean(L,1);
  for(i = 0; i < cnt; i++) {
    int start = ovector[i*2];
    int end = ovector[i*2+1];
    lua_pushlstring(L, subject+pgi->offset+start, end-start);
  }
  pgi->offset += ovector[1]; /* endof the overall match */
  return cnt+1;
}
static int
nl_pcre(lua_State *L) {
  pcre *re;
  struct pcre_global_info *pgi;
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
  pgi = (struct pcre_global_info *)lua_newuserdata(L, sizeof(*pgi));
  pgi->re = re;
  pgi->offset = 0;
  luaL_getmetatable(L, "noit.pcre");
  lua_setmetatable(L, -2);
  lua_pushcclosure(L, noit_lua_pcre_match, 1);
  return 1;
}
static int
noit_lua_pcre_gc(lua_State *L) {
  struct pcre_global_info *pgi;
  pgi = (struct pcre_global_info *)lua_touserdata(L,1);
  pcre_free(pgi->re);
  return 0;
}

#define SPLIT_PATH(path, base, element) do { \
  char *endp; \
  element = NULL; \
  base = alloca(strlen(path)+1); \
  memcpy(base, path, strlen(path)+1); \
  endp = base + strlen(path); \
  while(endp > base && *endp != '/') endp--; \
  if(*endp == '/') *endp = '\0'; \
  element = endp + 1; \
} while(0)

static int
nl_check(lua_State *L) {
  noit_check_t *check;
  uuid_t id;
  const char *id_str = lua_tostring(L,1);
  if(id_str && uuid_parse((char *)id_str, id) == 0) {
    noit_check_t *check = noit_poller_lookup(id);
    if(check) {
      noit_lua_setup_check(L, check);
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}

static int
nl_conf_get_string(lua_State *L) {
  char *val;
  const char *path = lua_tostring(L,1);
  if(path && lua_gettop(L) == 2) {
    noit_conf_section_t section;
    char *element, *base;
    SPLIT_PATH(path, base, element);
    
    section = noit_conf_get_section(NULL, base);
    if(!section || !element) lua_pushboolean(L, 0);
    else {
      noit_conf_set_string(section, element, lua_tostring(L,2));
      lua_pushboolean(L, 1);
    }
    return 1;
  }
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
  if(path && lua_gettop(L) == 2) {
    noit_conf_section_t section;
    char *element, *base;
    SPLIT_PATH(path, base, element);
    
    section = noit_conf_get_section(NULL, base);
    if(!section || !element) lua_pushboolean(L, 0);
    else {
      noit_conf_set_string(section, element, lua_tostring(L,2));
      lua_pushboolean(L, 1);
    }
    return 1;
  }
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
  if(path && lua_gettop(L) == 2) {
    noit_conf_section_t section;
    char *element, *base;
    SPLIT_PATH(path, base, element);
    
    section = noit_conf_get_section(NULL, base);
    if(!section || !element) lua_pushboolean(L, 0);
    else {
      noit_conf_set_string(section, element, lua_toboolean(L,2) ? "true" : "false");
      lua_pushboolean(L, 1);
    }
    return 1;
  }
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
  if(path && lua_gettop(L) == 2) {
    noit_conf_section_t section;
    char *element, *base;
    SPLIT_PATH(path, base, element);
    
    section = noit_conf_get_section(NULL, base);
    if(!section || !element) lua_pushboolean(L, 0);
    else {
      noit_conf_set_string(section, element, lua_tostring(L,2));
      lua_pushboolean(L, 1);
    }
    return 1;
  }
  if(path &&
     noit_conf_get_float(NULL, path, &val)) {
    lua_pushnumber(L,val);
  }
  else lua_pushnil(L);
  return 1;
}
static int
nl_conf_replace_value(lua_State *L) {
  const char *path = lua_tostring(L,1);
  if (path && lua_gettop(L) == 2) {
    noit_conf_section_t section;
    char *element, *base;
    SPLIT_PATH(path, base, element);
    if (!element) {
      lua_pushboolean(L, 0);
      return 1;
    }
    while (NULL != (section = noit_conf_get_section(NULL, path))) {
      noit_conf_remove_section(section);
    }
    section = noit_conf_get_section(NULL, base);
    if(!section) {
      lua_pushboolean(L, 0);
      return 1;
    }
    noit_conf_set_string(section, element, lua_tostring(L,2));
    lua_pushboolean(L, 1);
  }
  else lua_pushnil(L);
  return 1;
}
static int
nl_conf_replace_boolean(lua_State *L) {
  const char *path = lua_tostring(L,1);
  if (path && lua_gettop(L) == 2) {
    noit_conf_section_t section;
    char *element, *base;
    SPLIT_PATH(path, base, element);
    if (!element) {
      lua_pushboolean(L, 0);
      return 1;
    }
    while (NULL != (section = noit_conf_get_section(NULL, path))) {
      noit_conf_remove_section(section);
    }
    section = noit_conf_get_section(NULL, base);
    if(!section) {
      lua_pushboolean(L, 0);
      return 1;
    }
    noit_conf_set_string(section, element, lua_toboolean(L,2) ? "true" : "false");
    lua_pushboolean(L, 1);
  }
  else lua_pushnil(L);
  return 1;
}
struct xpath_iter {
  xmlXPathContextPtr ctxt;
  xmlXPathObjectPtr pobj;
  int cnt;
  int idx;
};
static int
noit_lua_xpath_iter(lua_State *L) {
  struct xpath_iter *xpi;
  xpi = lua_touserdata(L, lua_upvalueindex(1));
  if(xpi->pobj) {
    if(xpi->idx < xpi->cnt) {
      xmlNodePtr node, *nodeptr;
      node = xmlXPathNodeSetItem(xpi->pobj->nodesetval, xpi->idx);
      xpi->idx++;
      nodeptr = (xmlNodePtr *)lua_newuserdata(L, sizeof(node));
      *nodeptr = node;
      luaL_getmetatable(L, "noit.xmlnode");
      lua_setmetatable(L, -2);
      return 1;
    }
  }
  return 0;
}
static int
noit_lua_xpath(lua_State *L) {
  int n;
  const char *xpathexpr;
  xmlDocPtr *docptr, doc;
  xmlNodePtr *nodeptr = NULL;
  xmlXPathContextPtr ctxt;
  struct xpath_iter *xpi;

  n = lua_gettop(L);
  /* the first arg is implicitly self (it's a method) */
  docptr = lua_touserdata(L, lua_upvalueindex(1));
  if(docptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(n < 2 || n > 3) luaL_error(L, "expects 1 or 2 arguments, got %d", n);
  doc = *docptr;
  xpathexpr = lua_tostring(L, 2);
  if(!xpathexpr) luaL_error(L, "no xpath expression provided");
  ctxt = xmlXPathNewContext(doc);
  if(n == 3) {
    nodeptr = lua_touserdata(L, 3);
    if(nodeptr) ctxt->node = *nodeptr;
  }
  if(!ctxt) luaL_error(L, "invalid xpath");

  xpi = (struct xpath_iter *)lua_newuserdata(L, sizeof(*xpi));
  xpi->ctxt = ctxt;
  xpi->pobj = xmlXPathEval((xmlChar *)xpathexpr, xpi->ctxt);
  if(!xpi->pobj || xpi->pobj->type != XPATH_NODESET)
    xpi->cnt = 0;
  else
    xpi->cnt = xmlXPathNodeSetGetLength(xpi->pobj->nodesetval);
  xpi->idx = 0;
  luaL_getmetatable(L, "noit.xpathiter");
  lua_setmetatable(L, -2);
  lua_pushcclosure(L, noit_lua_xpath_iter, 1);
  return 1;
}
static int
noit_lua_xmlnode_name(lua_State *L) {
  xmlNodePtr *nodeptr;
  /* the first arg is implicitly self (it's a method) */
  nodeptr = lua_touserdata(L, lua_upvalueindex(1));
  if(nodeptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(lua_gettop(L) == 1) {
    xmlChar *v;
    v = (xmlChar *)(*nodeptr)->name;
    if(v) {
      lua_pushstring(L, (const char *)v);
    }
    else lua_pushnil(L);
    return 1;
  }
  luaL_error(L,"must be called with no arguments");
  return 0;
}
static int
noit_lua_xmlnode_attr(lua_State *L) {
  xmlNodePtr *nodeptr;
  /* the first arg is implicitly self (it's a method) */
  nodeptr = lua_touserdata(L, lua_upvalueindex(1));
  if(nodeptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(lua_gettop(L) == 3 && lua_isstring(L,2)) {
    const char *attr = lua_tostring(L,2);
    if(lua_isnil(L,3))
      xmlSetProp(*nodeptr, (xmlChar *)attr, NULL);
    else
      xmlSetProp(*nodeptr, (xmlChar *)attr, (xmlChar *)lua_tostring(L,3));
    return 0;
  }
  if(lua_gettop(L) == 2 && lua_isstring(L,2)) {
    xmlChar *v;
    const char *attr = lua_tostring(L,2);
    v = xmlGetProp(*nodeptr, (xmlChar *)attr);
    if(v) {
      lua_pushstring(L, (const char *)v);
      xmlFree(v);
    }
    else lua_pushnil(L);
    return 1;
  }
  luaL_error(L,"must be called with one argument");
  return 0;
}
static int
noit_lua_xmlnode_contents(lua_State *L) {
  xmlNodePtr *nodeptr;
  /* the first arg is implicitly self (it's a method) */
  nodeptr = lua_touserdata(L, lua_upvalueindex(1));
  if(nodeptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(lua_gettop(L) == 2 && lua_isstring(L,2)) {
    const char *data = lua_tostring(L,2);
    xmlChar *enc = xmlEncodeEntitiesReentrant((*nodeptr)->doc, (xmlChar *)data);
    xmlNodeSetContent(*nodeptr, (xmlChar *)enc);
    xmlFree(enc);
    return 0;
  }
  if(lua_gettop(L) == 1) {
    xmlChar *v;
    v = xmlNodeGetContent(*nodeptr);
    if(v) {
      lua_pushstring(L, (const char *)v);
      xmlFree(v);
    }
    else lua_pushnil(L);
    return 1;
  }
  luaL_error(L,"must be called with no arguments");
  return 0;
}
static int
noit_lua_xmlnode_next(lua_State *L) {
  xmlNodePtr *nodeptr;
  nodeptr = lua_touserdata(L, lua_upvalueindex(1));
  if(*nodeptr) {
    xmlNodePtr *newnodeptr;
    newnodeptr = (xmlNodePtr *)lua_newuserdata(L, sizeof(*nodeptr));
    *newnodeptr = *nodeptr;
    luaL_getmetatable(L, "noit.xmlnode");
    lua_setmetatable(L, -2);
    *nodeptr = (*nodeptr)->next;
    return 1;
  }
  return 0;
}
static int
noit_lua_xmlnode_addchild(lua_State *L) {
  xmlNodePtr *nodeptr;
  nodeptr = lua_touserdata(L, lua_upvalueindex(1));
  if(nodeptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(lua_gettop(L) == 2 && lua_isstring(L,2)) {
    xmlNodePtr *newnodeptr;
    newnodeptr = (xmlNodePtr *)lua_newuserdata(L, sizeof(*nodeptr));
    *newnodeptr = xmlNewChild(*nodeptr, NULL,
                              (xmlChar *)lua_tostring(L,2), NULL);
    luaL_getmetatable(L, "noit.xmlnode");
    lua_setmetatable(L, -2);
    return 1;
  }
  luaL_error(L,"must be called with one argument");
  return 0;
}
static int
noit_lua_xmlnode_children(lua_State *L) {
  xmlNodePtr *nodeptr, node, cnode;
  /* the first arg is implicitly self (it's a method) */
  nodeptr = lua_touserdata(L, lua_upvalueindex(1));
  if(nodeptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  node = *nodeptr;
  cnode = node->children;
  nodeptr = lua_newuserdata(L, sizeof(cnode));
  *nodeptr = cnode;
  luaL_getmetatable(L, "noit.xmlnode");
  lua_setmetatable(L, -2);
  lua_pushcclosure(L, noit_lua_xmlnode_next, 1);
  return 1;
}
static int
noit_lua_xml_tostring(lua_State *L) {
  int n;
  xmlDocPtr *docptr;
  char *xmlstring;
  n = lua_gettop(L);
  /* the first arg is implicitly self (it's a method) */
  docptr = lua_touserdata(L, lua_upvalueindex(1));
  if(docptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(n != 1) luaL_error(L, "expects no arguments, got %d", n - 1);
  xmlstring = noit_xmlSaveToBuffer(*docptr);
  lua_pushstring(L, xmlstring);
  free(xmlstring);
  return 1;
}
static int
noit_lua_xml_docroot(lua_State *L) {
  int n;
  xmlDocPtr *docptr;
  xmlNodePtr *ptr;
  n = lua_gettop(L);
  /* the first arg is implicitly self (it's a method) */
  docptr = lua_touserdata(L, lua_upvalueindex(1));
  if(docptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(n != 1) luaL_error(L, "expects no arguments, got %d", n - 1);
  ptr = lua_newuserdata(L, sizeof(*ptr));
  *ptr = xmlDocGetRootElement(*docptr);
  luaL_getmetatable(L, "noit.xmlnode");
  lua_setmetatable(L, -2);
  return 1;
}
static int
noit_lua_xpathiter_gc(lua_State *L) {
  struct xpath_iter *xpi;
  xpi = lua_touserdata(L, 1);
  xmlXPathFreeContext(xpi->ctxt);
  if(xpi->pobj) xmlXPathFreeObject(xpi->pobj);
  return 0;
}
static int
noit_xmlnode_index_func(lua_State *L) {
  int n;
  const char *k;
  xmlNodePtr *udata;
  n = lua_gettop(L); /* number of arguments */
  assert(n == 2);
  if(!luaL_checkudata(L, 1, "noit.xmlnode")) {
    luaL_error(L, "metatable error, arg1 not a noit.xmlnode!");
  }
  udata = lua_touserdata(L, 1);
  if(!lua_isstring(L, 2)) {
    luaL_error(L, "metatable error, arg2 not a string!");
  }
  k = lua_tostring(L, 2);
  switch(*k) {
    case 'a':
      LUA_DISPATCH(attr, noit_lua_xmlnode_attr);
      LUA_DISPATCH(attribute, noit_lua_xmlnode_attr);
      LUA_DISPATCH(addchild, noit_lua_xmlnode_addchild);
      break;
    case 'c':
      LUA_DISPATCH(children, noit_lua_xmlnode_children);
      LUA_DISPATCH(contents, noit_lua_xmlnode_contents);
      break;
    case 'n':
      LUA_DISPATCH(name, noit_lua_xmlnode_name);
      break;
    default:
      break;
  }
  luaL_error(L, "noit.xmlnode no such element: %s", k);
  return 0;
}
static int
nl_parsexml(lua_State *L) {
  xmlDocPtr *docptr, doc;
  const char *in;
  size_t inlen;

  if(lua_gettop(L) != 1) luaL_error(L, "parsexml requires one argument"); 

  in = lua_tolstring(L, 1, &inlen);
  doc = xmlParseMemory(in, inlen);
  if(!doc) {
    lua_pushnil(L);
    return 1;
  }

  docptr = (xmlDocPtr *)lua_newuserdata(L, sizeof(doc)); 
  *docptr = doc;
  luaL_getmetatable(L, "noit.xmldoc");
  lua_setmetatable(L, -2);
  return 1;
}
static int
noit_lua_xmldoc_gc(lua_State *L) {
  xmlDocPtr *holder;
  holder = (xmlDocPtr *)lua_touserdata(L,1);
  xmlFreeDoc(*holder);
  return 0;
}
static int
noit_xmldoc_index_func(lua_State *L) {
  int n;
  const char *k;
  xmlDocPtr *udata;
  n = lua_gettop(L); /* number of arguments */
  assert(n == 2);
  if(!luaL_checkudata(L, 1, "noit.xmldoc")) {
    luaL_error(L, "metatable error, arg1 not a noit.xmldoc!");
  }
  udata = lua_touserdata(L, 1);
  if(!lua_isstring(L, 2)) {
    luaL_error(L, "metatable error, arg2 not a string!");
  }
  k = lua_tostring(L, 2);
  switch(*k) {
    case 'r':
     LUA_DISPATCH(root, noit_lua_xml_docroot);
     break;
    case 't':
     LUA_DISPATCH(tostring, noit_lua_xml_tostring);
     break;
    case 'x':
     LUA_DISPATCH(xpath, noit_lua_xpath);
     break;
    default:
     break;
  }
  luaL_error(L, "noit.xmldoc no such element: %s", k);
  return 0;
}

static int
noit_lua_json_tostring(lua_State *L) {
  int n;
  json_crutch **docptr;
  const char *jsonstring;
  n = lua_gettop(L);
  /* the first arg is implicitly self (it's a method) */
  docptr = lua_touserdata(L, lua_upvalueindex(1));
  if(docptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(n != 1) luaL_error(L, "expects no arguments, got %d", n - 1);
  jsonstring = json_object_to_json_string((*docptr)->root);
  lua_pushstring(L, jsonstring);
  /* jsonstring is freed with the root object later */
  return 1;
}
static int
noit_json_object_to_luatype(lua_State *L, struct json_object *o) {
  if(!o) {
    lua_pushnil(L);
    return 1;
  }
  switch(json_object_get_type(o)) {
    case json_type_null: lua_pushnil(L); break;
    case json_type_object:
    {
      struct jl_lh_table *lh;
      struct jl_lh_entry *el;
      lh = json_object_get_object(o);
      lua_createtable(L, 0, lh->count);
      jl_lh_foreach(lh, el) {
        noit_json_object_to_luatype(L, (struct json_object *)el->v);
        lua_setfield(L, -2, el->k);
      }
      break;
    }
    case json_type_string: lua_pushstring(L, json_object_get_string(o)); break;
    case json_type_boolean: lua_pushboolean(L, json_object_get_boolean(o)); break;
    case json_type_double: lua_pushnumber(L, json_object_get_double(o)); break;
    case json_type_int:
    {
      int64_t i64;
      uint64_t u64;
      char istr[64];
      switch(json_object_get_int_overflow(o)) {
        case json_overflow_int:
          lua_pushnumber(L, json_object_get_int(o)); break;
        case json_overflow_int64:
          i64 = json_object_get_int64(o);
          snprintf(istr, sizeof(istr), "%lld", (long long int)i64);
          lua_pushstring(L, istr);
          break;
        case json_overflow_uint64:
          u64 = json_object_get_uint64(o);
          snprintf(istr, sizeof(istr), "%llu", (long long unsigned int)u64);
          lua_pushstring(L, istr);
          break;
      }
      break;
    }
    case json_type_array:
    {
      int i, cnt;
      struct jl_array_list *al;
      al = json_object_get_array(o);
      cnt = al ? jl_array_list_length(al) : 0;
      lua_createtable(L, 0, cnt);
      for(i=0;i<cnt;i++) {
        noit_json_object_to_luatype(L, (struct json_object *)jl_array_list_get_idx(al, i));
        lua_rawseti(L, -2, i);
      }
      break;
    }
  }
  return 1;
}
static int
noit_lua_json_document(lua_State *L) {
  int n;
  json_crutch **docptr;
  n = lua_gettop(L);
  /* the first arg is implicitly self (it's a method) */
  docptr = lua_touserdata(L, lua_upvalueindex(1));
  if(docptr != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(n != 1) luaL_error(L, "expects no arguments, got %d", n - 1);
  return noit_json_object_to_luatype(L, (*docptr)->root);
}
static int
nl_parsejson(lua_State *L) {
  json_crutch **docptr, *doc;
  const char *in;
  size_t inlen;

  if(lua_gettop(L) != 1) luaL_error(L, "parsejson requires one argument"); 

  in = lua_tolstring(L, 1, &inlen);
  doc = calloc(1, sizeof(*doc));
  doc->tok = json_tokener_new();
  doc->root = json_tokener_parse_ex(doc->tok, in, inlen);
  if(is_error(doc->root)) {
    json_tokener_free(doc->tok);
    if(doc->root) json_object_put(doc->root);
    free(doc);
    lua_pushnil(L);
    return 1;
  }

  docptr = (json_crutch **)lua_newuserdata(L, sizeof(doc)); 
  *docptr = doc;
  luaL_getmetatable(L, "noit.json");
  lua_setmetatable(L, -2);
  return 1;
}
static int
noit_lua_json_gc(lua_State *L) {
  json_crutch **json;
  json = (json_crutch **)lua_touserdata(L,1);
  if((*json)->tok) json_tokener_free((*json)->tok);
  if((*json)->root) json_object_put((*json)->root);
  free(*json);
  return 0;
}
static int
noit_json_index_func(lua_State *L) {
  int n;
  const char *k;
  json_crutch **udata;
  n = lua_gettop(L); /* number of arguments */
  assert(n == 2);
  if(!luaL_checkudata(L, 1, "noit.json")) {
    luaL_error(L, "metatable error, arg1 not a noit.json!");
  }
  udata = lua_touserdata(L, 1);
  if(!lua_isstring(L, 2)) {
    luaL_error(L, "metatable error, arg2 not a string!");
  }
  k = lua_tostring(L, 2);
  switch(*k) {
    case 'd':
     LUA_DISPATCH(document, noit_lua_json_document);
     break;
    case 't':
     LUA_DISPATCH(tostring, noit_lua_json_tostring);
     break;
    default:
     break;
  }
  luaL_error(L, "noit.json no such element: %s", k);
  return 0;
}

struct spawn_info {
  pid_t pid;
  int last_errno;
  eventer_t in;
  eventer_t out;
  eventer_t err;
};

int nl_spawn(lua_State *L) {
  int in[2] = {-1,-1}, out[2] = {-1,-1}, err[2] = {-1,-1};
  int arg_count = 0, rv, i;
  const char *path;
  const char **argv, **envp = NULL;
  struct spawn_info *spawn_info;
  posix_spawnattr_t *attr = NULL;
  posix_spawn_file_actions_t *filea;
  noit_lua_resume_info_t *ri;

  ri = noit_lua_get_resume_info(L);
  assert(ri);
  spawn_info = (struct spawn_info *)lua_newuserdata(L, sizeof(*spawn_info));
  memset(spawn_info, 0, sizeof(*spawn_info));
  spawn_info->pid = -1;
  luaL_getmetatable(L, "noit.process");
  lua_setmetatable(L, -2);

  path = lua_tostring(L,1);

  /* argv */
  lua_pushnil(L);  /* first key */
  while (lua_next(L, 2) != 0) arg_count++, lua_pop(L, 1);
  argv = alloca(sizeof(*argv) * (arg_count + 1));
  lua_pushnil(L);  /* first key */
  arg_count = 0;
  while (lua_next(L, 2) != 0) {
    argv[arg_count++] = lua_tostring(L, -1);
    lua_pop(L, 1);
  }
  argv[arg_count] = NULL;

  /* envp */
  arg_count = 0;
  if(!lua_isnil(L,3)) {
    lua_pushnil(L);  /* first key */
    while (lua_next(L, 3) != 0) arg_count++, lua_pop(L, 1);
    envp = alloca(sizeof(*envp) * (arg_count + 1));
    lua_pushnil(L);  /* first key */
    arg_count = 0;
    while (lua_next(L, 3) != 0) {
      envp[arg_count++] = lua_tostring(L, -1);
      lua_pop(L, 1);
    }
    envp[arg_count] = NULL;
  }
  if(arg_count == 0) {
    envp = alloca(sizeof(*envp));
    envp[0] = NULL;
  }

  filea = (posix_spawn_file_actions_t *)alloca(sizeof(*filea));
  if(posix_spawn_file_actions_init(filea)) {
    spawn_info->last_errno = errno;
    noitL(nlerr, "posix_spawn_file_actions_init -> %s\n", strerror(spawn_info->last_errno));
    goto err;
  }
#define PIPE_SAFE(p, idx, tfd) do { \
  if(pipe(p) < 0) { \
    spawn_info->last_errno = errno; \
    noitL(nlerr, "pipe -> %s\n", strerror(spawn_info->last_errno)); \
    goto err; \
  } \
  posix_spawn_file_actions_adddup2(filea, p[idx], tfd); \
  posix_spawn_file_actions_addclose(filea, p[idx ? 0 : 1]); \
} while(0)

  PIPE_SAFE(in, 0, 0);
  PIPE_SAFE(out, 1, 1);
  PIPE_SAFE(err, 1, 2);
  attr = (posix_spawnattr_t *)alloca(sizeof(*attr));
  memset(attr, 0, sizeof(*attr));
  if(posix_spawnattr_init(attr)) {
    attr = NULL;
    spawn_info->last_errno = errno;
    noitL(nlerr, "posix_spawnattr_init(%d) -> %s\n", errno, strerror(errno));
    goto err;
  }
  rv = posix_spawnp(&spawn_info->pid, path, filea, attr,
                   (char * const *)argv, (char * const *)envp);
  if(rv != 0) {
    spawn_info->last_errno = errno;
    noitL(nlerr, "posix_spawn(%d) -> %s\n", errno, strerror(errno));
    goto err;
  }
  /* Cleanup the parent half */
  if(filea) posix_spawn_file_actions_destroy(filea);
  if(attr) posix_spawnattr_destroy(attr);
  close(in[0]); close(out[1]); close(err[1]);

#define NEWEVENT(e, ourfd, L, ri) do { \
  struct nl_slcl *cl; \
  cl = calloc(1, sizeof(*cl)); \
  cl->free = nl_extended_free; \
  cl->L = L; \
  e = eventer_alloc(); \
  e->fd = ourfd; \
  e->mask = EVENTER_EXCEPTION; \
  e->callback = NULL; \
  cl->eptr = noit_lua_event(L, e); \
  e->closure = cl; \
  noit_lua_check_register_event(ri, e); \
} while(0)

  NEWEVENT(spawn_info->in,  in[1],  L, ri);
  NEWEVENT(spawn_info->out, out[0], L, ri);
  NEWEVENT(spawn_info->err, err[0], L, ri);
  return 4;

 err:
  noitL(nlerr, "nl_spawn -> %s\n", strerror(spawn_info->last_errno));
  if(in[0] != -1) close(in[0]);
  if(in[1] != -1) close(in[1]);
  if(out[0] != -1) close(out[0]);
  if(out[1] != -1) close(out[1]);
  if(err[0] != -1) close(err[0]);
  if(err[1] != -1) close(err[1]);
  if(filea) posix_spawn_file_actions_destroy(filea);
  if(attr) posix_spawnattr_destroy(attr);
  lua_pushinteger(L, spawn_info->last_errno);
  return 2;
}

static int
noit_lua_process_wait(lua_State *L) {
  int rv, status;
  struct spawn_info *spawn_info;
  /* the first arg is implicitly self (it's a method) */
  spawn_info = lua_touserdata(L, lua_upvalueindex(1));
  if(spawn_info != lua_touserdata(L, 1))
    luaL_error(L, "must be called as method");
  if(spawn_info->pid == -1) {
    lua_pushnil(L);
    lua_pushinteger(L, EINVAL);
    return 1;
  }
  while((rv = waitpid(spawn_info->pid, &status, 0)) == -1 && errno == EINTR);
  if(rv == spawn_info->pid) {
    lua_pushinteger(L, status);
    return 1;
  }
  lua_pushnil(L);
  lua_pushinteger(L, errno);
  return 2;
}

static int
noit_lua_process_index_func(lua_State *L) {
  int n;
  const char *k;
  struct spawn_info *udata;
  n = lua_gettop(L); /* number of arguments */
  assert(n == 2);
  if(!luaL_checkudata(L, 1, "noit.process")) {
    luaL_error(L, "metatable error, arg1 not a noit.process!");
  }
  udata = lua_touserdata(L, 1);
  if(!lua_isstring(L, 2)) {
    luaL_error(L, "metatable error, arg2 not a string!");
  }
  k = lua_tostring(L, 2);
  switch(*k) {
    case 'w':
      LUA_DISPATCH(wait, noit_lua_process_wait);
      break;
    default:
      break;
  }
  luaL_error(L, "noit.process no such element: %s", k);
  return 0;
}

static int
noit_lua_eventer_gc(lua_State *L) {
  eventer_t *eptr, e;

  eptr = (eventer_t *)lua_touserdata(L,1);
  /* Simply null it out so if we try to use it, we'll notice */
  e = *eptr;
  if(e) {
    noit_lua_resume_info_t *ci;
    struct nl_slcl *cl = e->closure;
    int newmask;

    ci = noit_lua_get_resume_info(cl->L);
    assert(ci);
    noit_lua_check_deregister_event(ci, e, 0);
    eventer_remove_fd(e->fd);
    e->opset->close(e->fd, &newmask, e);
    eventer_free(e);
    if(cl->free) cl->free(cl);
  }
  *eptr = NULL;
  return 0;
}

static int
noit_lua_process_gc(lua_State *L) {
  struct spawn_info *spawn_info;

  spawn_info = (struct spawn_info *)lua_touserdata(L,1);
  if(spawn_info->pid != -1) {
    int status;
    if(spawn_info->pid != waitpid(spawn_info->pid, &status, WNOHANG)) {
      kill(spawn_info->pid, SIGKILL);
      while(waitpid(spawn_info->pid, &status, 0) == -1 && errno == EINTR);
    }
    spawn_info->pid = -1;
  }
  return 0;
}

static int
nl_cancel_coro(lua_State *L) {
  noit_lua_resume_info_t *ci;
  lua_State *co = lua_tothread(L,1);
  ci = noit_lua_get_resume_info(co);
  assert(ci);
  noit_lua_cancel_coro(ci);
  return 0;
}

static const luaL_Reg noitlib[] = {
  { "cancel_coro", nl_cancel_coro },
  { "waitfor", nl_waitfor },
  { "notify", nl_waitfor_notify },
  { "sleep", nl_sleep },
  { "gettimeofday", nl_gettimeofday },
  { "uuid", nl_uuid },
  { "socket", nl_socket },
  { "dns", nl_dns_lookup },
  { "valid_ip", nl_valid_ip },
  { "log", nl_log },
  { "open", nl_open },
  { "write", nl_write },
  { "close", nl_close },
  { "chmod", nl_chmod },
  { "stat", nl_stat },
  { "readdir", nl_readdir },
  { "crc32", nl_crc32 },
  { "base32_decode", nl_base32_decode },
  { "base32_encode", nl_base32_encode },
  { "base64_decode", nl_base64_decode },
  { "base64_encode", nl_base64_encode },
  { "utf8tohtml", nl_utf8tohtml },
  { "hmac_sha1_encode", nl_hmac_sha1_encode },
  { "hmac_sha256_encode", nl_hmac_sha256_encode },
  { "md5_hex", nl_md5_hex },
  { "pcre", nl_pcre },
  { "gunzip", nl_gunzip },
  { "check", nl_check },
  { "conf", nl_conf_get_string },
  { "conf_get", nl_conf_get_string },
  { "conf_get_string", nl_conf_get_string },
  { "conf_string", nl_conf_get_string },
  { "conf_replace_string", nl_conf_replace_value },
  { "conf_get_integer", nl_conf_get_integer },
  { "conf_integer", nl_conf_get_integer },
  { "conf_replace_integer", nl_conf_replace_value },
  { "conf_get_boolean", nl_conf_get_boolean },
  { "conf_boolean", nl_conf_get_boolean },
  { "conf_replace_boolean", nl_conf_replace_boolean },
  { "conf_get_number", nl_conf_get_float },
  { "conf_number", nl_conf_get_float },
  { "conf_replace_number", nl_conf_replace_value },
  { "parsexml", nl_parsexml },
  { "parsejson", nl_parsejson },
  { "spawn", nl_spawn },
  { NULL, NULL }
};

int luaopen_noit(lua_State *L) {
  luaL_newmetatable(L, "noit.eventer");
  lua_pushcclosure(L, noit_eventer_index_func, 0);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, noit_lua_eventer_gc);
  lua_setfield(L, -2, "__gc");

  luaL_newmetatable(L, "noit.eventer.ssl_ctx");
  lua_pushcclosure(L, noit_ssl_ctx_index_func, 0);
  lua_setfield(L, -2, "__index");

  luaL_newmetatable(L, "noit.process");
  lua_pushcfunction(L, noit_lua_process_gc);
  lua_setfield(L, -2, "__gc");
  luaL_newmetatable(L, "noit.process");
  lua_pushcfunction(L, noit_lua_process_index_func);
  lua_setfield(L, -2, "__index");

  luaL_newmetatable(L, "noit.dns");
  lua_pushcfunction(L, noit_lua_dns_gc);
  lua_setfield(L, -2, "__gc");
  luaL_newmetatable(L, "noit.dns");
  lua_pushcfunction(L, noit_lua_dns_index_func);
  lua_setfield(L, -2, "__index");

  luaL_newmetatable(L, "noit.gunzip");
  lua_pushcfunction(L, noit_lua_gunzip_gc);
  lua_setfield(L, -2, "__gc");

  luaL_newmetatable(L, "noit.pcre");
  lua_pushcfunction(L, noit_lua_pcre_gc);
  lua_setfield(L, -2, "__gc");

  luaL_newmetatable(L, "noit.json");
  lua_pushcfunction(L, noit_lua_json_gc);
  lua_setfield(L, -2, "__gc");
  luaL_newmetatable(L, "noit.json");
  lua_pushcclosure(L, noit_json_index_func, 0);
  lua_setfield(L, -2, "__index");

  luaL_newmetatable(L, "noit.xmldoc");
  lua_pushcfunction(L, noit_lua_xmldoc_gc);
  lua_setfield(L, -2, "__gc");
  luaL_newmetatable(L, "noit.xmldoc");
  lua_pushcclosure(L, noit_xmldoc_index_func, 0);
  lua_setfield(L, -2, "__index");

  luaL_newmetatable(L, "noit.xmlnode");
  lua_pushcclosure(L, noit_xmlnode_index_func, 0);
  lua_setfield(L, -2, "__index");

  luaL_newmetatable(L, "noit.xpathiter");
  lua_pushcfunction(L, noit_lua_xpathiter_gc);
  lua_setfield(L, -2, "__gc");

  luaL_openlib(L, "noit", noitlib, 0);

#define LUA_DEFINE_INT(L, name) do { \
  lua_pushinteger(L, name); \
  lua_setglobal(L, #name); \
} while(0)
  LUA_DEFINE_INT(L, S_IFIFO);
  LUA_DEFINE_INT(L, S_IFCHR);
  LUA_DEFINE_INT(L, S_IFDIR);
  LUA_DEFINE_INT(L, S_IFBLK);
  LUA_DEFINE_INT(L, S_IFREG);
  LUA_DEFINE_INT(L, S_IFLNK);
  LUA_DEFINE_INT(L, S_IFSOCK);
  LUA_DEFINE_INT(L, O_RDONLY);
  LUA_DEFINE_INT(L, O_WRONLY);
  LUA_DEFINE_INT(L, O_RDWR);
  LUA_DEFINE_INT(L, O_APPEND);
  LUA_DEFINE_INT(L, O_SYNC);
#ifdef O_NOFOLLOW
  LUA_DEFINE_INT(L, O_NOFOLLOW);
#else
#define O_NOFOLLOW 0
  LUA_DEFINE_INT(L, O_NOFOLLOW);
#undef O_NOFOLLOW
#endif
  LUA_DEFINE_INT(L, O_CREAT);
  LUA_DEFINE_INT(L, O_TRUNC);
  LUA_DEFINE_INT(L, O_EXCL);
  return 0;
}

void noit_lua_init() {
  eventer_name_callback("lua/sleep", nl_sleep_complete);
  eventer_name_callback("lua/socket_read",
                        noit_lua_socket_read_complete);
  eventer_name_callback("lua/socket_write",
                        noit_lua_socket_write_complete);
  eventer_name_callback("lua/socket_recv",
                        noit_lua_socket_recv_complete);
  eventer_name_callback("lua/socket_send",
                        noit_lua_socket_send_complete);
  eventer_name_callback("lua/socket_connect",
                        noit_lua_socket_connect_complete);
  eventer_name_callback("lua/socket_accept",
                        noit_lua_socket_accept_complete);
  eventer_name_callback("lua/ssl_upgrade", noit_lua_ssl_upgrade);
  nlerr = noit_log_stream_find("error/lua");
  nldeb = noit_log_stream_find("debug/lua");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;
  noit_lua_init_dns();
}
