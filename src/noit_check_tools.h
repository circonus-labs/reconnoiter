/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_CHECK_TOOLS_H
#define _NOIT_CHECK_TOOLS_H

#include "noit_defines.h"
#include "utils/noit_hash.h"

API_EXPORT(int)
  noit_check_interpolate(char *buff, int len, const char *fmt,
                         noit_hash_table *attrs,
                         noit_hash_table *config);

#endif

