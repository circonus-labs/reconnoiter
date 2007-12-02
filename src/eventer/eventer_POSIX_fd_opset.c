/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "eventer/eventer.h"

#include <sys/socket.h>

static int
POSIX_accept(int fd, struct sockaddr *addr, socklen_t *len,
             int *mask, void *closure) {
  *mask = EVENTER_READ | EVENTER_EXCEPTION;
  return accept(fd, addr, len);
}

static int
POSIX_read(int fd, void *buffer, size_t len,
           int *mask, void *closure) {
  *mask = EVENTER_READ | EVENTER_EXCEPTION;
  return read(fd, buffer, len);
}

static int
POSIX_write(int fd, const void *buffer, size_t len,
            int *mask, void *closure) {
  *mask = EVENTER_WRITE | EVENTER_EXCEPTION;
  return write(fd, buffer, len);
}

static int
POSIX_close(int fd,
            int *mask, void *closure) {
  *mask = 0;
  return close(fd);
}

struct _fd_opset _eventer_POSIX_fd_opset = {
  POSIX_accept,
  POSIX_read,
  POSIX_write,
  POSIX_close
};

eventer_fd_opset_t eventer_POSIX_fd_opset = &_eventer_POSIX_fd_opset;
