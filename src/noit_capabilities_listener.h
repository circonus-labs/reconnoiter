/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_CAPABILITIES_LISTENER_H
#define _NOIT_CAPABILITIES_LISTENER_H

#include "noit_defines.h"
#include "eventer/eventer.h"

#define NOIT_CAPABILITIES_SERVICE 0xca4aca4a

API_EXPORT(void)
  noit_capabilities_listener_init(void);

API_EXPORT(int)
  noit_capabilities_handler(eventer_t e, int mask, void *closure,
                            struct timeval *now);

#endif

