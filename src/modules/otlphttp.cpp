/*
 * Copyright (c) 2022, Circonus, Inc. All rights reserved.
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
#include <grpcpp/grpcpp.h>
#include <google/protobuf/stubs/common.h>
#include "opentelemetry/proto/metrics/v1/metrics.pb.h"
#include "opentelemetry/proto/collector/metrics/v1/metrics_service.pb.h"
#include "opentelemetry/proto/collector/metrics/v1/metrics_service.grpc.pb.h"

namespace OtelProto = opentelemetry::proto;
namespace OtelCommon = OtelProto::common::v1;
namespace OtelMetrics = OtelProto::metrics::v1;
namespace OtelCollectorMetrics = OtelProto::collector::metrics::v1;

static mtev_log_stream_t nlerr;
static mtev_log_stream_t nldeb;
class GRPCService : public OtelCollectorMetrics::MetricsService::Service
{
  grpc::Status Export(grpc::ServerContext* context,
                      const OtelCollectorMetrics::ExportMetricsServiceRequest* request,
                      OtelCollectorMetrics::ExportMetricsServiceResponse* response) override;
};

GRPCService grpcservice;
static std::thread *grpcserver;

typedef struct _mod_config {
  mtev_hash_table *options;
  std::string grpc_server;
  int grpc_port;
  bool enable_grpc;
  bool enable_http;
  bool use_grpc_ssl;
  bool grpc_ssl_use_broker_cert;
  bool grpc_ssl_use_root_cert;
} otlphttp_mod_config_t;

typedef struct otlphttp_closure_s {
  noit_module_t *self;
} otlphttp_closure_t;

struct value_list {
  char *v;
  struct value_list *next;
};

class otlphttp_upload_t
{
  public:
  mtev_dyn_buffer_t data;
  bool complete;
  noit_check_t *check;
  uuid_t check_id;
  struct timeval start_time;
  histogram_approx_mode_t mode;
  mtev_hash_table *immediate_metrics;

  explicit otlphttp_upload_t(noit_check_t *check) : complete{false}, check{check}, mode{HIST_APPROX_HIGH} {
    mtev_gettimeofday(&start_time, NULL);
    immediate_metrics = static_cast<mtev_hash_table *>(calloc(1, sizeof(*immediate_metrics)));
    mtev_hash_init(immediate_metrics);
    memcpy(check_id, check->checkid, UUID_SIZE);
    mtev_dyn_buffer_init(&data);
  }
  ~otlphttp_upload_t() {
    mtev_dyn_buffer_destroy(&data);
    mtev_hash_destroy(immediate_metrics, NULL, mtev_memory_safe_free);
    free(immediate_metrics);
  }
};

static const char *units_convert(const char *in, double *mult) {
  if(mult) *mult = 1;
#define UCC(cs, scale, rv) do { \
  if(!strcmp(cs, in)) { \
    if(mult) *mult = scale; \
    return rv; \
  } \
} while(0)
  switch(*in) {
    case '1':
      UCC("1", 1, nullptr);
      break;
    case 'b':
      UCC("bit", 1, "bits");
      UCC("bits", 1, "bits");
      break;
    case 'B':
      UCC("By", 1, "bytes");
      break;
    case 'd':
      UCC("d", 86400, "seconds");
      break;
    case 'G':
      UCC("Gi", 1073741824, "bytes");
      UCC("GIB", 1073741824, "bytes");
      UCC("GB", 1000000000, "bytes");
      break;
    case 'h':
      UCC("min", 3600, "seconds");
      break;
    case 'K':
      UCC("Ki", 1024, "bytes");
      UCC("KIB", 1024, "bytes");
      UCC("KB", 1000, "bytes");
      break;
    case 'm':
      UCC("min", 60, "seconds");
    case 'M':
      UCC("Mi", 1048576, "bytes");
      UCC("MIB", 1048576, "bytes");
      UCC("MB", 1000000, "bytes");
      break;
    case 's':
      UCC("s", 1, "seconds");
      UCC("ms", 0.001, "seconds");
      UCC("us", 0.000001, "seconds");
      UCC("ns", 0.000000001, "seconds");
      break;
    case 'T':
      UCC("Ti", 1099511627776, "bytes");
      UCC("TIB", 1099511627776, "bytes");
      UCC("TB", 1000000000000, "bytes");
      break;
  }
  return in;
}
#define READ_CHUNK 32768

static void 
metric_local_batch(otlphttp_upload_t *rxc, const char *name, double *, int64_t *, struct timeval w);

static void
free_otlphttp_upload(void *pul)
{
  otlphttp_upload_t *p = (otlphttp_upload_t *)pul;
  delete p;
}

static otlphttp_upload_t *
rest_get_upload(mtev_http_rest_closure_t *restc, int *mask, int *complete)
{

  otlphttp_upload_t *rxc;
  mtev_http_request *req = mtev_http_session_request(restc->http_ctx);

  rxc = (otlphttp_upload_t *)restc->call_closure;

  while(!rxc->complete) {
    int len;
    mtev_dyn_buffer_ensure(&rxc->data, READ_CHUNK);
    len = mtev_http_session_req_consume(restc->http_ctx,
                                        mtev_dyn_buffer_write_pointer(&rxc->data), READ_CHUNK,
                                        READ_CHUNK,
                                        mask);
    if(len > 0) {
      mtev_dyn_buffer_advance(&rxc->data, len);
    }
    if(len < 0 && errno == EAGAIN) return NULL;
    else if(len < 0) {
      *complete = 1;
      return NULL;
    }
    if(len == 0 && mtev_http_request_payload_complete(req)) {
      rxc->complete = mtev_true;
    }
  }

  *complete = 1;
  return rxc;
}

static int otlphttp_submit(noit_module_t *self, noit_check_t *check,
                             noit_check_t *cause)
{
  otlphttp_closure_t *ccl;
  struct timeval duration;
  /* We are passive, so we don't do anything for transient checks */
  if(check->flags & NP_TRANSIENT) return 0;

  if(!check->closure) {
    ccl = new otlphttp_closure_t;
    check->closure = static_cast<void *>(ccl);
    ccl->self = self;
  } else {
    // Don't count the first run
    struct timeval now;
    char human_buffer[256];
    int stats_count = 0;
    stats_t *s = noit_check_get_stats_inprogress(check);

    mtev_memory_begin();
    mtev_gettimeofday(&now, NULL);
    sub_timeval(now, check->last_fire_time, &duration);
    noit_stats_set_whence(check, &now);
    noit_stats_set_duration(check, duration.tv_sec * 1000 + duration.tv_usec / 1000);

    /* We just want to set the number of metrics here to the number
     * of metrics in the stats_t struct */
    if (s) {
      mtev_hash_table *metrics = noit_check_stats_metrics(s);
      if (metrics) {
        stats_count = mtev_hash_size(metrics);
      }
    }

    snprintf(human_buffer, sizeof(human_buffer),
             "dur=%ld,run=%d,stats=%d", duration.tv_sec * 1000 + duration.tv_usec / 1000,
             check->generation, stats_count);
    mtevL(nldeb, "otlphttp(%s) [%s]\n", check->target, human_buffer);

    // Not sure what to do here
    noit_stats_set_available(check, (stats_count > 0) ?
        NP_AVAILABLE : NP_UNAVAILABLE);
    noit_stats_set_state(check, (stats_count > 0) ?
        NP_GOOD : NP_BAD);
    noit_stats_set_status(check, human_buffer);
    if(check->last_fire_time.tv_sec)
      noit_check_passive_set_stats(check);

    memcpy(&check->last_fire_time, &now, sizeof(now));
    mtev_memory_end();
  }
  return 0;
}

