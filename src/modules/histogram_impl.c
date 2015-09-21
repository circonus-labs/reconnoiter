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

#include <mtev_defines.h>

#ifndef SKIP_LIBMTEV
#include <mtev_log.h>
#endif

#include <errno.h>
#include <assert.h>
#include <sys/socket.h>
#include <math.h>
#include <arpa/inet.h>

#include "histogram_impl.h"

static union {
   u_int64_t private_nan_internal_rep;
   double    private_nan_double_rep;
} private_nan_union = { .private_nan_internal_rep = 0x7fffffffffffffff };

#define private_nan private_nan_union.private_nan_double_rep

static double power_of_ten[256] = {
  1, 10, 100, 1000, 10000, 100000, 1e+06, 1e+07, 1e+08, 1e+09, 1e+10,
  1e+11, 1e+12, 1e+13, 1e+14, 1e+15, 1e+16, 1e+17, 1e+18, 1e+19, 1e+20,
  1e+21, 1e+22, 1e+23, 1e+24, 1e+25, 1e+26, 1e+27, 1e+28, 1e+29, 1e+30,
  1e+31, 1e+32, 1e+33, 1e+34, 1e+35, 1e+36, 1e+37, 1e+38, 1e+39, 1e+40,
  1e+41, 1e+42, 1e+43, 1e+44, 1e+45, 1e+46, 1e+47, 1e+48, 1e+49, 1e+50,
  1e+51, 1e+52, 1e+53, 1e+54, 1e+55, 1e+56, 1e+57, 1e+58, 1e+59, 1e+60,
  1e+61, 1e+62, 1e+63, 1e+64, 1e+65, 1e+66, 1e+67, 1e+68, 1e+69, 1e+70,
  1e+71, 1e+72, 1e+73, 1e+74, 1e+75, 1e+76, 1e+77, 1e+78, 1e+79, 1e+80,
  1e+81, 1e+82, 1e+83, 1e+84, 1e+85, 1e+86, 1e+87, 1e+88, 1e+89, 1e+90,
  1e+91, 1e+92, 1e+93, 1e+94, 1e+95, 1e+96, 1e+97, 1e+98, 1e+99, 1e+100,
  1e+101, 1e+102, 1e+103, 1e+104, 1e+105, 1e+106, 1e+107, 1e+108, 1e+109,
  1e+110, 1e+111, 1e+112, 1e+113, 1e+114, 1e+115, 1e+116, 1e+117, 1e+118,
  1e+119, 1e+120, 1e+121, 1e+122, 1e+123, 1e+124, 1e+125, 1e+126, 1e+127,
  1e-128, 1e-127, 1e-126, 1e-125, 1e-124, 1e-123, 1e-122, 1e-121, 1e-120,
  1e-119, 1e-118, 1e-117, 1e-116, 1e-115, 1e-114, 1e-113, 1e-112, 1e-111,
  1e-110, 1e-109, 1e-108, 1e-107, 1e-106, 1e-105, 1e-104, 1e-103, 1e-102,
  1e-101, 1e-100, 1e-99, 1e-98, 1e-97, 1e-96,
  1e-95, 1e-94, 1e-93, 1e-92, 1e-91, 1e-90, 1e-89, 1e-88, 1e-87, 1e-86,
  1e-85, 1e-84, 1e-83, 1e-82, 1e-81, 1e-80, 1e-79, 1e-78, 1e-77, 1e-76,
  1e-75, 1e-74, 1e-73, 1e-72, 1e-71, 1e-70, 1e-69, 1e-68, 1e-67, 1e-66,
  1e-65, 1e-64, 1e-63, 1e-62, 1e-61, 1e-60, 1e-59, 1e-58, 1e-57, 1e-56,
  1e-55, 1e-54, 1e-53, 1e-52, 1e-51, 1e-50, 1e-49, 1e-48, 1e-47, 1e-46,
  1e-45, 1e-44, 1e-43, 1e-42, 1e-41, 1e-40, 1e-39, 1e-38, 1e-37, 1e-36,
  1e-35, 1e-34, 1e-33, 1e-32, 1e-31, 1e-30, 1e-29, 1e-28, 1e-27, 1e-26,
  1e-25, 1e-24, 1e-23, 1e-22, 1e-21, 1e-20, 1e-19, 1e-18, 1e-17, 1e-16,
  1e-15, 1e-14, 1e-13, 1e-12, 1e-11, 1e-10, 1e-09, 1e-08, 1e-07, 1e-06,
  1e-05, 0.0001, 0.001, 0.01, 0.1
};

