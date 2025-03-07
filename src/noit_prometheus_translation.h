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

#ifndef NOIT_PROMETHEUS_TRANSLATION_H
#define NOIT_PROMETHEUS_TRANSLATION_H

#include <mtev_dyn_buffer.h>
#include <stdbool.h>
#include <circllhist.h>
#include "noit_metric.h"
#include "prometheus.pb-c.h"

typedef struct {
  const char *units;
  bool is_histogram;
  double hist_boundary;
} prometheus_coercion_t;

typedef struct {
  histogram_adhoc_bin_t *bins;
  int nbins;
  int nallocdbins;
  struct timeval whence;
  char name[MAX_METRIC_TAGGED_NAME];
} prometheus_hist_in_progress_t;

void noit_prometheus_hist_in_progress_free(void *vhip);

char *noit_prometheus_metric_name_from_labels(Prometheus__Label **labels,
                                              size_t label_count,
                                              const char *units,
                                              bool coerce_hist);

bool noit_prometheus_snappy_uncompress(mtev_dyn_buffer_t *uncompressed_data_out,
                                       size_t *uncompressed_size_out,
                                       const void *data_in,
                                       size_t data_in_len);

prometheus_coercion_t noit_prometheus_metric_name_coerce(Prometheus__Label **labels,
                                                         size_t label_count,
                                                         bool do_units,
                                                         bool do_hist,
                                                         const char **allowed_units);

noit_metric_message_t *
noit_prometheus_translate_to_noit_metric_message(prometheus_coercion_t *coercion,
                                                 const int64_t account_id,
                                                 const uuid_t check_uuid,
                                                 const char *metric_name,
                                                 const Prometheus__Sample *sample);

#endif