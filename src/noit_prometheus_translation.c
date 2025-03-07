
/*
 * Copyright (c) 2025, Circonus, Inc. All rights reserved.
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
 *     * Neither the name Circonnus, Inc. nor the names
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
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include "noit_prometheus_translation.h"
#include <mtev_defines.h>
#include <mtev_memory.h>

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <mtev_b64.h>
#include <mtev_dyn_buffer.h>
#include <mtev_hash.h>
#include <mtev_json.h>
#include <mtev_rand.h>
#include <mtev_rest.h>
#include <mtev_uuid.h>

#include <circllhist.h>

#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_metric.h"
#include "noit_metric_director.h"
#include "noit_module.h"
#include "noit_mtev_bridge.h"

#include <snappy/snappy.h>

static const char *_allowed_units[] = {"seconds",  "requests", "responses", "transactions",
                                       "packetes", "bytes",    "octets",    NULL};

char *noit_prometheus_metric_name_from_labels(Prometheus__Label **labels,
                                              size_t label_count,
                                              const char *units,
                                              bool coerce_hist)
{
  char final_name[MAX_METRIC_TAGGED_NAME] = {0};
  char *name = final_name;
  char buffer[MAX_METRIC_TAGGED_NAME] = {0};
  char encode_buffer[MAX_METRIC_TAGGED_NAME] = {0};
  char *b = buffer;
  size_t tag_count = 0;
  for (size_t i = 0; i < label_count; i++) {
    Prometheus__Label *l = labels[i];
    if (strcmp("__name__", l->name) == 0) {
      strncpy(name, l->value, sizeof(final_name) - 1);
    }
    else {
      /* if we're coercing histograms, remove the "le" label */
      if (coerce_hist && !strcmp("le", l->name))
        continue;
      if (tag_count > 0) {
        strlcat(b, ",", sizeof(buffer));
      }
      bool wrote_cat = false;
      size_t tl = strlen(l->name);
      if (noit_metric_tagset_is_taggable_key(l->name, tl)) {
        strlcat(b, l->name, sizeof(buffer));
        wrote_cat = true;
      }
      else {
        int len = mtev_b64_encode((const unsigned char *) l->name, tl, encode_buffer,
                                  sizeof(encode_buffer) - 1);
        if (len > 0) {
          encode_buffer[len] = '\0';

          strlcat(b, "b\"", sizeof(buffer));
          strlcat(b, encode_buffer, sizeof(buffer));
          strlcat(b, "\"", sizeof(buffer));
          wrote_cat = true;
        }
      }
      if (wrote_cat) {
        strlcat(b, ":", sizeof(buffer));
        tl = strlen(l->value);
        if (noit_metric_tagset_is_taggable_value(l->value, tl)) {
          strlcat(b, l->value, sizeof(buffer));
        }
        else {
          int len = mtev_b64_encode((const unsigned char *) l->value, tl, encode_buffer,
                                    sizeof(encode_buffer) - 1);
          if (len > 0) {
            encode_buffer[len] = '\0';
            strlcat(b, "b\"", sizeof(buffer));
            strlcat(b, encode_buffer, sizeof(buffer));
            strlcat(b, "\"", sizeof(buffer));
          }
        }
      }
      tag_count++;
    }
  }
  strlcat(name, "|ST[", sizeof(final_name));
  strlcat(name, buffer, sizeof(final_name));
  if (units) {
    if (noit_metric_tagset_is_taggable_value(units, strlen(units))) {
      if (strlen(buffer) > 0)
        strlcat(name, ",", sizeof(final_name));
      strlcat(name, "units:", sizeof(final_name));
      strlcat(name, units, sizeof(final_name));
    }
    else {
      int len = mtev_b64_encode((const unsigned char *) units, strlen(units), encode_buffer,
                                sizeof(encode_buffer) - 1);
      if (len > 0) {
        if (strlen(buffer) > 0)
          strlcat(name, ",", sizeof(final_name));
        strlcat(name, "units:", sizeof(final_name));
        encode_buffer[len] = '\0';
        strlcat(name, encode_buffer, sizeof(final_name));
      }
    }
  }
  strlcat(name, "]", sizeof(final_name));

  /* we don't have to canonicalize here as reconnoiter will do that for us */
  return strdup(final_name);
}

bool noit_prometheus_snappy_uncompress(mtev_dyn_buffer_t *uncompressed_data_out,
                                       size_t *uncompressed_size_out,
                                       const void *data_in,
                                       size_t data_in_len)
{
  if (!snappy_uncompressed_length(data_in, data_in_len, uncompressed_size_out)) {
    return false;
  }
  mtev_dyn_buffer_ensure(uncompressed_data_out, *uncompressed_size_out);
  int x = snappy_uncompress(data_in, data_in_len,
                            (char *) mtev_dyn_buffer_write_pointer(uncompressed_data_out));
  if (x) {
    mtev_dyn_buffer_destroy(uncompressed_data_out);
    return false;
  }
  return true;
}