struct histogram {
  u_int16_t allocd;
  u_int16_t used;
  struct {
    hist_bucket_t bucket;
    u_int64_t count;
  } *bvs;
};

u_int64_t bvl_limits[7] = {
  0x00000000000000ffULL, 0x0000000000000ffffULL,
  0x0000000000ffffffULL, 0x00000000fffffffffULL,
  0x000000ffffffffffULL, 0x0000fffffffffffffULL,
  0x00ffffffffffffffULL
};
typedef enum {
  BVL1 = 0,
  BVL2 = 1,
  BVL3 = 2,
  BVL4 = 3,
  BVL5 = 4,
  BVL6 = 5,
  BVL7 = 6,
  BVL8 = 7
} bvdatum_t;

static ssize_t
bv_size(histogram_t *h, int idx) {
  int i;
  for(i=0; i<BVL8; i++)
    if(h->bvs[idx].count <= bvl_limits[i]) return 3 + i + 1;
  return 3+8;
}

static ssize_t
bv_write(histogram_t *h, int idx, void *buff, ssize_t size) {
  int i;
  u_int8_t *cp;
  ssize_t needed;
  bvdatum_t tgt_type = BVL8;
  for(i=0; i<BVL8; i++)
    if(h->bvs[idx].count <= bvl_limits[i]) {
      tgt_type = i;
      break;
    }
  needed = 3 + tgt_type + 1;
  if(needed > size) return -1;
  cp = buff;
  cp[0] = h->bvs[idx].bucket.val;
  cp[1] = h->bvs[idx].bucket.exp;
  cp[2] = tgt_type;
  for(i=tgt_type;i>=0;i--)
    cp[i+3] = ((h->bvs[idx].count >> (i * 8)) & 0xff);
  return needed;
}
static ssize_t
bv_read(histogram_t *h, int idx, const void *buff, ssize_t len) {
  const u_int8_t *cp;
  u_int64_t count = 0;
  bvdatum_t tgt_type;
  int i;

  assert(idx == h->used);
  if(len < 3) return -1;
  cp = buff;
  tgt_type = cp[2];
  if(tgt_type > BVL8) return -1;
  if(len < 3 + tgt_type + 1) return -1;
  h->bvs[idx].bucket.val = cp[0];
  h->bvs[idx].bucket.exp = cp[1];
  h->used++;
  for(i=tgt_type;i>=0;i--)
    count |= ((u_int64_t)cp[i+3]) << (i * 8);
  h->bvs[idx].count = count;
  return 3 + tgt_type + 1;
}

ssize_t
hist_serialize_estimate(histogram_t *h) {
  /* worst case if 2 for the length + 3+8 * used */
  int i;
  ssize_t len = 2;
  if(h == NULL) return len;
  for(i=0;i<h->used;i++) len += bv_size(h, i);
  return len;
}

#define ADVANCE(tracker, n) cp += (n), tracker += (n), len -= (n)
ssize_t
hist_serialize(histogram_t *h, void *buff, ssize_t len) {
  ssize_t written = 0;
  u_int8_t *cp = buff;
  u_int16_t nlen;
  int i;

  if(len < 2) return -1;
  nlen = htons(h ? h->used : 0);
  memcpy(cp, &nlen, sizeof(nlen));
  ADVANCE(written, 2);
  for(i=0;h && i<h->used;i++) {
    ssize_t incr_written;
    incr_written = bv_write(h, i, cp, len);
    if(incr_written < 0) return -1;
    ADVANCE(written, incr_written);
  }
  return written;
}

