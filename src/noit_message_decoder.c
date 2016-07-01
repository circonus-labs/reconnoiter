/*
 * Copyright (c) 2016, Circonus, Inc. All rights reserved.
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

#include "noit_message_decoder.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>
#include <mtev_log.h>

#include "noit_metric.h"

#define MOVE_TO_NEXT_TAB(cp, lvalue) do { \
  lvalue = memchr(cp, '\t', strlen(cp)); \
  if(lvalue){\
    ++lvalue; \
    cp = lvalue; \
    } \
} while(0)

int noit_message_decoder_parse_line(const char *payload, int payload_len,
    uuid_t *id, const char **metric_name, int *metric_name_len,
    noit_metric_value_t *metric, mtev_boolean has_noit) {
  const char *cp, *metric_type_str, *time_str, *check_id_str;
  char *value_str;
  char *dp, id_str_copy[UUID_PRINTABLE_STRING_LENGTH];
  cp = payload;

  // Go to the timestamp column
  MOVE_TO_NEXT_TAB(cp, time_str);
  if(has_noit == mtev_true) { // non bundled messages store the source IP in the second column
    if(time_str == NULL)
      return -1;
    MOVE_TO_NEXT_TAB(cp, time_str);
  }

  if(time_str == NULL)
    return -1;
  /* extract time */
  metric->whence_ms = strtoull(time_str, &dp, 10);
  metric->whence_ms *= 1000; /* s -> ms */
  if(dp && *dp == '.')
    metric->whence_ms += (int) (1000.0 * atof(dp));

  MOVE_TO_NEXT_TAB(cp, check_id_str);
  if(!check_id_str)
    return -2;
  MOVE_TO_NEXT_TAB(cp, *metric_name);
  if(!*metric_name)
    return -3;

  memcpy(id_str_copy, *metric_name - UUID_STR_LEN - 1, UUID_STR_LEN);
  id_str_copy[UUID_STR_LEN] = '\0';

  if(uuid_parse(id_str_copy, *id) != 0) {
    mtevL(mtev_error, "uuid_parse(%s) failed\n", id_str_copy);
    return -7;
  }

  if(*metric_name - check_id_str < UUID_PRINTABLE_STRING_LENGTH)
    return -6;

  if(*payload == 'M') {
    MOVE_TO_NEXT_TAB(cp, metric_type_str);
    if(!metric_type_str)
      return -4;
    MOVE_TO_NEXT_TAB(cp, value_str);
    if(!value_str)
      return -5;

    *metric_name_len = metric_type_str - *metric_name - 1;

    metric->type = *metric_type_str;
    if(!strcmp(value_str, "[[null]]")) {
      metric->type = METRIC_ABSENT;
      metric->value.v_string = NULL;
    } else {
      switch (*metric_type_str) {
      case METRIC_INT32:
        metric->value.v_int32 = strtol(value_str, NULL, 10);
        break;
      case METRIC_UINT32:
        metric->value.v_uint32 = strtoul(value_str, NULL, 10);
        break;
      case METRIC_INT64:
        metric->value.v_int64 = strtoll(value_str, NULL, 10);
        break;
      case METRIC_UINT64:
        metric->value.v_uint64 = strtoull(value_str, NULL, 10);
        break;
      case METRIC_DOUBLE:
        metric->value.v_double = strtod(value_str, NULL);
        break;
      case METRIC_STRING:
        metric->value.v_string = value_str;
        break;
      default:
        return -9;
      }
    }
    return 1;
  }

  if(*payload == 'H') {
    MOVE_TO_NEXT_TAB(cp, value_str);
    if(!value_str)
      return -4;

    *metric_name_len = value_str - *metric_name - 1;

    int vstrlen = strlen(value_str);

    while ((vstrlen > 1) && (value_str[vstrlen - 1] == '\n'))
      vstrlen--;
    metric->type = METRIC_STRING;
    if(vstrlen == 0)
      metric->value.v_string = NULL;
    else {
      metric->value.v_string = value_str;
    }
    return 1;
  }

  if(*payload == 'S' || *payload == 'C' || *payload == 'D') {
    metric->type = METRIC_GUESS;
    metric->value.v_string = (char*)payload;
    return 1;
  }

  return 0;

}
