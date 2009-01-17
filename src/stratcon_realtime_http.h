/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _STRATCON_REALTIME_HTTP_H
#define _STRATCON_REALTIME_HTTP_H

#include "noit_conf.h"

/* This is in the public header as the stratcon_datastore must know
 * how to resolve this
 */
struct realtime_tracker {
  int sid;        /* set by request */
  int interval;   /* set by request */
  char *noit;     /* resolved by datastore */
  uuid_t checkid; /* resolved by datastore */
  struct realtime_tracker *next; /* next in series */
  eventer_t conn; /* used to track noitd connection feeding this */
  struct realtime_context *rc; /* link back to the rc that justified us */
};

API_EXPORT(void)
  stratcon_realtime_http_init(const char *toplevel);

#endif
