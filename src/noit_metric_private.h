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

#ifndef _NOIT_METRIC_PRIVATE_H
#define _NOIT_METRIC_PRIVATE_H

#include "noit_metric.h"
#include <mtev_log.h>

API_EXPORT(const char *)
  noit_metric_tags_parse_one(const char *tagnm, size_t tagnmlen,
                             noit_metric_tag_t *output);

API_EXPORT(ssize_t)
  noit_metric_tags_canonical(const noit_metric_tag_t *tags,
                             size_t tag_count, char *tagnm, size_t tagnmlen,
                             mtev_boolean alread_decoded);

static inline int
noit_metric_tags_compare(const void *v_l, const void *v_r) {
  const noit_metric_tag_t *l = (noit_metric_tag_t *) v_l;
  const noit_metric_tag_t *r = (noit_metric_tag_t *) v_r;
  char lb[NOIT_TAG_MAX_PAIR_LEN], rb[NOIT_TAG_MAX_PAIR_LEN];
  mtevAssert(l->total_size <= NOIT_TAG_MAX_PAIR_LEN);
  mtevAssert(r->total_size <= NOIT_TAG_MAX_PAIR_LEN);
  int llen = noit_metric_tagset_decode_tag(lb, sizeof(lb), l->tag, l->total_size);
  mtevAssert(llen >= 0);
  int rlen = noit_metric_tagset_decode_tag(rb, sizeof(rb), r->tag, r->total_size);
  mtevAssert(rlen >= 0);
  size_t memcmp_len = llen < rlen ? llen : rlen;
  int cmp_rslt = memcmp(lb, rb, memcmp_len);
  if(cmp_rslt != 0) return cmp_rslt;
  if(llen < rlen) return -1;
  if(llen > rlen) return 1;
  return 0;
}

static inline int
noit_metric_tags_decoded_compare(const void *v_l, const void *v_r) {
  const noit_metric_tag_t *l = (noit_metric_tag_t *) v_l;
  const noit_metric_tag_t *r = (noit_metric_tag_t *) v_r;
  size_t memcmp_len = l->total_size < r->total_size ? l->total_size : r->total_size;
  int cmp_rslt = memcmp(l->tag, r->tag, memcmp_len);
  if(cmp_rslt != 0) return cmp_rslt;
  if(l->total_size < r->total_size) return -1;
  if(l->total_size > r->total_size) return 1;
  return 0;
}

#endif