static mtev_boolean
cross_module_reverse_allowed(noit_check_t *check, const char *secret) {
  void *vstr;
  mtev_hash_table *config;
  static int reverse_check_module_id = -1;
  if(reverse_check_module_id < 0) {
    reverse_check_module_id = noit_check_registered_module_by_name("reverse");
    if(reverse_check_module_id < 0) return mtev_false;
  }
  config = noit_check_get_module_config(check, reverse_check_module_id);
  if(!config) return mtev_false;
  if(mtev_hash_retrieve(config, "secret_key", strlen("secret_key"), &vstr)) {
    if(!strcmp((const char *)vstr, secret)) return mtev_true;
  }
  return mtev_false;
}

static void
metric_local_batch_flush_immediate(otlphttp_upload_t *rxc) {
  if(mtev_hash_size(rxc->immediate_metrics)) {
    noit_check_log_bundle_metrics(rxc->check, &rxc->start_time, rxc->immediate_metrics);
    mtev_hash_delete_all(rxc->immediate_metrics, NULL, mtev_memory_safe_free);
  }
}

class name_builder {
  private:
  std::string base_name;
  char final_name[MAX_METRIC_TAGGED_NAME];
  std::vector<std::tuple<std::string,std::string,bool>> tags;
  bool materialized{false};
  public:
  name_builder(const std::string &base_name, const google::protobuf::RepeatedPtrField<OtelCommon::KeyValue> &kvs) : base_name{base_name} {
    final_name[0] = '\0';
    for(auto kv : kvs) {
      add(kv);
    }
  }
  explicit name_builder(const std::string &base_name) : base_name{base_name} {
    final_name[0] = '\0';
  }

  const char *name() {
    if(materialized) return final_name;
    char *name = final_name;
    char buffer[MAX_METRIC_TAGGED_NAME] = {0};
    char encode_buffer[MAX_METRIC_TAGGED_NAME] = {0};
    char *b = buffer;
    size_t tag_count = 0;
    strlcat(name, base_name.c_str(), sizeof(final_name));
    for (auto t : tags) {
      const auto &[key, value, has_value] = t;
      if (tag_count > 0) {
        strlcat(b, ",", sizeof(buffer));
      }
      /* make base64 encoded tags out of the incoming otlphttp tags for safety */
      /* TODO base64 encode these */
      size_t tl = key.size();
      int len = mtev_b64_encode((const unsigned char *)key.data(), tl, encode_buffer, sizeof(encode_buffer) - 1);
      if (len > 0) {
        encode_buffer[len] = '\0';
  
        strlcat(b, "b\"", sizeof(buffer));
        strlcat(b, encode_buffer, sizeof(buffer));
        strlcat(b, "\"", sizeof(buffer));
  
        if(has_value) {
          strlcat(b, ":b\"", sizeof(buffer));
          tl = value.size();
          len = mtev_b64_encode((const unsigned char *)value.data(), tl, encode_buffer, sizeof(encode_buffer) - 1);
          if (len > 0) {
            encode_buffer[len] = '\0';
            strlcat(b, encode_buffer, sizeof(buffer));
          }
          strlcat(b, "\"", sizeof(buffer));
        }
        tag_count++;
      }
    }
    strlcat(name, "|ST[", sizeof(final_name));
    strlcat(name, buffer, sizeof(final_name));
    strlcat(name, "]", sizeof(final_name));
  
    /* we don't have to canonicalize here as reconnoiter will do that for us */
    materialized = true;
    return final_name;
  }

