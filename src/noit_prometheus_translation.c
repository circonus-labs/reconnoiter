
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>

#include <mtev_rand.h>
#include <mtev_rest.h>
#include <mtev_hash.h>
#include <mtev_json.h>
#include <mtev_uuid.h>
#include <mtev_b64.h>
#include <mtev_dyn_buffer.h>

#include <circllhist.h>

#include "noit_metric.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_mtev_bridge.h"

#include <snappy/snappy.h>


char *
noit_prometheus_metric_name_from_labels(Prometheus__Label **labels, size_t label_count, const char *units, bool coerce_hist)
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
    } else {
      /* if we're coercing histograms, remove the "le" label */
      if(coerce_hist && !strcmp("le", l->name)) continue;
      if (tag_count > 0) {
        strlcat(b, ",", sizeof(buffer));
      }
      bool wrote_cat = false;
      size_t tl = strlen(l->name);
      if(noit_metric_tagset_is_taggable_key(l->name, tl)) {
        strlcat(b, l->name, sizeof(buffer));
        wrote_cat = true;
      } else {
        int len = mtev_b64_encode((const unsigned char *)l->name, tl, encode_buffer, sizeof(encode_buffer) - 1);
        if (len > 0) {
          encode_buffer[len] = '\0';

          strlcat(b, "b\"", sizeof(buffer));
          strlcat(b, encode_buffer, sizeof(buffer));
          strlcat(b, "\"", sizeof(buffer));
          wrote_cat = true;
        }
      }
      if(wrote_cat) {
        strlcat(b, ":", sizeof(buffer));
        tl = strlen(l->value);
        if(noit_metric_tagset_is_taggable_value(l->value, tl)) {
          strlcat(b, l->value, sizeof(buffer));
        } else {
          int len = mtev_b64_encode((const unsigned char *)l->value, tl, encode_buffer, sizeof(encode_buffer) - 1);
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
  if(units) {
    if(noit_metric_tagset_is_taggable_value(units, strlen(units))) {
      if(strlen(buffer) > 0) strlcat(name, ",", sizeof(final_name));
      strlcat(name, "units:", sizeof(final_name));
      strlcat(name, units, sizeof(final_name));
    } else {
      int len = mtev_b64_encode((const unsigned char *)units, strlen(units),
                                encode_buffer, sizeof(encode_buffer) - 1);
      if(len > 0) {
        if(strlen(buffer) > 0) strlcat(name, ",", sizeof(final_name));
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

bool noit_prometheus_snappy_uncompress(mtev_dyn_buffer_t *uncompressed_data_out, const void *data_in, size_t data_in_len)
{
  size_t uncompressed_size;
  if (!snappy_uncompressed_length(data_in, data_in_len, &uncompressed_size)) {
    return false;
  }
  mtev_dyn_buffer_ensure(uncompressed_data_out, uncompressed_size);
  int x = snappy_uncompress(data_in, data_in_len, 
                            (char *)mtev_dyn_buffer_write_pointer(uncompressed_data_out));
  if (x) {
    mtev_dyn_buffer_destroy(uncompressed_data_out);
    return false;
  }
  return true;
}