ssize_t
hist_deserialize(histogram_t *h, const void *buff, ssize_t len) {
  const u_int8_t *cp = buff;
  ssize_t bytes_read = 0;
  u_int16_t nlen, cnt;
  if(len < 2) goto bad_read;
  if(h->bvs) free(h->bvs);
  h->bvs = NULL;
  memcpy(&nlen, cp, sizeof(nlen));
  ADVANCE(bytes_read, 2);
  h->used = 0;
  cnt = ntohs(nlen);
  h->allocd = cnt;
  if(h->allocd == 0) return bytes_read;
  h->bvs = calloc(h->allocd, sizeof(*h->bvs));
  if(!h->bvs) goto bad_read; /* yeah, yeah... bad label name */
  while(len > 0 && cnt > 0) {
    ssize_t incr_read = 0;
    if(h->used >= h->allocd) {
      return -1;
    }
    incr_read = bv_read(h, h->used, cp, len);
    if(incr_read < 0) goto bad_read;
    ADVANCE(bytes_read, incr_read);
    cnt--;
  }
  return bytes_read;

 bad_read:
  if(h->bvs) free(h->bvs);
  h->bvs = NULL;
  h->used = h->allocd = 0;
  return -1;
}

static inline
int hist_bucket_cmp(hist_bucket_t h1, hist_bucket_t h2) {
  if(h1.val == h2.val && h1.exp == h2.exp) return 0;
  /* place NaNs at the beginning always */
  if(h1.val == (int8_t)0xff) return 1;
  if(h2.val == (int8_t)0xff) return -1;
  if(h1.val < 0 && h2.val >= 0) return 1;
  if(h1.val <= 0 && h2.val > 0) return 1;
  if(h1.val > 0 && h2.val <= 0) return -1;
  if(h1.val >= 0 && h2.val < 0) return -1;
  /* here they are either both positive or both negative (or one is zero) */
  if(h1.exp == h2.exp) return (h1.val < h2.val) ? 1 : -1;
  if(h1.exp > h2.exp) return (h1.val < 0) ? 1 : -1;
  if(h1.exp < h2.exp) return (h1.val < 0) ? -1 : 1;
  /* unreachable */
  return 0;
}

double
hist_bucket_to_double(hist_bucket_t hb) {
  u_int8_t *pidx;
  assert(private_nan != 0);
  pidx = (u_int8_t *)&hb.exp;
  if(hb.val > 99 || hb.val < -99) return private_nan;
  if(hb.val < 10 && hb.val > -10) return 0.0;
  return (((double)hb.val)/10.0) * power_of_ten[*pidx];
}

double
hist_bucket_to_double_bin_width(hist_bucket_t hb) {
  u_int8_t *pidx;
  pidx = (u_int8_t *)&hb.exp;
  return power_of_ten[*pidx]/10.0;
}

double
hist_bucket_midpoint(hist_bucket_t in) {
  double out, interval;
  if(in.val > 99 || in.val < -99) return private_nan;
  out = hist_bucket_to_double(in);
  if(out == 0) return 0;
  interval = hist_bucket_to_double_bin_width(in);
  if(out < 0) interval *= -1.0;
  return out + interval/2.0;
}

/* This is used for quantile calculation,
 * where we want the side of the bucket closest to -inf */
static double
hist_bucket_left(hist_bucket_t in) {
  double out, interval;
  if(in.val > 99 || in.val < -99) return private_nan;
  out = hist_bucket_to_double(in);
  if(out == 0) return 0;
  interval = hist_bucket_to_double_bin_width(in);
  if(out < 0) return out - interval;
  return out;
}

double
hist_approx_mean(histogram_t *hist) {
  int i;
  double divisor = 0.0;
  double sum = 0.0;
  for(i=0; i<hist->used; i++) {
    if(hist->bvs[i].bucket.val > 99 || hist->bvs[i].bucket.val < -99) continue;
    double midpoint = hist_bucket_midpoint(hist->bvs[i].bucket);
    double cardinality = (double)hist->bvs[i].count;
    divisor += cardinality;
    sum += midpoint * cardinality;
  }
  if(divisor == 0.0) return private_nan;
  return sum/divisor;
}

/* 0 success,
 * -1 (empty histogram),
 * -2 (out of order quantile request)
 * -3 (out of bound quantile)
 */
