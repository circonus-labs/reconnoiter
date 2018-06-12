/*
 * Copyright (c) 2016-2017, Circonus, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name Circonus, Inc. nor the names of its contributors
 *       may be used to endorse or promote products derived from this
 *       software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _NOIT_METRIC_ROLLUP_H
#define _NOIT_METRIC_ROLLUP_H

#include <mtev_defines.h>
#include <stdint.h>
#include <mtev_uuid.h>

#include "noit_metric.h"

typedef struct {
  metric_type_t type;
  uint16_t count;
  uint8_t stddev_present;
  float stddev;
  union {
    int32_t v_int32;
    uint32_t v_uint32;
    int64_t v_int64;
    uint64_t v_uint64;
    double v_double;
  } value;
  float derivative;
  float derivative_stddev;
  float counter;
  float counter_stddev;
} nnt_multitype;

typedef struct {
  noit_metric_value_t last_value;
  nnt_multitype accumulated;
  int drun;
  int crun;
  uint64_t first_value_time_ms;
} noit_numeric_rollup_accu;

API_EXPORT(void)
noit_metric_rollup_accumulate_numeric(noit_numeric_rollup_accu* accumulator, noit_metric_value_t* value);

#endif