  name_builder &add(const std::string &cat, const OtelCommon::KeyValue &f) {
    materialized = false;
    if(f.has_value()) {
      add(cat + "." + f.key(), f.value());
    } else {
      tags.push_back(std::make_tuple(cat + "." + f.key(), "", false));
    }
    return *this;
  }

  name_builder &add(const std::string &cat, const OtelCommon::AnyValue &v) {
    materialized = false;
    switch(v.value_case()) {
    case OtelCommon::AnyValue::kStringValue:
      tags.emplace_back(cat, v.string_value(), v.has_string_value());
      break;
    case OtelCommon::AnyValue::kBoolValue:
      tags.push_back(std::make_tuple(cat, v.bool_value() ? "true" : "false", true));
      break;
    case OtelCommon::AnyValue::kIntValue:
      tags.push_back(std::make_tuple(cat, std::to_string(v.int_value()), true));
      break;
    case OtelCommon::AnyValue::kDoubleValue:
      tags.push_back(std::make_tuple(cat, std::to_string(v.double_value()), true));
      break;
    case OtelCommon::AnyValue::kArrayValue:
      for ( auto subv : v.array_value().values() ) {
        add(cat, subv);
      }
      break;
    case OtelCommon::AnyValue::kKvlistValue:
      for ( auto kv : v.kvlist_value().values() ) {
        add(cat, kv);
      }
      break;
    default:
      tags.push_back(std::make_tuple(cat, "", false));
    }
    return *this;
  }
  name_builder &add(const OtelCommon::KeyValue &f) {
    materialized = false;
    if(f.has_value()) {
      add(f.key(), f.value());
    } else {
      tags.push_back(std::make_tuple(f.key(), "", false));
    }
    return *this;
  }
  name_builder &add(const std::string &cat, const std::string &name) {
    materialized = false;
    tags.push_back(std::make_tuple(cat, name, true));
    return *this;
  }
};

void handle_dp(otlphttp_upload_t *rxc, name_builder &metric,
               const OtelMetrics::NumberDataPoint &dp,
               double scale) {
  auto whence_ns = dp.time_unix_nano();
  if(whence_ns == 0) return;
  struct timeval whence = { static_cast<time_t>(whence_ns / 1000000000ULL),
                             static_cast<time_t>((whence_ns / 1000ULL) % 1000000) };
  
  switch(dp.value_case()) {
  case OtelMetrics::NumberDataPoint::kAsInt:
    {
    int64_t vi = dp.as_int();
    if(scale >= 1) {
      vi *= scale;
      metric_local_batch(rxc, metric.name(), NULL, &vi, whence);
    }
    else {
      double vd = vi;
      vi *= scale;
      metric_local_batch(rxc, metric.name(), &vd, NULL, whence);
    }
    break;
    }
  case OtelMetrics::NumberDataPoint::kAsDouble:
    {
    double vd = dp.as_double();
    vd *= scale;
    metric_local_batch(rxc, metric.name(), &vd, NULL, whence);
    break;
    }
  default:
    mtevL(nldeb, "unsupported type: %d\n", dp.value_case());
    break;
  }
}

static
void handle_hist(otlphttp_upload_t *rxc, name_builder &metric,
               uint64_t whence_ns,
               histogram_adhoc_bin_t *bins, size_t size,
               bool cumulative, double sum) {
  if(whence_ns == 0) return;
  struct timeval whence = { static_cast<time_t>(whence_ns / 1000000000ULL),
                             static_cast<time_t>((whence_ns / 1000ULL) % 1000000) };
  
  histogram_t *h = hist_create_approximation_from_adhoc(rxc->mode, bins, size, cumulative ? 0 : sum);
  ssize_t est = hist_serialize_b64_estimate(h);
  if(est > 0) {
    char *hist_encoded = static_cast<char *>(malloc(est));
    ssize_t hist_encoded_len = hist_serialize_b64(h, hist_encoded, est);
    noit_stats_log_immediate_histo_tv(rxc->check, metric.name(), hist_encoded, hist_encoded_len,
                                      static_cast<mtev_boolean>(cumulative), whence);
    free(hist_encoded);
  }
  hist_free(h);
}

static
void handle_hist(otlphttp_upload_t *rxc, name_builder &metric,
               const OtelMetrics::HistogramDataPoint &dp,
               bool cumulative, double scale) {
  auto whence_ns = dp.time_unix_nano();
  auto size = dp.bucket_counts_size();
  histogram_adhoc_bin_t bins[size];
  int i=0;
  double last_lower = 0 - pow(10,128) / scale;
  if(dp.explicit_bounds(i) >= 0) {
    last_lower = 0;
  }
  for(auto cnt : dp.bucket_counts()) {
    bins[i].count = cnt;
    bins[i].lower = last_lower * scale;
    bins[i].upper = (i == size-1) ? pow(10,128) : (dp.explicit_bounds(i) * scale);
    last_lower = bins[i].upper;
    i++;
  }
  handle_hist(rxc, metric, whence_ns, bins, size, cumulative, dp.sum());
}

