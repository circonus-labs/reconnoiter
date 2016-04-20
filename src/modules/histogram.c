/*
 * Copyright (c) 2012-2015, Circonus, Inc. All rights reserved.
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
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <inttypes.h>
#include <mtev_defines.h>

#include <assert.h>

#include <mtev_log.h>
#include <mtev_b64.h>

#include "noit_mtev_bridge.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "histogram_impl.h"

static mtev_log_stream_t metrics_log = NULL;
static int histogram_module_id = -1;

static int
histogram_onload(mtev_image_t *self) {
  histogram_module_id = noit_check_register_module("histogram");
  if(histogram_module_id < 0) return -1;
  return 0;
}

struct histogram_config {
  mtev_boolean histogram;
  mtev_boolean mean;
  mtev_boolean sum;
  double *quantiles;
  int n_quantiles;
};

static int double_sort(const void *av, const void *bv) {
  const double *a = av;
  const double *b = bv;
  if(*a < *b) return -1;
  if(*a > *b) return 1;
  return 0;
}
static int
histogram_config(mtev_dso_generic_t *self, mtev_hash_table *o) {
  struct histogram_config *conf = mtev_image_get_userdata(&self->hdr);
  const char *duty;
  char *duty_copy, *cp, *brk;
  int i;

  if(!mtev_hash_retr_str(o, "duty", strlen("duty"), &duty)) {
    duty = "histogram";
  }
  if(!conf) conf = calloc(1, sizeof(*conf));
  mtev_image_set_userdata(&self->hdr, conf);

  duty_copy = strdup(duty);
  for(i = 0, cp = strtok_r(duty_copy, ",", &brk);
      cp; cp = strtok_r(NULL, ",", &brk), i++);
  free(duty_copy);
  conf->quantiles = calloc(1, sizeof(double)*i);
  duty_copy = strdup(duty);
  for(cp = strtok_r(duty_copy, ",", &brk);
      cp; cp = strtok_r(NULL, ",", &brk)) {
    if(!strcmp(cp,"histogram")) conf->histogram = mtev_true;
    else if(!strcmp(cp,"mean")) conf->mean = mtev_true;
    else if(!strcmp(cp,"sum")) conf->sum = mtev_true;
    else {
      char *endptr;
      double q;
      q = strtod(cp, &endptr);
      if(*endptr == '\0') conf->quantiles[conf->n_quantiles++] = q;
    }
  }
  qsort(conf->quantiles, conf->n_quantiles, sizeof(double), double_sort);
  free(duty_copy);
  return 0;
}

typedef struct histotier {
  histogram_t *tensecs[6];
  histogram_t *secs[10];
  histogram_t *last_aggr;
  u_int8_t last_second;
  u_int64_t last_minute;
} histotier;

static void
debug_print_hist(histogram_t *ht) {
  if(ht != NULL) {
    int i, buckets;
    buckets = hist_bucket_count(ht);
    for(i=0;i<buckets;i++) {
      u_int64_t cnt;
      double v;
      hist_bucket_idx(ht, i, &v, &cnt);
      mtevL(noit_debug, "  %7.2g : %" PRIu64 "\n", v, cnt);
    }
  }
}
static void
log_histo(struct histogram_config *conf,
          noit_check_t *check, u_int64_t whence_s,
          const char *metric_name, histogram_t *h,
          mtev_boolean live_feed) {
  mtev_boolean extended_id = mtev_false;
  char uuid_str[256*3+37];
  const char *v;
  char *hist_serial = NULL;
  char *hist_encode = NULL;
  struct timeval whence;
  ssize_t est, enc_est;
  whence.tv_sec = whence_s;
  whence.tv_usec = 0;

  if(!conf->histogram) return;

  SETUP_LOG(metrics, );
  if(metrics_log) {
    v = mtev_log_stream_get_property(metrics_log, "extended_id");
    if(v && !strcmp(v, "on")) extended_id = mtev_true;
  }
  uuid_str[0] = '\0';
  if(extended_id) {
    strlcat(uuid_str, check->target, sizeof(uuid_str)-37);
    strlcat(uuid_str, "`", sizeof(uuid_str)-37);
    strlcat(uuid_str, check->module, sizeof(uuid_str)-37);
    strlcat(uuid_str, "`", sizeof(uuid_str)-37);
    strlcat(uuid_str, check->name, sizeof(uuid_str)-37);
    strlcat(uuid_str, "`", sizeof(uuid_str)-37);
  }
  uuid_unparse_lower(check->checkid, uuid_str + strlen(uuid_str));

#define SECPART(a) ((unsigned long)(a)->tv_sec)
#define MSECPART(a) ((unsigned long)((a)->tv_usec / 1000))

  est = hist_serialize_estimate(h);
  hist_serial = malloc(est);
  if(!hist_serial) {
    mtevL(noit_error, "malloc(%d) failed\n", (int)est);
    goto cleanup;
  }
  enc_est = ((est + 2)/3)*4;
  hist_encode = malloc(enc_est);
  if(!hist_encode) {
    mtevL(noit_error, "malloc(%d) failed\n", (int)enc_est);
    goto cleanup;
  }
  if(hist_serialize(h, hist_serial, est) != est) {
    mtevL(noit_error, "histogram serialization failure\n");
    goto cleanup;
  }
  enc_est = mtev_b64_encode((unsigned char *)hist_serial, est,
                            hist_encode, enc_est);
  if(enc_est < 0) {
    mtevL(noit_error, "base64 histogram encoding failure\n");
    goto cleanup;
  }

  if(live_feed && check->feeds) {
    mtev_skiplist_node *curr, *next;
    curr = next = mtev_skiplist_getlist(check->feeds);
    while(curr) {
      const char *feed_name = (char *)curr->data;
      mtev_log_stream_t ls = mtev_log_stream_find(feed_name);
      mtev_skiplist_next(check->feeds, &next);
      if(!ls ||
         mtev_log(ls, &whence, __FILE__, __LINE__,
           "H1\t%lu.%03lu\t%s\t%s\t%.*s\n",
           SECPART(&whence), MSECPART(&whence),
           uuid_str, metric_name, (int)enc_est, hist_encode))
        noit_check_transient_remove_feed(check, feed_name);
      curr = next;
    }
  }

  if(!live_feed) {
    SETUP_LOG(metrics, goto cleanup);
    mtev_log(metrics_log, &whence, __FILE__, __LINE__,
             "H1\t%lu.%03lu\t%s\t%s\t%.*s\n",
             SECPART(&whence), MSECPART(&whence),
             uuid_str, metric_name, (int)enc_est, hist_encode);
  }
 cleanup:
  if(hist_serial) free(hist_serial);
  if(hist_encode) free(hist_encode);
}

static void
sweep_roll_n_log(struct histogram_config *conf, noit_check_t *check, histotier *ht, const char *name) {
  histogram_t *tgt;
  u_int64_t aligned_seconds = ht->last_minute * 60;
  int cidx = 0, sidx = 0;
  /* find the first histogram to use as an aggregation target */
  for(cidx=0; cidx<6; cidx++) {
    if(NULL != (tgt = ht->tensecs[cidx])) {
      ht->tensecs[cidx] = NULL;
      break;
    }
  }
  if(tgt == NULL) {
    for(sidx=0; sidx<10; sidx++) {
      if(NULL != (tgt = ht->secs[sidx])) {
        ht->secs[sidx] = NULL;
        break;
      }
    }
  }
  if(tgt != NULL) {
    /* aggregate all remaining tensecs */
    /* we can do all of them b/c we've removed the tgt */
    if(cidx < 5) hist_accumulate(tgt, (const histogram_t * const *)ht->tensecs, 6);
    if(sidx < 9) hist_accumulate(tgt, (const histogram_t * const *)ht->secs, 10);
    for(cidx=0;cidx<6;cidx++) {
      hist_free(ht->tensecs[cidx]);
      ht->tensecs[cidx] = NULL;
    }
    for(sidx=0;sidx<10;sidx++) {
      hist_free(ht->secs[sidx]);
      ht->secs[sidx] = NULL;
    }
  }

  /* push this out to the log streams */
  log_histo(conf, check, aligned_seconds, name, tgt, mtev_false);
  debug_print_hist(tgt);

  /* drop the tgt, it's ours */
  if(ht->last_aggr) hist_free(ht->last_aggr);
  ht->last_aggr = tgt;
}

