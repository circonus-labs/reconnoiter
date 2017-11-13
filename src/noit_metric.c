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
 *LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "noit_metric.h"

#include <mtev_json_object.h>
#include <mtev_str.h>
#include <circllhist.h>

#include <stdio.h>

mtev_boolean
noit_metric_as_double(metric_t *metric, double *out) {
  if(metric == NULL || metric->metric_value.vp == NULL) return mtev_false;
  switch (metric->metric_type) {
  case METRIC_INT32:
    if(out) *out = (double)*(metric->metric_value.i); break;
  case METRIC_UINT32:
    if(out) *out = (double)*(metric->metric_value.I); break;
  case METRIC_INT64:
    if(out) *out = (double)*(metric->metric_value.l); break;
  case METRIC_UINT64:
    if(out) *out = (double)*(metric->metric_value.L); break;
  case METRIC_DOUBLE:
    if(out) *out = *(metric->metric_value.n); break;
  default: return mtev_false;
  }
  return true;
}

void
noit_metric_to_json(noit_metric_message_t *metric, char **json, size_t *len, mtev_boolean include_original)
{
  if (json == NULL) {
    *len = 0;
    return;
  }

  struct mtev_json_object *o = mtev_json_object_new_object();

  char type[2] = {0};
  sprintf(type, "%c", metric->type);
  mtev_json_object_object_add(o, "type", mtev_json_object_new_string(type));
  struct mtev_json_object *whence = mtev_json_object_new_int(0);
  mtev_json_object_set_int_overflow(whence, mtev_json_overflow_uint64);
  mtev_json_object_set_uint64(whence, metric->value.whence_ms);
  mtev_json_object_object_add(o, "timestamp_ms", whence);

  char uuid_str[UUID_PRINTABLE_STRING_LENGTH] = {0};
  uuid_unparse_lower(metric->id.id, uuid_str);

  if(metric->noit.name) {
    char name[metric->noit.name_len + 1];
    strncpy(name, metric->noit.name, metric->noit.name_len);
    name[metric->noit.name_len] = '\0';
    mtev_json_object_object_add(o, "noit", mtev_json_object_new_string(name));
  }
  mtev_json_object_object_add(o, "check_uuid", mtev_json_object_new_string(uuid_str));
  if(metric->id.stream.tags) {
    mtev_json_object *jst_tags = mtev_json_object_new_array();
    for(int i=0;i<metric->id.stream.tag_count;i++) {
      mtev_json_object_array_add(jst_tags,
                                 mtev_json_object_new_string_len(metric->id.stream.tags[i].tag,
                                 metric->id.stream.tags[i].total_size));
    }
    mtev_json_object_object_add(o, "stream_tags", jst_tags);
  }
  if(metric->id.measurement.tag_count) {
    mtev_json_object *jm_tags = mtev_json_object_new_array();
    for(int i=0;i<metric->id.measurement.tag_count;i++) {
      mtev_json_object_array_add(jm_tags,
                                 mtev_json_object_new_string_len(metric->id.measurement.tags[i].tag,
                                 metric->id.measurement.tags[i].total_size));
    }
    mtev_json_object_object_add(o, "measurement_tags", jm_tags);
  }

  if (metric->value.type == METRIC_ABSENT) {
    mtev_json_object_object_add(o, "value_type", NULL);
  } else {
    char value_type[2] = {0};
    sprintf(value_type, "%c", metric->value.type);
    mtev_json_object_object_add(o, "value_type", mtev_json_object_new_string(value_type));
  }

  if (metric->type == MESSAGE_TYPE_M) {
    char name[metric->id.name_len + 1];
    strncpy(name, metric->id.name, metric->id.name_len);
    name[metric->id.name_len] = '\0';
    struct mtev_json_object *int_value = mtev_json_object_new_int(metric->value.value.v_int32);

    if(metric->value.is_null) {
      mtev_json_object_object_add(o, name, NULL);
    } else {
      switch (metric->value.type) {
      case METRIC_GUESS:
      case METRIC_INT32:
        mtev_json_object_object_add(o, name, int_value);
        break;

      case METRIC_UINT32:
        mtev_json_object_set_int_overflow(int_value, mtev_json_overflow_uint64);
        mtev_json_object_set_uint64(int_value, metric->value.value.v_uint32);
        mtev_json_object_object_add(o, name, int_value);
        break;

      case METRIC_INT64:
        mtev_json_object_set_int_overflow(int_value, mtev_json_overflow_int64);
        mtev_json_object_set_int64(int_value, metric->value.value.v_int64);
        mtev_json_object_object_add(o, name, int_value);
        break;

      case METRIC_UINT64:
        mtev_json_object_set_int_overflow(int_value, mtev_json_overflow_uint64);
        mtev_json_object_set_uint64(int_value, metric->value.value.v_uint64);
        mtev_json_object_object_add(o, name, int_value);
        break;

      case METRIC_DOUBLE:
        {
          mtev_json_object_object_add(o, name, mtev_json_object_new_double(metric->value.value.v_double));
          break;

        }
      case METRIC_STRING:
        mtev_json_object_object_add(o, name, mtev_json_object_new_string(metric->value.value.v_string));
        break;
      default:
        mtev_json_object_object_add(o, name, NULL);
        break;
      }
    }
  } else if (metric->type == MESSAGE_TYPE_S) {
    char *status = strdup(metric->id.name);
    const char *field = strtok(status, "\t");
    if (field != NULL) {
      mtev_json_object_object_add(o, "state", mtev_json_object_new_string(field));
    }
    field = strtok(NULL, "\t");
    if (field != NULL) {
      mtev_json_object_object_add(o, "available", mtev_json_object_new_string(field));
    }
    field = strtok(NULL, "\t");
    if (field != NULL) {
      mtev_json_object_object_add(o, "duration", mtev_json_object_new_int(atoi(field)));
    }
    field = strtok(NULL, "\t");
    if (field != NULL) {
      mtev_json_object_object_add(o, "status", mtev_json_object_new_string(field));
    }
    free(status);
  } else if (metric->type == MESSAGE_TYPE_H) {
    histogram_t *histo = hist_alloc();
    ssize_t s = hist_deserialize_b64(histo, metric->value.value.v_string, strlen(metric->value.value.v_string));
    if (s > 0) {
      struct mtev_json_object *histogram = mtev_json_object_new_array();
      for (int i = 0; i < hist_bucket_count(histo); i++) {
        /* for each bvs in the histogram, create a "bucket" object in the json */
        struct mtev_json_object *bucket = mtev_json_object_new_object();
        double b = 0.0;
        uint64_t bc = 0;
        hist_bucket_idx(histo, i, &b, &bc);
        mtev_json_object_object_add(bucket, "bucket",
                                    mtev_json_object_new_double(b));
        struct mtev_json_object *count_o = mtev_json_object_new_int(0);
        mtev_json_object_set_int_overflow(count_o, mtev_json_overflow_uint64);
        mtev_json_object_set_int64(count_o, bc);

        mtev_json_object_object_add(bucket, "count", count_o);
        mtev_json_object_array_add(histogram, bucket);
      }
      hist_free(histo);
      char name[metric->id.name_len + 1];
      strncpy(name, metric->id.name, metric->id.name_len);
      name[metric->id.name_len] = '\0';
      mtev_json_object_object_add(o, name, histogram);
    }
  }

  const char *j = mtev_json_object_to_json_string(o);
  *json = strdup(j);
  *len = strlen(j);
  mtev_json_object_put(o);
}