static
void handle_hist(otlphttp_upload_t *rxc, name_builder &metric,
               const OtelMetrics::ExponentialHistogramDataPoint &dp,
               bool cumulative, double scale) {
  auto whence_ns = dp.time_unix_nano();
  auto size = (dp.zero_count() == 0) ? 0 : 1;
  auto base = pow(2, pow(2, dp.scale()));
  if(dp.has_positive()) {
    size += dp.positive().bucket_counts_size();
  }
  if(dp.has_negative()) {
    size += dp.negative().bucket_counts_size();
  }

  histogram_adhoc_bin_t bins[size];
  int obinidx = 0;

  if(auto zcnt = dp.zero_count(); zcnt != 0) {
    bins[obinidx].count = zcnt;
    bins[obinidx].lower = bins[obinidx].upper = 0;
    obinidx++;
  }
  if(dp.has_positive()) {
    auto positive = dp.positive();
    int i = 0;
    auto offset = positive.offset();
    for(auto cnt : positive.bucket_counts()) {
      bins[obinidx].count = cnt;
      bins[obinidx].lower = pow(base, offset+i);
      bins[obinidx].upper = pow(base, offset+i+1);
      obinidx++;
      i++;
    }
  }
  if(dp.has_negative()) {
    auto negative = dp.negative();
    int i = 0;
    auto offset = negative.offset();
    for(auto cnt : negative.bucket_counts()) {
      bins[obinidx].count = cnt;
      bins[obinidx].lower = - pow(base, offset+i+1);
      bins[obinidx].upper = - pow(base, offset+i);
      obinidx++;
      i++;
    }
  }
  assert(obinidx == size);
  handle_hist(rxc, metric, whence_ns, bins, size, cumulative, dp.sum());
}

static void
handle_message(otlphttp_upload_t *rxc, const OtelCollectorMetrics::ExportMetricsServiceRequest &msg) {
  mtevL(nldeb, "otlp resource metrics: %d\n", msg.resource_metrics_size());
  for(int i=0; i<msg.resource_metrics_size(); i++) {
    auto rm = msg.resource_metrics(i);
    mtevL(nldeb, "otlp resource metrics[%d] ilm: %d\n", i, rm.scope_metrics_size()); //instrumentation_library_metrics_size());
    for(int li=0; li<rm.scope_metrics_size(); li++) {
      auto lm = rm.scope_metrics(li);

      for(int mi=0; mi<lm.metrics_size(); mi++) {
        auto m = lm.metrics(mi);
        auto name = m.name();
        auto unit = m.unit();

        mtevL(nldeb, "otlp resource metrics[%d][%d][%d]: type %d, name: %s\n", i, li, mi, m.data_case(), name.c_str());
        switch(m.data_case()) {
        case OtelMetrics::Metric::kGauge:
        {
          for( auto dp : m.gauge().data_points() ) {
            name_builder metric{name, dp.attributes()};
            double mult = 1;
            if(unit.size() > 0) {
              const char *unit_c = units_convert(unit.c_str(), &mult);
              if(unit_c) metric.add("units", unit_c);
            }
            handle_dp(rxc, metric, dp, mult);
          }
          break;
        }
        case OtelMetrics::Metric::kSum:
        {
          auto sum = m.sum();
          auto cumulative = sum.aggregation_temporality() == OtelMetrics::AGGREGATION_TEMPORALITY_CUMULATIVE;
          for( auto dp : m.sum().data_points() ) {
            name_builder metric{name, dp.attributes()};
            double mult = 1;
            if(unit.size() > 0) {
              const char *unit_c = units_convert(unit.c_str(), &mult);
              if(unit_c) metric.add("units", unit_c);
            }
            if(!cumulative && sum.is_monotonic()) {
              // use histograms?
            } else {
              handle_dp(rxc, metric, dp, mult);
            }
          }
          break;
        }
        case OtelMetrics::Metric::kHistogram:
        {
          auto hist = m.histogram();
          auto cumulative = hist.aggregation_temporality() == OtelMetrics::AGGREGATION_TEMPORALITY_CUMULATIVE;
          for( auto dp : hist.data_points() ) {
            name_builder metric{name, dp.attributes()};
            double mult = 1;
            if(unit.size() > 0) {
              const char *unit_c = units_convert(unit.c_str(), &mult);
              if(unit_c) metric.add("units", unit_c);
            }
            handle_hist(rxc, metric, dp, cumulative, mult);
          }
          break;
        }
        case OtelMetrics::Metric::kExponentialHistogram:
        {
          auto hist = m.exponential_histogram();
          auto cumulative = hist.aggregation_temporality() == OtelMetrics::AGGREGATION_TEMPORALITY_CUMULATIVE;
          for( auto dp : hist.data_points() ) {
            name_builder metric{name, dp.attributes()};
            double mult = 1;
            if(unit.size() > 0) {
              const char *unit_c = units_convert(unit.c_str(), &mult);
              if(unit_c) metric.add("units", unit_c);
            }
            handle_hist(rxc, metric, dp, cumulative, mult);
          }
          break;
        }
        case OtelMetrics::Metric::kSummary:
        case OtelMetrics::Metric::DATA_NOT_SET:
        {
          break;
        }
        }
      }
    }
  }
}
static void 
metric_local_batch(otlphttp_upload_t *rxc, const char *name, double *val, int64_t *vali, struct timeval w) {
  char cmetric[MAX_METRIC_TAGGED_NAME + 1 + sizeof(uint64_t)];
  void *vm;

  if(!noit_check_build_tag_extended_name(cmetric, MAX_METRIC_TAGGED_NAME, name, rxc->check)) {
    return;
  }
  int cmetric_len = strlen(cmetric);
  /* We will append the time stamp afer the null terminator to keep the key
   * appropriately unique.
   */
  uint64_t t = w.tv_sec * 1000 + w.tv_usec / 1000;
  memcpy(cmetric + cmetric_len + 1, &t, sizeof(uint64_t));
  cmetric_len += 1 + sizeof(uint64_t);

  if(mtev_hash_size(rxc->immediate_metrics) > 1000) {
    metric_local_batch_flush_immediate(rxc);
    return;
  }
  if(mtev_hash_retrieve(rxc->immediate_metrics, cmetric, cmetric_len, &vm)) {
    /* collision, just log it out */
    metric_local_batch_flush_immediate(rxc);
    return;
  }
  metric_t *m = noit_metric_alloc();
  m->metric_name = mtev_strndup(cmetric, cmetric_len);
  if(val) {
    m->metric_type = METRIC_DOUBLE;
    memcpy(&m->whence, &w, sizeof(struct timeval));
    m->metric_value.vp = malloc(sizeof(double));
    *(m->metric_value.n) = *val;
  } else if(vali) {
    m->metric_type = METRIC_INT64;
    memcpy(&m->whence, &w, sizeof(struct timeval));
    m->metric_value.vp = malloc(sizeof(int64_t));
    *(m->metric_value.n) = *vali;
  } else {
    assert(val || vali);
  }

  noit_stats_mark_metric_logged(noit_check_get_stats_inprogress(rxc->check), m, mtev_false);
  mtev_hash_store(rxc->immediate_metrics, m->metric_name, cmetric_len, m);
}

