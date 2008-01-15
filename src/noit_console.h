/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_CONSOLE_H
#define _NOIT_CONSOLE_H

#include "noit_defines.h"
#include "eventer/eventer.h"

int
noit_console_handler(eventer_t e, int mask, void *closure,
                     struct timeval *now);

#endif
