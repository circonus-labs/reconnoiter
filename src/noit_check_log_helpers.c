/*
 * Copyright (c) 2011, OmniTI Computer Consulting, Inc.
 * All rights reserved.
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

#include "noit_defines.h"
#include "noit_check.h"
#include "noit_check_log_helpers.h"
#include <stdio.h>
#include <zlib.h>
#include "utils/noit_b64.h"
#include "utils/noit_str.h"
#include "utils/noit_log.h"
#include "bundle.pb-c.h"

int
noit_check_log_bundle_compress_b64(noit_compression_type_t ctype,
                                   const char *buf_in,
                                   unsigned int len_in,
                                   char ** buf_out,
                                   unsigned int * len_out) {
  uLong initial_dlen, dlen = 0;
  char *compbuff = NULL, *b64buff;

  // Compress saves 25% of space (ex 470 -> 330)
  switch(ctype) {
    case NOIT_COMPRESS_ZLIB:
      /* Compress */
      initial_dlen = dlen = compressBound((uLong)len_in);
      compbuff = malloc(initial_dlen);
      if(!compbuff) return -1;
      if(Z_OK != compress2((Bytef *)compbuff, &dlen,
                           (Bytef *)buf_in, len_in, 9)) {
        noitL(noit_error, "Error compressing bundled metrics.\n");
        free(compbuff);
        return -1;
      }
      break;
    case NOIT_COMPRESS_NONE:
      // Or don't
      dlen = (uLong)len_in;
      compbuff = (char *)buf_in;
      break;
  }

  /* Encode */
  // Problems with the calculation?
  initial_dlen = ((dlen + 2) / 3 * 4);
  b64buff = malloc(initial_dlen);
  if (!b64buff) {
    if(ctype == NOIT_COMPRESS_ZLIB) free(compbuff);
    return -1;
  }
  dlen = noit_b64_encode((unsigned char *)compbuff, dlen,
                         (char *)b64buff, initial_dlen);
  if(ctype == NOIT_COMPRESS_ZLIB) free(compbuff);
  if(dlen == 0) {
    noitL(noit_error, "Error base64'ing bundled metrics.\n");
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
                                     unsigned int len_out) {
  int rv;
  uLong initial_dlen, dlen, rawlen;
  char *compbuff, *rawbuff;

  /* Decode */
  initial_dlen = ((len_in / 4) * 3);
  compbuff = malloc(initial_dlen);
  if (!compbuff) return -1;
  dlen = noit_b64_decode((char *)buf_in, len_in,
                         (unsigned char *)compbuff, initial_dlen);
  if(dlen == 0) {
    noitL(noit_error, "Error base64'ing bundled metrics.\n");
    free(compbuff);
    return -1;
  }

  switch(ctype) {
    case NOIT_COMPRESS_ZLIB:
      /* Decompress */
      rawlen = len_out;
      if(Z_OK != (rv = uncompress((Bytef *)buf_out, &rawlen,
                                  (Bytef *)compbuff, dlen)) ||
         rawlen != len_out) {
        noitL(noit_error, "Error decompressing bundle: %d (%u != %u).\n",
              rv, (unsigned int)rawlen, (unsigned int)len_out);
        free(compbuff);
        return -1;
      }
      break;
    case NOIT_COMPRESS_NONE:
      // Or don't
      rawlen = (uLong)dlen;
      rawbuff = compbuff;
      if(rawlen != len_out) return -1;
      memcpy(buf_out, rawbuff, rawlen);
      break;
  }

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
                      (long long unsigned int)*(m->metric_value.L)); break;      case METRIC_DOUBLE:
        rv = snprintf(b, l, "%.12e", *(m->metric_value.n)); break;
      case METRIC_STRING:
        rv = snprintf(b, l, "%s", m->metric_value.s); break;
      default:
        return -1;
    }
  }
  return rv;
}

