/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_FILTERS_H
#define _NOIT_FILTERS_H

#include "noit_defines.h"
#include "utils/noit_hash.h"
#include "noit_console.h"
#include "noit_conf.h"
#include "noit_check.h"

API_EXPORT(void)
  noit_filters_init();

API_EXPORT(void)
  noit_refresh_filtersets();

API_EXPORT(noit_boolean)
  noit_apply_filterset(const char *filterset,
                       noit_check_t *check,
                       metric_t *metric);

#endif
