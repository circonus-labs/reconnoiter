/*
 * Copyright (c) 2011, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2015-2017, Circonus, Inc. All rights reserved.
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
 *     * Neither the name OmniTI Computer Consulting, Inc. nor the names
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
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <mtev_defines.h>

#include <stdio.h>

#include <mtev_b64.h>
#include <mtev_str.h>
#include <mtev_log.h>
#include <mtev_dyn_buffer.h>
#include <mtev_conf.h>
#include <mtev_compress.h>

#include "noit_mtev_bridge.h"
#include "bundle.pb-c.h"
#include "noit_metric.h"
#include "noit_check_log_helpers.h"
#include "flatbuffers/metric_reader.h"
#include "flatbuffers/metric_batch_reader.h"
#include "flatbuffers/metric_batch_verifier.h"
#include "noit_message_decoder.h"

static void *__c_allocator_alloc(void *d, size_t size) {
  return malloc(size);
}
static void __c_allocator_free(void *d, void *p) {
  free(p);
}
static ProtobufCAllocator __c_allocator = {
  .alloc = __c_allocator_alloc,
  .free = __c_allocator_free,
  .allocator_data = NULL
};
#define protobuf_c_system_allocator __c_allocator

#undef ns
#define ns(x) FLATBUFFERS_WRAP_NAMESPACE(circonus, x)
#undef nsc
#define nsc(x) FLATBUFFERS_WRAP_NAMESPACE(flatbuffers, x)


int
noit_check_log_bundle_compress_b64(noit_compression_type_t ctype,
                                   const char *buf_in,
                                   unsigned int len_in,
                                   char ** buf_out,
                                   unsigned int * len_out) {
  size_t initial_dlen, dlen = 0;
  char *compbuff = NULL, *b64buff;
  mtev_compress_type ct = MTEV_COMPRESS_NONE;

  // Compress saves 25% of space (ex 470 -> 330)
  switch(ctype) {
    case NOIT_COMPRESS_ZLIB:
      ct = MTEV_COMPRESS_GZIP;
      break;
    case NOIT_COMPRESS_LZ4:
      ct = MTEV_COMPRESS_LZ4F;
      break;
    case NOIT_COMPRESS_NONE:
      ct = MTEV_COMPRESS_NONE;
      break;
  }

  /* Compress */
  if(0 != mtev_compress(ct, buf_in, len_in,
                        (unsigned char **)&compbuff, &dlen)) {
    mtevL(noit_error, "Error compressing bundled metrics.\n");
    free(compbuff);
    return -1;
  }

  /* Encode */
  // Problems with the calculation?
  initial_dlen = ((dlen + 2) / 3 * 4);
  b64buff = malloc(initial_dlen);
  if (!b64buff) {
    free(compbuff);
    return -1;
  }
  dlen = mtev_b64_encode((unsigned char *)compbuff, dlen,
                         (char *)b64buff, initial_dlen);
  free(compbuff);
  if(dlen == 0) {
    mtevL(noit_error, "Error base64'ing bundled metrics.\n");
    free(b64buff);
    return -1;
  }
  *buf_out = b64buff;
  *len_out = (unsigned int)dlen;
  return 0;
}

int
noit_check_log_bundle_decompress_b64(noit_compression_type_t ctype,
                                     const char *buf_in,
                                     unsigned int len_in,
                                     char *buf_out,
                                     unsigned int *len_out) {
  int rv = 0;
  size_t initial_dlen, dlen, rawlen;
  char *compbuff, *rawbuff;

  /* Decode */
  initial_dlen = (((len_in + 3) / 4) * 3);
  compbuff = malloc(initial_dlen);
  if (!compbuff) return -1;
  dlen = mtev_b64_decode((char *)buf_in, len_in,
                         (unsigned char *)compbuff, initial_dlen);
  if(dlen == 0) {
    mtevL(noit_error, "Error base64'ing bundled metrics.\n");
    free(compbuff);
    return -1;
  }

  mtev_stream_decompress_ctx_t *ctx = mtev_create_stream_decompress_ctx();

  switch(ctype) {
    case NOIT_COMPRESS_ZLIB:
      mtev_stream_decompress_init(ctx, MTEV_COMPRESS_GZIP);
      break;
    case NOIT_COMPRESS_LZ4:
      mtev_stream_decompress_init(ctx, MTEV_COMPRESS_LZ4F);
      break;
    case NOIT_COMPRESS_NONE:
      mtev_stream_decompress_init(ctx, MTEV_COMPRESS_NONE);
      break;
  }

  size_t size_t_len_out = *len_out;
  if (0 != mtev_stream_decompress(ctx, (const unsigned char *)compbuff, &dlen, (unsigned char *)buf_out, (size_t *)&size_t_len_out)) {
    mtevL(noit_error, "Failed to decompress b64 encoded chunk\n");
    if(compbuff) free(compbuff);
    return -1;
  }
  *len_out = size_t_len_out;
  free(compbuff);

  mtev_stream_decompress_finish(ctx);
  mtev_destroy_stream_decompress_ctx(ctx);

  return 0;
}

