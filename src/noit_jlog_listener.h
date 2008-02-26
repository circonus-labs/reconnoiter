/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_JLOG_LISTENER_H
#define _NOIT_JLOG_LISTENER_H

#include "noit_defines.h"
#include "eventer/eventer.h"

API_EXPORT(void)
  noit_jlog_listener_init(void);

API_EXPORT(int)
  noit_jlog_handler(eventer_t e, int mask, void *closure,
                    struct timeval *now);

#endif
