/*
 * Copyright (c) 2007-2012, OmniTI Computer Consulting, Inc.
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

#include <uuid/uuid.h>
#include <netinet/in.h>

#include "noit_dtrace_probes.h"
#include "noit_fb.h"
#include "noit_mtev_bridge.h"
#include "noit_check.h"
#include "noit_filters.h"
#include "bundle.pb-c.h"
#include "noit_check_log_helpers.h"

/* Log format is tab delimited:
 * NOIT CONFIG (implemented in noit_check_log_helpers.c):
 *    'n' TIMESTAMP strlen(xmlconfig) base64(gzip(xmlconfig))
 *  | 'n' BROKER TIMESTAMP strlen(xmlconfig) base64(gzip(xmlconfig))
 *
 * DELETE:
 *    'D' TIMESTAMP UUID NAME
 *  | 'D' BROKER TIMESTAMP UUID NAME
 *
 * CHECK:
 *    'C' TIMESTAMP UUID TARGET MODULE NAME
 *  | 'C' BROKER TIMESTAMP UUID TARGET MODULE NAME
 *
 * STATUS:
 *    'S' TIMESTAMP UUID STATE AVAILABILITY DURATION STATUS_MESSAGE
 *  | 'S' BROKER TIMESTAMP UUID STATE AVAILABILITY DURATION STATUS_MESSAGE
 *
 * METRICS:
 *    'M' TIMESTAMP UUID NAME TYPE VALUE
 *  | 'M' BROKER TIMESTAMP UUID NAME TYPE VALUE
 *
 * BUNDLE:
 *    'B#' TIMESTAMP UUID TARGET MODULE NAME strlen(base64(gzipped(payload))) base64(gzipped(payload))
 *  | 'B#' BROKER TIMESTAMP UUID TARGET MODULE NAME strlen(base64(gzipped(payload))) base64(gzipped(payload))
 *
 *
 * UUID:
 *    lower-cased-uuid
 *  | TARGET`MODULE`NAME`lower-cased-uuid
 *
 * BINARY
 *  'BF' strlen(base64(flatbuffer payload)) base64(flatbuffer payload)
 *
 */

static mtev_log_stream_t check_log = NULL;
static mtev_log_stream_t filterset_log = NULL;
static mtev_log_stream_t status_log = NULL;
static mtev_log_stream_t delete_log = NULL;
static mtev_log_stream_t bundle_log = NULL;
#if defined(NOIT_CHECK_LOG_M)
static mtev_log_stream_t metrics_log = NULL;
#endif

static int
  noit_check_log_bundle_serialize(mtev_log_stream_t, noit_check_t *);
static int
  _noit_check_log_bundle_metric(mtev_log_stream_t, Metric *, metric_t *);

#define METRICS_PER_BUNDLE 500
#define SECPART(a) ((unsigned long)(a)->tv_sec)
#define MSECPART(a) ((unsigned long)((a)->tv_usec / 1000))
#define MAKE_CHECK_UUID_STR(uuid_str, len, ls, check) do { \
  mtev_boolean extended_id = mtev_false; \
  const char *v; \
  v = mtev_log_stream_get_property(ls, "extended_id"); \
  if(v && !strcmp(v, "on")) extended_id = mtev_true; \
  uuid_str[0] = '\0'; \
  if(extended_id) { \
    strlcat(uuid_str, check->target, len-37); \
    strlcat(uuid_str, "`", len-37); \
    strlcat(uuid_str, check->module, len-37); \
    strlcat(uuid_str, "`", len-37); \
    strlcat(uuid_str, check->name, len-37); \
    strlcat(uuid_str, "`", len-37); \
  } \
  uuid_unparse_lower(check->checkid, uuid_str + strlen(uuid_str)); \
} while(0)