grpc::Status GRPCService::Export(grpc::ServerContext* context,
                                 const OtelCollectorMetrics::ExportMetricsServiceRequest* request,
                                 OtelCollectorMetrics::ExportMetricsServiceResponse* response)
{
  otlphttp_upload_t *rxc = NULL;
  std::string error;
  std::string check_name;
  std::string check_uuid;
  std::string secret;
  const char *check_secret{nullptr};

  mtevL(nlerr, "Client metadata:\n");
  const std::multimap<grpc::string_ref, grpc::string_ref> metadata =
      context->client_metadata();
  for (auto iter = metadata.begin(); iter != metadata.end(); ++iter) {
    std::string key{iter->first.data(), iter->first.length()};
    mtevL(nlerr, "Header key: %s\n", key.c_str());
    // Check for binary value
    size_t isbin = iter->first.find("-bin");
    if ((isbin != std::string::npos) && (isbin + 4 == iter->first.size())) {
      mtevL(nlerr, "Value: ");
      for (auto c : iter->second) {
        mtevL(nlerr, "%x", c);
      }
      mtevL(nlerr, "\n");
      continue;
    }
    std::string value{iter->second.data(), iter->second.length()};
    mtevL(nlerr, "Value: %s\n", value.c_str());
    if (key == "check_uuid") {
      check_uuid = value;
    }
    else if (key == "check_name") {
      check_name = value;
    }
    else if (key == "secret" || key == "api_key") {
      secret = value;
    }
  }

  noit_check_t *check{nullptr};
  uuid_t check_id;
  if (!check_uuid.empty()) {
    mtev_uuid_parse(check_uuid.c_str(), check_id);
    check = noit_poller_lookup(check_id);
    if(!check) {
      error = "no such check: ";
      error += check_uuid;
      goto error;
    }
  }
  else {
    error = "no check_uuid specified by grpc metadata";
    goto error;
  }

  if(strcmp(check->module, "otlphttp")) {
    error = "no such otlphttp check: ";
    error += check_uuid;
    goto error;
  }

  (void)mtev_hash_retr_str(check->config, "secret", strlen("secret"), &check_secret);
  if (secret != check_secret) {
    error = "incorrect secret specified for check_uuid: ";
    error += check_uuid;
    goto error;
  }

  rxc = new otlphttp_upload_t(check);

  if (const char *mode_str = mtev_hash_dict_get(check->config, "hist_approx_mode")) {
    if(!strcmp(mode_str, "low")) rxc->mode = HIST_APPROX_LOW;
    else if(!strcmp(mode_str, "mid")) rxc->mode = HIST_APPROX_MID;
    else if(!strcmp(mode_str, "harmonic_mean")) rxc->mode = HIST_APPROX_HARMONIC_MEAN;
    else if(!strcmp(mode_str, "high")) rxc->mode = HIST_APPROX_HIGH;
    // Else it just sticks the with initial defaults */
  }

  mtev_memory_init_thread();
  mtev_memory_begin();
  handle_message(rxc, *request);
  metric_local_batch_flush_immediate(rxc);
  mtev_memory_end();

  return grpc::Status::OK;

error:
  return grpc::Status(grpc::StatusCode::NOT_FOUND, "Bad check id");
}

