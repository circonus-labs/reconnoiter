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

#include "otlp.hpp"

struct value_list {
  char *v;
  struct value_list *next;
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
    default:
      mtevL(nldeb_verbose, "[otlp] unsupported units: %c\n", *in);
      break;
  }
  return in;
}

void free_otlp_upload(void *pul)
{
  otlp_upload *p = (otlp_upload *)pul;
  delete p;
}

static int otlp_submit(noit_module_t *self, noit_check_t *check,
                       noit_check_t *cause)
{
  otlp_closure *ccl;
  struct timeval duration;
  /* We are passive, so we don't do anything for transient checks */
  if(check->flags & NP_TRANSIENT) return 0;

  if(!check->closure) {
    ccl = new otlp_closure;
    check->closure = static_cast<void *>(ccl);
    ccl->self = self;
  } else {
    // Don't count the first run
    struct timeval now;
    char human_buffer[256];
    int stats_count = 0;
    stats_t *s = noit_check_get_stats_inprogress(check);

    mtev_memory_begin();
    mtev_gettimeofday(&now, nullptr);
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
    mtevL(nldeb_verbose, "[otlp] (%s) [%s]\n", check->target, human_buffer);

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

mtev_boolean
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

void metric_local_batch_flush_immediate(otlp_upload *rxc) {
  if(mtev_hash_size(rxc->immediate_metrics)) {
    noit_check_log_bundle_metrics(rxc->check, &rxc->start_time, rxc->immediate_metrics);
    mtev_hash_delete_all(rxc->immediate_metrics, nullptr, mtev_memory_safe_free);
  }
}

static void 
metric_local_batch(otlp_upload *rxc, const char *name, double *val, int64_t *vali,
                   struct timeval w) {
  char cmetric[MAX_METRIC_TAGGED_NAME];

  if(!noit_check_build_tag_extended_name(cmetric, MAX_METRIC_TAGGED_NAME, name, rxc->check)) {
    return;
  }
  auto cmetric_len = strlen(cmetric);
  if(auto metric = static_cast<metric_t *>(mtev_hash_get(rxc->immediate_metrics, cmetric, cmetric_len))) {
    if (metric->whence.tv_sec == w.tv_sec && metric->whence.tv_usec == w.tv_usec) {
      return;
    }
    metric_local_batch_flush_immediate(rxc);
  }
  auto m = noit_metric_alloc();
  m->metric_name = strdup(cmetric);
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

  if(mtev_hash_size(rxc->immediate_metrics) > 1000) {
    metric_local_batch_flush_immediate(rxc);
  }
  if (!mtev_hash_store(rxc->immediate_metrics, m->metric_name, cmetric_len, m)) {
    metric_local_batch_flush_immediate(rxc);
    if (!mtev_hash_store(rxc->immediate_metrics, m->metric_name, cmetric_len, m)) {
      mtevL(nlerr, "%s: could not sure metric %s in otlp\n", __func__, m->metric_name);
      mtev_memory_safe_free(m);
    }
  }
  noit_stats_mark_metric_logged(noit_check_get_stats_inprogress(rxc->check), m, mtev_false);
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
      /* make base64 encoded tags out of the incoming otlp tags for safety */
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

static void handle_dp(otlp_upload *rxc, name_builder &metric,
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
      metric_local_batch(rxc, metric.name(), nullptr, &vi, whence);
    }
    else {
      double vd = vi;
      vd *= scale;
      metric_local_batch(rxc, metric.name(), &vd, nullptr, whence);
    }
    break;
    }
  case OtelMetrics::NumberDataPoint::kAsDouble:
    {
    double vd = dp.as_double();
    vd *= scale;
    metric_local_batch(rxc, metric.name(), &vd, nullptr, whence);
    break;
    }
  default:
    mtevL(nldeb_verbose, "[otlp] unsupported type: %d\n", dp.value_case());
    break;
  }
}

static
void handle_hist(otlp_upload *rxc, name_builder &metric,
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
void handle_hist(otlp_upload *rxc, name_builder &metric,
               const OtelMetrics::HistogramDataPoint &dp,
               bool cumulative, double scale) {
  auto whence_ns = dp.time_unix_nano();
  auto size = dp.bucket_counts_size();
  histogram_adhoc_bin_t bins[size];
  int i=0;
  double last_lower = 0 - pow(10,128) / scale;
  double explicit_bounds = 0.0;
  if (dp.explicit_bounds_size()) {
    explicit_bounds = dp.explicit_bounds(i);
    if (explicit_bounds >= 0) {
      last_lower = 0;
    }
  }
  for(auto cnt : dp.bucket_counts()) {
    bins[i].count = cnt;
    bins[i].lower = last_lower * scale;
    bins[i].upper = (i == size-1) ? pow(10,128) : (explicit_bounds * scale);
    last_lower = bins[i].upper;
    i++;
  }
  handle_hist(rxc, metric, whence_ns, bins, size, cumulative, dp.sum());
}

static
void handle_hist(otlp_upload *rxc, name_builder &metric,
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

void handle_message(otlp_upload *rxc, const OtelCollectorMetrics::ExportMetricsServiceRequest &msg)
{
  mtevL(nldeb_verbose, "[otlp] resource metrics: %d\n", msg.resource_metrics_size());
  for(int i=0; i<msg.resource_metrics_size(); i++) {
    auto rm = msg.resource_metrics(i);
    mtevL(nldeb_verbose, "[otlp] resource metrics[%d] ilm: %d\n", i, rm.scope_metrics_size());
    for(int li=0; li<rm.scope_metrics_size(); li++) {
      auto lm = rm.scope_metrics(li);

      for(int mi=0; mi<lm.metrics_size(); mi++) {
        auto m = lm.metrics(mi);
        auto name = m.name();
        auto unit = m.unit();

        mtevL(nldeb_verbose, "[otlp] resource metrics[%d][%d][%d]: type %d, name: %s\n",
              i, li, mi, m.data_case(), name.c_str());
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

int noit_otlp_initiate_check(noit_module_t *self, noit_check_t *check, int once,
                             noit_check_t *cause)
{
  check->flags |= NP_PASSIVE_COLLECTION;
  if (!check->closure) {
    otlp_closure *ccl = new otlp_closure;
    check->closure = static_cast<void *>(ccl);
    ccl->self = self;
  }
  INITIATE_CHECK(otlp_submit, self, check, cause);
  return 0;
}

int noit_otlp_config(noit_module_t *self, mtev_hash_table *options)
{
  otlp_mod_config *conf;
  conf = static_cast<otlp_mod_config*>(noit_module_get_userdata(self));
  if (conf) {
    if(conf->options) {
      mtev_hash_destroy(conf->options, free, free);
      free(conf->options);
    }
  }
  else {
    conf = make_new_mod_config();
  }
  conf->options = options;
  noit_module_set_userdata(self, conf);
  return 1;
}