static bool is_standard_suffix(const char *suffix)
{
  return !strcmp(suffix, "count") || !strcmp(suffix, "sum") || !strcmp(suffix, "bucket") ||
    !strcmp(suffix, "total");
}
static const char *units_suffix(const char *in, const char **allowed_units_in)
{
  const char **allowed_units = allowed_units_in ? allowed_units_in : _allowed_units;
  for (int i = 0; allowed_units[i]; i++) {
    mtevL(mtev_debug, "ALLOWED: %s\n", allowed_units[i]);
    if (!strcmp(allowed_units[i], in)) {
      return allowed_units[i];
    }
  }
  return NULL;
}

static const char *prom_name_munge_units(char *in, const char **allowed_units)
{
  const char *units = NULL;
  char *hist_suff = NULL;
  char *ls = strrchr(in, '_');
  if (ls && is_standard_suffix(ls + 1)) {
    hist_suff = ls + 1;
    *ls = '\0';
    ls = strrchr(in, '_');
  }
  if (ls && NULL != (units = units_suffix(ls + 1, allowed_units))) {
    *ls = '\0';
  }
  if (hist_suff) {
    *(--hist_suff) = '_';
    if (ls) {
      int len = strlen(hist_suff);
      memmove(ls, hist_suff, len + 1);
    }
  }
  return units;
}

prometheus_coercion_t noit_prometheus_metric_name_coerce(Prometheus__Label **labels,
                                                         size_t label_count,
                                                         bool do_units,
                                                         bool do_hist,
                                                         const char **allowed_units)
{
  prometheus_coercion_t rv = {};
  char *name = NULL;
  const char *units = NULL;
  const char *le = NULL;
  for (size_t i = 0; i < label_count && (!name || !units || !le); i++) {
    Prometheus__Label *l = labels[i];
    if (strcmp("__name__", l->name) == 0) {
      name = l->value;
    }
    else if (strcmp("units", l->name) == 0) {
      units = l->value;
    }
    else if (strcmp("le", l->name) == 0) {
      le = l->value;
    }
  }
  if (!name) {
    return rv;
  }
  if (do_units) {
    if (units) {
      units = NULL;
    }
    else {
      if (name) {
        rv.units = prom_name_munge_units(name, allowed_units);
      }
    }
  }
  if (do_hist && le) {
    char *bucket = strrchr(name, '_');
    if (bucket && !strcmp(bucket, "_bucket")) {
      rv.is_histogram = true;
      rv.hist_boundary = strtod(le, NULL);
      *bucket = '\0';
    }
  }
  return rv;
}

noit_metric_message_t *noit_prometheus_translate_to_noit_metric_message(prometheus_coercion_t *coercion,
                                                                        const int64_t account_id,
                                                                        const uuid_t check_uuid,
                                                                        const char *metric_name,
                                                                        const Prometheus__Sample *sample) {
  if (!coercion || !metric_name || !sample) {
    return NULL;
  }
  if (sample->timestamp < 0) {
    return NULL;
  }
  noit_metric_message_t *message = (noit_metric_message_t *)calloc(1, sizeof(noit_metric_message_t));
  noit_metric_director_message_ref(message);
  /* Typically, "original message" is intended to hold the original noit metric record (IE:
     'M' record) and the metric name is stored as an offset into this; however, prometheus data
     doesn't work that way, so instead, we put the metric name here and later assign the id.name
     value to it. This ensures that the metric name is correctly freed up when the message
     ref count hits zero. There's probably a way to do this more efficiently without the extra
     strdup */
  message->original_message = strdup(metric_name);
  message->original_message_len = strlen(metric_name);
  message->original_allocated = mtev_true;
  message->noit.name = NULL;
  message->noit.name_len = 0;
  message->type = (coercion->is_histogram ? MESSAGE_TYPE_H : MESSAGE_TYPE_M);
  message->value.whence_ms = (uint64_t) sample->timestamp * 1000;
  message->id.account_id = account_id;
  mtev_uuid_copy(message->id.id, check_uuid);
  message->id.name = message->original_message;
  /* TODO: Should name_len_with_tags and name_len differ here? Does it matter? */
  message->id.name_len_with_tags = strlen(metric_name);
  message->id.name_len = message->id.name_len_with_tags;

  if (message->type == MESSAGE_TYPE_M) {
    /* data from prometheus is always a double */
    message->value.type = METRIC_DOUBLE;
    message->value.is_null = false;
    message->value.value.v_double = sample->value;
  }
  else {
    // TODO: Need to handle histograms
    message->value.type = METRIC_HISTOGRAM;
    message->value.is_null = true;
  }
  return message;
}