int
hist_approx_quantile(histogram_t *hist, double *q_in, int nq, double *q_out) {
  int i_q, i_b;
  double total_cnt = 0.0, bucket_width = 0.0,
         bucket_left = 0.0, lower_cnt = 0.0, upper_cnt = 0.0;
  if(nq < 1) return 0; /* nothing requested, easy to satisfy successfully */

  /* Sum up all samples from all the bins */
  for (i_b=0;i_b<hist->used;i_b++) {
    /* ignore NaN */
    if(hist->bvs[i_b].bucket.val < -99 || hist->bvs[i_b].bucket.val > 99)
      continue;
    total_cnt += (double)hist->bvs[i_b].count;
  }

  /* Run through the quantiles and make sure they are in order */
  for (i_q=1;i_q<nq;i_q++) if(q_in[i_q-1] > q_in[i_q]) return -2;
  /* We use q_out as temporary space to hold the count-normailzed quantiles */
  for (i_q=0;i_q<nq;i_q++) {
    if(q_in[i_q] < 0.0 || q_in[i_q] > 1.0) return -3;
    q_out[i_q] = total_cnt * q_in[i_q];
  }


  i_b = 0;
#define TRACK_VARS(idx) do { \
  bucket_width = hist_bucket_to_double_bin_width(hist->bvs[idx].bucket); \
  bucket_left = hist_bucket_left(hist->bvs[idx].bucket); \
  lower_cnt = upper_cnt; \
  upper_cnt = lower_cnt + hist->bvs[idx].count; \
} while(0)

  /* Find the least bin (first) */
  for(i_b=0;i_b<hist->used;i_b++) {
    /* We don't include NaNs */
    if(hist->bvs[i_b].bucket.val < -99 || hist->bvs[i_b].bucket.val > 99)
      continue;
    TRACK_VARS(i_b);
    break;
  }

  /* Next walk the bins and the quantiles together */
  for(i_q=0;i_q<nq;i_q++) {
    /* And within that, advance the bins as needed */
    while(i_b < (hist->used-1) && upper_cnt < q_out[i_q]) {
      i_b++;
      TRACK_VARS(i_b);
    }
    if(lower_cnt == q_out[i_q]) {
      q_out[i_q] = bucket_left;
    }
    else if(upper_cnt == q_out[i_q]) {
      q_out[i_q] = bucket_left + bucket_width;
    }
    else {
      if(bucket_width == 0) q_out[i_q] = bucket_left;
      else q_out[i_q] = bucket_left +
             (q_out[i_q] - lower_cnt) / (upper_cnt - lower_cnt) * bucket_width;
    }
  }
  return 0;
}

hist_bucket_t
double_to_hist_bucket(double d) {
  double d_copy = d;
  hist_bucket_t hb = { (int8_t)0xff, 0 };
  assert(private_nan != 0);
  if(isnan(d) || isinf(d)) return hb;
  if(d == 0) hb.val = 0;
  else {
    double exp, esign;
    int big_exp;
    u_int8_t *pidx;
    int sign = (d < 0) ? -1 : 1;
    d = fabs(d);
    exp = log10(d);
    if(exp < -128) exp = -128;
    else if(exp > 127) exp = 127;
    esign = (exp < 0) ? -1 : 1;

    /*
     * This next line deserves a comment.
     * in cases where we have values < 1, the exponentiation is
     * off by one if we're looking for A.B (which we are).
     * ... except in the case of 0.1 or 0.00001 which would result
     * in A = 10, we want A = 1 in those cases and the originally
     * calculated exp.
     */
    if(exp < 0 && exp != (int)exp) exp -= 1;
    exp = esign * floor(fabs(exp));
    hb.exp = (int8_t)exp;
    big_exp = (int32_t)exp; /* redo the math, to see if we rolled the int8_t */
    if(hb.exp != big_exp) { /* we rolled */
      if(big_exp < 0) { /* too small -> 0 */
        hb.val = hb.exp = 0;
      } else { /* too big -> NaN */
        hb.val = (int8_t)0xff;
        hb.exp = 0;
      }
      return hb;
    }
    pidx = (u_int8_t *)&hb.exp;
    d /= power_of_ten[*pidx];
    d *= 10;
    hb.val = sign * (int)floor(d);
    if(hb.val == 100 || hb.val == -100) {
      hb.val /= 10;
      hb.exp--;
    }
    if(hb.val == 0) {
      hb.exp = 0;
      return hb;
    }
    if(!((hb.val >= 10 && hb.val < 100) ||
         (hb.val <= -10 && hb.val > -100))) {
      u_int64_t double_pun = 0;
      memcpy(&double_pun, &d_copy, sizeof(d_copy));
#ifndef SKIP_LIBMTEV
      mtevL(mtev_error, "double_to_hist_bucket(%f / %llx) -> %u.%u\n",
            d_copy, (unsigned long long)double_pun, hb.val, hb.exp);
#endif
      hb.val = (int8_t)0xff;
      hb.exp = 0;
    }
  }
  return hb;
}

