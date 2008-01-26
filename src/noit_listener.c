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
#include <arpa/inet.h>

#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "noit_listener.h"
#include "noit_conf.h"

static int
noit_listener_acceptor(eventer_t e, int mask,
                       void *closure, struct timeval *tv) {
  int conn, newmask = EVENTER_READ;
  socklen_t salen;
  listener_closure_t listener_closure = (listener_closure_t)closure;
  union {
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
  } s;

  if(mask & EVENTER_EXCEPTION) {
    eventer_remove_fd(e->fd);
    close(e->fd);
    return 0;
  }

  conn = e->opset->accept(e->fd, (struct sockaddr *)&s, &salen, &newmask, e);
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
    newe->callback = listener_closure->dispatch_callback;
    newe->closure = listener_closure->dispatch_closure;
    eventer_add(newe);
  }
 accept_bail:
  return newmask | EVENTER_EXCEPTION;
}

int
noit_listener(char *host, unsigned short port, int type,
              int backlog, eventer_func_t handler, void *closure) {
  int rv, fd;
  int8_t family;
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
  } s;
  const char *event_name;

  noit_log(noit_debug, NULL, "noit_listener(%s, %d, %d, %d, %s, %p)\n",
           host, port, type, backlog,
           (event_name = eventer_name_for_callback(handler))?event_name:"??",
           closure);
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
        noit_log(noit_stderr, NULL, "Cannot translate '%s' to IP\n", host);
        return -1;
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
  s.addr6.sin6_family = family;
  s.addr6.sin6_port = htons(port);
  memcpy(&s.addr6.sin6_addr, &a, sizeof(a));
  if(bind(fd, (struct sockaddr *)&s,
          (family == AF_INET) ?  sizeof(s.addr4) : sizeof(s.addr6)) < 0) {
    noit_log(noit_stderr, NULL, "bind failed: %s\b", strerror(errno));
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
  listener_closure->dispatch_callback = handler;
  listener_closure->dispatch_closure = closure;

  event = eventer_alloc();
  event->fd = fd;
  event->mask = EVENTER_READ | EVENTER_EXCEPTION;
  event->callback = noit_listener_acceptor;
  event->closure = listener_closure;

  eventer_add(event);
  return 0;
}

void
noit_listener_init() {
  int i, cnt = 0;
  noit_conf_section_t *listener_configs;

  listener_configs = noit_conf_get_sections(NULL, "/global/listeners/listener",
                                            &cnt);
  noit_log(noit_stderr, NULL, "Found %d /global/listeners/listener stanzas\n",
           cnt);
  for(i=0; i<cnt; i++) {
    char address[256];
    char type[256];
    unsigned short port;
    int portint;
    int backlog;
    eventer_func_t f;

    if(!noit_conf_get_stringbuf(listener_configs[i],
                                "type", type, sizeof(type))) {
      noit_log(noit_stderr, NULL,
               "No type specified in listener stanza %d\n", i+1);
      continue;
    }
    f = eventer_callback_for_name(type);
    if(!f) {
      noit_log(noit_stderr, NULL,
               "Cannot find handler for listener type: '%s'\n", type);
      continue;
    }
    if(!noit_conf_get_int(listener_configs[i], "port", &portint))
      portint = 0;
    port = (unsigned short) portint;
    if(portint == 0 || (port != portint)) {
      noit_log(noit_stderr, NULL,
               "Invalid port [%d] specified in stanza %d\n", port, i+1);
      continue;
    }
    if(!noit_conf_get_stringbuf(listener_configs[i],
                                "address", address, sizeof(address))) {
      address[0] = '*';
      address[1] = '\0';
    }
    if(!noit_conf_get_int(listener_configs[i], "backlog", &backlog))
      backlog = 5;

    noit_listener(address, port, SOCK_STREAM, backlog, f, NULL);
  }
}
