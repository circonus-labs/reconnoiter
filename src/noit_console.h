/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_CONSOLE_H
#define _NOIT_CONSOLE_H

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "noitedit/histedit.h"
#include "noit_console_telnet.h"

typedef struct __noit_console_closure {
  eventer_t e;           /* The event it is attached to.  This
                          * is needed so it can write itself out */
  int   wants_shutdown;  /* Set this to 1 to have it die */

  /* nice console support */
  EditLine *el;
  History *hist;

  int   pty_master;
  int   pty_slave;

  /* Output buffer for non-blocking sends */
  char *outbuf;
  int   outbuf_allocd;
  int   outbuf_len;
  int   outbuf_completed;

  /* This tracks telnet protocol state (if we're doing telnet) */
  noit_console_telnet_closure_t telnet;

} * noit_console_closure_t;;

API_EXPORT(void) noit_console_init();

API_EXPORT(int)
  noit_console_handler(eventer_t e, int mask, void *closure,
                       struct timeval *now);


API_EXPORT(int)
  nc_printf(noit_console_closure_t ncct, const char *fmt, ...);

API_EXPORT(int)
  nc_vprintf(noit_console_closure_t ncct, const char *fmt, va_list arg);

API_EXPORT(int)
  nc_write(noit_console_closure_t ncct, void *buf, int len);

API_EXPORT(int)
  noit_console_continue_sending(noit_console_closure_t ncct,
                                int *mask);
#endif