int
noit_stats_snprint_metric_value(char *b, int l, metric_t *m) {
  int rv;
  if(!m->metric_value.s) { /* they are all null */
    rv = snprintf(b, l, "[[null]]");
  }
  else {
    switch(m->metric_type) {
      case METRIC_INT32:
        rv = snprintf(b, l, "%d", *(m->metric_value.i)); break;
      case METRIC_UINT32:
        rv = snprintf(b, l, "%u", *(m->metric_value.I)); break;
      case METRIC_INT64:
        rv = snprintf(b, l, "%lld", (long long int)*(m->metric_value.l)); break;
      case METRIC_UINT64:
        rv = snprintf(b, l, "%llu",
                      (long long unsigned int)*(m->metric_value.L)); break;
      case METRIC_DOUBLE:
        rv = snprintf(b, l, "%.12e", *(m->metric_value.n)); break;
      case METRIC_STRING:
        rv = snprintf(b, l, "%s", m->metric_value.s); break;
      default:
        return -1;
    }
  }
  return rv;
}

static int
noit_check_log_b12_to_sm(const char *line, int len, char ***out, int noit_ip, noit_compression_type_t ctype) {
  Bundle *bundle = NULL;
  unsigned int ulen;
  int i, size, cnt = 0, has_status = 0;
  const char *cp1, *cp2, *rest, *error_str = NULL;
  char *timestamp, *uuid_str, *target, *module, *name, *ulen_str, *nipstr = NULL;
  unsigned char *raw_protobuf = NULL;

  *out = NULL;
  if(len < 3) return 0;
  if(line[0] != 'B' || line[2] != '\t') return 0;
  switch(line[1]) {
    case '1': ctype = NOIT_COMPRESS_ZLIB; break;
    case '2': ctype = NOIT_COMPRESS_NONE; break;
    default: return 0;
  }

  /* All good, and we're off to the races */
  line += 3; len -= 3;
  cp1 = line;

  if(noit_ip == -1) {
    /* auto-detect */
    noit_ip = !noit_is_timestamp(line, len);
  }

#define SET_FIELD_FROM_BUNDLE(tgt) do { \
  if(*cp1 == '\0') { error_str = "short line @ " #tgt; goto bad_line; } \
  cp2 = mtev_memmem(cp1, len - (cp1 - line), "\t", 1); \
  if(cp2 == NULL) { error_str = "no tab after " #tgt; goto bad_line; } \
  tgt = (char *)alloca(cp2 - cp1 + 1); \
  if(!tgt) { error_str = "alloca failed for " #tgt; goto bad_line; } \
  memcpy(tgt, cp1, cp2 - cp1); \
  tgt[cp2 - cp1] = '\0'; \
  cp1 = cp2 + 1; \
} while(0)
  if(noit_ip > 0) SET_FIELD_FROM_BUNDLE(nipstr);
  SET_FIELD_FROM_BUNDLE(timestamp);
  SET_FIELD_FROM_BUNDLE(uuid_str);
  SET_FIELD_FROM_BUNDLE(target);
  SET_FIELD_FROM_BUNDLE(module);
  SET_FIELD_FROM_BUNDLE(name);
  SET_FIELD_FROM_BUNDLE(ulen_str);
  rest = cp1;

  ulen = strtoul(ulen_str, NULL, 10);
  raw_protobuf = malloc(ulen);
  if(!raw_protobuf) {
    mtevL(noit_error, "bundle decode: memory exhausted\n");
    goto bad_line;
  }
  if(noit_check_log_bundle_decompress_b64(ctype,
                                          rest, len - (rest - line),
                                          (char *)raw_protobuf,
                                          &ulen)) {
    mtevL(noit_error, "bundle decode: failed to decompress\n");
    goto bad_line;
  }
  /* decode the protobuf */
  bundle = bundle__unpack(&protobuf_c_system_allocator, ulen, raw_protobuf);
  if(!bundle) {
    mtevL(noit_error, "bundle decode: protobuf invalid: %s\n", uuid_str);
    goto bad_line;
  }
  has_status = bundle->status ? 1 : 0;
  cnt = bundle->n_metrics;
  *out = calloc(sizeof(**out), cnt + has_status);
  if(!*out) { error_str = "memory exhaustion"; goto bad_line; }
  if(has_status) {
    Status *status = bundle->status;
    /* build out status line */
    size = 2 /* S\t */ + strlen(timestamp) + 1 /* \t */ + strlen(uuid_str) +
           5 /* \tG\tA\t */ + 11 /* max(strlen(duration)) */ +
           1 /* \t */ +
           (status->status ? strlen(status->status) : 8 /* [[null]] */) +
           1 /* \0 */;
    **out = malloc(size);
    snprintf(**out, size, "S\t%s\t%s\t%c\t%c\t%d\t%s",
             timestamp, uuid_str, status->state, status->available,
             status->duration, status->status ? status->status : "[[null]]");
  }
  /* build our metric lines */
  for(i=0; i<cnt; i++) {
    Metric *metric = bundle->metrics[i];
    metric_t m;
    char scratch[64], *value_str;
    int value_size = 0;

    memset(&m, 0, sizeof(m));
    m.metric_name = metric->name;
    m.metric_type = metric->metrictype;
    m.metric_value.vp = NULL;
    if(metric->has_whence_ms) {
      m.whence.tv_sec = metric->whence_ms / 1000;
      m.whence.tv_usec = 1000 * (metric->whence_ms % 1000);
    }
    scratch[0] = '\0';
    value_str = scratch;
    switch(m.metric_type) {
#define CHECK_VALUE_TYPE(metric_type, src, dst) \
    case metric_type: \
      if(metric->has_##src == 0) { \
        value_str = "[[null]]"; \
        m.metric_value.s = value_str; \
        value_size = strlen(value_str); \
      } else { \
        dst = &metric->src;\
        noit_stats_snprint_metric_value(scratch, 64, &m);\
        value_size = strlen(scratch);\
      } \
      break;

      CHECK_VALUE_TYPE(METRIC_INT32, valuei32, m.metric_value.i)
      CHECK_VALUE_TYPE(METRIC_UINT32, valueui32, m.metric_value.I)
      CHECK_VALUE_TYPE(METRIC_INT64, valuei64, m.metric_value.l)
      CHECK_VALUE_TYPE(METRIC_UINT64, valueui64, m.metric_value.L)
      CHECK_VALUE_TYPE(METRIC_DOUBLE, valuedbl, m.metric_value.n);
      case METRIC_STRING:
        value_str = metric->valuestr ? metric->valuestr : "[[null]]";
        m.metric_value.s = value_str;
        value_size = strlen(value_str);
        break;
      default:
        break;
    }
    if(value_size == 0 && m.metric_type != METRIC_STRING) continue; /* WTF, bad metric_type? */

    char *ltimestamp = timestamp;
    char mltimestamp[32];
    if(m.whence.tv_sec || m.whence.tv_usec) {
      snprintf(mltimestamp, sizeof(mltimestamp), "%zu.%03u", (size_t)m.whence.tv_sec,
               (unsigned int)(m.whence.tv_usec / 1000));
      ltimestamp = mltimestamp;
    }

    size = 2 /* M\t */ + strlen(ltimestamp) + 1 /* \t */ +
           strlen(uuid_str) + 1 /* \t */ + strlen(metric->name) +
           3 /* \t<type>\t */ + value_size + 1 /* \0 */;
    (*out)[i+has_status] = malloc(size);
    snprintf((*out)[i+has_status], size, "M\t%s\t%s\t%s\t%c\t%s",
             ltimestamp, uuid_str, metric->name, m.metric_type, value_str);
  }
  goto good_line;

 bad_line:
  if(*out) {
    int i;
    for(i=0; i<cnt + has_status; i++) if((*out)[i]) free((*out)[i]);
    free(*out);
    *out = NULL;
  }
  if(error_str) {
    mtevL(noit_error, "bundle: bad line '%.*s' due to %s\n", len, line, error_str);
    cnt = 0;
    has_status = 0;
  }

 good_line:
  if(bundle) bundle__free_unpacked(bundle, &protobuf_c_system_allocator);
  if(raw_protobuf) free(raw_protobuf);

  return cnt + has_status;
}

static int 
noit_check_log_bf_to_sm(const char *line, int len, char ***out, int noit_ip)
{
  unsigned int ulen;
  int i, size, has_status = 0;
  const char *cp1, *cp2, *rest, *error_str = NULL;
  char *target, *module, *name, *ulen_str, *nipstr = NULL;
  unsigned char *raw_data = NULL;
  const char *value_str;
  size_t value_size;
  size_t metrics_len = 0;
  char scratch[64];

  *out = NULL;
  if(len < 3) return 0;
  if(line[0] != 'B' || line[1] != 'F' || line[2] != '\t') return 0;

  line += 3; len -= 3;
  cp1 = line;
#define SET_FIELD_FROM_BUNDLE(tgt) do { \
  if(*cp1 == '\0') { error_str = "short line @ " #tgt; goto bad_line; } \
  cp2 = mtev_memmem(cp1, len - (cp1 - line), "\t", 1); \
  if(cp2 == NULL) { error_str = "no tab after " #tgt; goto bad_line; } \
  tgt = (char *)alloca(cp2 - cp1 + 1); \
  if(!tgt) { error_str = "alloca failed for " #tgt; goto bad_line; } \
  memcpy(tgt, cp1, cp2 - cp1); \
  tgt[cp2 - cp1] = '\0'; \
  cp1 = cp2 + 1; \
} while(0)
  SET_FIELD_FROM_BUNDLE(ulen_str);
  rest = cp1;
  
  ulen = strtoul(ulen_str, NULL, 10);
  raw_data = malloc(ulen);
  if(!raw_data) {
    mtevL(noit_error, "bundle decode: memory exhausted\n");
    goto bad_line;
  }
  if(noit_check_log_bundle_decompress_b64(NOIT_COMPRESS_LZ4,
                                          rest, len - (rest - line),
                                          (char *)raw_data,
                                          &ulen)) {
    mtevL(noit_error, "bundle decode: failed to decompress\n");
    abort();
    goto bad_line;
  }

  /* flatbuffers reader */
  int fb_ret = ns(MetricBatch_verify_as_root(raw_data, ulen));
  if(fb_ret != 0) {
    mtevL(mtev_error, "Corrupt metric batch flatbuffer: %s\n", flatcc_verify_error_string(fb_ret));
    goto bad_line;
  }
  ns(MetricBatch_table_t) message = ns(MetricBatch_as_root(raw_data));

  mtevAssert(message != NULL);

  uint64_t timestamp = ns(MetricBatch_timestamp(message));
  flatbuffers_string_t check_name = ns(MetricBatch_check_name(message));
  flatbuffers_string_t check_uuid = ns(MetricBatch_check_uuid(message));
  int account_id = ns(MetricBatch_account_id(message));
  ns(MetricValue_vec_t) metrics = ns(MetricBatch_metrics(message));
  metrics_len = ns(MetricValue_vec_len(metrics));
  
  *out = calloc(sizeof(**out), metrics_len);
  if(!*out) { error_str = "memory exhaustion"; goto bad_line; }
 
  mtev_dyn_buffer_t uuid_str;
  mtev_dyn_buffer_init(&uuid_str); 
  for (int i = 0; i < metrics_len; i++) {
    ns(MetricValue_table_t) m = ns(MetricValue_vec_at(metrics, i));
    flatbuffers_string_t metric_name = ns(MetricValue_name(m));
    char ts[22];
    size_t uuid_len = flatbuffers_string_len(check_name) + flatbuffers_string_len(check_uuid) + 2;
    uint64_t ltimestamp = ns(MetricValue_timestamp(m));

    if(ltimestamp == 0) ltimestamp = timestamp;
    snprintf(ts, sizeof(ts), "%"PRIu64".%03u", ltimestamp / 1000 , (unsigned int)(ltimestamp % 1000));

    mtev_dyn_buffer_ensure(&uuid_str, uuid_len);
    mtev_dyn_buffer_reset(&uuid_str);
    mtev_dyn_buffer_add_printf(&uuid_str, "%s`%s", check_name, check_uuid);
    char type = 'x';

    value_str = scratch;
    scratch[0] = '\0';
    switch(ns(MetricValue_value_type(m))) {
    case ns(MetricValueUnion_IntValue):
      {
        type = 'i';
        ns(IntValue_table_t) v = ns(MetricValue_value(m));
        snprintf(scratch, sizeof(scratch), "%d", ns(IntValue_value(v)));
        break;
      }
    case ns(MetricValueUnion_UintValue):
      {
        type = 'I';
        ns(UintValue_table_t) v = ns(MetricValue_value(m));
        snprintf(scratch, sizeof(scratch), "%u", ns(UintValue_value(v)));
        break;
      }
    case ns(MetricValueUnion_LongValue):
      {
        type = 'l';
        ns(LongValue_table_t) v = ns(MetricValue_value(m));
        snprintf(scratch, sizeof(scratch), "%lld", (long long)ns(LongValue_value(v)));
        break;
      }
    case ns(MetricValueUnion_UlongValue):
      {
        type = 'L';
        ns(UlongValue_table_t) v = ns(MetricValue_value(m));
        snprintf(scratch, sizeof(scratch), "%llu", (unsigned long long)ns(UlongValue_value(v)));
        break;
      }
    case ns(MetricValueUnion_DoubleValue):
      {
        type = 'n';
        ns(DoubleValue_table_t) v = ns(MetricValue_value(m));
        snprintf(scratch, sizeof(scratch), "%0.6f", ns(DoubleValue_value(v)));
        break;
      }
    case ns(MetricValueUnion_AbsentNumericValue):
      {
        type = 'n';
        snprintf(scratch, sizeof(scratch), "[[null]]");
        break;
      }
    case ns(MetricValueUnion_StringValue):
      {
        type = 's';
        ns(StringValue_table_t) v = ns(MetricValue_value(m));
        value_str = ns(StringValue_value(v));
        break;
      }
    case ns(MetricValueUnion_AbsentStringValue):
      {
        type = 's';
        snprintf(scratch, sizeof(scratch), "[[null]]");
        break;
      }
    };
    if(type == 'x') continue;

    value_size = strlen(value_str);

    size = 2 /* M\t */ + strlen(ts) + 1 /* \t */ +
      mtev_dyn_buffer_used(&uuid_str) + 1 /* \t */ + flatbuffers_string_len(metric_name) +
           3 /* \t<type>\t */ + value_size + 1 /* \0 */;
    (*out)[i+has_status] = malloc(size);
    snprintf((*out)[i], size, "M\t%s\t%.*s\t%s\t%c\t%s",
             ts, (int)mtev_dyn_buffer_used(&uuid_str), mtev_dyn_buffer_data(&uuid_str),
             metric_name, type, value_str);
  }

  return metrics_len + has_status;

 bad_line:
  if(*out) {
    /* bad_line is not called during the loop through the values
     * so there is no need to free (*out)[i] malloc'd for each row
     */
    *out = NULL;
  }
  if(error_str) mtevL(noit_error, "bundle: bad line due to %s\n", error_str);

  return 0; 
 
}

int
noit_check_log_b_to_sm(const char *line, int len, char ***out, int noit_ip) 
{
  if(len < 3) return 0;
  if(line[0] != 'B' || line[2] != '\t') return 0;
  switch(line[1]) {
    case '1':
      return noit_check_log_b12_to_sm(line, len, out, noit_ip, NOIT_COMPRESS_ZLIB);
    case '2':
      return noit_check_log_b12_to_sm(line, len, out, noit_ip, NOIT_COMPRESS_NONE);
    case 'F':
      return noit_check_log_bf_to_sm(line, len, out, noit_ip);
    default: return 0;
  }
}

int
noit_conf_write_log() {
  static uint32_t last_write_gen = 0;
  static mtev_log_stream_t config_log = NULL;
  struct timeval __now;
  mtev_boolean notify_only = mtev_false;
  const char *v;

  if(!mtev_log_stream_exists("config")) return -1;

  SETUP_LOG(config, return -1);
  if(!N_L_S_ON(config_log)) return 0;

  v = mtev_log_stream_get_property(config_log, "notify_only");
  if(v && (!strcmp(v, "on") || !strcmp(v, "true"))) notify_only = mtev_true;

  /* We know we haven't changed */
  if(last_write_gen == mtev_conf_config_gen()) return 0;
  mtev_gettimeofday(&__now, NULL);

  if(notify_only) {
    mtevL(config_log, "n\t%lu.%03lu\t%d\t\n",
          (unsigned long int)__now.tv_sec,
          (unsigned long int)__now.tv_usec / 1000UL, 0);
    last_write_gen = mtev_conf_config_gen();
    return 0;
  }

  size_t raw_len, buff_len;
  char *buff = mtev_conf_enc_in_mem(&raw_len, &buff_len, CONFIG_B64, mtev_true);
  if(buff == NULL) return -1;
  mtevL(config_log, "n\t%lu.%03lu\t%d\t%.*s\n",
        (unsigned long int)__now.tv_sec,
        (unsigned long int)__now.tv_usec / 1000UL, (int)raw_len,
        (int)buff_len, buff);
  free(buff);
  last_write_gen = mtev_conf_config_gen();
  return 0;
}