static void
handle_extra_feeds(noit_check_t *check,
                   int (*log_f)(mtev_log_stream_t ls, noit_check_t *check)) {
  mtev_log_stream_t ls;
  mtev_skiplist_node *curr, *next;
  const char *feed_name;

  if(!check->feeds) return;
  curr = next = mtev_skiplist_getlist(check->feeds);
  while(curr) {
    /* We advance next here (before we try to use curr).
     * We may need to remove the node we're looking at and that would
     * disturb the iterator, so advance in advance. */
    mtev_skiplist_next(check->feeds, &next);
    feed_name = (char *)mtev_skiplist_data(curr);
    ls = mtev_log_stream_find(feed_name);
    if(!ls || log_f(ls, check)) {
      noit_check_transient_remove_feed(check, feed_name);
      /* mtev_skiplisti_remove(check->feeds, curr, free); */
    }
    curr = next;
  }
  /* We're done... we may have destroyed the last feed.
   * that combined with transience means we should kill the check */
  /* noit_check_transient_remove_feed(check, NULL); */
}

static int
_noit_check_log_delete(mtev_log_stream_t ls,
                       noit_check_t *check) {
  stats_t *c;
  struct timeval *whence;
  char uuid_str[256*3+37];
  SETUP_LOG(delete, );
  MAKE_CHECK_UUID_STR(uuid_str, sizeof(uuid_str), status_log, check);

  c = noit_check_get_stats_current(check);
  whence = noit_check_stats_whence(c, NULL);
  return mtev_log(ls, whence, __FILE__, __LINE__,
                  "D\t%lu.%03lu\t%s\t%s\n",
                  SECPART(whence), MSECPART(whence), uuid_str, check->name);
}
void
noit_check_log_delete(noit_check_t *check) {
  if(!(check->flags & NP_TRANSIENT)) {
    handle_extra_feeds(check, _noit_check_log_delete);
    SETUP_LOG(delete, return);
    _noit_check_log_delete(delete_log, check);
  }
}

static int
_noit_check_log_check(mtev_log_stream_t ls,
                      noit_check_t *check) {
  struct timeval __now;
  char uuid_str[256*3+37];
  SETUP_LOG(check, );
  MAKE_CHECK_UUID_STR(uuid_str, sizeof(uuid_str), check_log, check);

  mtev_gettimeofday(&__now, NULL);
  return mtev_log(ls, &__now, __FILE__, __LINE__,
                  "C\t%lu.%03lu\t%s\t%s\t%s\t%s\n",
                  SECPART(&__now), MSECPART(&__now),
                  uuid_str, check->target, check->module, check->name);
}

void
noit_check_log_check(noit_check_t *check) {
  if(!(check->flags & NP_TRANSIENT)) {
    handle_extra_feeds(check, _noit_check_log_check);
    SETUP_LOG(check, return);
    _noit_check_log_check(check_log, check);
  }
}

static int
_noit_filterset_log_auto_add(mtev_log_stream_t ls,
                             char *filter, noit_check_t *check, metric_t *m, mtev_boolean allow) {
  struct timeval __now;
  char uuid_str[256*3+37];
  SETUP_LOG(check, );
  MAKE_CHECK_UUID_STR(uuid_str, sizeof(uuid_str), check_log, check);

  mtev_gettimeofday(&__now, NULL);
  return mtev_log(ls, &__now, __FILE__, __LINE__,
                  "F1\t%lu.%03lu\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
                  SECPART(&__now), MSECPART(&__now),
                  uuid_str, filter, check->target, check->module, check->name, m->metric_name, allow ? "allow" : "deny");
}

void
noit_filterset_log_auto_add(char *filter, noit_check_t *check, metric_t *m, mtev_boolean allow) {
  SETUP_LOG(filterset, return);
  _noit_filterset_log_auto_add(filterset_log, filter, check, m, allow);
}