static void
sweep_roll_tensec(histotier *ht) {
  histogram_t *tgt;
  int tgt_bucket = ht->last_second / 10;
  int sidx;
  /* We can't very well rollup the same bucket twice */
  assert(tgt_bucket >= 0 && tgt_bucket < 6 && ht->tensecs[tgt_bucket] == NULL);
  for(sidx=0;sidx<10;sidx++) {
    if(NULL != (tgt = ht->secs[sidx])) {
      ht->secs[sidx] = NULL;
      break;
    }
  }
  if(tgt == NULL) return; /* nothing to do */
  ht->tensecs[tgt_bucket] = tgt;
  hist_accumulate(tgt, (const histogram_t * const *)ht->secs, 10);
  for(sidx=0;sidx<10;sidx++) {
    hist_free(ht->secs[sidx]);
    ht->secs[sidx] = NULL;
  }
}
static void
update_histotier(histotier *ht, u_int64_t s,
                 struct histogram_config *conf, noit_check_t *check,
                 const char *name, double val, u_int64_t cnt) {
  u_int64_t minute = s/60;
  u_int8_t second = s%60;
  u_int8_t sec_bucket = s%10;
  noit_check_metric_count_add(cnt);
  if((second != ht->last_second || check->flags & NP_TRANSIENT) &&
     check->feeds) {
    int last_second = ht->last_second;
    int last_minute = ht->last_minute;
    u_int8_t last_bucket;

    /* If we are transient we're coming to this sloppy.
     * Someone else owns this ht. So, if we're high-traffic
     * then last_second has already been bumped, so we need to
     * rewind it.
     */
    if(check->flags & NP_TRANSIENT && second == last_second) {
      last_second--;
      if(last_second < 0) {
        last_second = 59;
        last_minute--;
      }
    }
    last_bucket = last_second % 10;
    if(ht->secs[last_bucket] && hist_num_buckets(ht->secs[last_bucket]))
      log_histo(conf, check, last_minute * 60 + last_second,
                name, ht->secs[last_bucket], mtev_true);
  }
  if(minute > ht->last_minute) {
    sweep_roll_n_log(conf, check, ht, name);
  }
  else if(second/10 > ht->last_second/10) {
    sweep_roll_tensec(ht);
  }
  if(cnt > 0) {
    if(ht->secs[sec_bucket] == NULL)
      ht->secs[sec_bucket] = hist_alloc();
    hist_insert(ht->secs[sec_bucket], val, cnt);
  }
  ht->last_minute = minute;
  ht->last_second = second;
}

