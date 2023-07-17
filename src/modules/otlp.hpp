/*
 * Copyright (c) 2023, Circonus, Inc. All rights reserved.
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
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,3
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#ifndef OLTP_HPP
#define OLTP_HPP

#include <mtev_defines.h>
#include <mtev_memory.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>
#include <fcntl.h>

#include <mtev_rest.h>
#include <mtev_hash.h>
#include <mtev_json.h>
#include <mtev_uuid.h>
#include <mtev_b64.h>
#include <mtev_dyn_buffer.h>
#include <circllhist.h>

extern "C" {
#include "noit_config.h"
#include "noit_metric.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_mtev_bridge.h"
}
#include <tuple>
#include <thread>
#include <iostream>
#include <fstream>
#include <google/protobuf/stubs/common.h>
#include "opentelemetry/proto/metrics/v1/metrics.pb.h"
#include "opentelemetry/proto/collector/metrics/v1/metrics_service.pb.h"

namespace OtelProto = opentelemetry::proto;
namespace OtelCommon = OtelProto::common::v1;
namespace OtelMetrics = OtelProto::metrics::v1;
namespace OtelCollectorMetrics = OtelProto::collector::metrics::v1;

static mtev_log_stream_t nlerr;
static mtev_log_stream_t nldeb;
static mtev_log_stream_t nldeb_verbose;

static constexpr int READ_CHUNK{32768};

struct otlp_mod_config {
  mtev_hash_table *options;
};

struct otlp_closure {
  noit_module_t *self;
};

class otlp_upload final
{
  public:
  mtev_dyn_buffer_t data;
  bool complete;
  noit_check_t *check;
  uuid_t check_id;
  struct timeval start_time;
  histogram_approx_mode_t mode;
  mtev_hash_table *immediate_metrics;

  otlp_upload() = delete;
  explicit otlp_upload(noit_check_t *check) : complete{false}, check{check}, mode{HIST_APPROX_HIGH} {
    mtev_gettimeofday(&start_time, nullptr);
    immediate_metrics = static_cast<mtev_hash_table *>(calloc(1, sizeof(*immediate_metrics)));
    mtev_hash_init(immediate_metrics);
    memcpy(check_id, check->checkid, UUID_SIZE);
    mtev_dyn_buffer_init(&data);
  }
  ~otlp_upload() {
    mtev_dyn_buffer_destroy(&data);
    mtev_hash_destroy(immediate_metrics, nullptr, mtev_memory_safe_free);
    free(immediate_metrics);
  }
};

otlp_mod_config *make_new_mod_config();
void free_otlp_upload(void *pul);
void metric_local_batch_flush_immediate(otlp_upload *rxc);
mtev_boolean cross_module_reverse_allowed(noit_check_t *check, const char *secret);
void handle_message(otlp_upload *rxc, const OtelCollectorMetrics::ExportMetricsServiceRequest &msg);
int noit_otlp_config(noit_module_t *self, mtev_hash_table *options);
int noit_otlp_initiate_check(noit_module_t *self, noit_check_t *check, int once,
                             noit_check_t *cause);

#endif
