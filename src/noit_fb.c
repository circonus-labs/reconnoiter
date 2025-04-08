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

#include "flatbuffers/metric_list_reader.h"
#include "flatbuffers/metric_batch_builder.h"
#include "flatbuffers/metric_common_builder.h"
#include "flatbuffers/metric_list_builder.h"

static void
flatbuffer_encode_metric(flatcc_builder_t *B, metric_t *m, const uint16_t generation)
{
  char uuid_str[256*3+37];
  int len = sizeof(uuid_str);

  noit_ns(MetricValue_name_create_str(B, noit_metric_get_full_metric_name(m)));
  noit_ns(MetricValue_samples_start(B));
  noit_ns(MetricValue_samples_push_start(B));
  noit_ns(MetricSample_generation_add(B, generation));
  if(m->whence.tv_sec || m->whence.tv_usec) {
    uint64_t whence_ms = m->whence.tv_sec * 1000ULL + m->whence.tv_usec / 1000;
    noit_ns(MetricSample_timestamp_add(B, whence_ms));
  }
  else {
    noit_ns(MetricSample_timestamp_add(B, 0));
  }

#define ENCODE_TYPE(FBNAME, FBTYPE, MFIELD)                             \
  if (m->metric_value.MFIELD != NULL) {                                 \
    noit_ns(FBTYPE ## _ref_t) x = noit_ns(FBTYPE ## _create(B, *m->metric_value.MFIELD)); \
    noit_ns(MetricSample_value_ ## FBTYPE ##_add(B, x));                      \
  } else {                                                              \
    noit_ns(AbsentNumericValue_ref_t) x = noit_ns(AbsentNumericValue_create(B));  \
    noit_ns(MetricSample_value_AbsentNumericValue_add(B, x));                 \
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
      flatbuffers_string_ref_t x = flatbuffers_string_create(B, m->metric_value.s, strlen(m->metric_value.s));
      noit_ns(StringValue_ref_t) sv = noit_ns(StringValue_create(B, x));
      noit_ns(MetricSample_value_StringValue_add(B, sv));
    } else {
      noit_ns(AbsentStringValue_ref_t) x = noit_ns(AbsentStringValue_create(B));
      noit_ns(MetricSample_value_AbsentStringValue_add(B, x));
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
  noit_ns(MetricValue_samples_push_end(B));
  noit_ns(MetricValue_samples_end(B));

}

void *
noit_fb_start_metriclist(void)
{
  flatcc_builder_t *builder = malloc(sizeof(flatcc_builder_t));
  flatcc_builder_init(builder);

  noit_ns(MetricList_start_as_root(builder));
  noit_ns(MetricList_metrics_start(builder));
  return builder;
}

void *
noit_fb_finalize_metriclist(void *builder, size_t *out_size)
{
  noit_ns(MetricList_metrics_end((flatcc_builder_t *)builder));
  noit_ns(MetricList_end_as_root((flatcc_builder_t *)builder));
  void *buffer = flatcc_builder_finalize_buffer((flatcc_builder_t *)builder, out_size);
  flatcc_builder_clear((flatcc_builder_t *)builder);
  free(builder);
  return buffer;
}

void *
noit_fb_start_metricbatch(uint64_t whence_ms, uuid_t check_uuid,
                          const char *check_name, int account_id)
{
  flatcc_builder_t *builder = malloc(sizeof(flatcc_builder_t));
  flatcc_builder_init(builder);

  noit_ns(MetricBatch_start_as_root(builder));
  noit_ns(MetricBatch_timestamp_add)(builder, whence_ms);
  noit_ns(MetricBatch_check_name_create_str(builder, check_name));
  noit_ns(MetricBatch_check_uuid_create(builder, (uint8_t *)check_uuid, UUID_SIZE));
  noit_ns(MetricBatch_account_id_add(builder, account_id));
  noit_ns(MetricBatch_metrics_start(builder));
  return builder;
}

void *
noit_fb_finalize_metricbatch(void *builder, size_t *out_size)
{
  noit_ns(MetricBatch_metrics_end((flatcc_builder_t *)builder));
  noit_ns(MetricBatch_end_as_root((flatcc_builder_t *)builder));
  void *buffer = flatcc_builder_finalize_buffer((flatcc_builder_t *)builder, out_size);
  flatcc_builder_clear((flatcc_builder_t *)builder);
  free(builder);
  return buffer;
}


void
noit_fb_add_metric_to_metriclist(void *builder, uint64_t whence_ms, uuid_t check_uuid,
                                 const char *check_name, int account_id, metric_t *m, const uint16_t generation)
{
  noit_ns(MetricList_metrics_push_start(builder));
  noit_ns(Metric_timestamp_add)(builder, whence_ms);
  noit_ns(Metric_check_name_create_str(builder, check_name));
  noit_ns(Metric_check_uuid_create(builder, (uint8_t *)check_uuid, UUID_SIZE));
  noit_ns(Metric_account_id_add)(builder, account_id);
  noit_ns(Metric_value_start(builder));
  flatbuffer_encode_metric(builder, m, generation);
  noit_ns(Metric_value_end(builder));
  noit_ns(MetricList_metrics_push_end(builder));
}

void
noit_fb_add_histogram_to_metriclist(void *builder,  uint64_t whence_ms, uuid_t check_uuid,
                                    const char *check_name, int account_id, const char *name, histogram_t *h,
                                    const uint16_t generation)
{
  noit_ns(MetricList_metrics_push_start(builder));
  noit_ns(Metric_timestamp_add)(builder, whence_ms);
  noit_ns(Metric_check_name_create_str(builder, check_name));
  noit_ns(Metric_check_uuid_create(builder, (uint8_t *)check_uuid, UUID_SIZE));
  noit_ns(Metric_account_id_add)(builder, account_id);
  noit_ns(Metric_value_start(builder));
  noit_ns(MetricValue_name_create_str(builder, name));
  noit_ns(MetricValue_samples_start(builder));
  noit_ns(MetricValue_samples_push_start(builder));
  noit_ns(MetricSample_timestamp_add(builder, whence_ms));
  noit_ns(MetricSample_generation_add(builder, generation));
  noit_ns(MetricSample_value_Histogram_start(builder));
  noit_ns(Histogram_buckets_start(builder));
  for (int i = 0; i < hist_bucket_count(h); i++) {
    hist_bucket_t bucket;
    uint64_t count;
    hist_bucket_idx_bucket(h, i, &bucket, &count);
    noit_ns(Histogram_buckets_push_create(builder, count, bucket.val, bucket.exp));
  }
  noit_ns(Histogram_buckets_end(builder));
  noit_ns(MetricSample_value_Histogram_end(builder));
  noit_ns(MetricValue_samples_push_end(builder));
  noit_ns(MetricValue_samples_end(builder));
  noit_ns(Metric_value_end(builder));
  noit_ns(MetricList_metrics_push_end(builder));
}

void
noit_fb_add_metric_to_metricbatch(void *builder, metric_t *m, const uint16_t generation)
{
  noit_ns(MetricBatch_metrics_push_start(builder));
  flatbuffer_encode_metric(builder, m, generation);
  noit_ns(MetricBatch_metrics_push_end(builder));
}

void
noit_fb_add_histogram_to_metricbatch(void *builder, const char *name, histogram_t *h, const uint16_t generation)
{
  noit_ns(MetricBatch_metrics_push_start(builder));
  noit_ns(MetricValue_name_create_str(builder, name));
  noit_ns(MetricValue_samples_start(builder));
  noit_ns(MetricValue_samples_push_start(builder));
  noit_ns(MetricSample_generation_add(builder, generation));
  noit_ns(MetricSample_value_Histogram_start(builder));
  noit_ns(Histogram_buckets_start(builder));
  for (int i = 0; i < hist_bucket_count(h); i++) {
    hist_bucket_t bucket;
    uint64_t count;
    hist_bucket_idx_bucket(h, i, &bucket, &count);
    noit_ns(Histogram_buckets_push_create(builder, count, bucket.val, bucket.exp));
  }
  noit_ns(Histogram_buckets_end(builder));
  noit_ns(MetricSample_value_Histogram_end(builder));
  noit_ns(MetricValue_samples_push_end(builder));
  noit_ns(MetricValue_samples_end(builder));
  noit_ns(MetricBatch_metrics_push_end(builder));
}

void *
noit_fb_serialize_metric(uint64_t whence_ms, uuid_t check_uuid,
                         const char *check_name, int account_id, metric_t *m, const uint16_t generation,
                         size_t *out_size)
{
  flatcc_builder_t builder;
  flatcc_builder_t *B = &builder;
  flatcc_builder_init(B);

  noit_ns(Metric_start_as_root(B));
  noit_ns(Metric_timestamp_add)(B, whence_ms);
  noit_ns(Metric_check_name_create_str(B, check_name));
  noit_ns(Metric_check_uuid_create(B, (uint8_t *)check_uuid, UUID_SIZE));
  noit_ns(Metric_account_id_add)(B, account_id);
  noit_ns(Metric_value_start(B));
  flatbuffer_encode_metric(B, m, generation);
  noit_ns(Metric_value_end(B));
  noit_ns(Metric_end_as_root(B));

  void *buffer = flatcc_builder_finalize_buffer(B, out_size);
  flatcc_builder_clear(B);
  return buffer;
}

void *
noit_fb_serialize_histogram(uint64_t whence_ms, uuid_t check_uuid,
                            const char *check_name, int account_id, const char *name,
                            histogram_t *h, const uint16_t generation, size_t *out_size)
{
  flatcc_builder_t builder;
  flatcc_builder_t *B = &builder;
  flatcc_builder_init(B);
  noit_ns(Metric_start_as_root(B));
  noit_ns(Metric_timestamp_add)(B, whence_ms);
  noit_ns(Metric_check_name_create_str(B, check_name));
  noit_ns(Metric_check_uuid_create(B, (uint8_t *)check_uuid, UUID_SIZE));
  noit_ns(Metric_account_id_add)(B, account_id);
  noit_ns(Metric_value_start(B));
  noit_ns(MetricValue_name_create_str(B, name));
  noit_ns(MetricValue_samples_start(B));
  noit_ns(MetricValue_samples_push_start(B));
  noit_ns(MetricSample_generation_add(B, generation));
  noit_ns(MetricSample_value_Histogram_start(B));
  noit_ns(Histogram_buckets_start(B));
  for (int i = 0; i < hist_bucket_count(h); i++) {
    hist_bucket_t bucket;
    uint64_t count;
    hist_bucket_idx_bucket(h, i, &bucket, &count);
    noit_ns(Histogram_buckets_push_create(B, count, bucket.val, bucket.exp));
  }
  noit_ns(Histogram_buckets_end(B));
  noit_ns(MetricSample_value_Histogram_end(B));
  noit_ns(MetricValue_samples_push_end(B));
  noit_ns(MetricValue_samples_end(B));
  noit_ns(Metric_value_end(B));
  noit_ns(Metric_end_as_root(B));

  void *buffer = flatcc_builder_finalize_buffer(B, out_size);
  flatcc_builder_clear(B);
  return buffer;
}


void *
noit_fb_serialize_metricbatch(uint64_t whence_ms, uuid_t check_uuid, 
                              const char *check_name, int account_id, metric_t **m,
                              const uint16_t *generation,
                              size_t m_count,
                              size_t* out_size)
{
  flatcc_builder_t builder, *B;
  B = &builder;
  flatcc_builder_init(B);

  noit_ns(MetricBatch_start_as_root(B));
  noit_ns(MetricBatch_timestamp_add)(B, whence_ms);
  noit_ns(MetricBatch_check_name_create_str(B, check_name));
  noit_ns(MetricBatch_check_uuid_create(B, (uint8_t *)check_uuid, UUID_SIZE));
  noit_ns(MetricBatch_account_id_add(B, account_id));
  noit_ns(MetricBatch_metrics_start(B));
  for(size_t i=0; i<m_count; i++) {
    noit_ns(MetricBatch_metrics_push_start(B));
    flatbuffer_encode_metric(B, m[i], generation ? generation[i] : 0);
    noit_ns(MetricBatch_metrics_push_end(B));
  }
  noit_ns(MetricBatch_metrics_end(B));
  noit_ns(MetricBatch_end_as_root(B));
  void *buffer = flatcc_builder_finalize_buffer(B, out_size);
  flatcc_builder_clear(B);
  return buffer;
}

