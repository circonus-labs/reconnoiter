/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _EVENTER_EVENTER_IMPL_H
#define _EVENTER_EVENTER_IMPL_H

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "eventer/eventer_jobq.h"

extern eventer_impl_t registered_eventers[];

int eventer_impl_propset(const char *key, const char *value);
int eventer_impl_init();
void eventer_add_asynch(eventer_jobq_t *q, eventer_t e);
void eventer_dispatch_recurrent(struct timeval *now);
eventer_t eventer_remove_recurrent(eventer_t e);
void eventer_add_recurrent(eventer_t e);

#endif
