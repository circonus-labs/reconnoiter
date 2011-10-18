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

#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "noit_listener.h"
#include "noit_conf.h"

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;
static noit_hash_table listener_commands = NOIT_HASH_EMPTY;
noit_hash_table *
noit_listener_commands() {
  return &listener_commands;
}

void
acceptor_closure_free(acceptor_closure_t *ac) {
  if(ac->remote_cn) free(ac->remote_cn);
  if(ac->service_ctx_free && ac->service_ctx)
    ac->service_ctx_free(ac->service_ctx);
  free(ac);
}

static int
noit_listener_accept_ssl(eventer_t e, int mask,
                         void *closure, struct timeval *tv) {
  int rv;
  listener_closure_t listener_closure = (listener_closure_t)closure;
  acceptor_closure_t *ac = NULL;
  if(!closure) goto socketfail;
  ac = listener_closure->dispatch_closure;

  rv = eventer_SSL_accept(e, &mask);
  if(rv > 0) {
    eventer_ssl_ctx_t *sslctx;
    e->callback = listener_closure->dispatch_callback;
    /* We must make a copy of the acceptor_closure_t for each new
     * connection.
     */
    if((sslctx = eventer_get_eventer_ssl_ctx(e)) != NULL) {
      const char *cn, *end;
      cn = eventer_ssl_get_peer_subject(sslctx);
      if(cn && (cn = strstr(cn, "CN=")) != NULL) {
        cn += 3;
        end = cn;
        while(*end && *end != '/') end++;
        ac->remote_cn = malloc(end - cn + 1);
        memcpy(ac->remote_cn, cn, end - cn);
        ac->remote_cn[end-cn] = '\0';
      }
    }
    e->closure = ac;
    noitL(nlerr, "noit_listener[%s] SSL_accept on fd %d [%s]\n",
          eventer_name_for_callback(e->callback),
          e->fd, ac->remote_cn ? ac->remote_cn : "anonymous");
    return e->callback(e, mask, e->closure, tv);
  }
  if(errno == EAGAIN) return mask|EVENTER_EXCEPTION;

 socketfail:
  if(listener_closure) free(listener_closure);
  if(ac) acceptor_closure_free(ac);
  eventer_remove_fd(e->fd);
  e->opset->close(e->fd, &mask, e);
  return 0;
}

static int
noit_listener_acceptor(eventer_t e, int mask,
                       void *closure, struct timeval *tv) {
  int conn, newmask = EVENTER_READ;
  socklen_t salen;
  listener_closure_t listener_closure = (listener_closure_t)closure;
  acceptor_closure_t *ac = NULL;

  if(mask & EVENTER_EXCEPTION) {
 socketfail:
    if(ac) acceptor_closure_free(ac);
    /* We don't shut down the socket, it's out listener! */
    return EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
  }

  do {
    ac = malloc(sizeof(*ac));
    memcpy(ac, listener_closure->dispatch_closure, sizeof(*ac));
    salen = sizeof(ac->remote);
    conn = e->opset->accept(e->fd, &ac->remote.remote_addr, &salen, &newmask, e);
    if(conn >= 0) {
      eventer_t newe;
      noitL(nlerr, "noit_listener[%s] accepted fd %d\n",
            eventer_name_for_callback(listener_closure->dispatch_callback),
            conn);
      if(eventer_set_fd_nonblocking(conn)) {
        close(conn);
        free(ac);
        goto accept_bail;
      }
      newe = eventer_alloc();
      newe->fd = conn;
      newe->mask = EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
  
      if(listener_closure->sslconfig->size) {
        const char *cert, *key, *ca, *ciphers, *crl;
        eventer_ssl_ctx_t *ctx;
        /* We have an SSL configuration.  While our socket accept is
         * complete, we now have to SSL_accept, which could require
         * several reads and writes and needs its own event callback.
         */
  #define SSLCONFGET(var,name) do { \
    if(!noit_hash_retr_str(listener_closure->sslconfig, name, strlen(name), \
                           &var)) var = NULL; } while(0)
        SSLCONFGET(cert, "certificate_file");
        SSLCONFGET(key, "key_file");
        SSLCONFGET(ca, "ca_chain");
        SSLCONFGET(ciphers, "ciphers");
        ctx = eventer_ssl_ctx_new(SSL_SERVER, cert, key, ca, ciphers);
        if(!ctx) {
          newe->opset->close(newe->fd, &newmask, e);
          eventer_free(newe);
          goto socketfail;
        }
        SSLCONFGET(crl, "crl");
        if(crl) {
          if(!eventer_ssl_use_crl(ctx, crl)) {
            noitL(noit_error, "Failed to load CRL from %s\n", crl);
            eventer_ssl_ctx_free(ctx);
            newe->opset->close(newe->fd, &newmask, e);
            eventer_free(newe);
            goto socketfail;
          }
        }

        eventer_ssl_ctx_set_verify(ctx, eventer_ssl_verify_cert,
                                   listener_closure->sslconfig);
        EVENTER_ATTACH_SSL(newe, ctx);
        newe->callback = noit_listener_accept_ssl;
        newe->closure = malloc(sizeof(*listener_closure));
        memcpy(newe->closure, listener_closure, sizeof(*listener_closure));
        ((listener_closure_t)newe->closure)->dispatch_closure = ac;
      }
      else {
        newe->callback = listener_closure->dispatch_callback;
        /* We must make a copy of the acceptor_closure_t for each new
         * connection.
         */
        newe->closure = ac;
      }
      eventer_add(newe);
    }
    else {
      if(errno == EAGAIN) {
        if(ac) acceptor_closure_free(ac);
      }
      else if(errno != EINTR) {
        noitL(noit_error, "accept socket error: %s\n", strerror(errno));
        goto socketfail;
      }
    }
  } while(conn >= 0);
 accept_bail:
  return newmask | EVENTER_EXCEPTION;
}