static int
hist_internal_find(histogram_t *hist, hist_bucket_t hb, int *idx) {
  /* This is a simple binary search returning the idx in which
   * the specified bucket belongs... returning 1 if it is there
   * or 0 if the value would need to be inserted here (moving the
   * rest of the buckets forward one.
   */
  int rv = -1, l = 0, r = hist->used - 1;
  *idx = 0;
  if(hist->used == 0) return 0;
  while(l < r) {
    int check = (r+l)/2;
    rv = hist_bucket_cmp(hist->bvs[check].bucket, hb);
    if(rv == 0) l = r = check;
    else if(rv > 0) l = check + 1;
    else r = check - 1;

  }
  /* if rv == 0 we found a match, no need to compare again */
  if(rv != 0) rv = hist_bucket_cmp(hist->bvs[l].bucket, hb);
  *idx = l;
  if(rv == 0) return 1;   /* this is it */
  if(rv < 0) return 0;    /* it goes here (before) */
  (*idx)++;               /* it goes after here */
  assert(*idx >= 0 && *idx <= hist->used);
  return 0;
}

u_int64_t
hist_insert_raw(histogram_t *hist, hist_bucket_t hb, u_int64_t count) {
  int found, idx;
  if(count == 0) return 0;
  if(hist->bvs == NULL) {
    hist->bvs = malloc(DEFAULT_HIST_SIZE * sizeof(*hist->bvs));
    hist->allocd = DEFAULT_HIST_SIZE;
  }
  found = hist_internal_find(hist, hb, &idx);
  if(!found) {
    if(hist->used == hist->allocd) {
      /* A resize is required */
      histogram_t dummy;
      dummy.bvs = malloc((hist->allocd + DEFAULT_HIST_SIZE) *
                         sizeof(*hist->bvs));
      if(idx > 0)
        memcpy(dummy.bvs, hist->bvs, idx * sizeof(*hist->bvs));
      dummy.bvs[idx].bucket = hb;
      dummy.bvs[idx].count = count;
      if(idx < hist->used)
        memcpy(dummy.bvs + idx + 1, hist->bvs + idx,
               (hist->used - idx)*sizeof(*hist->bvs));
      free(hist->bvs);
      hist->bvs = dummy.bvs;
      hist->allocd += DEFAULT_HIST_SIZE;
    }
    else {
      /* We need to shuffle out data to poke the new one in */
      memmove(hist->bvs + idx + 1, hist->bvs + idx,
              (hist->used - idx)*sizeof(*hist->bvs));
      hist->bvs[idx].bucket = hb;
      hist->bvs[idx].count = count;
    }
    hist->used++;
  }
  else {
    /* Just need to update the counters */
    u_int64_t newval = hist->bvs[idx].count + count;
    if(newval < hist->bvs[idx].count) /* we rolled */
      newval = ~(u_int64_t)0;
    count = newval - hist->bvs[idx].count;
    hist->bvs[idx].count = newval;
  }
  return count;
}

u_int64_t
hist_insert(histogram_t *hist, double val, u_int64_t count) {
  if(count == 0) return 0;
  return hist_insert_raw(hist, double_to_hist_bucket(val), count);
}

u_int64_t
hist_remove(histogram_t *hist, double val, u_int64_t count) {
  hist_bucket_t hb;
  int idx;
  hb = double_to_hist_bucket(val);
  if(hist_internal_find(hist, hb, &idx)) {
    u_int64_t newval = hist->bvs[idx].count - count;
    if(newval > hist->bvs[idx].count) newval = 0; /* we rolled */
    count = hist->bvs[idx].count - newval;
    hist->bvs[idx].count = newval;
    return count;
  }
  return 0;
}

int
hist_bucket_count(histogram_t *hist) {
  return hist ? hist->used : 0;
}