static int
_noit_check_log_status(mtev_log_stream_t ls,
                       noit_check_t *check) {
  stats_t *c;
  struct timeval *whence;
  char uuid_str[256*3+37];
  SETUP_LOG(status, );
  MAKE_CHECK_UUID_STR(uuid_str, sizeof(uuid_str), status_log, check);

  c = noit_check_get_stats_current(check);
  whence = noit_check_stats_whence(c, NULL);
  return mtev_log(ls, whence, __FILE__, __LINE__,
                  "S\t%lu.%03lu\t%s\t%c\t%c\t%d\t%s\n",
                  SECPART(whence), MSECPART(whence), uuid_str,
                  (char)noit_check_stats_state(c, NULL),
                  (char)noit_check_stats_available(c, NULL),
                  noit_check_stats_duration(c, NULL),
                  noit_check_stats_status(c, NULL));
}
void
noit_check_log_status(noit_check_t *check) {
  handle_extra_feeds(check, _noit_check_log_status);
  if(!(check->flags & (NP_TRANSIENT | NP_SUPPRESS_STATUS))) {
    SETUP_LOG(status, return);
    _noit_check_log_status(status_log, check);
  }
}

static int
account_id_from_name(const char *check_name)
{
  int account_id = 0;
  char *x = strstr(check_name, "`c_");
  if (x != NULL) {
    /* take advantage of atoi parsing numbers until the first non-number */
    account_id = atoi(x + 3);
  }
  return account_id;
}

static int
noit_check_log_bundle_metric_flatbuffer_serialize_log(mtev_log_stream_t ls,
                                                      noit_check_t *check,
                                                      struct timeval *whence,
                                                      metric_t *m)
{
  int rv = 0;
  char check_name[256 * 3] = {0};
  char uuid_str[UUID_STR_LEN + 1];
  int len = sizeof(check_name);

  static char *ip_str = "ip";

  if(!noit_apply_filterset(check->filterset, check, m)) return 0;
  if(m->logged) return 0;

  const char *v;
  mtev_boolean extended_id = mtev_false;
  v = mtev_log_stream_get_property(ls, "extended_id");
  if(v && !strcmp(v, "on")) extended_id = mtev_true;
  check_name[0] = '\0';
  if(extended_id) {
    strlcat(check_name, check->target, len);
    strlcat(check_name, "`", len);
    strlcat(check_name, check->module, len);
    strlcat(check_name, "`", len);
    strlcat(check_name, check->name, len);
    strlcat(check_name, "`", len);
  }

  uuid_str[0] = '\0';
  uuid_unparse_lower(check->checkid, uuid_str);

  size_t size = 0;
  /* TODO: this is a circonus specific line based on how we name checks
   *
   * Could be a hook?
   */
  int account_id = account_id_from_name(check_name);
  void *buffer = noit_fb_serialize_metricbatch((SECPART(whence) * 1000) + MSECPART(whence), uuid_str, check_name, account_id,
                                               m, 0, &size);

  unsigned int outsize;
  char *outbuf = NULL;
  noit_check_log_bundle_compress_b64(NOIT_COMPRESS_LZ4, buffer, size, &outbuf, &outsize);

  rv = mtev_log(ls, whence, __FILE__, __LINE__,
                "BF\t%d\t%.*s\n", (int)size,
                (unsigned int)outsize, outbuf);
  free(outbuf);
  free(buffer);
  return rv;

}