int
noit_listener(char *host, unsigned short port, int type,
              int backlog, noit_hash_table *sslconfig,
              noit_hash_table *config,
              eventer_func_t handler, void *service_ctx) {
  int rv, fd;
  int8_t family;
  int sockaddr_len;
  socklen_t reuse;
  listener_closure_t listener_closure;
  eventer_t event;
  union {
    struct in_addr addr4;
    struct in6_addr addr6;
  } a;
  union {
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr_un addru;
  } s;
  const char *event_name;

  noitL(nldeb, "noit_listener(%s, %d, %d, %d, %s, %p)\n",
        host, port, type, backlog,
        (event_name = eventer_name_for_callback(handler))?event_name:"??",
        service_ctx);
  if(host[0] == '/') {
    family = AF_UNIX;
  }
  else {
    family = AF_INET;
    rv = inet_pton(family, host, &a);
    if(rv != 1) {
      family = AF_INET6;
      rv = inet_pton(family, host, &a);
      if(rv != 1) {
        if(!strcmp(host, "*")) {
          family = AF_INET;
          a.addr4.s_addr = INADDR_ANY;
        } else {
          noitL(noit_stderr, "Cannot translate '%s' to IP\n", host);
          return -1;
        }
      }
    }
  }

  fd = socket(family, type, 0);
  if(fd < 0) {
    noitL(noit_stderr, "Cannot create socket: %s\n", strerror(errno));
    return -1;
  }

  if(eventer_set_fd_nonblocking(fd)) {
    close(fd);
    noitL(noit_stderr, "Cannot make socket non-blocking: %s\n",
          strerror(errno));
    return -1;
  }

  reuse = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                 (void*)&reuse, sizeof(reuse)) != 0) {
    close(fd);
    noitL(noit_stderr, "Cannot set SO_REUSEADDR: %s\n", strerror(errno));
    return -1;
  }

  memset(&s, 0, sizeof(s));
  if(family == AF_UNIX) {
    struct stat sb;
    /* unlink the path if it is a socket */
    if(stat(host, &sb) == -1) {
      if(errno != ENOENT) {
        noitL(noit_stderr, "%s: %s\n", host, strerror(errno));
        close(fd);
        return -1;
      }
    }
    else {
      if(sb.st_mode & S_IFSOCK)
        unlink(host);
      else {
        noitL(noit_stderr, "unlink %s failed: %s\n", host, strerror(errno));
        close(fd);
        return -1;
      }
    }
    s.addru.sun_family = AF_UNIX;
    strncpy(s.addru.sun_path, host, sizeof(s.addru.sun_path)-1);
    sockaddr_len = sizeof(s.addru);
  }
  else {
    if(family == AF_INET6) {
      s.addr6.sin6_family = family;
      s.addr6.sin6_port = htons(port);
      memcpy(&s.addr6.sin6_addr, &a.addr6, sizeof(a.addr6));
    }
    else {
      s.addr4.sin_family = family;
      s.addr4.sin_port = htons(port);
      memcpy(&s.addr4.sin_addr, &a.addr4, sizeof(a.addr4));
    }
    sockaddr_len = (family == AF_INET) ?  sizeof(s.addr4) : sizeof(s.addr6);
  }
  if(bind(fd, (struct sockaddr *)&s, sockaddr_len) < 0) {
    noitL(noit_stderr, "bind failed[%s]: %s\n", host, strerror(errno));
    close(fd);
    return -1;
  }

  if(type == SOCK_STREAM) {
    if(listen(fd, backlog) < 0) {
      close(fd);
      return -1;
    }
  }

  listener_closure = calloc(1, sizeof(*listener_closure));
  listener_closure->family = family;
  listener_closure->port = htons(port);
  listener_closure->sslconfig = calloc(1, sizeof(noit_hash_table));
  noit_hash_merge_as_dict(listener_closure->sslconfig, sslconfig);
  listener_closure->dispatch_callback = handler;

  listener_closure->dispatch_closure =
    calloc(1, sizeof(*listener_closure->dispatch_closure));
  listener_closure->dispatch_closure->config = config;
  listener_closure->dispatch_closure->dispatch = handler;
  listener_closure->dispatch_closure->service_ctx = service_ctx;

  event = eventer_alloc();
  event->fd = fd;
  event->mask = EVENTER_READ | EVENTER_EXCEPTION;
  event->callback = noit_listener_acceptor;
  event->closure = listener_closure;

  eventer_add(event);
  return 0;
}

