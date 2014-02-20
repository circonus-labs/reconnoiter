/*
 * Copyright (c) 2007-2012, OmniTI Computer Consulting, Inc.
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
#include "dtrace_probes.h"

#include <uuid/uuid.h>
#include <netinet/in.h>

#include "noit_check.h"
#include "noit_filters.h"
#include "utils/noit_log.h"
#include "jlog/jlog.h"

#include "bundle.pb-c.h"
#include "noit_check_log_helpers.h"

/* Log format is tab delimited:
 * NOIT CONFIG (implemented in noit_conf.c):
 *  'n' TIMESTAMP strlen(xmlconfig) base64(gzip(xmlconfig))
 *
 * DELETE:
 *  'D' TIMESTAMP UUID NAME
 *
 * CHECK:
 *  'C' TIMESTAMP UUID TARGET MODULE NAME
 *
 * STATUS:
 *  'S' TIMESTAMP UUID STATE AVAILABILITY DURATION STATUS_MESSAGE
 *
 * METRICS:
 *  'M' TIMESTAMP UUID NAME TYPE VALUE
 *
 * BUNDLE
 *  'B#' TIMESTAMP UUID TARGET MODULE NAME strlen(base64(gzipped(payload))) base64(gzipped(payload))
 *  
 */

static noit_log_stream_t check_log = NULL;
static noit_log_stream_t status_log = NULL;
static noit_log_stream_t metrics_log = NULL;
static noit_log_stream_t delete_log = NULL;
static noit_log_stream_t bundle_log = NULL;

#define SECPART(a) ((unsigned long)(a)->tv_sec)
#define MSECPART(a) ((unsigned long)((a)->tv_usec / 1000))
#define MAKE_CHECK_UUID_STR(uuid_str, len, ls, check) do { \
  noit_boolean extended_id = noit_false; \
  const char *v; \
  v = noit_log_stream_get_property(ls, "extended_id"); \
  if(v && !strcmp(v, "on")) extended_id = noit_true; \
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
                   int (*log_f)(noit_log_stream_t ls, noit_check_t *check)) {
  noit_log_stream_t ls;
  noit_skiplist_node *curr, *next;
  const char *feed_name;

  if(!check->feeds) return;
  curr = next = noit_skiplist_getlist(check->feeds);
  while(curr) {
    /* We advance next here (before we try to use curr).
     * We may need to remove the node we're looking at and that would
     * disturb the iterator, so advance in advance. */
    noit_skiplist_next(check->feeds, &next);
    feed_name = (char *)curr->data;
    ls = noit_log_stream_find(feed_name);
    if(!ls || log_f(ls, check)) {
      noit_check_transient_remove_feed(check, feed_name);
      /* noit_skiplisti_remove(check->feeds, curr, free); */
    }
    curr = next;
  }
  /* We're done... we may have destroyed the last feed.
   * that combined with transience means we should kill the check */
  /* noit_check_transient_remove_feed(check, NULL); */
}