static int
noit_check_log_bundle_metric_serialize(mtev_log_stream_t ls,
                                       noit_check_t *check,
                                       const struct timeval *whence,
                                       metric_t *m) {
  int size, rv = 0;
  unsigned int out_size;
  static char *ip_str = "ip";
  noit_compression_type_t comp;
  Bundle bundle = BUNDLE__INIT;
  char uuid_str[256*3+37];
  char *buf, *out_buf;
  mtev_boolean use_compression = mtev_true;
  const char *v_comp;

  if(!noit_apply_filterset(check->filterset, check, m)) return 0;
  if(m->logged) return 0;

  MAKE_CHECK_UUID_STR(uuid_str, sizeof(uuid_str), ls, check);
  v_comp = mtev_log_stream_get_property(ls, "compression");
  if(v_comp && !strcmp(v_comp, "off")) use_compression = mtev_false;

  bundle.status = NULL;
  bundle.has_period = mtev_false;
  bundle.has_timeout = mtev_false;

  bundle.n_metadata = 1;
  bundle.metadata = malloc(sizeof(Metadata*));
  bundle.metadata[0] = malloc(sizeof(Metadata));
  metadata__init(bundle.metadata[0]);
  bundle.metadata[0]->key = ip_str;
  bundle.metadata[0]->value = check->target_ip;

  bundle.n_metrics = 1;
  bundle.metrics = malloc(bundle.n_metrics * sizeof(Metric*));

  bundle.metrics[0] = malloc(sizeof(Metric));
  metric__init(bundle.metrics[0]);
  _noit_check_log_bundle_metric(ls, bundle.metrics[0], m);

  if(NOIT_CHECK_METRIC_ENABLED()) {
    char buff[256];
    noit_stats_snprint_metric(buff, sizeof(buff), m);
    NOIT_CHECK_METRIC(uuid_str, check->module, check->name, check->target,
                      m->metric_name, m->metric_type, buff);
  }

  size = bundle__get_packed_size(&bundle);
  buf = malloc(size);
  bundle__pack(&bundle, (uint8_t*)buf);

  // Compress + B64
  comp = use_compression ? NOIT_COMPRESS_ZLIB : NOIT_COMPRESS_NONE;
  noit_check_log_bundle_compress_b64(comp, buf, size, &out_buf, &out_size);
  rv = mtev_log(ls, whence, __FILE__, __LINE__,
                "B%c\t%lu.%03lu\t%s\t%s\t%s\t%s\t%d\t%.*s\n",
                use_compression ? '1' : '2',
                SECPART(whence), MSECPART(whence),
                uuid_str, check->target, check->module, check->name, size,
                (unsigned int)out_size, out_buf);

  free(buf);
  free(out_buf);
  free(bundle.metrics[0]);
  free(bundle.metrics);
  free(bundle.metadata[0]);
  free(bundle.metadata);
  return rv;
}