int
hist_bucket_idx(histogram_t *hist, int idx,
                double *bucket, u_int64_t *count) {
  if(idx < 0 || idx >= hist->used) return 0;
  *bucket = hist_bucket_to_double(hist->bvs[idx].bucket);
  *count = hist->bvs[idx].count;
  return 1;
}

int
hist_bucket_idx_bucket(histogram_t *hist, int idx,
                       hist_bucket_t *bucket, u_int64_t *count) {
  if(idx < 0 || idx >= hist->used) return 0;
  *bucket = hist->bvs[idx].bucket;
  *count = hist->bvs[idx].count;
  return 1;
}

static int
hist_needed_merge_size_fc(histogram_t **hist, int cnt,
                          void (*f)(histogram_t *tgt, int tgtidx,
                                    histogram_t *src, int srcidx),
                          histogram_t *tgt) {
  unsigned short *idx;
  int i, count = 0;
  idx = alloca(cnt * sizeof(*idx));
  memset(idx, 0, cnt * sizeof(*idx));
  while(1) {
    hist_bucket_t smallest = { .exp = 0, .val = 0 };
    for(i=0;i<cnt;i++)
      if(hist[i] != NULL && idx[i] < hist[i]->used) {
        smallest = hist[i]->bvs[idx[i]].bucket;
        break;
      }
    if(i == cnt) break; /* there is no min -- no items */
    for(;i<cnt;i++) { /* see if this is the smallest. */
      if(hist[i] != NULL && idx[i] < hist[i]->used)
        if(hist_bucket_cmp(smallest, hist[i]->bvs[idx[i]].bucket) < 0)
          smallest = hist[i]->bvs[idx[i]].bucket;
    }
    /* Now zip back through and advanced all smallests */
    for(i=0;i<cnt;i++) {
      if(hist[i] != NULL && idx[i] < hist[i]->used &&
          hist_bucket_cmp(smallest, hist[i]->bvs[idx[i]].bucket) == 0) {
        if(f) f(tgt, count, hist[i], idx[i]);
        idx[i]++;
      }
    }
    count++;
  }
  return count;
}

static void
internal_bucket_accum(histogram_t *tgt, int tgtidx,
                      histogram_t *src, int srcidx) {
  u_int64_t newval;
  assert(tgtidx < tgt->allocd);
  if(tgt->used == tgtidx) {
    tgt->bvs[tgtidx].bucket = src->bvs[srcidx].bucket;
    tgt->used++;
  }
  assert(hist_bucket_cmp(tgt->bvs[tgtidx].bucket,
                         src->bvs[srcidx].bucket) == 0);
  newval = tgt->bvs[tgtidx].count + src->bvs[srcidx].count;
  if(newval < tgt->bvs[tgtidx].count) newval = ~(u_int64_t)0;
  tgt->bvs[tgtidx].count = newval;
}

static int
hist_needed_merge_size(histogram_t **hist, int cnt) {
  return hist_needed_merge_size_fc(hist, cnt, NULL, NULL);
}

int
hist_accumulate(histogram_t *tgt, histogram_t **src, int cnt) {
  int tgtneeds;
  void *oldtgtbuff = tgt->bvs;
  histogram_t tgt_copy;
  histogram_t **inclusive_src = alloca(sizeof(histogram_t *) * (cnt+1));
  memcpy(&tgt_copy, tgt, sizeof(*tgt));
  memcpy(inclusive_src, src, sizeof(*src)*cnt);
  inclusive_src[cnt] = &tgt_copy;
  tgtneeds = hist_needed_merge_size(inclusive_src, cnt+1);
  tgt->allocd = tgtneeds;
  tgt->used = 0;
  tgt->bvs = calloc(tgt->allocd, sizeof(*tgt->bvs));
  hist_needed_merge_size_fc(inclusive_src, cnt+1, internal_bucket_accum, tgt);
  if(oldtgtbuff) free(oldtgtbuff);
  return tgt->used;
}

int
hist_num_buckets(histogram_t *hist) {
  return hist->used;
}

void
hist_clear(histogram_t *hist) {
  hist->used = 0;
}

histogram_t *
hist_alloc() {
  return calloc(1, sizeof(histogram_t));
}

void
hist_free(histogram_t *hist) {
  if(hist == NULL) return;
  if(hist->bvs != NULL) free(hist->bvs);
  free(hist);
}