static void free_histotier(void *vht) {
  int i;
  histotier *ht = vht;
  if(vht == NULL) return;
  for(i=0;i<10;i++)
    if(ht->secs[i]) hist_free(ht->secs[i]);
  for(i=0;i<6;i++)
    if(ht->tensecs[i]) hist_free(ht->tensecs[i]);
  if(ht->last_aggr) hist_free(ht->last_aggr);
}
static void free_hash_o_histotier(void *vh) {
  mtev_hash_table *h = vh;
  mtev_hash_destroy(h, free, free_histotier);
  free(h);
}
static mtev_hook_return_t
histogram_hook_impl(void *closure, noit_check_t *check, stats_t *stats,
                    metric_t *m) {
  void *vht;
  histotier *ht;
  mtev_hash_table *config, *metrics;
  const char *track = "";
  mtev_dso_generic_t *self = closure;
  struct histogram_config *conf = mtev_image_get_userdata(&self->hdr);

  config = noit_check_get_module_config(check, histogram_module_id);
  if(!config || mtev_hash_size(config) == 0) return MTEV_HOOK_CONTINUE;
  mtev_hash_retr_str(config, m->metric_name, strlen(m->metric_name), &track);
  if(!track || strcmp(track, "add"))
    return MTEV_HOOK_CONTINUE;

  metrics = noit_check_get_module_metadata(check, histogram_module_id);
  if(!metrics) {
    metrics = calloc(1, sizeof(*metrics));
    noit_check_set_module_metadata(check, histogram_module_id,
                                   metrics, free_hash_o_histotier);
  }
  if(!mtev_hash_retrieve(metrics, m->metric_name, strlen(m->metric_name),
                         &vht)) {
    ht = calloc(1, sizeof(*ht));
    vht = ht;
    mtev_hash_store(metrics, strdup(m->metric_name), strlen(m->metric_name),
                    vht);
  }
  else ht = vht;
  if(m->metric_value.vp != NULL) {
#define UPDATE_HISTOTIER(a) update_histotier(ht, time(NULL), conf, check, m->metric_name, *m->metric_value.a, 1)
    switch(m->metric_type) {
      case METRIC_UINT64:
        UPDATE_HISTOTIER(L); break;
      case METRIC_INT64:
        UPDATE_HISTOTIER(l); break;
      case METRIC_UINT32:
        UPDATE_HISTOTIER(I); break;
      case METRIC_INT32:
        UPDATE_HISTOTIER(i); break;
      case METRIC_DOUBLE:
        UPDATE_HISTOTIER(n); break;
      default: /*noop*/
        break;
    }
  }
  return MTEV_HOOK_CONTINUE;
}