#if !defined(NOIT_CHECK_LOG_M)
static int
_noit_check_log_metric(mtev_log_stream_t ls, noit_check_t *check,
                       const char *uuid_str,
                       const struct timeval *whence, metric_t *m) {
  return noit_check_log_bundle_metric_serialize(ls, check, whence, m);
}
static int
_noit_check_log_metrics(mtev_log_stream_t ls, noit_check_t *check) {
  return noit_check_log_bundle_serialize(ls, check);
}
void
noit_check_log_metrics(noit_check_t *check) {
  handle_extra_feeds(check, _noit_check_log_metrics);
  if(!(check->flags & (NP_TRANSIENT | NP_SUPPRESS_METRICS))) {
    SETUP_LOG(bundle, return);
    _noit_check_log_metrics(bundle_log, check);
  }
}
#else
static int
_noit_check_log_metric(mtev_log_stream_t ls, noit_check_t *check,
                         const char *uuid_str,
                         struct timeval *whence, metric_t *m) {
  char our_uuid_str[256*3+37];
  int srv = 0;
  if(!noit_apply_filterset(check->filterset, check, m)) return 0;
  if(m->logged) return 0;
  if(!N_L_S_ON(ls)) return 0;

  if(!uuid_str) {
    MAKE_CHECK_UUID_STR(our_uuid_str, sizeof(our_uuid_str), metrics_log, check);
    uuid_str = our_uuid_str;
  }

  if(!m->metric_value.s) { /* they are all null */
    srv = mtev_log(ls, whence, __FILE__, __LINE__,
                   "M\t%lu.%03lu\t%s\t%s\t%c\t[[null]]\n",
                   SECPART(whence), MSECPART(whence), uuid_str,
                   m->metric_name, m->metric_type);
  }
  else {
    switch(m->metric_type) {
      case METRIC_INT32:
        srv = mtev_log(ls, whence, __FILE__, __LINE__,
                       "M\t%lu.%03lu\t%s\t%s\t%c\t%d\n",
                       SECPART(whence), MSECPART(whence), uuid_str,
                       m->metric_name, m->metric_type, *(m->metric_value.i));
        break;
      case METRIC_UINT32:
        srv = mtev_log(ls, whence, __FILE__, __LINE__,
                       "M\t%lu.%03lu\t%s\t%s\t%c\t%u\n",
                       SECPART(whence), MSECPART(whence), uuid_str,
                       m->metric_name, m->metric_type, *(m->metric_value.I));
        break;
      case METRIC_INT64:
        srv = mtev_log(ls, whence, __FILE__, __LINE__,
                       "M\t%lu.%03lu\t%s\t%s\t%c\t%lld\n",
                       SECPART(whence), MSECPART(whence), uuid_str,
                       m->metric_name, m->metric_type,
                       (long long int)*(m->metric_value.l));
        break;
      case METRIC_UINT64:
        srv = mtev_log(ls, whence, __FILE__, __LINE__,
                       "M\t%lu.%03lu\t%s\t%s\t%c\t%llu\n",
                       SECPART(whence), MSECPART(whence), uuid_str,
                       m->metric_name, m->metric_type,
                       (long long unsigned int)*(m->metric_value.L));
        break;
      case METRIC_DOUBLE:
        srv = mtev_log(ls, whence, __FILE__, __LINE__,
                       "M\t%lu.%03lu\t%s\t%s\t%c\t%.12e\n",
                       SECPART(whence), MSECPART(whence), uuid_str,
                       m->metric_name, m->metric_type, *(m->metric_value.n));
        break;
      case METRIC_STRING:
        srv = mtev_log(ls, whence, __FILE__, __LINE__,
                       "M\t%lu.%03lu\t%s\t%s\t%c\t%s\n",
                       SECPART(whence), MSECPART(whence), uuid_str,
                       m->metric_name, m->metric_type, m->metric_value.s);
        break;
      default:
        mtevL(noit_error, "Unknown metric type '%c' 0x%x\n",
              m->metric_type, m->metric_type);
    }
  }
  return srv;
}
static int
_noit_check_log_metrics(mtev_log_stream_t ls, noit_check_t *check) {
  int rv = 0;
  int srv;
  struct timeval *whence;
  char uuid_str[256*3+37];
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  const char *key;
  int klen;
  stats_t *c;
  void *vm;
  SETUP_LOG(metrics, );
  MAKE_CHECK_UUID_STR(uuid_str, sizeof(uuid_str), metrics_log, check);

  c = noit_check_get_stats_current(check);
  whence = noit_check_stats_whence(c, NULL);
  while(mtev_hash_next(&c->metrics, &iter, &key, &klen, &vm)) {
    /* If we apply the filter set and it returns false, we don't log */
    metric_t *m = (metric_t *)vm;
    srv = _noit_check_log_metric(ls, check, uuid_str, &c->whence, m);
    if(srv) rv = srv;
  }
  return rv;
}
void
noit_check_log_metrics(noit_check_t *check) {
  handle_extra_feeds(check, _noit_check_log_metrics);
  if(!(check->flags & (NP_TRANSIENT | NP_SUPPRESS_METRICS))) {
    SETUP_LOG(metrics, return);
    _noit_check_log_metrics(metrics_log, check);
  }
}
#endif