static int
rest_otlphttp_handler(mtev_http_rest_closure_t *restc, int npats, char **pats)
{
  int mask, complete = 0, cnt = 0;
  otlphttp_upload_t *rxc = NULL;
  const char *error = "internal error", *secret = NULL;
  mtev_http_session_ctx *ctx = restc->http_ctx;
  const unsigned int DEBUGDATA_OUT_SIZE=4096;
  char debugdata_out[DEBUGDATA_OUT_SIZE];
  int debugflag=0;
  const char *debugchkflag;
  noit_check_t *check;
  uuid_t check_id;
  mtev_http_request *req;
  mtev_hash_table *hdrs;

  if(npats != 2) {
    error = "bad uri";
    goto error;
  }
  if(mtev_uuid_parse(pats[0], check_id)) {
    error = "uuid parse error";
    goto error;
  }

  if(restc->call_closure == NULL) {
    mtev_boolean allowed = mtev_false;
    check = noit_poller_lookup(check_id);
    if(!check) {
      error = "no such check";
      goto error;
    }
    if(strcmp(check->module, "otlphttp")) {
      error = "no such otlphttp check";
      goto error;
    }

    /* check "secret"  */
    (void)mtev_hash_retr_str(check->config, "secret", strlen("secret"), &secret);
    if(secret && !strcmp(pats[1], secret)) allowed = mtev_true;
    if(!allowed && cross_module_reverse_allowed(check, pats[1])) allowed = mtev_true;

    if(!allowed) {
      error = "secret mismatch";
      goto error;
    }

    mtevL(nldeb, "otlphttp new payload for %s\n", pats[0]);
    rxc = new otlphttp_upload_t(check);
    if(const char *mode_str = mtev_hash_dict_get(check->config, "hist_approx_mode")) {
      if(!strcmp(mode_str, "low")) rxc->mode = HIST_APPROX_LOW;
      else if(!strcmp(mode_str, "mid")) rxc->mode = HIST_APPROX_MID;
      else if(!strcmp(mode_str, "harmonic_mean")) rxc->mode = HIST_APPROX_HARMONIC_MEAN;
      else if(!strcmp(mode_str, "high")) rxc->mode = HIST_APPROX_HIGH;
      // Else it just sticks the with initial defaults */
    }
    restc->call_closure = static_cast<void *>(rxc);
    restc->call_closure_free = free_otlphttp_upload;
  }
  else rxc = static_cast<otlphttp_upload_t*>(restc->call_closure);

  /* flip threads */
  {
    mtev_http_connection *conn = mtev_http_session_connection(ctx);
    eventer_t e = mtev_http_connection_event(conn);
    if(e) {
      pthread_t tgt = CHOOSE_EVENTER_THREAD_FOR_CHECK(rxc->check);
      if(!pthread_equal(eventer_get_owner(e), tgt)) {
        eventer_set_owner(e, tgt);
        return EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
      }
    }
  }

  rxc = rest_get_upload(restc, &mask, &complete);
  if(rxc == NULL && !complete) return mask;

  if(!rxc) {
    error = "No data?";
    goto error;
  }
  
  if(OtelCollectorMetrics::ExportMetricsServiceRequest msg;
     msg.ParseFromArray(mtev_dyn_buffer_data(&rxc->data),
                        static_cast<int>(mtev_dyn_buffer_used(&rxc->data)))) {
    mtev_memory_begin();
    mtevL(nldeb, "otlphttp payload %zu bytes\n", mtev_dyn_buffer_used(&rxc->data));
    handle_message(rxc, msg);
    metric_local_batch_flush_immediate(rxc);
    mtev_memory_end();
  }
  else {
    mtevL(mtev_error, "Error parsing input %zu bytes\n", mtev_dyn_buffer_used(&rxc->data));
    error = "cannot parse protobuf";
    goto error;
  }

  mtev_http_response_status_set(ctx, 200, "OK");
  mtev_http_response_option_set(ctx, MTEV_HTTP_CLOSE);

  /*Examine headers for x-circonus-otlphttp-debug flag*/
  req = mtev_http_session_request(ctx);
  hdrs = mtev_http_request_headers_table(req);

  /*Check if debug header passed in. If present and set to true, set debugflag value to one.*/
  if(mtev_hash_retr_str(hdrs, "x-circonus-otlphttp-debug", strlen("x-circonus-otlphttp-debug"), &debugchkflag))
  {
    if (strcmp(debugchkflag,"true")==0)
    {
      debugflag=1;
    }
  }
  /*Otherwise, if set to one, output current metrics in addition to number of current metrics.*/
  if (debugflag == 1)
  {
    mtev_http_response_header_set(ctx, "Content-Type", "application/json");
    json_object *obj =  NULL;
    obj = json_object_new_object();
    stats_t *c;
    mtev_hash_table *metrics;
    json_object *metrics_obj;
    metrics_obj = json_object_new_object();

    /*Retrieve check information.*/
    check = rxc->check;
    c = noit_check_get_stats_inprogress(check);
    metrics = noit_check_stats_metrics(c);
    mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
    const char *k;
    int klen;
    void *data;
    memset(debugdata_out,'\0',sizeof(debugdata_out));

    /*Extract metrics*/
    while(mtev_hash_next(metrics, &iter, &k, &klen, &data)) {
      char buff[256], type_str[2];
      metric_t *tmp=(metric_t *)data;
      char *metric_name=tmp->metric_name;
      metric_type_t metric_type=tmp->metric_type;
      noit_stats_snprint_metric_value(buff, sizeof(buff), tmp);
      json_object *value_obj = json_object_new_object();
      snprintf(type_str, sizeof(type_str), "%c", metric_type);
      json_object_object_add(value_obj, "_type", json_object_new_string(type_str));
      json_object_object_add(value_obj, "_value", json_object_new_string(buff));
      json_object_object_add(metrics_obj, metric_name, value_obj);
    }

    /*Output stats and metrics.*/
    json_object_object_add(obj, "stats", json_object_new_int(cnt));
    json_object_object_add(obj, "metrics", metrics_obj);
    const char *json_out = json_object_to_json_string(obj);
    mtev_http_response_append(ctx, json_out, strlen(json_out));
    json_object_put(obj);
  }

  mtev_http_response_end(ctx);
  return 0;

 error:
  mtev_http_response_server_error(ctx, "application/json");
  mtev_http_response_append(ctx, "{ \"Status\": \"", 13);
  mtev_http_response_append(ctx, error, strlen(error));
  mtevL(mtev_error, "ERROR: %s\n", error);
  mtev_http_response_append(ctx, "\" }", 3);
  mtev_http_response_end(ctx);
  return 0;
}

