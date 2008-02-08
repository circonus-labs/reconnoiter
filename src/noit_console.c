/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <util.h>
#include <arpa/telnet.h>

#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "noit_listener.h"
#include "noit_console.h"
#include "noit_tokenizer.h"

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
int
nc_write(noit_console_closure_t ncct, void *buf, int len) {
  if(!ncct->outbuf_allocd) {
    ncct->outbuf = malloc(len);
    if(!ncct->outbuf) return 0;
    ncct->outbuf_allocd = len;
  }
  else if(ncct->outbuf_allocd < ncct->outbuf_len + len) {
    char *newbuf;
    newbuf = realloc(ncct->outbuf, ncct->outbuf_len + len);
    if(!newbuf) return 0;
    ncct->outbuf = newbuf;
  }
  memcpy(ncct->outbuf + ncct->outbuf_len, buf, len);
  ncct->outbuf_len += len;
  return len;
}

void
noit_console_closure_free(noit_console_closure_t ncct) {
  if(ncct->el) el_end(ncct->el);
  if(ncct->hist) history_end(ncct->hist);
  if(ncct->pty_master >= 0) close(ncct->pty_master);
  if(ncct->pty_slave >= 0) close(ncct->pty_slave);
  if(ncct->outbuf) free(ncct->outbuf);
  if(ncct->telnet) noit_console_telnet_free(ncct->telnet);
  free(ncct);
}

noit_console_closure_t
noit_console_closure_alloc() {
  noit_console_closure_t new_ncct;
  new_ncct = calloc(1, sizeof(*new_ncct));
  new_ncct->pty_master = -1;
  new_ncct->pty_slave = -1;
  return new_ncct;
}

int
noit_console_continue_sending(noit_console_closure_t ncct,
                              int *mask) {
  int len;
  eventer_t e = ncct->e;
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
  el_multi_init();
  signal(SIGTTOU, SIG_IGN);
  eventer_name_callback("noit_console", noit_console_handler);
}

void
noit_console_dispatch(eventer_t e, const char *buffer,
                      noit_console_closure_t ncct) {
  char *cmds[32];
  int i, cnt = 32;
  nc_printf(ncct, "You said: %s\r\n", buffer);
  i = noit_tokenize(buffer, cmds, &cnt);
  if(i>cnt) nc_printf(ncct, "Command length too long.\n");
  if(i<0) nc_printf(ncct, "Error at offset: %d\n", 0-i);
  for(i=0;i<cnt;i++) {
    nc_printf(ncct, "[%d] '%s'\r\n", i, cmds[i]);
    free(cmds[i]);
  }
}

int
noit_console_handler(eventer_t e, int mask, void *closure,
                     struct timeval *now) {
  int newmask = EVENTER_READ | EVENTER_EXCEPTION;
  int keep_going;
  noit_console_closure_t ncct = closure;

  if(mask & EVENTER_EXCEPTION || (ncct && ncct->wants_shutdown)) {
socket_error:
    /* Exceptions cause us to simply snip the connection */
    eventer_remove_fd(e->fd);
    e->opset->close(e->fd, &newmask, e);
    if(ncct) noit_console_closure_free(ncct);
    return 0;
  }

  if(!ncct) {
    int on = 1;
    ncct = closure = e->closure = noit_console_closure_alloc();
    ncct->e = e;
    if(openpty(&ncct->pty_master, &ncct->pty_slave, NULL, NULL, NULL) ||
       ioctl(ncct->pty_master, FIONBIO, &on)) {
      nc_printf(ncct, "Failed to open pty: %s\n", strerror(errno));
      ncct->wants_shutdown = 1;
    }
    else {
      HistEvent ev;
      ncct->hist = history_init();
      history(ncct->hist, &ev, H_SETSIZE, 500);
      ncct->el = el_init("noitd", ncct->pty_master, e->fd, e->fd);
      el_set(ncct->el, EL_EDITOR, "emacs");
      el_set(ncct->el, EL_HIST, history, ncct->hist);
      ncct->telnet = noit_console_telnet_alloc(ncct);
    }
  }

  /* If we still have data to send back to the client, this will take
   * care of that
   */
  if(noit_console_continue_sending(ncct, &newmask) == -1) {
    if(errno != EAGAIN) goto socket_error;
    return newmask | EVENTER_EXCEPTION;
  }

  for(keep_going=1 ; keep_going ; ) {
    int len, plen;
    char sbuf[4096];
    const char *buffer;

    keep_going = 0;

    buffer = el_gets(ncct->el, &plen);
    if(!el_eagain(ncct->el)) keep_going++;

    len = e->opset->read(e->fd, sbuf, sizeof(sbuf)-1, &newmask, e);
noitL(noit_stderr, "opset->read => %d bytes\n", len);
    if(len == 0 || (len < 0 && errno != EAGAIN)) {
      eventer_remove_fd(e->fd);
      close(e->fd);
      return 0;
    }
    if(len > 0) {
      keep_going++;
      sbuf[len] = '\0';
      if(ncct->telnet) {
        noit_console_telnet_telrcv(ncct, sbuf, len);
        ptyflush(ncct);
      }
      else {
        write(ncct->pty_slave, sbuf, len);
      }
    }
    if(buffer) {
      char *cmd_buffer;
      HistEvent ev;
      cmd_buffer = malloc(plen+1);
      memcpy(cmd_buffer, buffer, plen);
      /* chomp */
      cmd_buffer[plen] = '\0';
      if(cmd_buffer[plen-1] == '\n') cmd_buffer[plen-1] = '\0';
      noitL(noit_debug, "IN: '%s'\n", cmd_buffer);
      history(ncct->hist, &ev, H_ENTER, cmd_buffer);
      noit_console_dispatch(e, cmd_buffer, ncct);
      free(cmd_buffer);
      if(noit_console_continue_sending(ncct, &newmask) == -1) {
        if(errno != EAGAIN) goto socket_error;
        return newmask | EVENTER_EXCEPTION;
      }
    }
  }
  return newmask | EVENTER_EXCEPTION;
}