static int
_noit_check_log_bundle_metric(mtev_log_stream_t ls, Metric *metric, metric_t *m) {
  metric->metrictype = (int)m->metric_type;

  metric->name = m->metric_name;
  if(m->metric_value.vp != NULL) {
    switch (m->metric_type) {
      case METRIC_INT32:
        metric->has_valuei32 = mtev_true;
        metric->valuei32 = *(m->metric_value.i); break;
      case METRIC_UINT32:
        metric->has_valueui32 = mtev_true;
        metric->valueui32 = *(m->metric_value.I); break;
      case METRIC_INT64:
        metric->has_valuei64 = mtev_true;
        metric->valuei64 = *(m->metric_value.l); break;
      case METRIC_UINT64:
        metric->has_valueui64 = mtev_true;
        metric->valueui64 = *(m->metric_value.L); break;
      case METRIC_DOUBLE:
        metric->has_valuedbl = mtev_true;
        metric->valuedbl = *(m->metric_value.n); break;
      case METRIC_STRING:
        metric->valuestr = m->metric_value.s; break;
      default:
        return -1;
    }
  }
  return 0;
}

static int
noit_check_log_bundle_fb_serialize(mtev_log_stream_t ls, noit_check_t *check) {
  int rv = 0;
  static char *ip_str = "ip";
  char check_name[256 * 3] = {0};
  char uuid_str[UUID_PRINTABLE_STRING_LENGTH];
  int len = sizeof(check_name);
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  mtev_hash_iter iter2 = MTEV_HASH_ITER_ZERO;
  const char *key;
  int klen, i=0, j;
  unsigned int out_size;
  stats_t *c;
  void *vm;
  struct timeval *whence;
  char *buf, *out_buf;
  mtev_hash_table *metrics;

  c = noit_check_get_stats_current(check);
  whence = noit_check_stats_whence(c, NULL);

  const char *v;
  mtev_boolean extended_id = mtev_false;
  v = mtev_log_stream_get_property(ls, "extended_id");
  if(v && !strcmp(v, "on")) extended_id = mtev_true;
  check_name[0] = '\0';
  if(extended_id) {
    strlcat(check_name, check->target, len);
    strlcat(check_name, "`", len);
    strlcat(check_name, check->module, len);
    strlcat(check_name, "`", len);
    strlcat(check_name, check->name, len);
    strlcat(check_name, "`", len);
  }

  uuid_str[0] = '\0';
  uuid_unparse_lower(check->checkid, uuid_str);

  metrics = noit_check_stats_metrics(c);

  int account_id = account_id_from_name(check_name);
  void *B = noit_fb_start_metricbatch((SECPART(whence) * 1000) + MSECPART(whence), uuid_str, check_name, account_id);

  while(mtev_hash_next(metrics, &iter, &key, &klen, &vm)) {
    /* If we apply the filter set and it returns false, we don't log */
    metric_t *m = (metric_t *)vm;
    if(!noit_apply_filterset(check->filterset, check, m)) continue;
    if(m->logged) continue;
    noit_fb_add_metric_to_metricbatch(B, m, 0);
  }

  size_t fb_size;
  void *buffer = noit_fb_finalize_metricbatch(B, &fb_size);
  char *outbuf;
  unsigned int outsize;
  noit_check_log_bundle_compress_b64(NOIT_COMPRESS_LZ4, buffer, fb_size, &outbuf, &outsize);

  rv = mtev_log(ls, whence, __FILE__, __LINE__,
                "BF\t%d\t%.*s\n", (int)fb_size,
                (unsigned int)outsize, outbuf);
  free(outbuf);
  free(buffer);
  return rv;
}