static int noit_otlphttp_initiate_check(noit_module_t *self,
                                        noit_check_t *check,
                                        int once, noit_check_t *cause) {
  check->flags |= NP_PASSIVE_COLLECTION;
  if (check->closure == NULL) {
    otlphttp_closure_t *ccl = new otlphttp_closure_t;
    check->closure = static_cast<void *>(ccl);
    ccl->self = self;
  }
  INITIATE_CHECK(otlphttp_submit, self, check, cause);
  return 0;
}

static int noit_otlphttp_config(noit_module_t *self, mtev_hash_table *options) {
  otlphttp_mod_config_t *conf;
  conf = static_cast<otlphttp_mod_config_t*>(noit_module_get_userdata(self));
  if(conf) {
    if(conf->options) {
      mtev_hash_destroy(conf->options, free, free);
      free(conf->options);
    }
  }
  else
    conf = new otlphttp_mod_config_t;
  conf->options = options;
  noit_module_set_userdata(self, conf);
  return 1;
}

static std::string read_keycert(const std::string filename)
{
  std::ifstream file(filename, std::ios::binary);
  std::stringstream buffer;
  buffer << file.rdbuf();
  file.close();
  return buffer.str();
}

static void grpc_server_thread(std::string server_address,
                               bool use_grpc_ssl,
                               bool grpc_ssl_use_broker_cert,
                               bool grpc_ssl_use_root_cert,
                               std::string broker_crt,
                               std::string broker_key,
                               std::string root_crt)
{
  grpc::ServerBuilder builder;
  std::shared_ptr<grpc::ServerCredentials> server_creds;
  if (grpc_ssl_use_broker_cert) {
    mtevL(nlerr, "Setting up grpc ssl using broker cert and key\n");
    // read the cert and key
    std::string servercert = read_keycert(broker_crt);
    std::string serverkey = read_keycert(broker_key);

    // create a pem key cert pair using the cert and the key
    grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp;
    pkcp.private_key = serverkey;
    pkcp.cert_chain = servercert;

    // alter the server ssl opts to put in our own cert/key and optionally the root cert too
    grpc::SslServerCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs="";
    if (grpc_ssl_use_root_cert) {
      mtevL(nlerr, "Registering grpc ssl root cert\n");
      std::string rootcert = read_keycert(root_crt);
      ssl_opts.pem_root_certs = rootcert;
    }
    ssl_opts.pem_key_cert_pairs.push_back(pkcp);

    // create a server credentials object to use on the listening port
    server_creds = grpc::SslServerCredentials(ssl_opts);
  }
  else {  
    server_creds = grpc::SslServerCredentials(grpc::SslServerCredentialsOptions());
  }
  if (!use_grpc_ssl) {
    server_creds = grpc::InsecureServerCredentials();
  }
  builder.AddListeningPort(server_address, server_creds);
  builder.RegisterService(&grpcservice);
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  mtevL(nlerr, "otlphttp grpc server listening on %s\n", server_address.c_str());
  server->Wait();
  mtevL(nlerr, "otlphttp gprc server terminated!\n");
}

static int noit_otlphttp_onload(mtev_image_t *self) {
  if(!nlerr) nlerr = mtev_log_stream_find("error/otlphttp");
  if(!nldeb) nldeb = mtev_log_stream_find("debug/otlphttp");
  if(!nlerr) nlerr = noit_error;
  if(!nldeb) nldeb = noit_debug;
  return 0;
}