void
noit_listener_reconfig(const char *toplevel) {
  int i, cnt = 0;
  noit_conf_section_t *listener_configs;
  char path[256];

  snprintf(path, sizeof(path), "/%s/listeners//listener",
           toplevel ? toplevel : "*");
  listener_configs = noit_conf_get_sections(NULL, path, &cnt);
  noitL(noit_stderr, "Found %d %s stanzas\n", cnt, path);
  for(i=0; i<cnt; i++) {
    char address[256];
    char type[256];
    unsigned short port;
    int portint;
    int backlog;
    eventer_func_t f;
    noit_boolean ssl;
    noit_hash_table *sslconfig, *config;

    if(!noit_conf_get_stringbuf(listener_configs[i],
                                "ancestor-or-self::node()/@type",
                                type, sizeof(type))) {
      noitL(noit_stderr, "No type specified in listener stanza %d\n", i+1);
      continue;
    }
    f = eventer_callback_for_name(type);
    if(!f) {
      noitL(noit_stderr,
            "Cannot find handler for listener type: '%s'\n", type);
      continue;
    }
    if(!noit_conf_get_stringbuf(listener_configs[i],
                                "ancestor-or-self::node()/@address",
                                address, sizeof(address))) {
      address[0] = '*';
      address[1] = '\0';
    }
    if(!noit_conf_get_int(listener_configs[i],
                          "ancestor-or-self::node()/@port", &portint))
      portint = 0;
    port = (unsigned short) portint;
    if(address[0] != '/' && (portint == 0 || (port != portint))) {
      /* UNIX sockets don't require a port (they'll ignore it if specified */
      noitL(noit_stderr,
            "Invalid port [%d] specified in stanza %d\n", port, i+1);
      continue;
    }
    if(!noit_conf_get_int(listener_configs[i],
                          "ancestor-or-self::node()/@backlog", &backlog))
      backlog = 5;

    if(!noit_conf_get_boolean(listener_configs[i],
                              "ancestor-or-self::node()/@ssl", &ssl))
     ssl = noit_false;

    sslconfig = ssl ?
                  noit_conf_get_hash(listener_configs[i], "sslconfig") :
                  NULL;
    config = noit_conf_get_hash(listener_configs[i], "config");

    if(noit_listener(address, port, SOCK_STREAM, backlog,
                     sslconfig, config, f, NULL) != 0) {
      noit_hash_destroy(config,free,free);
      free(config);
    }
    if(sslconfig) {
      /* A copy of this is made within noit_listener */
      noit_hash_destroy(sslconfig,free,free);
      free(sslconfig);
    }
  }
  free(listener_configs);
}
int
noit_control_dispatch(eventer_t e, int mask, void *closure,
                      struct timeval *now) {
  u_int32_t cmd;
  int len;
  void *vdelegation_table;
  noit_hash_table *delegation_table = NULL;
  acceptor_closure_t *ac = closure;

  len = e->opset->read(e->fd, &cmd, sizeof(cmd), &mask, e);

  if(len == -1 && errno == EAGAIN)
    return EVENTER_READ | EVENTER_EXCEPTION;

  if(mask & EVENTER_EXCEPTION || len != sizeof(cmd)) {
    int newmask;
socket_error:
    /* Exceptions cause us to simply snip the connection */
    eventer_remove_fd(e->fd);
    e->opset->close(e->fd, &newmask, e);
    if(ac) acceptor_closure_free(ac);
    return 0;
  }

  ac->cmd = ntohl(cmd);
  /* Lookup cmd and dispatch */
  if(noit_hash_retrieve(&listener_commands,
                        (char *)&ac->dispatch, sizeof(ac->dispatch),
                        (void **)&vdelegation_table)) {
    void *vfunc;
    delegation_table = (noit_hash_table *)vdelegation_table;
    if(noit_hash_retrieve(delegation_table,
                          (char *)&ac->cmd, sizeof(ac->cmd), &vfunc)) {
      e->callback = *((eventer_func_t *)vfunc);
      return e->callback(e, mask, closure, now);
    }
    else {
    const char *event_name;
      noitL(noit_error, "listener (%s %p) has no command: 0x%8x\n",
            (event_name = eventer_name_for_callback(ac->dispatch))?event_name:"???",
            delegation_table, cmd);
    }
  }
  else {
    const char *event_name;
    noitL(noit_error, "No delegation table for listener (%s %p)\n",
          (event_name = eventer_name_for_callback(ac->dispatch))?event_name:"???",
          delegation_table);
  }
  goto socket_error;
}
void
noit_control_dispatch_delegate(eventer_func_t listener_dispatch,
                               u_int32_t cmd,
                               eventer_func_t delegate_dispatch) {
  u_int32_t *cmd_copy;
  eventer_func_t *handler_copy;
  void *vdelegation_table;
  noit_hash_table *delegation_table;
  if(!noit_hash_retrieve(&listener_commands,
                         (char *)&listener_dispatch, sizeof(listener_dispatch),
                         &vdelegation_table)) {
    delegation_table = calloc(1, sizeof(*delegation_table));
    handler_copy = malloc(sizeof(*handler_copy));
    *handler_copy = listener_dispatch;
    noit_hash_store(&listener_commands,
                    (char *)handler_copy, sizeof(*handler_copy),
                    delegation_table);
  }
  else
    delegation_table = (noit_hash_table *)vdelegation_table;

  cmd_copy = malloc(sizeof(*cmd_copy));
  *cmd_copy = cmd;
  handler_copy = malloc(sizeof(*handler_copy));
  *handler_copy = delegate_dispatch;
  noit_hash_replace(delegation_table,
                    (char *)cmd_copy, sizeof(*cmd_copy),
                    handler_copy,
                    free, free);
}

