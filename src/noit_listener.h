/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_LISTENER_H
#define _NOIT_LISTENER_H

#include "noit_defines.h"
#include "eventer/eventer.h"

typedef struct {
  int8_t family;
  unsigned short port;
  eventer_func_t dispatch_callback;
  void *dispatch_closure;
} * listener_closure_t;

API_EXPORT(void) noit_listener_init();

API_EXPORT(int)
noit_listener(char *host, unsigned short port, int type,
              int backlog, eventer_func_t handler, void *closure);

#endif
