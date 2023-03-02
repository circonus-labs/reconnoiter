/*
 * Copyright (c) 2018, Circonus, Inc. All rights reserved.
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

#include <mtev_rand.h>
#include <mtev_rest.h>
#include <mtev_hash.h>
#include <mtev_json.h>
#include <mtev_uuid.h>
#include <mtev_b64.h>
#include <mtev_dyn_buffer.h>

#include <circllhist.h>

#include <snappy/snappy.h>

#include "noit_metric.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_mtev_bridge.h"
#include "prometheus.pb-c.h"

static mtev_log_stream_t nlerr = NULL;
static mtev_log_stream_t nldeb = NULL;

static pthread_mutex_t batch_flush_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct _mod_config {
  mtev_hash_table *options;
} prometheus_mod_config_t;

typedef struct prometheus_closure_s {
  noit_module_t *self;
} prometheus_closure_t;

struct value_list {
  char *v;
  struct value_list *next;
};

static const char *_allowed_units[] = {
  "seconds",
  "requests",
  "responses",
  "transactions",
  "packetes",
  "bytes",
  "octets",
  NULL
};

typedef struct prometheus_upload
{
  mtev_dyn_buffer_t data;
  mtev_boolean complete;
  noit_check_t *check;
  uuid_t check_id;
  struct timeval start_time;
  mtev_hash_table *immediate_metrics;
  mtev_hash_table *hists;
  histogram_approx_mode_t histogram_mode;
  bool coerce_histograms;
  bool extract_units;
  const char **allowed_units;
  char *units_str;
} prometheus_upload_t;

#define READ_CHUNK 32768

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

struct hist_in_progress {
  histogram_adhoc_bin_t *bins;
  int nbins;
  int nallocdbins;
  struct timeval whence;
  char name[MAX_METRIC_TAGGED_NAME];
};

static void
hist_in_progress_free(void *vhip) {
  struct hist_in_progress *hip = vhip;
  free(hip->bins);
  free(hip);
}

static void
free_prometheus_upload(void *pul)
{
  prometheus_upload_t *p = (prometheus_upload_t *)pul;
  mtev_dyn_buffer_destroy(&p->data);
  mtev_memory_begin();
  mtev_hash_destroy(p->immediate_metrics, NULL, mtev_memory_safe_free);
  mtev_memory_end();
  free(p->immediate_metrics);
  mtev_hash_destroy(p->hists, NULL, hist_in_progress_free);
  free(p->hists);
  free(p->allowed_units);
  free(p->units_str);
  free(p);
}

static prometheus_upload_t *
rest_get_upload(mtev_http_rest_closure_t *restc, int *mask, int *complete)
{
  prometheus_upload_t *rxc;
  mtev_http_request *req = mtev_http_session_request(restc->http_ctx);

  rxc = (prometheus_upload_t *)restc->call_closure;

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

static int prometheus_submit(noit_module_t *self, noit_check_t *check,
                             noit_check_t *cause)
{
  prometheus_closure_t *ccl;
  struct timeval duration;
  /* We are passive, so we don't do anything for transient checks */
  if(check->flags & NP_TRANSIENT) return 0;

  if(!check->closure) {
    ccl = check->closure = (void *)calloc(1, sizeof(prometheus_closure_t));
    ccl->self = self;
  } else {
    // Don't count the first run
    struct timeval now;
    char human_buffer[256];
    int stats_count = 0;

    mtev_memory_begin();
    stats_t *s = noit_check_get_stats_inprogress(check);
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
    mtevL(nldeb, "prometheus(%s) [%s]\n", check->target, human_buffer);

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

static bool is_standard_suffix(const char *suffix) {
  return !strcmp(suffix, "count") || !strcmp(suffix, "sum") || !strcmp(suffix, "bucket") ||
         !strcmp(suffix, "total");
}
static const char *units_suffix(prometheus_upload_t *rxc, const char *in) {
  const char **allowed_units = (rxc && rxc->allowed_units) ? rxc->allowed_units : _allowed_units;
  for(int i=0; allowed_units[i]; i++) {
    mtevL(mtev_error, "ALLOWED: %s\n", allowed_units[i]);
    if(!strcmp(allowed_units[i], in)) return allowed_units[i];
  }
  return NULL;
}
static const char *
prom_name_munge_units(prometheus_upload_t *rxc, char *in) {
  const char *units = NULL;
  char *hist_suff = NULL;
  char *ls = strrchr(in, '_');
  if(ls && is_standard_suffix(ls+1)) {
    hist_suff = ls + 1;
    *ls = '\0';
    ls = strrchr(in, '_');
  }
  if(ls && NULL != (units = units_suffix(rxc, ls+1))) {
    *ls = '\0';
  }
  if(hist_suff) {
    *(--hist_suff) = '_';
    if(ls) {
      int len = strlen(hist_suff);
      memmove(ls, hist_suff, len+1);
    }
  }
  return units;
}

typedef struct {
  const char *units;
  bool is_histogram;
  double hist_boundary;
} coercion_t;

coercion_t
metric_name_coerce(prometheus_upload_t *rxc, Prometheus__Label **labels, size_t label_count,
                   bool do_units, bool do_hist) {
  coercion_t rv = {};
  char *name = NULL;
  const char *units = NULL;
  const char *le = NULL;
  for (size_t i = 0; i < label_count && (!name || !units || !le); i++) {
    Prometheus__Label *l = labels[i];
    if (strcmp("__name__", l->name) == 0) {
      name = l->value;
    }
    else if(strcmp("units", l->name) == 0) {
      units = l->value;
    }
    else if(strcmp("le", l->name) == 0) {
      le = l->value;
    }
  }
  if(!name) return rv;
  if(do_units) {
    if(units) units = NULL;
    else {
      if(name) {
        rv.units = prom_name_munge_units(rxc, name);
      }
    }
  }
  if(do_hist && le) {
    char *bucket = strrchr(name, '_');
    if(bucket && !strcmp(bucket, "_bucket")) {
      rv.is_histogram = true;
      rv.hist_boundary = strtod(le, NULL);
      *bucket = '\0';
    }
  }
  return rv;
}

static char *
metric_name_from_labels(Prometheus__Label **labels, size_t label_count, const char *units, bool coerce_hist)
{
  char final_name[MAX_METRIC_TAGGED_NAME] = {0};
  char *name = final_name;
  char buffer[MAX_METRIC_TAGGED_NAME] = {0};
  char encode_buffer[MAX_METRIC_TAGGED_NAME] = {0};
  char *b = buffer;
  size_t tag_count = 0;
  for (size_t i = 0; i < label_count; i++) {
    Prometheus__Label *l = labels[i];
    if (strcmp("__name__", l->name) == 0) {
      strncpy(name, l->value, sizeof(final_name) - 1);
    } else {
      /* if we're coercing histograms, remove the "le" label */
      if(coerce_hist && !strcmp("le", l->name)) continue;
      if (tag_count > 0) {
        strlcat(b, ",", sizeof(buffer));
      }
      bool wrote_cat = false;
      size_t tl = strlen(l->name);
      if(noit_metric_tagset_is_taggable_key(l->name, tl)) {
        strlcat(b, l->name, sizeof(buffer));
        wrote_cat = true;
      } else {
        int len = mtev_b64_encode((const unsigned char *)l->name, tl, encode_buffer, sizeof(encode_buffer) - 1);
        if (len > 0) {
          encode_buffer[len] = '\0';

          strlcat(b, "b\"", sizeof(buffer));
          strlcat(b, encode_buffer, sizeof(buffer));
          strlcat(b, "\"", sizeof(buffer));
          wrote_cat = true;
        }
      }
      if(wrote_cat) {
        strlcat(b, ":", sizeof(buffer));
        tl = strlen(l->value);
        if(noit_metric_tagset_is_taggable_value(l->value, tl)) {
          strlcat(b, l->value, sizeof(buffer));
        } else {
          int len = mtev_b64_encode((const unsigned char *)l->value, tl, encode_buffer, sizeof(encode_buffer) - 1);
          if (len > 0) {
            encode_buffer[len] = '\0';
            strlcat(b, "b\"", sizeof(buffer));
            strlcat(b, encode_buffer, sizeof(buffer));
            strlcat(b, "\"", sizeof(buffer));
          }
        }
      }
      tag_count++;
    }
  }
  strlcat(name, "|ST[", sizeof(final_name));
  strlcat(name, buffer, sizeof(final_name));
  if(units) {
    if(noit_metric_tagset_is_taggable_value(units, strlen(units))) {
      if(strlen(buffer) > 0) strlcat(name, ",", sizeof(final_name));
      strlcat(name, "units:", sizeof(final_name));
      strlcat(name, units, sizeof(final_name));
    } else {
      int len = mtev_b64_encode((const unsigned char *)units, strlen(units),
                                encode_buffer, sizeof(encode_buffer) - 1);
      if(len > 0) {
        if(strlen(buffer) > 0) strlcat(name, ",", sizeof(final_name));
        strlcat(name, "units:", sizeof(final_name));
        encode_buffer[len] = '\0';
        strlcat(name, encode_buffer, sizeof(final_name));
      }
    }
  }
  strlcat(name, "]", sizeof(final_name));

  /* we don't have to canonicalize here as reconnoiter will do that for us */
  return strdup(final_name);
}

static void
metric_local_batch_flush_immediate(prometheus_upload_t *rxc) {
  mtev_memory_begin();
  pthread_mutex_lock(&batch_flush_lock);
  if(mtev_hash_size(rxc->immediate_metrics)) {
    noit_check_log_bundle_metrics(rxc->check, &rxc->start_time, rxc->immediate_metrics);
    mtev_hash_delete_all(rxc->immediate_metrics, NULL, mtev_memory_safe_free);
  }
  pthread_mutex_unlock(&batch_flush_lock);
  mtev_memory_end();
}

static void
track_histogram(prometheus_upload_t *rxc, const char *name, double boundary, double val, struct timeval w) {
  struct hist_in_progress dummy = { .whence = w };
  int name_len = strlen(name);
  if(name_len >= sizeof(dummy.name)) return;
  if(!rxc->hists) {
    rxc->hists = calloc(1, sizeof(*rxc->hists));
    mtev_hash_init(rxc->hists);
  }
  memcpy(dummy.name, name, name_len+1); /* include \0 */
  if(isinf(boundary)) boundary = 10e128;
  struct hist_in_progress *tgt = NULL;
  void *vptr = NULL;
  if(mtev_hash_retrieve(rxc->hists, &dummy.whence, sizeof(dummy.whence) + name_len, &vptr)) {
    tgt = (struct hist_in_progress *)vptr;
  } else {
    tgt = malloc(sizeof(*tgt));
    memcpy(tgt, &dummy, sizeof(*tgt));
    tgt->nallocdbins = 16;
    tgt->bins = calloc(tgt->nallocdbins, sizeof(*tgt->bins));
    tgt->nbins = 0;
    mtev_hash_store(rxc->hists, &tgt->whence, sizeof(tgt->whence) + name_len, tgt);
  }
  if(tgt->nbins == tgt->nallocdbins) {
    tgt->nallocdbins *= 2;
    tgt->bins = realloc(tgt->bins, tgt->nallocdbins * sizeof(*tgt->bins));
  }
  tgt->bins[tgt->nbins].lower = 0;
  tgt->bins[tgt->nbins].upper = boundary;
  tgt->bins[tgt->nbins].count = val;
  tgt->nbins++;
}

static int
upper_sort(const void *av, const void *bv) {
  const histogram_adhoc_bin_t *a = av, *b = bv;
  if(a->upper < b->upper) return -1;
  if(a->upper == b->upper) return 0;
  return 1;
}
static void
flush_histogram(prometheus_upload_t *rxc, struct hist_in_progress *hip) {
  /* sort */
  qsort(hip->bins, hip->nbins, sizeof(*hip->bins), upper_sort);
  /* dedup -- should never actually happen */
  for(int s=0; s<hip->nbins-1; s++) {
    if(hip->bins[s].upper == hip->bins[s+1].upper) {
      memmove(&hip->bins[s], &hip->bins[s+1], sizeof(*hip->bins) * (hip->nbins - s - 1));
      s--;
      hip->nbins--;
    }
  }
  hip->bins[0].lower = (hip->bins[0].upper <= 0) ? -10e128 : 0;
  /* undo cummulative aspect and set lower bound */
  for(int s=1; s<hip->nbins; s++) {
    hip->bins[s].lower = hip->bins[s-1].upper;
    hip->bins[s].count -= hip->bins[s-1].count;
  }

  histogram_t *h = hist_create_approximation_from_adhoc(rxc->histogram_mode, hip->bins, hip->nbins, 0);
  ssize_t est = hist_serialize_b64_estimate(h);
  if(est > 0) {
    char *hist_encoded = (char *)malloc(est);
    ssize_t hist_encoded_len = hist_serialize_b64(h, hist_encoded, est);
    noit_stats_log_immediate_histo_tv(rxc->check, hip->name, hist_encoded, hist_encoded_len,
                                      mtev_true, hip->whence);
    free(hist_encoded);
  }
  hist_free(h);
}
static void
flush_histograms(prometheus_upload_t *rxc) {
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  if(!rxc->hists) return;
  mtev_memory_begin();
  while(mtev_hash_adv(rxc->hists, &iter)) {
    struct hist_in_progress *hip = iter.value.ptr;
    flush_histogram(rxc, hip);
  }
  mtev_memory_end();
}

static void 
metric_local_batch(prometheus_upload_t *rxc, const char *name, double val, struct timeval w) {
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
  mtev_memory_begin();
  metric_t *m = noit_metric_alloc();
  m->metric_name = malloc(cmetric_len);
  memcpy(m->metric_name, cmetric, cmetric_len);
  m->metric_type = METRIC_DOUBLE;
  memcpy(&m->whence, &w, sizeof(struct timeval));
  m->metric_value.vp = malloc(sizeof(double));
  *(m->metric_value.n) = val;

  noit_stats_mark_metric_logged(noit_check_get_stats_inprogress(rxc->check), m, mtev_false);
  mtev_hash_store(rxc->immediate_metrics, m->metric_name, cmetric_len, m);
  mtev_memory_end();
}

static int
rest_prometheus_handler(mtev_http_rest_closure_t *restc, int npats, char **pats)
{
  int error_code = 500;
  int mask, complete = 0, cnt;
  prometheus_upload_t *rxc = NULL;
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
      error_code = 404;
      goto error;
    }
    if(strcmp(check->module, "prometheus")) {
      error = "no such prometheus check";
      error_code = 404;
      goto error;
    }

    /* check "secret"  */
    secret = mtev_hash_dict_get(check->config, "secret");
    if(secret && !strcmp(pats[1], secret)) allowed = mtev_true;
    if(!allowed && cross_module_reverse_allowed(check, pats[1])) allowed = mtev_true;

    if(!allowed) {
      error = "secret mismatch";
      error_code = 403;
      goto error;
    }

    rxc = restc->call_closure = calloc(1, sizeof(*rxc));
    rxc->check = check;
    mtev_gettimeofday(&rxc->start_time, NULL);
    rxc->immediate_metrics = calloc(1, sizeof(*rxc->immediate_metrics));
    mtev_hash_init_locks(rxc->immediate_metrics, MTEV_HASH_DEFAULT_SIZE, MTEV_HASH_LOCK_MODE_MUTEX);
    memcpy(rxc->check_id, check_id, UUID_SIZE);
    restc->call_closure_free = free_prometheus_upload;
    mtev_dyn_buffer_init(&rxc->data);

    const char *val = mtev_hash_dict_get(check->config, "coerce_histograms");
    rxc->coerce_histograms = (val && !strcmp(val, "true"));

    val = mtev_hash_dict_get(check->config, "allowed_units");
    if(val) {
      rxc->units_str = strdup(val);
      int cnt = 1;
      for(const char *cp = rxc->units_str; *cp; cp++) {
        cnt += (*cp == ',') ? 1 : 0;
      }
      rxc->allowed_units = calloc(cnt+1, sizeof(*rxc->allowed_units));
      cnt = 1;
      rxc->allowed_units[0] = rxc->units_str;
      for(char *cp = rxc->units_str; *cp; cp++) {
        if(*cp == ',') {
          *cp = '\0';
          if(cp[1] && cp[1] != ',') {
            rxc->allowed_units[cnt++] = cp+1;
          }
        }
      }
    }

    val = mtev_hash_dict_get(check->config, "extract_units");
    rxc->extract_units = (val && !strcmp(val, "true"));

    rxc->histogram_mode = HIST_APPROX_HIGH;
    val = mtev_hash_dict_get(check->config, "hist_approx_mode");
    if(val) {
      if(!strcmp(val, "low")) rxc->histogram_mode = HIST_APPROX_LOW;
      else if(!strcmp(val, "mid")) rxc->histogram_mode = HIST_APPROX_MID;
      else if(!strcmp(val, "harmonic_mean")) rxc->histogram_mode = HIST_APPROX_HARMONIC_MEAN;
      else if(!strcmp(val, "high")) rxc->histogram_mode = HIST_APPROX_HIGH;
    }

    mtev_http_connection *conn = mtev_http_session_connection(ctx);
    eventer_t e = mtev_http_connection_event(conn);
    if(e) {
      /* We want to fan out a single check over multiple threads. */
      eventer_pool_t *dedicated_pool = noit_check_choose_pool(rxc->check);
      int rnd = mtev_rand();
      pthread_t tgt = dedicated_pool ?
        eventer_choose_owner_pool(dedicated_pool, rnd) :
        eventer_choose_owner(rnd);
      if(!pthread_equal(eventer_get_owner(e), tgt)) {
        eventer_set_owner(e, tgt);
        return EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
      }
    }
  }
  else rxc = restc->call_closure;

  rxc = rest_get_upload(restc, &mask, &complete);
  if(rxc == NULL && !complete) return mask;

  if(!rxc) {
    error = "No data?";
    error_code = 400;
    goto error;
  }

  /* prometheus arrives as snappy encoded.  we must uncompress */
  mtev_dyn_buffer_t uncompressed;
  mtev_dyn_buffer_init(&uncompressed);
  size_t uncompressed_size;
  if (!snappy_uncompressed_length((const char *)mtev_dyn_buffer_data(&rxc->data), 
                                  mtev_dyn_buffer_used(&rxc->data), &uncompressed_size)) {
    error = "Cannot snappy decompress incoming prometheus data";
    error_code = 400;
    mtevL(noit_error, "%s\n", error);
    goto error;
  }
  mtev_dyn_buffer_ensure(&uncompressed, uncompressed_size);
  int x = snappy_uncompress((const char *)mtev_dyn_buffer_data(&rxc->data), 
                            mtev_dyn_buffer_used(&rxc->data), 
                            (char *)mtev_dyn_buffer_write_pointer(&uncompressed));
  if (x) {
    mtev_dyn_buffer_destroy(&uncompressed);
    error = "Cannot snappy decompress incoming prometheus data";
    error_code = 400;
    mtevL(noit_error, "%s, error code: %d\n", error, x);
    goto error;
  }
  mtev_dyn_buffer_advance(&uncompressed, uncompressed_size);

  /* decode prometheus protobuf */
  /* decode the protobuf */
  Prometheus__WriteRequest *write = prometheus__write_request__unpack(&protobuf_c_system_allocator, 
                                                                      mtev_dyn_buffer_used(&uncompressed),
                                                                      mtev_dyn_buffer_data(&uncompressed));
  if(!write) {
    mtev_dyn_buffer_destroy(&uncompressed);
    error = "Prometheus__WriteRequest decode: protobuf invalid";
    error_code = 400;
    mtevL(noit_error, "%s\n", error);
    goto error;
  }

  cnt = write->n_timeseries;
  for (size_t i = 0; i < write->n_timeseries; i++) {
    Prometheus__TimeSeries *ts = write->timeseries[i];
    /* each timeseries has a list of labels (Tags) and a list of samples */
    coercion_t coercion = {};
    if(rxc->extract_units || rxc->coerce_histograms) {
      coercion = metric_name_coerce(rxc, ts->labels, ts->n_labels, rxc->extract_units,
                                    rxc->coerce_histograms);
    }
    char *metric_name = metric_name_from_labels(ts->labels, ts->n_labels, coercion.units,
                                                coercion.is_histogram && rxc->coerce_histograms);

    for (size_t j = 0; j < ts->n_samples; j++) {
      Prometheus__Sample *sample = ts->samples[j];
      struct timeval tv;
      tv.tv_sec = (time_t)(sample->timestamp / 1000L);
      tv.tv_usec = (suseconds_t)((sample->timestamp % 1000L) * 1000);

      if(coercion.is_histogram) {
        track_histogram(rxc, metric_name, coercion.hist_boundary, sample->value, tv);
      } else {
        metric_local_batch(rxc, metric_name, sample->value, tv);
      }
    }
    free(metric_name);
  }
  metric_local_batch_flush_immediate(rxc);
  flush_histograms(rxc);
  prometheus__write_request__free_unpacked(write, &protobuf_c_system_allocator);
  mtev_dyn_buffer_destroy(&uncompressed);

  mtev_http_response_status_set(ctx, 200, "OK");
  mtev_http_response_option_set(ctx, MTEV_HTTP_CLOSE);

  /*Examine headers for x-circonus-prometheus-debug flag*/
  req = mtev_http_session_request(ctx);
  hdrs = mtev_http_request_headers_table(req);

  /*Check if debug header passed in. If present and set to true, set debugflag value to one.*/
  if(mtev_hash_retr_str(hdrs, "x-circonus-prometheus-debug", strlen("x-circonus-prometheus-debug"), &debugchkflag))
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
    mtev_memory_begin();
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
      const char *metric_name=noit_metric_get_full_metric_name(tmp);
      metric_type_t metric_type=tmp->metric_type;
      noit_stats_snprint_metric_value(buff, sizeof(buff), tmp);
      json_object *value_obj = json_object_new_object();
      snprintf(type_str, sizeof(type_str), "%c", metric_type);
      json_object_object_add(value_obj, "_type", json_object_new_string(type_str));
      json_object_object_add(value_obj, "_value", json_object_new_string(buff));
      json_object_object_add(metrics_obj, metric_name, value_obj);
    }
    mtev_memory_end();

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
  mtev_http_response_standard(ctx, error_code, "ERROR", "application/json");
  mtev_http_response_append(ctx, "{ \"error\": \"", 12);
  mtev_http_response_append(ctx, error, strlen(error));
  mtev_http_response_append(ctx, "\" }", 3);
  mtev_http_response_end(ctx);
  return 0;
}