int
noit_convert_sockaddr_to_buff(char *buff, int blen, struct sockaddr *remote) {
  char name[128] = "";
  buff[0] = '\0';
  if(remote) {
    int len = 0;
    switch(remote->sa_family) {
      case AF_INET:
        len = sizeof(struct sockaddr_in);
        inet_ntop(remote->sa_family, &((struct sockaddr_in *)remote)->sin_addr,
                  name, len);
        break;
      case AF_INET6:
       len = sizeof(struct sockaddr_in6);
        inet_ntop(remote->sa_family, &((struct sockaddr_in6 *)remote)->sin6_addr,
                  name, len);
       break;
      case AF_UNIX:
        snprintf(name, sizeof(name), "%s", ((struct sockaddr_un *)remote)->sun_path);
        break;
      default: return 0;
    }
  }
  strlcpy(buff, name, blen);
  return strlen(buff);
}

void
noit_listener_init(const char *toplevel) {
  nlerr = noit_log_stream_find("error/listener");
  nldeb = noit_log_stream_find("debug/listener");
  if(!nlerr) nlerr = noit_error;
  if(!nldeb) nldeb = noit_debug;
  eventer_name_callback("noit_listener_acceptor", noit_listener_acceptor);
  eventer_name_callback("noit_listener_accept_ssl", noit_listener_accept_ssl);
  eventer_name_callback("control_dispatch", noit_control_dispatch);
  noit_listener_reconfig(toplevel);
}

