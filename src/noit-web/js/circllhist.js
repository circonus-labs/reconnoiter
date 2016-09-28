"use strict";

/*
 * Copyright (c) 2012-2016, Circonus, Inc. All rights reserved.
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

function circllhist() {
  this.alloc();
}

(function(o) {
  var power_of_ten = [
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
  ];
  
  var bvl_limits = [
    0x00000000000000ff, 0x0000000000000ffff,
    0x0000000000ffffff, 0x00000000fffffffff,
    0x000000ffffffffff, 0x0000fffffffffffff,
    0x00ffffffffffffff
  ];
  var BVL1 = 0,
    BVL2 = 1,
    BVL3 = 2,
    BVL4 = 3,
    BVL5 = 4,
    BVL6 = 5,
    BVL7 = 6,
    BVL8 = 7;
  
  function bv_size(h, idx) {
    for(var i=0; i<BVL8; i++)
      if(h.bvs[idx].count <= bvl_limits[i]) return 3 + i + 1;
    return 3+8;
  }
  
  function bv_write(h, idx) {
    var tgt_type = BVL8;
    for(var i=0; i<BVL8; i++)
      if(h.bvs[idx].count <= bvl_limits[i]) {
        tgt_type = i;
        break;
      }
    needed = 3 + tgt_type + 1;
    var buff = new Uint8Array(needed);
    buff[0] = h.bvs[idx].bucket.val;
    buff[1] = h.bvs[idx].bucket.exp;
    buff[2] = tgt_type;
    for(var i=tgt_type;i>=0;i--)
      buff[i+3] = ((h.bvs[idx].count >> (i * 8)) & 0xff);
    return buff
  }
  
  function bv_read(h, idx, buff, offset) {
    var count = 0;
  
    if(idx != h.bvs.length) return -1;
    if(len < 3) return -1;
    var tgt_type = buff[offset + 2];
    if(tgt_type > BVL8) return -1;
    if(len < 3 + tgt_type + 1) return -1;
    var bucket = { val: buff[offset + 0],
                   exp: buff[offset + 1] };
    for(i=tgt_type;i>=0;i--)
      count |= buff[offset+i+3] << (i * 8);
    bucket.count = count;
    h.bvs.push(bucket);
    return offset + 3 + tgt_type + 1;
  }
  
  var htons = function(b, i, v) {
    b[i] = (0xff & (v >> 8));
    b[i + 1] = (0xff & (v));
  };
  var ntohs = function(b, i) {
    return ((0xff & b[i]) << 8) | 
           ((0xff & b[i + 1]));
  };
  
  
  function hist_serialize(h) {
    var parts = [], len = 2;
    for(var i=0;h && i<h.bvs.length;i++) {
      var part = bv_write(h, i);
      len += part.length;
      parts.push(part);
    }
    var buff = new UintArry(len), off = 2;
    htons(buff, 0, h.bvs.length)
    for (var i=0; i<parts.length; i++) {
      for(var j=0;j<parts[i].length;j++) {
        buff[off++] = parts[i][j];
      }
    }
    return buff;
  }
  
  /*
  ssize_t
  hist_serialize_b64(const histogram_t *h, char *b64_serialized_histo_buff, ssize_t buff_len) {
    ssize_t serialize_buff_length = hist_serialize_estimate(h);
    void *serialize_buff = alloca(serialize_buff_length);
    ssize_t serialized_length = hist_serialize(h, serialize_buff, serialize_buff_length);
  
    return mtev_b64_encode(serialize_buff, serialized_length, b64_serialized_histo_buff, buff_len);
  }
  */
  
  function hist_deserialize(h, buff) {
    if(h == null) h = hist_alloc();
    h.bvs = [];
    cnt = ntohs(buff, 0);
    if(cnt == 0) return bytes_read;
    var offset = 2;
    while(cnt > 0) {
      offset = bv_read(h, h.bvs.length, buff, offset);
      if(offset < 0) { h.bvs = []; return h; }
      cnt--;
    }
    return h;
  }
  
  /*
  ssize_t hist_deserialize_b64(histogram_t *h, const void *b64_string, ssize_t b64_string_len) {
      int decoded_hist_len;
      unsigned char* decoded_hist = alloca(b64_string_len);
  
      decoded_hist_len = mtev_b64_decode(b64_string, b64_string_len, decoded_hist, b64_string_len);
  
      if (decoded_hist_len < 2) {
        return -1;
      }
  
      ssize_t bytes_read = hist_deserialize(h, decoded_hist, decoded_hist_len);
      if (bytes_read != decoded_hist_len) {
        return -1;
      }
      return bytes_read;
  }
  */
  
  function hist_bucket_cmp(h1, h2) {
    // checks if h1 < h2 on the real axis.
    if(h1.val == h2.val && h1.exp == h2.exp) return 0;
    /* place NaNs at the beginning always */
    if(h1.val == 0xff) return 1;
    if(h2.val == 0xff) return -1;
    /* zero values need special treatment */
    if(h1.val == 0) return (h2.val > 0) ? 1 : -1;
    if(h2.val == 0) return (h1.val < 0) ? 1 : -1;
    /* opposite signs? */
    if(h1.val < 0 && h2.val > 0) return 1;
    if(h1.val > 0 && h2.val < 0) return -1;
    /* here they are either both positive or both negative */
    if(h1.exp == h2.exp) return (h1.val < h2.val) ? 1 : -1;
    if(h1.exp > h2.exp) return (h1.val < 0) ? 1 : -1;
    if(h1.exp < h2.exp) return (h1.val < 0) ? -1 : 1;
    /* unreachable */
    return 0;
  }
  
  function hist_bucket_to_double(hb) {
    if(hb.val > 99 || hb.val < -99) return NaN;
    if(hb.val < 10 && hb.val > -10) return 0.0;
    return (hb.val/10.0) * power_of_ten[hb.exp & 0xff];
  }
  
  function hist_bucket_to_double_bin_width(hb) {
    if(hb.val > 99 || hb.val < -99) return NaN;
    if(hb.val < 10 && hb.val > -10) return 0.0;
    return power_of_ten[hb.exp & 0xff]/10.0;
  }
  
  function hist_bucket_midpoint(input) {
    if(input.val > 99 || input.val < -99) return NaN;
    var out = hist_bucket_to_double(input);
    if(out == 0) return 0;
    var interval = hist_bucket_to_double_bin_width(input);
    if(out < 0) interval *= -1.0;
    return out + interval/2.0;
  }
  
  /* This is used for quantile calculation,
   * where we want the side of the bucket closest to -inf */
  function hist_bucket_left(input) {
    if(input.val > 99 || input.val < -99) return NaN;
    var out = hist_bucket_to_double(input);
    if(out == 0) return 0;
    if(out > 0) return out;
    /* out < 0 */
    var interval = hist_bucket_to_double_bin_width(input);
    return out - interval;
  }
  
  function hist_approx_mean(hist) {
    var divisor = 0.0, sum = 0.0;
    for(var i=0; i<hist.bvs.length; i++) {
      if(hist.bvs[i].bucket.val > 99 || hist.bvs[i].bucket.val < -99) continue;
      var midpoint = hist_bucket_midpoint(hist.bvs[i].bucket);
      var cardinality = hist.bvs[i].count;
      divisor += cardinality;
      sum += midpoint * cardinality;
    }
    if(divisor == 0.0) return NaN;
    return sum/divisor;
  }
  
  function hist_approx_sum(hist) {
    var sum = 0.0;
    for(var i=0; i<hist.bvs.length; i++) {
      if(hist.bvs[i].bucket.val > 99 || hist.bvs[i].bucket.val < -99) continue;
      var value = hist_bucket_midpoint(hist.bvs[i].bucket);
      var cardinality = hist.bvs[i].count;
      sum += value * cardinality;
    }
    return sum;
  }
  
  /* 0 success,
   * -1 (empty histogram),
   * -2 (out of order quantile request)
   * -3 (out of bound quantile)
   * -4 (non-empy target q_out array)
   */
  function hist_approx_quantile(hist, q_in, q_out) {
    if(q_out.length != 0) return -4;
    var total_cnt = 0.0, bucket_width = 0.0,
        bucket_left = 0.0, lower_cnt = 0.0, upper_cnt = 0.0;
    if(q_in.length < 1) return 0; /* nothing requested, easy to satisfy successfully */
  
    /* Sum up all samples from all the bins */
    for (var i_b=0;i_b<hist.bvs.length;i_b++) {
      /* ignore NaN */
      if(hist.bvs[i_b].bucket.val < -99 || hist.bvs[i_b].bucket.val > 99)
        continue;
      total_cnt += hist.bvs[i_b].count;
    }
  
    /* Run through the quantiles and make sure they are in order */
    for (var i_q=1;i_q<q_in.length;i_q++) if(q_in[i_q-1] > q_in[i_q]) return -2;
    /* We use q_out as temporary space to hold the count-normailzed quantiles */
    for (var i_q=0;i_q<q_in.length;i_q++) {
      if(q_in[i_q] < 0.0 || q_in[i_q] > 1.0) return -3;
      q_out.push(total_cnt * q_in[i_q]);
    }
  
    /* Find the least bin (first) */
    for(var i_b=0;i_b<hist.bvs.length;i_b++) {
      /* We don't include NaNs */
      if(hist.bvs[i_b].bucket.val < -99 || hist.bvs[i_b].bucket.val > 99)
        continue;
      bucket_width = hist_bucket_to_double_bin_width(hist.bvs[i_b].bucket);
      bucket_left = hist_bucket_left(hist.bvs[i_b].bucket);
      lower_cnt = upper_cnt;
      upper_cnt = lower_cnt + hist.bvs[i_b].count;
      break;
    }
  
    /* Next walk the bins and the quantiles together */
    for(var i_q=0;i_q<q_in.length;i_q++) {
      /* And within that, advance the bins as needed */
      while(i_b < (hist.bvs.length-1) && upper_cnt < q_out[i_q]) {
        i_b++;
        bucket_width = hist_bucket_to_double_bin_width(hist.bvs[i_b].bucket);
        bucket_left = hist_bucket_left(hist.bvs[i_b].bucket);
        lower_cnt = upper_cnt;
        upper_cnt = lower_cnt + hist.bvs[i_b].count;
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
  
  function double_to_hist_bucket(d) {
    var d_copy = d;
    var hb = { val: 0xff, exp: 0 }; // NaN
    if(isNaN(d) || !isFinite(d)) return hb;
    else if(d==0) hb.val = 0;
    else {
      var sign = (d < 0) ? -1 : 1;
      d = Math.abs(d);
      var big_exp = Math.floor(Math.log10(d));
      hb.exp = big_exp;
      if(big_exp < -128 || big_exp > 127) { /* we rolled */
        if(big_exp < 0) {
          /* d is in [0 .. 1e-128). Return 0 */
          hb.val = 0;
          hb.exp = 0;
        } else {
          /* d is >= 1e128. Return NaN */
          hb.val = 0xff;
          hb.exp = 0;
        }
        return hb;
      }
      d /= power_of_ten[hb.exp & 0xff];
      d *= 10;
      // avoid rounding problem at the bucket boundary
      // e.g. d=0.11 results in hb.val = 10 (shoud be 11)
      // by allowing a error margin (in the order or magintude
      // of the exected rounding errors of the above transformations)
      hb.val = sign * Math.floor(d + 1e-13);
      if(hb.val == 100 || hb.val == -100) {
        if (hb.exp < 127) {
          hb.val /= 10;
          hb.exp++;
        } else { // can't increase exponent. Return NaN
          hb.val = 0xff;
          hb.exp = 0;
        }
      }
      if(hb.val == 0) {
        hb.exp = 0;
        return hb;
      }
      if(!((hb.val >= 10 && hb.val < 100) ||
           (hb.val <= -10 && hb.val > -100))) {
        hb.val = 0xff;
        hb.exp = 0;
      }
    }
    return hb;
  }
  
  function hist_internal_find(hist, hb) {
    /* This is a simple binary search returning the idx in which
     * the specified bucket belongs... returning { found: bool, idx: }
     * the value would need to be inserted here (moving the
     * rest of the buckets forward one.
     */
    var rv = -1, l = 0, r = hist.bvs.length - 1, idx = 0, found = false;
    if(hist.bvs.length == 0) return { found: found, idx: idx };
    while(l < r) {
      var check = Math.floor((r+l)/2);
      rv = hist_bucket_cmp(hist.bvs[check].bucket, hb);
      if(rv == 0) l = r = check;
      else if(rv > 0) l = check + 1;
      else r = check - 1;
    }
    /* if rv == 0 we found a match, no need to compare again */
    if(rv != 0) rv = hist_bucket_cmp(hist.bvs[l].bucket, hb);
    idx = l;
    if(rv == 0) found = true;         /* this is it */
    else if(rv < 0) found = false;    /* it goes here (before) */
    else idx++;                       /* it goes after here */
    return { found: found, idx: idx };
  }
  
  function hist_insert_raw(hist, hb, count) {
    if(count == 0) return 0;
    var result = hist_internal_find(hist, hb);
    if(!result.found) {
      hist.bvs.splice(result.idx, 0, { bucket: hb, count: count })
    }
    else {
      /* Just need to update the counters */
      hist.bvs[result.idx].count += count;
      count = hist.bvs[result.idx].count
    }
    return count;
  }
  
  function hist_insert(hist, val, count) {
    if(count == 0) return 0;
    return hist_insert_raw(hist, double_to_hist_bucket(val), count);
  }
  
  function hist_remove(hist, val, count) {
    var hb = double_to_hist_bucket(val);
    var result = hist_internal_find(hist, hb);
    if(result.found) {
      hist.bvs[result.idx].count -= count;
      if(hist.bvs[result.idx].count < 0)
        hist.bvs[result.idx].count = 0;
    }
    return 0;
  }
  
  function hist_merge(tgt, src) {
    // we just iterate through tgt and src lock-step and add-or-splice
    var t_idx=0, s_idx=0;
    for ( ; s_idx<src.bvs.length && t_idx<tgt.bvs.length; ) {
      var cmp = hist_bucket_cmp(tgt.bvs[t_idx].bucket, src.bvs[s_idx].bucket)
      if(cmp > 0) { t_idx++; }
      else if(cmp < 0) { tgt.bvs.splice(t_idx, 0, src.bvs[s_idx]); t_idx++; s_idx++; }
      else if(cmp == 0) { tgt.bvs[t_idx].count += src.bvs[s_idx].count; t_idx++; s_idx++; }
    }
    while (s_idx < src.bvs.length) {
      tgt.bvs.push(src.bvs[s_idx++]);
    }
  }
  
  function hist_population(hist) {
    var count = 0;
    for(var i=0; i<hist.bvs.length; i++)
      count += hist.bvs[i].count;
    return count;
  }
  
  function hist_bucket_count(hist) {
    return hist ? hist.bvs.length : 0;
  }
  
  function hist_clear(hist) {
    hist.bvs = []
  }
  
  function hist_alloc() {
    return { bvs: [] }
  }
  
  o.prototype.alloc = function() { this.bvs = []; };
  o.prototype.clear = function() { hist_clear(this) };
  o.prototype.bucket_count = function() { return hist_bucket_count(this); };
  o.prototype.merge = function(src) { hist_merge(this, src); };
  o.prototype.insert = function(val, count) { return hist_insert(this, val, count); };
  o.prototype.insert_raw = function(hb, count) { return hist_insert_raw(this, hb, count); };
  o.prototype.remove = function(val, count) { return hist_remove(this, val, count); };
  o.prototype.population = function() { return hist_population(this); };
  o.prototype.approx_mean = function() { return hist_approx_mean(this); };
  o.prototype.approx_quantile = function(qin,qout) { return hist_approx_quantile(this,qin,qout); };
  o.prototype.approx_sum = function() { return hist_approx_sum(this); };
  // Shortcuts
  o.prototype.mean = function() { return this.approx_mean(); };
  o.prototype.quantile = function(qin,qout) { return this.approx_quantile(qin,qout); };
  o.prototype.sum = function() { return this.approx_sum(); };
  
  // These are just useful non-method helpers that should be exposed.
  o.prototype.double_to_hist_bucket = double_to_hist_bucket;
  o.prototype.hist_bucket_left = hist_bucket_left;
  o.prototype.hist_bucket_midpoint = hist_bucket_midpoint;
  o.prototype.hist_bucket_to_double = hist_bucket_to_double;
  o.prototype.hist_bucket_to_double_bin_width = hist_bucket_to_double_bin_width;
  
})(circllhist);

if( typeof exports !== 'undefined' ) {
  if( typeof module !== 'undefined' && module.exports ) {
    exports = module.exports = circllhist
  }
  exports.circllhist = circllhist
} 
else if( typeof root !== 'undefined' ) {
  root.circllhist = circllhist
}