int
noit_check_log_b_to_sm(const char *line, int len, char ***out) {
  Bundle *bundle = NULL;
  noit_compression_type_t ctype;
  unsigned int ulen;
  int i, size, cnt = 0, has_status = 0;
  const char *cp1, *cp2, *rest, *error_str = NULL;
  char *timestamp, *uuid_str, *target, *module, *name, *ulen_str;
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
#define SET_FIELD_FROM_BUNDLE(tgt) do { \
  if(*cp1 == '\0') { error_str = "short line @ " #tgt; goto bad_line; } \
  cp2 = strnstrn("\t", 1, cp1, len - (cp1 - line)); \
  if(cp2 == NULL) { error_str = "no tab after " #tgt; goto bad_line; } \
  tgt = (char *)alloca(cp2 - cp1 + 1); \
  if(!tgt) { error_str = "alloca failed for " #tgt; goto bad_line; } \
  memcpy(tgt, cp1, cp2 - cp1); \
  tgt[cp2 - cp1] = '\0'; \
  cp1 = cp2 + 1; \
} while(0)
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
    noitL(noit_error, "bundle decode: memory exhausted\n");
    goto bad_line;
  }
  if(noit_check_log_bundle_decompress_b64(ctype,
                                          rest, len - (rest - line),
                                          (char *)raw_protobuf,
                                          ulen)) {
    noitL(noit_error, "bundle decode: failed to decompress\n");
    goto bad_line;
  }
  /* decode the protobuf */
  bundle = bundle__unpack(&protobuf_c_system_allocator, ulen, raw_protobuf);
  if(!bundle) {
    noitL(noit_error, "bundle decode: protobuf invalid\n");
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
    char scratch[64], *value_str;;
    int value_size = 0;

    m.metric_name = metric->name;
    m.metric_type = metric->metrictype;
    m.metric_value.vp = NULL;
    scratch[0] = '\0';
    value_str = scratch;
    switch(m.metric_type) {
      case METRIC_INT32:
        m.metric_value.i = &metric->valuei32;
        noit_stats_snprint_metric_value(scratch, 64, &m);
        value_size = strlen(scratch);
        break;
      case METRIC_UINT32:
        m.metric_value.I = &metric->valueui32;
        noit_stats_snprint_metric_value(scratch, 64, &m);
        value_size = strlen(scratch);
        break;
      case METRIC_INT64:
        m.metric_value.l = &metric->valuei64;
        noit_stats_snprint_metric_value(scratch, 64, &m);
        value_size = strlen(scratch);
        break;
      case METRIC_UINT64:
        m.metric_value.L = &metric->valueui64;
        noit_stats_snprint_metric_value(scratch, 64, &m);
        value_size = strlen(scratch);
        break;
      case METRIC_DOUBLE:
        m.metric_value.n = &metric->valuedbl;
        noit_stats_snprint_metric_value(scratch, 64, &m);
        value_size = strlen(scratch);
        break;
      case METRIC_STRING:
        m.metric_value.s = metric->valuestr ? metric->valuestr : "[[null]]";
        value_str = metric->valuestr ? metric->valuestr : "[[null]]";
        value_size = strlen(value_str);
        break;
      default:
        break;
    }
    if(value_size == 0) continue; /* WTF, bad metric_type? */

    size = 2 /* M\t */ + strlen(timestamp) + 1 /* \t */ +
           strlen(uuid_str) + 1 /* \t */ + strlen(metric->name) +
           3 /* \t<type>\t */ + value_size + 1 /* \0 */;
    (*out)[i+has_status] = malloc(size);
    snprintf((*out)[i+has_status], size, "M\t%s\t%s\t%s\t%c\t%s",
             timestamp, uuid_str, metric->name, m.metric_type, value_str);
  }
  goto good_line;

 bad_line:
  if(*out) {
    int i;
    for(i=0; i<cnt + has_status; i++) if((*out)[i]) free((*out)[i]);
    free(*out);
    *out = NULL;
  }
  if(error_str) noitL(noit_error, "bundle: bad line due to %s\n", error_str);
 good_line:
  if(bundle) bundle__free_unpacked(bundle, &protobuf_c_system_allocator);
  if(raw_protobuf) free(raw_protobuf);
  return cnt;
}