static int
noit_check_log_bundle_serialize(mtev_log_stream_t ls, noit_check_t *check) {
  int rv = 0;
  static char *ip_str = "ip";
  char uuid_str[256*3+37];
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  mtev_hash_iter iter2 = MTEV_HASH_ITER_ZERO;
  const char *key;
  int klen, i=0, size, j, n_metrics = 0;
  unsigned int out_size;
  stats_t *c;
  void *vm;
  struct timeval *whence;
  char *buf, *out_buf;
  mtev_hash_table *metrics;
  noit_compression_type_t comp;
  SETUP_LOG(bundle, );
  MAKE_CHECK_UUID_STR(uuid_str, sizeof(uuid_str), bundle_log, check);
  mtev_boolean use_compression = mtev_true;
  const char *v_comp, *v_mpb;
  v_comp = mtev_log_stream_get_property(ls, "compression");
  if(v_comp && !strcmp(v_comp, "off")) use_compression = mtev_false;
  v_mpb = mtev_log_stream_get_property(ls, "metrics_per_bundle");
  int metrics_per_bundle = 0;
  if(v_mpb) metrics_per_bundle = atoi(v_mpb);
  if(metrics_per_bundle <= 0) metrics_per_bundle = METRICS_PER_BUNDLE;

  // Get a bundle
  c = noit_check_get_stats_current(check);
  whence = noit_check_stats_whence(c, NULL);

  // Just count
  metrics = noit_check_stats_metrics(c);
  while(mtev_hash_next(metrics, &iter, &key, &klen, &vm)) {
    n_metrics++;
  }

  int n_bundles = ((MAX(n_metrics,1) - 1) / metrics_per_bundle) + 1;
  Bundle *bundles = malloc(n_bundles * sizeof(*bundles));

  for(i=0; i<n_bundles; i++) {
    Bundle *bundle = &bundles[i];
    bundle__init(bundle);
    if(i==0) { // Only the first one gets a status
      bundle->status = malloc(sizeof(Status));
      status__init(bundle->status);
      bundle->status->available = noit_check_stats_available(c, NULL);
      bundle->status->state = noit_check_stats_state(c, NULL);
      bundle->status->duration = noit_check_stats_duration(c, NULL);
      bundle->status->status = (char *)noit_check_stats_status(c, NULL);
      bundle->has_period = mtev_true;
      bundle->period = check->period;
      bundle->has_timeout = mtev_true;
      bundle->timeout = check->timeout;
    }

    // Set attributes
    bundle->n_metadata = 1;
    bundle->metadata = malloc(sizeof(Metadata*));
    bundle->metadata[0] = malloc(sizeof(Metadata));
    metadata__init(bundle->metadata[0]);
    bundle->metadata[0]->key = ip_str;
    bundle->metadata[0]->value = check->target_ip;
    bundle->n_metrics = 0;
    if(n_metrics > 0) {
      /* All bundles have METRICS_PER_BUNDLE except the last,
       * which has the remainder of metrics
       */
      bundle->metrics = calloc(metrics_per_bundle, sizeof(Metric*));
    }
  }

  i = 0;
  if(n_metrics > 0) {
    // Now convert
    while(mtev_hash_next(metrics, &iter2, &key, &klen, &vm)) {
      /* make sure we don't go past our allocation for some reason*/
      if((i / metrics_per_bundle) >= n_bundles) break;

      Bundle *bundle = &bundles[i / metrics_per_bundle];
      int b_i = i % metrics_per_bundle;
      /* If we apply the filter set and it returns false, we don't log */
      metric_t *m = (metric_t *)vm;
      if(!noit_apply_filterset(check->filterset, check, m)) continue;
      if(m->logged) continue;
      bundle->metrics[b_i] = malloc(sizeof(Metric));
      metric__init(bundle->metrics[b_i]);
      _noit_check_log_bundle_metric(ls, bundle->metrics[b_i], m);
      if(NOIT_CHECK_METRIC_ENABLED()) {
        char buff[256];
        noit_stats_snprint_metric(buff, sizeof(buff), m);
        NOIT_CHECK_METRIC(uuid_str, check->module, check->name, check->target,
                          m->metric_name, m->metric_type, buff);
      }
      i++;
      bundle->n_metrics = b_i + 1;
    }
  }

  int rv_sum = 0;
  for(i=0; i<n_bundles; i++) {
    Bundle *bundle = &bundles[i];
    size = bundle__get_packed_size(bundle);
    buf = malloc(size);
    bundle__pack(bundle, (uint8_t*)buf);

    // Compress + B64
    comp = use_compression ? NOIT_COMPRESS_ZLIB : NOIT_COMPRESS_NONE;
    noit_check_log_bundle_compress_b64(comp, buf, size, &out_buf, &out_size);
    rv = mtev_log(ls, whence, __FILE__, __LINE__,
                  "B%c\t%lu.%03lu\t%s\t%s\t%s\t%s\t%d\t%.*s\n",
                  use_compression ? '1' : '2',
                  SECPART(whence), MSECPART(whence),
                  uuid_str, check->target, check->module, check->name, size,
                  (unsigned int)out_size, out_buf);

    free(buf);
    free(out_buf);
    // Free all the resources
    for (j=0; j<bundle->n_metrics; j++) {
      free(bundle->metrics[j]);
    }
    free(bundle->metrics);
    free(bundle->status);
    free(bundle->metadata[0]);
    free(bundle->metadata);
    if(rv < 0) rv_sum = rv;
    else if(rv_sum >= 0) rv_sum += rv;
  }
  free(bundles);
  return rv_sum;
}

