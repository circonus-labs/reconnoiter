/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include <stdio.h>
#include <unistd.h>

#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "noit_listener.h"
#include "noit_console.h"

void
noit_console_init() {
  eventer_name_callback("noit_console", noit_console_handler);
}

int
noit_console_handler(eventer_t e, int mask, void *closure,
                     struct timeval *now) {
  int newmask = EVENTER_READ;
  if(mask & EVENTER_EXCEPTION) {
    eventer_remove_fd(e->fd);
    close(e->fd);
    return 0;
  }
  if(mask & EVENTER_READ) {
    int len;
    char buffer[4096];
    len = e->opset->read(e->fd, buffer, sizeof(buffer)-1, &newmask, e);
    if(len <= 0) {
      eventer_remove_fd(e->fd);
      close(e->fd);
      return 0;
    }
    printf("IN: %.*s", len, buffer);
  }
  return newmask | EVENTER_EXCEPTION;
}

