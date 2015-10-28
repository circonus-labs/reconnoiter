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

#ifndef BE_HIST_IMPL_H
#define BE_HIST_IMPL_H

#define DEFAULT_HIST_SIZE 100
#include <mtev_config.h>

typedef struct hist_rollup_config hist_rollup_config_t;

typedef struct hist_bucket {
  int8_t val; /* value * 10 */
  int8_t exp; /* -128 -> 127 */
} hist_bucket_t;

typedef struct histogram {
  u_int16_t allocd;
  u_int16_t used;
  struct {
    hist_bucket_t bucket;
    u_int64_t count;
  } *bvs;
} histogram_t;

/* These 16 bits give us:
 * Allows us to express exactly:
 *   given A.B == val / 10;
 *   A.B e X
 *   where -9 <= A <= 9, but A != 0
 *         0 <= B <= 9
 *         X is all possible exp (above)
 *  if A is zero, the overall value ia zero.
 */

double hist_bucket_to_double(hist_bucket_t hb);
double hist_bucket_to_double_bin_width(hist_bucket_t hb);
hist_bucket_t double_to_hist_bucket(double d);
double hist_bucket_midpoint(hist_bucket_t in);
double hist_approx_mean(histogram_t *);
int hist_approx_quantile(histogram_t *, double *q_in, int nq, double *q_out);

histogram_t *hist_alloc();
void hist_free(histogram_t *hist);
u_int64_t hist_insert(histogram_t *hist, double val, u_int64_t count);
u_int64_t hist_insert_raw(histogram_t *hist, hist_bucket_t hb, u_int64_t count);
u_int64_t hist_remove(histogram_t *hist, double val, u_int64_t count);
int hist_bucket_count(histogram_t *hist);
int hist_bucket_idx(histogram_t *hist, int idx, double *v, u_int64_t *c);
int hist_bucket_idx_bucket(histogram_t *hist, int idx, hist_bucket_t *b, u_int64_t *c);
int hist_accumulate(histogram_t *tgt, histogram_t **src, int cnt);
int hist_num_buckets(histogram_t *hist);
void hist_clear(histogram_t *hist);

ssize_t hist_serialize_estimate(histogram_t *h);
ssize_t hist_serialize(histogram_t *h, void *buff, ssize_t len);
ssize_t hist_deserialize(histogram_t *h, const void *buff, ssize_t len);

#endif