static int noit_otlphttp_init(noit_module_t *self) {
  const char *config_val;
  otlphttp_mod_config_t *conf = static_cast<otlphttp_mod_config_t*>(noit_module_get_userdata(self));

  conf->enable_grpc = true;
  if (mtev_hash_retr_str(conf->options,
                         "enable_grpc", strlen("enable_grpc"),
                         (const char **)&config_val)) {
    if (!strcasecmp(config_val, "false") || !strcasecmp(config_val, "off")) {
      conf->enable_grpc = false;
    }
  }

  conf->enable_http = true;
  if (mtev_hash_retr_str(conf->options,
                         "enable_http", strlen("enable_http"),
                         (const char **)&config_val)) {
    if (!strcasecmp(config_val, "false") || !strcasecmp(config_val, "off")) {
      conf->enable_http = false;
    }
  }

  conf->grpc_server = "127.0.0.1";
  if (mtev_hash_retr_str(conf->options,
                         "grpc_server", strlen("grpc_server"),
                         (const char **)&config_val)) {
    conf->grpc_server = config_val;
  }

  conf->grpc_port = 4317;
  if (mtev_hash_retr_str(conf->options,
                         "grpc_port", strlen("grpc_port"),
                         (const char **)&config_val)) {
    conf->grpc_port = std::atoi(config_val);
    if (conf->grpc_port <= 0) {
      mtevL(nlerr, "Valid port not specified, using default port 4317\n");
      conf->grpc_port = 4317;
    }
  }

  conf->use_grpc_ssl = false;
  conf->grpc_ssl_use_broker_cert = false;
  conf->grpc_ssl_use_root_cert = false;
  if (conf->enable_grpc) {
    conf->use_grpc_ssl = true;
    if (mtev_hash_retr_str(conf->options,
                           "use_grpc_ssl", strlen("use_grpc_ssl"),
                           (const char **)&config_val)) {
      if (!strcasecmp(config_val, "false") || !strcasecmp(config_val, "off")) {
        conf->use_grpc_ssl = false;
      }
    }
    if (conf->use_grpc_ssl) {
      if (mtev_hash_retr_str(conf->options,
                             "grpc_ssl_use_broker_cert", strlen("grpc_ssl_use_broker_cert"),
                             (const char **)&config_val)) {
        if (!strcasecmp(config_val, "true") || !strcasecmp(config_val, "on")) {
          conf->grpc_ssl_use_broker_cert = true;
          if (mtev_hash_retr_str(conf->options,
                                 "grpc_ssl_use_root_cert", strlen("grpc_ssl_use_root_cert"),
                                 (const char **)&config_val)) {
            if (!strcasecmp(config_val, "true") || !strcasecmp(config_val, "on")) {
              conf->grpc_ssl_use_root_cert = true;
            }
          }
        }
      }
    }
  }

  noit_module_set_userdata(self, conf);

  mtevL(nlerr, "otlphttp config: http enabled: %s, grpc_enabled: %s\n",
        conf->enable_http ? "yes" : "no", conf->enable_grpc ? "yes" : "no");
  mtevL(nlerr, "grpc: server address: %s:%d, use ssl: %s, use broker cert: %s, use root cert: %s\n",
        conf->grpc_server.c_str(), conf->grpc_port, conf->use_grpc_ssl ? "yes" : "no",
        conf->grpc_ssl_use_broker_cert ? "yes" : "no",
        conf->grpc_ssl_use_root_cert ? "yes" : "no"); 

  if (conf->enable_http) {
    eventer_pool_t *dp = noit_check_choose_pool_by_module(self->hdr.name);
    /* register rest handler */
    mtev_rest_mountpoint_t *rule;
    rule = mtev_http_rest_new_rule("POST", "/module/otlphttp/v1/",
                                  "^(" UUID_REGEX ")/([^/]*)$",
                                  rest_otlphttp_handler);
    if(dp) mtev_rest_mountpoint_set_eventer_pool(rule, dp);
    rule = mtev_http_rest_new_rule("POST", "/module/otlphttp/",
                                  "^(" UUID_REGEX ")/([^/]*)/v1/metrics",
                                  rest_otlphttp_handler);
    if(dp) mtev_rest_mountpoint_set_eventer_pool(rule, dp);

    mtevL(nlerr, "otlphttp REST endpoint now active\n");
  }

  std::string certificate_file;
  std::string key_file;
  std::string ca_chain;
  if (conf->enable_grpc) {
    if (conf->use_grpc_ssl && conf->grpc_ssl_use_broker_cert) {
      // read cert/key settings from sslconfig
      mtev_conf_section_t listeners = mtev_conf_get_section_read(MTEV_CONF_ROOT,
                                                                 "//listeners");
      if (mtev_conf_section_is_empty(listeners)) {
        mtev_conf_release_section_read(listeners);
        mtevL(nlerr, "Cannot find or empty //listeners config.\n");
        return 1;
      }
      const char *value;
      mtev_hash_table *sslconfig = mtev_conf_get_hash(listeners, "sslconfig");
      if (mtev_hash_retr_str(sslconfig, "certificate_file", strlen("certificate_file"), &value)) {
        certificate_file = value;
      }
      if (mtev_hash_retr_str(sslconfig, "key_file", strlen("key_file"), &value)) {
        key_file = value;
      }
      if (certificate_file.empty() || key_file.empty()) {
        mtevL(nlerr, "need sslconfig/listeners to have certificate_file and key_file configured "
                     "for grpc ssl using broker cert.\n");
        return 1;
      }
      if (conf->grpc_ssl_use_root_cert) {
        if (mtev_hash_retr_str(sslconfig, "ca_chain", strlen("ca_chain"), &value)) {
          ca_chain = value;
        }
        if (ca_chain.empty()) {
          mtevL(nlerr, "need sslconfig/listeners to have ca_chain configured for grpc ssl using "
                       "CA root cert\n");
        }
      }

      mtevL(nlerr, "Broker cert is: %s\n", certificate_file.c_str());
      mtevL(nlerr, "Broker key is: %s\n", key_file.c_str());
      mtevL(nlerr, "Broker ca_chain is: %s\n", ca_chain.c_str());
    }

    std::string server_address = conf->grpc_server + ":" + std::to_string(conf->grpc_port);
    grpcserver = new std::thread{grpc_server_thread, server_address, conf->use_grpc_ssl,
                                 conf->grpc_ssl_use_broker_cert, conf->grpc_ssl_use_root_cert,
                                 certificate_file, key_file, ca_chain};
    mtevL(nlerr, "otlphttp grpc listener thread started\n");
  }

  return 0;
}

#include "otlphttp.xmlh"
noit_module_t otlphttp = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "otlphttp",
    .description = "otlphttp collection",
    .xml_description = otlphttp_xml_description,
    .onload = noit_otlphttp_onload
  },
  noit_otlphttp_config,
  noit_otlphttp_init,
  noit_otlphttp_initiate_check,
  nullptr
};
