/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "noit_listener.h"
#include "noit_console.h"
#include "noit_tokenizer.h"

struct __noit_console_closure {
  char *outbuf;
  int   outbuf_allocd;
  int   outbuf_len;
  int   outbuf_completed;
};

int
nc_printf(noit_console_closure_t ncct, const char *fmt, ...) {
  int len;
  va_list arg;
  va_start(arg, fmt);
  len = nc_vprintf(ncct, fmt, arg);
  va_end(arg);
  return len;
}
int
nc_vprintf(noit_console_closure_t ncct, const char *fmt, va_list arg) {
#ifdef va_copy
  va_list copy;
#endif
  int lenwanted;
  
  if(!ncct->outbuf_allocd) {
    ncct->outbuf = malloc(4096);
    if(!ncct->outbuf) return 0;
    ncct->outbuf_allocd = 4096;
  }
  while(1) {
    char *newbuf;
#ifdef va_copy
    va_copy(copy, arg);
    lenwanted = vsnprintf(ncct->outbuf + ncct->outbuf_len,
                          ncct->outbuf_allocd - ncct->outbuf_len,
                          fmt, copy);
    va_end(copy);
#else
    lenwanted = vsnprintf(ncct->outbuf + ncct->outbuf_len,
                          ncct->outbuf_allocd - ncct->outbuf_len,
                          fmt, arg);
#endif
    if(ncct->outbuf_len + lenwanted < ncct->outbuf_allocd) {
      /* All went well, things are as we want them. */
      ncct->outbuf_len += lenwanted;
      return lenwanted;
    }

    /* We need to enlarge the buffer */
    lenwanted += ncct->outbuf_len;
    lenwanted /= 4096;
    lenwanted += 1;
    lenwanted *= 4096;
    newbuf = realloc(ncct->outbuf, lenwanted);
    if(!newbuf) {
      return 0;
    }
    ncct->outbuf = newbuf;
    ncct->outbuf_allocd = lenwanted;
  }
  return -1;
}
void
noit_console_closure_free(noit_console_closure_t ncct) {
  if(ncct->outbuf) free(ncct->outbuf);
  free(ncct);
}

noit_console_closure_t
noit_console_closure_alloc() {
  noit_console_closure_t new_ncct;
  new_ncct = calloc(1, sizeof(*new_ncct));
  return new_ncct;
}

int
noit_console_continue_sending(eventer_t e, noit_console_closure_t ncct,
                              int *mask) {
  int len;
  if(!ncct->outbuf_len) return 0;
  while(ncct->outbuf_len > ncct->outbuf_completed) {
    len = e->opset->write(e->fd, ncct->outbuf + ncct->outbuf_completed,
                          ncct->outbuf_len - ncct->outbuf_completed,
                          mask, e);
    if(len < 0) {
      if(errno == EAGAIN) return -1;
      /* Do something else here? */
      return -1;
    }
    ncct->outbuf_completed += len;
  }
  len = ncct->outbuf_len;
  free(ncct->outbuf);
  ncct->outbuf = NULL;
  ncct->outbuf_allocd = 0;
  ncct->outbuf_len = ncct->outbuf_completed = 0;
  return len;
}

void
noit_console_init() {
  eventer_name_callback("noit_console", noit_console_handler);
}

void
noit_console_dispatch(eventer_t e, const char *buffer,
                      noit_console_closure_t ncct) {
  char *cmds[32];
  int i, cnt = 32;
  nc_printf(ncct, "You said: %s", buffer);
  i = noit_tokenize(buffer, cmds, &cnt);
  if(i>cnt) nc_printf(ncct, "Command length too long.\n");
  if(i<0) nc_printf(ncct, "Error at offset: %d\n", 0-i);
  for(i=0;i<cnt;i++) {
    nc_printf(ncct, "[%d] '%s'\n", i, cmds[i]);
    free(cmds[i]);
  }
}

int
noit_console_handler(eventer_t e, int mask, void *closure,
                     struct timeval *now) {
  int newmask = EVENTER_READ;
  noit_console_closure_t ncct = closure;

  if(mask & EVENTER_EXCEPTION) {
    /* Exceptions cause us to simply snip the connection */
    eventer_remove_fd(e->fd);
    e->opset->close(e->fd, &newmask, e);
    if(ncct) noit_console_closure_free(ncct);
    return 0;
  }

  if(!ncct) ncct = closure = e->closure = noit_console_closure_alloc();

  /* If we still have data to send back to the client, this will take
   * care of that
   */
  if(noit_console_continue_sending(e, ncct, &newmask) == -1)
    return newmask;

  if(mask & EVENTER_READ) {
    int len;
    char buffer[4096];
    len = e->opset->read(e->fd, buffer, sizeof(buffer)-1, &newmask, e);
    if(len <= 0) {
      eventer_remove_fd(e->fd);
      close(e->fd);
      return 0;
    }
    buffer[len] = '\0';
    printf("IN: %s", buffer);
    noit_console_dispatch(e, buffer, ncct);
    if(noit_console_continue_sending(e, ncct, &newmask) == -1)
      return newmask;
  }
  return newmask | EVENTER_EXCEPTION;
}