static int noit_prometheus_initiate_check(noit_module_t *self,
                                          noit_check_t *check,
                                          int once, noit_check_t *cause) {
  check->flags |= NP_PASSIVE_COLLECTION;
  if (check->closure == NULL) {
    prometheus_closure_t *ccl;
    ccl = check->closure = (void *)calloc(1, sizeof(prometheus_closure_t));
    ccl->self = self;
  }
  INITIATE_CHECK(prometheus_submit, self, check, cause);
  return 0;
}

static int noit_prometheus_config(noit_module_t *self, mtev_hash_table *options) {
  prometheus_mod_config_t *conf;
  conf = noit_module_get_userdata(self);
  if(conf) {
    if(conf->options) {
      mtev_hash_destroy(conf->options, free, free);
      free(conf->options);
    }
  }
  else
    conf = calloc(1, sizeof(*conf));
  conf->options = options;
  noit_module_set_userdata(self, conf);
  return 1;
}

static int noit_prometheus_onload(mtev_image_t *self) {
  if(!nlerr) nlerr = mtev_log_stream_find("error/prometheus");
  if(!nldeb) nldeb = mtev_log_stream_find("debug/prometheus");
  if(!nlerr) nlerr = noit_error;
  if(!nldeb) nldeb = noit_debug;
  return 0;
}

static int noit_prometheus_init(noit_module_t *self) {
  prometheus_mod_config_t *conf = noit_module_get_userdata(self);

  noit_module_set_userdata(self, conf);

  eventer_pool_t *dp = noit_check_choose_pool_by_module(self->hdr.name);

  /* register rest handler */
  mtev_rest_mountpoint_t *rule;
  rule = mtev_http_rest_new_rule("PUT", "/module/prometheus/",
                                 "^(" UUID_REGEX ")/([^/]*).*$",
                                 rest_prometheus_handler);
  if(dp) mtev_rest_mountpoint_set_eventer_pool(rule, dp);
  rule = mtev_http_rest_new_rule("POST", "/module/prometheus/",
                                 "^(" UUID_REGEX ")/([^/]*).*$",
                                 rest_prometheus_handler);
  if(dp) mtev_rest_mountpoint_set_eventer_pool(rule, dp);
  return 0;
}

#include "prometheus.xmlh"
noit_module_t prometheus = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "prometheus",
    .description = "prometheus collection",
    .xml_description = prometheus_xml_description,
    .onload = noit_prometheus_onload
  },
  noit_prometheus_config,
  noit_prometheus_init,
  noit_prometheus_initiate_check,
  NULL
};