static mtev_hook_return_t
histogram_hook_special_impl(void *closure, noit_check_t *check, stats_t *stats,
                            const char *metric_name, metric_type_t type, const char *v,
                            mtev_boolean success) {
  void *vht;
  histotier *ht;
  mtev_hash_table *config, *metrics;
  const char *track = "";
  mtev_dso_generic_t *self = closure;
  struct histogram_config *conf = mtev_image_get_userdata(&self->hdr);

  if(success) return MTEV_HOOK_CONTINUE;

  config = noit_check_get_module_config(check, histogram_module_id);
  if(!config || mtev_hash_size(config) == 0) return MTEV_HOOK_CONTINUE;
  mtev_hash_retr_str(config, metric_name, strlen(metric_name), &track);
  if(!track || strcmp(track, "add"))
    return MTEV_HOOK_CONTINUE;

  metrics = noit_check_get_module_metadata(check, histogram_module_id);
  if(!metrics) {
    metrics = calloc(1, sizeof(*metrics));
    noit_check_set_module_metadata(check, histogram_module_id,
                                   metrics, free_hash_o_histotier);
  }
  if(!mtev_hash_retrieve(metrics, metric_name, strlen(metric_name),
                         &vht)) {
    ht = calloc(1, sizeof(*ht));
    vht = ht;
    mtev_hash_store(metrics, strdup(metric_name), strlen(metric_name),
                    vht);
  }
  else ht = vht;
  if(v != NULL) {
    /* We expect: H[<float>]=%d */
    const char *lhs;
    char *endptr;
    double bucket;
    u_int64_t cnt;
    if(v[0] != 'H' || v[1] != '[') return MTEV_HOOK_CONTINUE;
    if(NULL == (lhs = strchr(v+2, ']'))) return MTEV_HOOK_CONTINUE;
    lhs++;
    if(*lhs++ != '=') return MTEV_HOOK_CONTINUE;
    bucket = strtod(v+2, &endptr);
    if(endptr == v+2) return MTEV_HOOK_CONTINUE;
    cnt = strtoull(lhs, &endptr, 10);
    if(endptr == lhs) return MTEV_HOOK_CONTINUE;
    update_histotier(ht, time(NULL), conf, check, metric_name, bucket, cnt);
  }
  return MTEV_HOOK_CONTINUE;
}

static void
histogram_sweep_calculations(struct histogram_config *conf, noit_check_t *check) {
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  const char *metric_name;
  int klen;
  void *data;
  mtev_hash_table *metrics;
  double *out_q;

  noit_check_get_stats_current(check);
  /* Only need to do work if it's asked for */
  if(!conf->mean && !conf->sum && conf->n_quantiles < 1) return;
  metrics = noit_check_get_module_metadata(check, histogram_module_id);
  if(!metrics) return;
  out_q = alloca(sizeof(double *) * conf->n_quantiles);

  while(mtev_hash_next(metrics, &iter, &metric_name, &klen, &data)) {
    char mname[1024];
    histotier *ht = data;
    if(ht->last_aggr == NULL) continue;
    if(conf->mean) {
      double mean_value;
      snprintf(mname, sizeof(mname), "%s:mean", metric_name);
      mean_value = hist_approx_mean(ht->last_aggr);
      noit_stats_set_metric(check, mname, METRIC_DOUBLE, &mean_value);
      if (check->flags & NP_SUPPRESS_METRICS) {
        noit_stats_log_immediate_metric(check, mname, METRIC_DOUBLE, &mean_value);
      }
    }
    if(conf->sum) {
      double sum;
      snprintf(mname, sizeof(mname), "%s:sum", metric_name);
      sum = hist_approx_sum(ht->last_aggr);
      noit_stats_set_metric(check, mname, METRIC_DOUBLE, &sum);
      if (check->flags & NP_SUPPRESS_METRICS) {
        noit_stats_log_immediate_metric(check, mname, METRIC_DOUBLE, &sum);
      }
    }
    if(conf->n_quantiles) {
      if(hist_approx_quantile(ht->last_aggr,
           conf->quantiles, conf->n_quantiles, out_q) == 0) {
        int i;
        for(i=0;i<conf->n_quantiles;i++) {
          snprintf(mname, sizeof(mname), "%s:q(%0.5f)", metric_name, conf->quantiles[i]);
          noit_stats_set_metric(check, mname, METRIC_DOUBLE, &out_q[i]);
          if (check->flags & NP_SUPPRESS_METRICS) {
            noit_stats_log_immediate_metric(check, mname, METRIC_DOUBLE, &out_q[i]);
          }
        }
      }
    }
  }
}

