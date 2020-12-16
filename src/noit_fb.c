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

#include "noit_fb.h"
#include <mtev_log.h>

static void
flatbuffer_encode_metric(flatcc_builder_t *B, metric_t *m, const uint16_t generation)
{
  char uuid_str[256*3+37];
  int len = sizeof(uuid_str);

  ns(MetricValue_name_create_str(B, m->metric_name));
  ns(MetricValue_generation_add(B, generation));
  if(m->whence.tv_sec || m->whence.tv_usec) {
    uint64_t whence_ms = m->whence.tv_sec * 1000ULL + m->whence.tv_usec / 1000;
    ns(MetricValue_timestamp_add(B, whence_ms));
  }

#define ENCODE_TYPE(FBNAME, FBTYPE, MFIELD)                             \
  if (m->metric_value.MFIELD != NULL) {                                 \
    ns(FBTYPE ## _ref_t) x = ns(FBTYPE ## _create(B, *m->metric_value.MFIELD)); \
    ns(MetricValue_value_ ## FBTYPE ##_add(B, x));                      \
  } else {                                                              \
    ns(AbsentNumericValue_ref_t) x = ns(AbsentNumericValue_create(B));  \
    ns(MetricValue_value_AbsentNumericValue_add(B, x));                 \
  }

  /* any of these types can be null */
  switch(m->metric_type) {
  case METRIC_INT32:
    ENCODE_TYPE(int32, IntValue, i);
    break;
  case METRIC_UINT32:
    ENCODE_TYPE(uint32, UintValue, I);
    break;
  case METRIC_INT64:
    ENCODE_TYPE(int64, LongValue, l);
    break;
  case METRIC_UINT64:
    ENCODE_TYPE(uint64, UlongValue, L);
    break;
  case METRIC_DOUBLE:
    ENCODE_TYPE(dubs, DoubleValue, n);
    break;
  case METRIC_STRING:
    if (m->metric_value.s != NULL) {
      nsc(string_ref_t) x = nsc(string_create(B, m->metric_value.s, strlen(m->metric_value.s)));
      ns(StringValue_ref_t) sv = ns(StringValue_create(B, x));
      ns(MetricValue_value_StringValue_add(B, sv));
    } else {
      ns(AbsentStringValue_ref_t) x = ns(AbsentStringValue_create(B));
      ns(MetricValue_value_AbsentStringValue_add(B, x));
    }
    break;
  case METRIC_HISTOGRAM:
  case METRIC_HISTOGRAM_CUMULATIVE:
    /* This will need to be filled in once histograms are included in the metric_t */
    break;
  case METRIC_ABSENT:
  case METRIC_GUESS:
    break;
  };

#undef ENCODE_TYPE

}

void *
noit_fb_start_metriclist(void)
{
  flatcc_builder_t *builder = malloc(sizeof(flatcc_builder_t));
  flatcc_builder_init(builder);

  ns(MetricList_start_as_root(builder));
  ns(MetricList_metrics_start(builder));
  return builder;
}

void *
noit_fb_finalize_metriclist(void *builder, size_t *out_size)
{
  ns(MetricList_metrics_end((flatcc_builder_t *)builder));
  ns(MetricList_end_as_root((flatcc_builder_t *)builder));
  void *buffer = flatcc_builder_finalize_buffer((flatcc_builder_t *)builder, out_size);
  flatcc_builder_clear((flatcc_builder_t *)builder);
  free(builder);
  return buffer;
}

void *
noit_fb_start_metricbatch(uint64_t whence_ms, const char *check_uuid,
                          const char *check_name, int account_id)
{
  flatcc_builder_t *builder = malloc(sizeof(flatcc_builder_t));
  flatcc_builder_init(builder);

  ns(MetricBatch_start_as_root(builder));
  ns(MetricBatch_timestamp_add)(builder, whence_ms);
  ns(MetricBatch_check_name_create_str(builder, check_name));
  ns(MetricBatch_check_uuid_create_str(builder, check_uuid));
  ns(MetricBatch_account_id_add(builder, account_id));
  ns(MetricBatch_metrics_start(builder));
  return builder;
}

void *
noit_fb_finalize_metricbatch(void *builder, size_t *out_size)
{
  ns(MetricBatch_metrics_end((flatcc_builder_t *)builder));
  ns(MetricBatch_end_as_root((flatcc_builder_t *)builder));
  void *buffer = flatcc_builder_finalize_buffer((flatcc_builder_t *)builder, out_size);
  flatcc_builder_clear((flatcc_builder_t *)builder);
  free(builder);
  return buffer;
}


void
noit_fb_add_metric_to_metriclist(void *builder, uint64_t whence_ms, const char *check_uuid,
                                 const char *check_name, int account_id, metric_t *m, const uint16_t generation)
{
  ns(MetricList_metrics_push_start(builder));
  ns(Metric_timestamp_add)(builder, whence_ms);
  ns(Metric_check_name_create_str(builder, check_name));
  ns(Metric_check_uuid_create_str(builder, check_uuid));
  ns(Metric_account_id_add)(builder, account_id);
  ns(Metric_value_start(builder));
  flatbuffer_encode_metric(builder, m, generation);
  ns(Metric_value_end(builder));
  ns(MetricList_metrics_push_end(builder));
}

void
noit_fb_add_histogram_to_metriclist(void *builder,  uint64_t whence_ms, const char *check_uuid,
                                    const char *check_name, int account_id, const char *name, histogram_t *h,
                                    const uint16_t generation)
{
  ns(MetricList_metrics_push_start(builder));
  ns(Metric_timestamp_add)(builder, whence_ms);
  ns(Metric_check_name_create_str(builder, check_name));
  ns(Metric_check_uuid_create_str(builder, check_uuid));
  ns(Metric_account_id_add)(builder, account_id);
  ns(Metric_value_start(builder));
  ns(MetricValue_name_create_str(builder, name));
  ns(MetricValue_generation_add(builder, generation));
  ns(MetricValue_value_Histogram_start(builder));
  ns(Histogram_buckets_start(builder));
  for (int i = 0; i < hist_bucket_count(h); i++) {
    hist_bucket_t bucket;
    uint64_t count;
    hist_bucket_idx_bucket(h, i, &bucket, &count);
    ns(Histogram_buckets_push_start(builder));
    ns(HistogramBucket_val_add(builder, bucket.val));
    ns(HistogramBucket_exp_add(builder, bucket.exp));
    ns(HistogramBucket_count_add(builder, count));
    ns(Histogram_buckets_push_end(builder));
  }
  ns(Histogram_buckets_end(builder));
  ns(MetricValue_value_Histogram_end(builder));
  ns(Metric_value_end(builder));
  ns(MetricList_metrics_push_end(builder));
}

void
noit_fb_add_metric_to_metricbatch(void *builder, metric_t *m, const uint16_t generation)
{
  ns(MetricBatch_metrics_push_start(builder));
  flatbuffer_encode_metric(builder, m, generation);
  ns(MetricBatch_metrics_push_end(builder));
}

void
noit_fb_add_histogram_to_metricbatch(void *builder, const char *name, histogram_t *h, const uint16_t generation)
{
  ns(MetricBatch_metrics_push_start(builder));
  ns(MetricValue_name_create_str(builder, name));
  ns(MetricValue_generation_add(builder, generation));
  ns(MetricValue_value_Histogram_start(builder));
  ns(Histogram_buckets_start(builder));
  for (int i = 0; i < hist_bucket_count(h); i++) {
    hist_bucket_t bucket;
    uint64_t count;
    hist_bucket_idx_bucket(h, i, &bucket, &count);
    ns(Histogram_buckets_push_start(builder));
    ns(HistogramBucket_val_add(builder, bucket.val));
    ns(HistogramBucket_exp_add(builder, bucket.exp));
    ns(HistogramBucket_count_add(builder, count));
    ns(Histogram_buckets_push_end(builder));
  }
  ns(Histogram_buckets_end(builder));
  ns(MetricValue_value_Histogram_end(builder));
  ns(MetricBatch_metrics_push_end(builder));
}

void *
noit_fb_serialize_metric(uint64_t whence_ms, const char *check_uuid,
                         const char *check_name, int account_id, metric_t *m, const uint16_t generation,
                         size_t *out_size)
{
  flatcc_builder_t builder;
  flatcc_builder_t *B = &builder;
  flatcc_builder_init(B);

  ns(Metric_start_as_root(B));
  ns(Metric_timestamp_add)(B, whence_ms);
  ns(Metric_check_name_create_str(B, check_name));
  ns(Metric_check_uuid_create_str(B, check_uuid));
  ns(Metric_account_id_add)(B, account_id);
  ns(Metric_value_start(B));
  flatbuffer_encode_metric(B, m, generation);
  ns(Metric_value_end(B));
  ns(Metric_end_as_root(B));

  void *buffer = flatcc_builder_finalize_buffer(B, out_size);
  flatcc_builder_clear(B);
  return buffer;
}

void *
noit_fb_serialize_histogram(uint64_t whence_ms, const char *check_uuid,
                            const char *check_name, int account_id, const char *name,
                            histogram_t *h, const uint16_t generation, size_t *out_size)
{
  flatcc_builder_t builder;
  flatcc_builder_t *B = &builder;
  flatcc_builder_init(B);
  ns(Metric_start_as_root(B));
  ns(Metric_timestamp_add)(B, whence_ms);
  ns(Metric_check_name_create_str(B, check_name));
  ns(Metric_check_uuid_create_str(B, check_uuid));
  ns(Metric_account_id_add)(B, account_id);
  ns(Metric_value_start(B));
  ns(MetricValue_name_create_str(B, name));
  ns(MetricValue_generation_add(B, generation));
  ns(MetricValue_value_Histogram_start(B));
  ns(Histogram_buckets_start(B));
  for (int i = 0; i < hist_bucket_count(h); i++) {
    hist_bucket_t bucket;
    uint64_t count;
    hist_bucket_idx_bucket(h, i, &bucket, &count);
    ns(Histogram_buckets_push_start(B));
    ns(HistogramBucket_val_add(B, bucket.val));
    ns(HistogramBucket_exp_add(B, bucket.exp));
    ns(HistogramBucket_count_add(B, count));
    ns(Histogram_buckets_push_end(B));
  }
  ns(Histogram_buckets_end(B));
  ns(MetricValue_value_Histogram_end(B));
  ns(Metric_value_end(B));
  ns(Metric_end_as_root(B));

  void *buffer = flatcc_builder_finalize_buffer(B, out_size);
  flatcc_builder_clear(B);
  return buffer;
}


void *
noit_fb_serialize_metricbatch(uint64_t whence_ms, const char *check_uuid, 
                              const char *check_name, int account_id, metric_t **m,
                              const uint16_t *generation,
                              size_t m_count,
                              size_t* out_size)
{
  flatcc_builder_t builder, *B;
  B = &builder;
  flatcc_builder_init(B);

  ns(MetricBatch_start_as_root(B));
  ns(MetricBatch_timestamp_add)(B, whence_ms);
  ns(MetricBatch_check_name_create_str(B, check_name));
  ns(MetricBatch_check_uuid_create_str(B, check_uuid));
  ns(MetricBatch_account_id_add(B, account_id));
  ns(MetricBatch_metrics_start(B));
  for(size_t i=0; i<m_count; i++) {
    ns(MetricBatch_metrics_push_start(B));
    flatbuffer_encode_metric(B, m[i], generation ? generation[i] : 0);
    ns(MetricBatch_metrics_push_end(B));
  }
  ns(MetricBatch_metrics_end(B));
  ns(MetricBatch_end_as_root(B));
  void *buffer = flatcc_builder_finalize_buffer(B, out_size);
  flatcc_builder_clear(B);
  return buffer;
}

