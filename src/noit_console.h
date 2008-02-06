/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_CONSOLE_H
#define _NOIT_CONSOLE_H

#include "noit_defines.h"
#include "eventer/eventer.h"


struct __noit_console_closure;
typedef struct __noit_console_closure * noit_console_closure_t;

API_EXPORT(void) noit_console_init();

API_EXPORT(int)
  noit_console_handler(eventer_t e, int mask, void *closure,
                       struct timeval *now);


API_EXPORT(int)
  nc_printf(noit_console_closure_t ncct, const char *fmt, ...);

API_EXPORT(int)
  nc_vprintf(noit_console_closure_t ncct, const char *fmt, va_list arg);


#endif