static int
_noit_check_log_delete(noit_log_stream_t ls,
                       noit_check_t *check) {
  stats_t *c;
  char uuid_str[256*3+37];
  SETUP_LOG(delete, );
  MAKE_CHECK_UUID_STR(uuid_str, sizeof(uuid_str), status_log, check);

  c = &check->stats.current;
  return noit_log(ls, &c->whence, __FILE__, __LINE__,
                  "D\t%lu.%03lu\t%s\t%s\n",
                  SECPART(&c->whence), MSECPART(&c->whence), uuid_str, check->name);
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
_noit_check_log_check(noit_log_stream_t ls,
                      noit_check_t *check) {
  struct timeval __now;
  char uuid_str[256*3+37];
  SETUP_LOG(check, );
  MAKE_CHECK_UUID_STR(uuid_str, sizeof(uuid_str), check_log, check);

  gettimeofday(&__now, NULL);
  return noit_log(ls, &__now, __FILE__, __LINE__,
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
_noit_check_log_status(noit_log_stream_t ls,
                       noit_check_t *check) {
  stats_t *c;
  char uuid_str[256*3+37];
  SETUP_LOG(status, );
  MAKE_CHECK_UUID_STR(uuid_str, sizeof(uuid_str), status_log, check);

  c = &check->stats.current;
  return noit_log(ls, &c->whence, __FILE__, __LINE__,
                  "S\t%lu.%03lu\t%s\t%c\t%c\t%d\t%s\n",
                  SECPART(&c->whence), MSECPART(&c->whence), uuid_str,
                  (char)c->state, (char)c->available, c->duration, c->status);
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
_noit_check_log_metric(noit_log_stream_t ls, noit_check_t *check,
                       const char *uuid_str,
                       struct timeval *whence, metric_t *m) {
  char our_uuid_str[256*3+37];
  int srv = 0;
  if(!noit_apply_filterset(check->filterset, check, m)) return 0;
  if(!N_L_S_ON(ls)) return 0;

  if(!uuid_str) {
    MAKE_CHECK_UUID_STR(our_uuid_str, sizeof(our_uuid_str), metrics_log, check);
    uuid_str = our_uuid_str;
  }

  if(!m->metric_value.s) { /* they are all null */
    srv = noit_log(ls, whence, __FILE__, __LINE__,
                   "M\t%lu.%03lu\t%s\t%s\t%c\t[[null]]\n",
                   SECPART(whence), MSECPART(whence), uuid_str,
                   m->metric_name, m->metric_type);
  }
  else {
    switch(m->metric_type) {
      case METRIC_INT32:
        srv = noit_log(ls, whence, __FILE__, __LINE__,
                       "M\t%lu.%03lu\t%s\t%s\t%c\t%d\n",
                       SECPART(whence), MSECPART(whence), uuid_str,
                       m->metric_name, m->metric_type, *(m->metric_value.i));
        break;
      case METRIC_UINT32:
        srv = noit_log(ls, whence, __FILE__, __LINE__,
                       "M\t%lu.%03lu\t%s\t%s\t%c\t%u\n",
                       SECPART(whence), MSECPART(whence), uuid_str,
                       m->metric_name, m->metric_type, *(m->metric_value.I));
        break;
      case METRIC_INT64:
        srv = noit_log(ls, whence, __FILE__, __LINE__,
                       "M\t%lu.%03lu\t%s\t%s\t%c\t%lld\n",
                       SECPART(whence), MSECPART(whence), uuid_str,
                       m->metric_name, m->metric_type,
                       (long long int)*(m->metric_value.l));
        break;
      case METRIC_UINT64:
        srv = noit_log(ls, whence, __FILE__, __LINE__,
                       "M\t%lu.%03lu\t%s\t%s\t%c\t%llu\n",
                       SECPART(whence), MSECPART(whence), uuid_str,
                       m->metric_name, m->metric_type,
                       (long long unsigned int)*(m->metric_value.L));
        break;
      case METRIC_DOUBLE:
        srv = noit_log(ls, whence, __FILE__, __LINE__,
                       "M\t%lu.%03lu\t%s\t%s\t%c\t%.12e\n",
                       SECPART(whence), MSECPART(whence), uuid_str,
                       m->metric_name, m->metric_type, *(m->metric_value.n));
        break;
      case METRIC_STRING:
        srv = noit_log(ls, whence, __FILE__, __LINE__,
                       "M\t%lu.%03lu\t%s\t%s\t%c\t%s\n",
                       SECPART(whence), MSECPART(whence), uuid_str,
                       m->metric_name, m->metric_type, m->metric_value.s);
        break;
      default:
        noitL(noit_error, "Unknown metric type '%c' 0x%x\n",
              m->metric_type, m->metric_type);
    }
  }
  return srv;
}
static int
_noit_check_log_metrics(noit_log_stream_t ls, noit_check_t *check) {
  int rv = 0;
  int srv;
  char uuid_str[256*3+37];
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *key;
  int klen;
  stats_t *c;
  void *vm;
  SETUP_LOG(metrics, );
  MAKE_CHECK_UUID_STR(uuid_str, sizeof(uuid_str), metrics_log, check);

  c = &check->stats.current;
  while(noit_hash_next(&c->metrics, &iter, &key, &klen, &vm)) {
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


static int
_noit_check_log_bundle_metric(noit_log_stream_t ls, Metric *metric, metric_t *m) {
  metric->metrictype = (int)m->metric_type;

  metric->name = m->metric_name;
  if(m->metric_value.vp != NULL) {
    switch (m->metric_type) {
      case METRIC_INT32:
        metric->has_valuei32 = noit_true;
        metric->valuei32 = *(m->metric_value.i); break;
      case METRIC_UINT32:
        metric->has_valueui32 = noit_true;
        metric->valueui32 = *(m->metric_value.I); break;
      case METRIC_INT64:
        metric->has_valuei64 = noit_true;
        metric->valuei64 = *(m->metric_value.l); break;
      case METRIC_UINT64:
        metric->has_valueui64 = noit_true;
        metric->valueui64 = *(m->metric_value.L); break;
      case METRIC_DOUBLE:
        metric->has_valuedbl = noit_true;
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
noit_check_log_bundle_serialize(noit_log_stream_t ls, noit_check_t *check) {
  int rv = 0;
  static char *ip_str = "ip";
  char uuid_str[256*3+37];
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  noit_hash_iter iter2 = NOIT_HASH_ITER_ZERO;
  const char *key;
  int klen, i=0, size, j;
  unsigned int out_size;
  stats_t *c;
  void *vm;
  char *buf, *out_buf;
  noit_compression_type_t comp;
  Bundle bundle = BUNDLE__INIT;
  SETUP_LOG(bundle, );
  MAKE_CHECK_UUID_STR(uuid_str, sizeof(uuid_str), bundle_log, check);
  noit_boolean use_compression = noit_true;
  const char *v_comp;
  v_comp = noit_log_stream_get_property(ls, "compression");
  if(v_comp && !strcmp(v_comp, "off")) use_compression = noit_false;

  // Get a bundle
  c = &check->stats.current;

  // Set attributes
  bundle.status = malloc(sizeof(Status));
  status__init(bundle.status);
  bundle.status->available = c->available;
  bundle.status->state = c->state;
  bundle.status->duration = c->duration;
  bundle.status->status = c->status;
  bundle.has_period = noit_true;
  bundle.period = check->period;
  bundle.has_timeout = noit_true;
  bundle.timeout = check->timeout;

  bundle.n_metadata = 1;
  bundle.metadata = malloc(sizeof(Metadata*));
  bundle.metadata[0] = malloc(sizeof(Metadata));
  metadata__init(bundle.metadata[0]);
  bundle.metadata[0]->key = ip_str;
  bundle.metadata[0]->value = check->target_ip;

  // Just count
  while(noit_hash_next(&c->metrics, &iter, &key, &klen, &vm)) {
    bundle.n_metrics++;
  }

  if(bundle.n_metrics > 0) {
    bundle.metrics = malloc(bundle.n_metrics * sizeof(Metric*));

    // Now convert
    while(noit_hash_next(&c->metrics, &iter2, &key, &klen, &vm)) {
      /* If we apply the filter set and it returns false, we don't log */
      metric_t *m = (metric_t *)vm;
      if(!noit_apply_filterset(check->filterset, check, m)) continue;
      bundle.metrics[i] = malloc(sizeof(Metric));
      metric__init(bundle.metrics[i]);
      _noit_check_log_bundle_metric(ls, bundle.metrics[i], m);
      if(NOIT_CHECK_METRIC_ENABLED()) {
        char buff[256];
        noit_stats_snprint_metric(buff, sizeof(buff), m);
        NOIT_CHECK_METRIC(uuid_str, check->module, check->name, check->target,
                          m->metric_name, m->metric_type, buff);
      }
      i++;
    }
    bundle.n_metrics = i;
  }

  size = bundle__get_packed_size(&bundle);
  buf = malloc(size);
  bundle__pack(&bundle, (uint8_t*)buf);

  // Compress + B64
  comp = use_compression ? NOIT_COMPRESS_ZLIB : NOIT_COMPRESS_NONE;
  noit_check_log_bundle_compress_b64(comp, buf, size, &out_buf, &out_size);
  rv = noit_log(ls, &(c->whence), __FILE__, __LINE__,
                "B%c\t%lu.%03lu\t%s\t%s\t%s\t%s\t%d\t%.*s\n",
                use_compression ? '1' : '2',
                SECPART(&(c->whence)), MSECPART(&(c->whence)),
                uuid_str, check->target, check->module, check->name, size,
                (unsigned int)out_size, out_buf);

  free(buf);
  free(out_buf);
  // Free all the resources
  for (j=0; j<i; j++) {
    free(bundle.metrics[j]);
  }
  free(bundle.metrics);
  free(bundle.status);
  return rv;
}

void
noit_check_log_bundle(noit_check_t *check) {
  handle_extra_feeds(check, noit_check_log_bundle_serialize);
  if(!(check->flags & (NP_TRANSIENT | NP_SUPPRESS_STATUS | NP_SUPPRESS_METRICS))) {
    SETUP_LOG(bundle, return);
    noit_check_log_bundle_serialize(bundle_log, check);
  }
}

void
noit_check_log_metric(noit_check_t *check, struct timeval *whence,
                      metric_t *m) {
  char uuid_str[256*3+37];
  MAKE_CHECK_UUID_STR(uuid_str, sizeof(uuid_str), metrics_log, check);

  /* handle feeds -- hust like handle_extra_feeds, but this
   * is with different arguments.
   */
  if(check->feeds) {
    noit_skiplist_node *curr, *next;
    curr = next = noit_skiplist_getlist(check->feeds);
    while(curr) {
      const char *feed_name = (char *)curr->data;
      noit_log_stream_t ls = noit_log_stream_find(feed_name);
      noit_skiplist_next(check->feeds, &next);
      if(!ls || _noit_check_log_metric(ls, check, uuid_str, whence, m))
        noit_check_transient_remove_feed(check, feed_name);
      curr = next;
    }
  }
  if(!(check->flags & NP_TRANSIENT)) {
    SETUP_LOG(metrics, return);
    _noit_check_log_metric(metrics_log, check, uuid_str, whence, m);
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

