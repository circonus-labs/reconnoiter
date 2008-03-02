/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _STRATCON_LOG_STREAMER_H
#define _STRATCON_LOG_STREAMER_H

#include "noit_defines.h"

API_EXPORT(void)
  stratcon_jlog_streamer_init(const char *toplevel);
API_EXPORT(void)
  stratcon_jlog_streamer_reload(const char *toplevel);

#endif