static mtev_hook_return_t
histogram_stats_fixup(void *closure, noit_check_t *check) {
  mtev_dso_generic_t *self = closure;
  struct histogram_config *conf = mtev_image_get_userdata(&self->hdr);
  if(check->flags & NP_TRANSIENT) return MTEV_HOOK_CONTINUE;
  histogram_sweep_calculations(conf, check);
  return MTEV_HOOK_CONTINUE;
}

static mtev_hook_return_t
_histogram_logger_impl(void *closure, noit_check_t *check, mtev_boolean passive) {
  const char *track = "";
  mtev_hash_table *config;

  config = noit_check_get_module_config(check, histogram_module_id);
  if(!config || mtev_hash_size(config) == 0) return MTEV_HOOK_CONTINUE;

  mtev_hash_retr_str(config, "metrics", strlen("metrics"), &track);
  if(!track || strcmp(track, "replace"))
    return MTEV_HOOK_CONTINUE;
  /* If we're replacing other metrics, then we prevent logging */
  if(strcmp(track, "replace") == 0) return MTEV_HOOK_DONE;
  return MTEV_HOOK_CONTINUE;
}
static mtev_hook_return_t
histogram_logger(void *closure, noit_check_t *check) {
  return _histogram_logger_impl(closure, check, mtev_false);
}
static mtev_hook_return_t
histogram_logger_passive(void *closure, noit_check_t *check) {
  return _histogram_logger_impl(closure, check, mtev_true);
}
static void
heartbeat_all_metrics(struct histogram_config *conf,
                      noit_check_t *check, mtev_hash_table *metrics) {
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  const char *k;
  int klen;
  void *data;
  u_int64_t s = time(NULL);
  while(mtev_hash_next(metrics, &iter, &k, &klen, &data)) {
    histotier *ht = data;
    update_histotier(ht, s, conf, check, k, 0, 0);
  }
}
static mtev_hook_return_t
histogram_hb_hook_impl(void *closure, noit_module_t *self,
                       noit_check_t *check, noit_check_t *cause) {
  /* We'll use this as an artificial heartbeat to induce a histo sweep-n-roll
   * if no new data has come in yet.
   */
  struct histogram_config *conf = mtev_image_get_userdata(&self->hdr);
  noit_check_t *metrics_source = check;
  mtev_hash_table *metrics;
#define NEED_PARENT (NP_TRANSIENT | NP_PASSIVE_COLLECTION)

  /* If this is a passive check, we should be looking at the source
   * from which it was cloned as this fella isn't getting any data
   * pushed into it.
   */
  if((check->flags & NEED_PARENT) == NEED_PARENT) {
    metrics_source = noit_poller_lookup(check->checkid);
    if(metrics_source == NULL) return MTEV_HOOK_CONTINUE;
  }
  /* quick disqualifictaion */
  metrics = noit_check_get_module_metadata(metrics_source, histogram_module_id);
  if(!metrics || mtev_hash_size(metrics) == 0) return MTEV_HOOK_CONTINUE;
  heartbeat_all_metrics(conf, check, metrics);
  return MTEV_HOOK_CONTINUE;
}
static int
histogram_init(mtev_dso_generic_t *self) {
  check_stats_set_metric_hook_register("histogram", histogram_hook_impl, self);
  check_stats_set_metric_coerce_hook_register("histogram", histogram_hook_special_impl, self);
  check_set_stats_hook_register("histogram", histogram_stats_fixup, self);
  check_log_stats_hook_register("histogram", histogram_logger, self);
  check_passive_log_stats_hook_register("histogram", histogram_logger_passive, self);
  check_postflight_hook_register("histogram", histogram_hb_hook_impl, self);
  return 0;
}

mtev_dso_generic_t histogram = {
  {
    MTEV_GENERIC_MAGIC,
    MTEV_GENERIC_ABI_VERSION,
    "histogram",
    "Passive histogram support for metrics collection",
    "",
    histogram_onload
  },
  histogram_config,
  histogram_init
};
