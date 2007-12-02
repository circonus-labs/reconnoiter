/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "eventer/eventer_impl.h"

#ifdef HAVE_KQUEUE
extern struct _eventer_impl eventer_kqueue_impl;
#endif

eventer_impl_t registered_eventers[] = {
#ifdef HAVE_KQUEUE
  &eventer_kqueue_impl,
#endif
  NULL
};

eventer_impl_t __eventer = NULL;
