/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "noit_listener.h"
#include "noit_conf.h"

void
acceptor_closure_free(acceptor_closure_t *ac) {
  if(ac->remote_cn) free(ac->remote_cn);
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
      char *cn, *end;
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
    eventer_remove_fd(e->fd);
    e->opset->close(e->fd, &mask, e);
    return 0;
  }

  ac = malloc(sizeof(*ac));
  memcpy(ac, listener_closure->dispatch_closure, sizeof(*ac));
  conn = e->opset->accept(e->fd, &ac->remote_addr, &salen, &newmask, e);
  if(conn >= 0) {
    socklen_t on = 1;
    eventer_t newe;
    if(ioctl(conn, FIONBIO, &on)) {
      close(conn);
      goto accept_bail;
    }
    newe = eventer_alloc();
    newe->fd = conn;
    newe->mask = EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;

    if(listener_closure->sslconfig->size) {
      char *cert, *key, *ca, *ciphers;
      eventer_ssl_ctx_t *ctx;
      /* We have an SSL configuration.  While our socket accept is
       * complete, we now have to SSL_accept, which could require
       * several reads and writes and needs its own event callback.
       */
#define SSLCONFGET(var,name) do { \
  if(!noit_hash_retrieve(listener_closure->sslconfig, name, strlen(name), \
                         (void **)&var)) var = NULL; } while(0)
      SSLCONFGET(cert, "certificate_file");
      SSLCONFGET(key, "key_file");
      SSLCONFGET(ca, "ca_chain");
      SSLCONFGET(ciphers, "ciphers");
      ctx = eventer_ssl_ctx_new(SSL_SERVER, cert, key, ca, ciphers);
      if(!ctx) {
        eventer_free(newe);
        goto socketfail;
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
  socklen_t on;
  long reuse;
  listener_closure_t listener_closure;
  eventer_t event;
  union {
    struct in_addr addr4;
    struct in6_addr addr6;
  } a;
  union {
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    struct sockaddr_un unix;
  } s;
  const char *event_name;

  noitL(noit_debug, "noit_listener(%s, %d, %d, %d, %s, %p)\n",
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
  if(fd < 0) return -1;

  on = 1;
  if(ioctl(fd, FIONBIO, &on)) {
    close(fd);
    return -1;
  }

  reuse = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                 (void*)&reuse, sizeof(reuse)) != 0) {
    close(fd);
    return -1;
  }

  memset(&s, 0, sizeof(s));
  if(family == AF_UNIX) {
    struct stat sb;
    /* unlink the path iff it is a socket */
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
    strncpy(s.unix.sun_path, host, sizeof(s.unix.sun_path)-1);
    sockaddr_len = sizeof(s.unix);
  }
  else {
    s.addr6.sin6_family = family;
    s.addr6.sin6_port = htons(port);
    memcpy(&s.addr6.sin6_addr, &a, sizeof(a));
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

    sslconfig = noit_conf_get_hash(listener_configs[i], "sslconfig");
    config = noit_conf_get_hash(listener_configs[i], "config");

    noit_listener(address, port, SOCK_STREAM, backlog,
                  sslconfig, config, f, NULL);
  }
}
void
noit_listener_init(const char *toplevel) {
  eventer_name_callback("noit_listener_acceptor", noit_listener_acceptor);
  eventer_name_callback("noit_listener_accept_ssl", noit_listener_accept_ssl);
  noit_listener_reconfig(toplevel);
}