void
noit_check_log_bundle(noit_check_t *check) {
  handle_extra_feeds(check, noit_check_log_bundle_serialize);
  if(!(check->flags & (NP_TRANSIENT | NP_SUPPRESS_STATUS | NP_SUPPRESS_METRICS))) {
    SETUP_LOG(bundle, return);
    noit_check_log_bundle_serialize(bundle_log, check);
    //noit_check_log_bundle_fb_serialize(bundle_log, check);
  }
}

void
noit_check_log_metric(noit_check_t *check, const struct timeval *whence,
                      metric_t *m) {
  char uuid_str[256*3+37];
#if defined(NOIT_CHECK_LOG_M)
  MAKE_CHECK_UUID_STR(uuid_str, sizeof(uuid_str), metrics_log, check);
#else
  MAKE_CHECK_UUID_STR(uuid_str, sizeof(uuid_str), bundle_log, check);
#endif

  /* handle feeds -- hust like handle_extra_feeds, but this
   * is with different arguments.
   */
  if(check->feeds) {
    mtev_skiplist_node *curr, *next;
    curr = next = mtev_skiplist_getlist(check->feeds);
    while(curr) {
      const char *feed_name = (char *)mtev_skiplist_data(curr);
      mtev_log_stream_t ls = mtev_log_stream_find(feed_name);
      mtev_skiplist_next(check->feeds, &next);
      if(!ls || _noit_check_log_metric(ls, check, uuid_str, whence, m))
        noit_check_transient_remove_feed(check, feed_name);
      curr = next;
    }
  }
  if(!(check->flags & NP_TRANSIENT)) {
#if defined(NOIT_CHECK_LOG_M)
    SETUP_LOG(metrics, return);
    _noit_check_log_metric(metrics_log, check, uuid_str, whence, m);
#else
    SETUP_LOG(bundle, return);
    _noit_check_log_metric(bundle_log, check, uuid_str, whence, m);
#endif
    if(NOIT_CHECK_METRIC_ENABLED()) {
      char buff[256];
      noit_stats_snprint_metric(buff, sizeof(buff), m);
      NOIT_CHECK_METRIC(uuid_str, check->module, check->name, check->target,
                        m->metric_name, m->metric_type, buff);
    }
  }
}

int
noit_stats_snprint_metric(char *b, int l, metric_t *m) {
  int rv, nl;
  nl = snprintf(b, l, "%s[%c] = ", m->metric_name, m->metric_type);
  if(nl >= l || nl <= 0) return nl;
  rv = noit_stats_snprint_metric_value(b+nl, l-nl, m);
  if(rv == -1)
    rv = snprintf(b+nl, l-nl, "[[unknown type]]");
  return rv + nl;
}
