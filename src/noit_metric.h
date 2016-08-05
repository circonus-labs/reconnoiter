/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2015, Circonus, Inc. All rights reserved.
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
 *     * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
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

#ifndef _NOIT_METRIC_H
#define _NOIT_METRIC_H

#include <mtev_defines.h>
#include <mtev_atomic.h>

typedef enum {
  METRIC_ABSENT = 0,
  METRIC_NULL = 1,
  METRIC_GUESS = '0',
  METRIC_INT32 = 'i',
  METRIC_UINT32 = 'I',
  METRIC_INT64 = 'l',
  METRIC_UINT64 = 'L',
  METRIC_DOUBLE = 'n',
  METRIC_STRING = 's'
} metric_type_t;

#define IS_METRIC_TYPE_NUMERIC(t) \
  ((t) == METRIC_INT32 || (t) == METRIC_UINT32 || \
   (t) == METRIC_INT64 || (t) == METRIC_UINT64 || (t) == METRIC_DOUBLE)

typedef struct {
  char *metric_name;
  metric_type_t metric_type;
  union {
    double *n;
    int32_t *i;
    u_int32_t *I;
    int64_t *l;
    u_int64_t *L;
    char *s;
    void *vp; /* used for clever assignments */
  } metric_value;
  mtev_boolean logged;
  unsigned long accumulator; /* used to track divisor of averages */
} metric_t;

typedef enum {
  MESSAGE_TYPE_C = 'C',
  MESSAGE_TYPE_D = 'D',
  MESSAGE_TYPE_S = 'S',
  MESSAGE_TYPE_H = 'H',
  MESSAGE_TYPE_M = 'M'
} noit_message_type;

typedef struct {
  uuid_t id;
  const char *name;
  int name_len;
} noit_metric_id_t;

typedef struct {
  u_int64_t whence_ms; /* when this was recieved */
  metric_type_t type; /* the type of the following data item */
  union {
    int32_t v_int32;
    u_int32_t v_uint32;
    int64_t v_int64;
    u_int64_t v_uint64;
    double v_double;
    char *v_string;
    metric_type_t v_type_if_absent;
  } value; /* the data itself */
} noit_metric_value_t;

typedef struct {
  noit_metric_id_t id;
  noit_metric_value_t value;
  noit_message_type type;
  char* original_message;
  mtev_atomic32_t refcnt;
} noit_metric_message_t;

void noit_metric_to_json(noit_metric_message_t *metric, char **json, size_t *len, mtev_boolean include_original);


/* If possible coerce the metric to a double, return success */
API_EXPORT(mtev_boolean) noit_metric_as_double(metric_t *m, double *);

#endif
