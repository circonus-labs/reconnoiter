/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_LIVESTREAM_LISTENER_H
#define _NOIT_LIVESTREAM_LISTENER_H

#include "noit_defines.h"
#include "eventer/eventer.h"

#define NOIT_LIVESTREAM_DATA_FEED 0xfa57feed

API_EXPORT(void)
  noit_livestream_listener_init(void);

API_EXPORT(int)
  noit_livestream_handler(eventer_t e, int mask, void *closure,
                          struct timeval *now);

#endif

