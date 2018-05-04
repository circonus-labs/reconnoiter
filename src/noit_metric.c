/*
 * Copyright (c) 2016-2017, Circonus, Inc. All rights reserved.
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
 *LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "noit_metric.h"
#include "noit_metric_private.h"

#include <mtev_b64.h>
#include <mtev_json_object.h>
#include <mtev_str.h>
#include <mtev_log.h>
#include <circllhist.h>
#include <ctype.h>

#include <stdio.h>

#define MAX_TAGS 256

mtev_boolean
noit_metric_as_double(metric_t *metric, double *out) {
  if(metric == NULL || metric->metric_value.vp == NULL) return mtev_false;
  switch (metric->metric_type) {
  case METRIC_INT32:
    if(out) *out = (double)*(metric->metric_value.i); break;
  case METRIC_UINT32:
    if(out) *out = (double)*(metric->metric_value.I); break;
  case METRIC_INT64:
    if(out) *out = (double)*(metric->metric_value.l); break;
  case METRIC_UINT64:
    if(out) *out = (double)*(metric->metric_value.L); break;
  case METRIC_DOUBLE:
    if(out) *out = *(metric->metric_value.n); break;
  default: return mtev_false;
  }
  return true;
}

void
noit_metric_to_json(noit_metric_message_t *metric, char **json, size_t *len, mtev_boolean include_original)
{
  if (json == NULL) {
    *len = 0;
    return;
  }

  struct mtev_json_object *o = mtev_json_object_new_object();

  char type[2] = {0};
  sprintf(type, "%c", metric->type);
  mtev_json_object_object_add(o, "type", mtev_json_object_new_string(type));
  struct mtev_json_object *whence = mtev_json_object_new_int(0);
  mtev_json_object_set_int_overflow(whence, mtev_json_overflow_uint64);
  mtev_json_object_set_uint64(whence, metric->value.whence_ms);
  mtev_json_object_object_add(o, "timestamp_ms", whence);

  char uuid_str[UUID_PRINTABLE_STRING_LENGTH] = {0};
  uuid_unparse_lower(metric->id.id, uuid_str);

  if(metric->noit.name) {
    char name[metric->noit.name_len + 1];
    strncpy(name, metric->noit.name, metric->noit.name_len);
    name[metric->noit.name_len] = '\0';
    mtev_json_object_object_add(o, "noit", mtev_json_object_new_string(name));
  }
  mtev_json_object_object_add(o, "check_uuid", mtev_json_object_new_string(uuid_str));
  if(metric->id.stream.tags) {
    mtev_json_object *jst_tags = mtev_json_object_new_array();
    for(int i=0;i<metric->id.stream.tag_count;i++) {
      mtev_json_object_array_add(jst_tags,
                                 mtev_json_object_new_string_len(metric->id.stream.tags[i].tag,
                                 metric->id.stream.tags[i].total_size));
    }
    mtev_json_object_object_add(o, "stream_tags", jst_tags);
  }
  if(metric->id.measurement.tag_count) {
    mtev_json_object *jm_tags = mtev_json_object_new_array();
    for(int i=0;i<metric->id.measurement.tag_count;i++) {
      mtev_json_object_array_add(jm_tags,
                                 mtev_json_object_new_string_len(metric->id.measurement.tags[i].tag,
                                 metric->id.measurement.tags[i].total_size));
    }
    mtev_json_object_object_add(o, "measurement_tags", jm_tags);
  }

  if (metric->value.type == METRIC_ABSENT) {
    mtev_json_object_object_add(o, "value_type", NULL);
  } else {
    char value_type[2] = {0};
    sprintf(value_type, "%c", metric->value.type);
    mtev_json_object_object_add(o, "value_type", mtev_json_object_new_string(value_type));
  }

  if (metric->type == MESSAGE_TYPE_M) {
    char name[metric->id.name_len + 1];
    strncpy(name, metric->id.name, metric->id.name_len);
    name[metric->id.name_len] = '\0';
    struct mtev_json_object *int_value = mtev_json_object_new_int(metric->value.value.v_int32);

    if(metric->value.is_null) {
      mtev_json_object_object_add(o, name, NULL);
    } else {
      switch (metric->value.type) {
      case METRIC_GUESS:
      case METRIC_INT32:
        mtev_json_object_object_add(o, name, int_value);
        break;

      case METRIC_UINT32:
        mtev_json_object_set_int_overflow(int_value, mtev_json_overflow_uint64);
        mtev_json_object_set_uint64(int_value, metric->value.value.v_uint32);
        mtev_json_object_object_add(o, name, int_value);
        break;

      case METRIC_INT64:
        mtev_json_object_set_int_overflow(int_value, mtev_json_overflow_int64);
        mtev_json_object_set_int64(int_value, metric->value.value.v_int64);
        mtev_json_object_object_add(o, name, int_value);
        break;

      case METRIC_UINT64:
        mtev_json_object_set_int_overflow(int_value, mtev_json_overflow_uint64);
        mtev_json_object_set_uint64(int_value, metric->value.value.v_uint64);
        mtev_json_object_object_add(o, name, int_value);
        break;

      case METRIC_DOUBLE:
        {
          mtev_json_object_object_add(o, name, mtev_json_object_new_double(metric->value.value.v_double));
          break;

        }
      case METRIC_STRING:
        mtev_json_object_object_add(o, name, mtev_json_object_new_string(metric->value.value.v_string));
        break;
      default:
        mtev_json_object_object_add(o, name, NULL);
        break;
      }
    }
  } else if (metric->type == MESSAGE_TYPE_S) {
    char *status = strdup(metric->id.name);
    const char *field = strtok(status, "\t");
    if (field != NULL) {
      mtev_json_object_object_add(o, "state", mtev_json_object_new_string(field));
    }
    field = strtok(NULL, "\t");
    if (field != NULL) {
      mtev_json_object_object_add(o, "available", mtev_json_object_new_string(field));
    }
    field = strtok(NULL, "\t");
    if (field != NULL) {
      mtev_json_object_object_add(o, "duration", mtev_json_object_new_int(atoi(field)));
    }
    field = strtok(NULL, "\t");
    if (field != NULL) {
      mtev_json_object_object_add(o, "status", mtev_json_object_new_string(field));
    }
    free(status);
  } else if (metric->type == MESSAGE_TYPE_H) {
    histogram_t *histo = hist_alloc();
    ssize_t s = hist_deserialize_b64(histo, metric->value.value.v_string, strlen(metric->value.value.v_string));
    if (s > 0) {
      struct mtev_json_object *histogram = mtev_json_object_new_array();
      for (int i = 0; i < hist_bucket_count(histo); i++) {
        /* for each bvs in the histogram, create a "bucket" object in the json */
        struct mtev_json_object *bucket = mtev_json_object_new_object();
        double b = 0.0;
        uint64_t bc = 0;
        hist_bucket_idx(histo, i, &b, &bc);
        mtev_json_object_object_add(bucket, "bucket",
                                    mtev_json_object_new_double(b));
        struct mtev_json_object *count_o = mtev_json_object_new_int(0);
        mtev_json_object_set_int_overflow(count_o, mtev_json_overflow_uint64);
        mtev_json_object_set_int64(count_o, bc);

        mtev_json_object_object_add(bucket, "count", count_o);
        mtev_json_object_array_add(histogram, bucket);
      }
      hist_free(histo);
      char name[metric->id.name_len + 1];
      strncpy(name, metric->id.name, metric->id.name_len);
      name[metric->id.name_len] = '\0';
      mtev_json_object_object_add(o, name, histogram);
    }
  }

  const char *j = mtev_json_object_to_json_string(o);
  *json = strdup(j);
  *len = strlen(j);
  mtev_json_object_put(o);
}

/*
 * map for ascii tags
  perl -e '$valid = qr/[`+A-Za-z0-9!@#\$%^&"'\/\?\._-]/;
  foreach $i (0..7) {
  foreach $j (0..31) { printf "%d,", chr($i*32+$j) =~ $valid; }
  print "\n";
  }'
*/
static uint8_t vtagmap_key[256] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,1,1,1,1,1,1,1,0,0,0,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

/* Same as above, but allow for ':' and '=' */
static uint8_t vtagmap_value[256] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,1,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,1,0,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1,
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

/*
 * map for base64 encoded tags
 
  perl -e '$valid = qr/[A-Za-z0-9+\/=]/;
  foreach $i (0..7) {
  foreach $j (0..31) { printf "%d,", chr($i*32+$j) =~ $valid; }
  print "\n";
  }'
*/
static uint8_t base64_vtagmap[256] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,0,0,
  0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
  0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};


static inline mtev_boolean
noit_metric_tagset_is_taggable_key_char(char c) {
  uint8_t cu = c;
  return vtagmap_key[cu] == 1;
}

static inline mtev_boolean
noit_metric_tagset_is_taggable_value_char(char c) {
  uint8_t cu = c;
  return vtagmap_value[cu] == 1;
}

mtev_boolean
noit_metric_tagset_is_taggable_b64_char(char c) {
  uint8_t cu = c;
  return base64_vtagmap[cu] == 1;
}

mtev_boolean
noit_metric_tagset_is_taggable_part(const char *key, size_t len, mtev_boolean (*tf)(char))
{
  /* there are 2 tag formats supported, plain old tags that obey the vtagmap
     charset, and base64 encoded tags that obey the:

     ^b"<base64 chars>"$ format
  */
  if (len >= 3) {
    /* must start with b" */
    if (memcmp(key, "b\"", 2) == 0) {
      /* and end with " */
      if (key[len - 1] == '"') {
        size_t sum_good = 3;
        for (size_t i = 2; i < len - 1; i++) {
          sum_good += (size_t)noit_metric_tagset_is_taggable_b64_char(key[i]);
        }
        return len == sum_good;
      }
      return mtev_false;
    }
  }
  size_t sum_good = 0;
  for (size_t i = 0; i < len; i++) {
    sum_good += (size_t)tf(key[i]);
  }
  return len == sum_good;
}

mtev_boolean
noit_metric_tagset_is_taggable_key(const char *val, size_t len)
{
  return noit_metric_tagset_is_taggable_part(val, len, noit_metric_tagset_is_taggable_key_char);
}

mtev_boolean
noit_metric_tagset_is_taggable_value(const char *val, size_t len)
{
  return noit_metric_tagset_is_taggable_part(val, len, noit_metric_tagset_is_taggable_value_char);
}

size_t
noit_metric_tagset_encode_tag(char *encoded_tag, size_t max_len, const char *decoded_tag, size_t decoded_len)
{
  char scratch[NOIT_TAG_MAX_PAIR_LEN];
  if(max_len > sizeof(scratch)) return -1;
  int i = 0, sepcnt = 0;
  for(i=0; i<decoded_len; i++)
    if(decoded_tag[i] == 0x1f) {
      sepcnt = i;
      break;
    }
  if(sepcnt == 0) return -1;
  int first_part_needs_b64 = 0;
  for(i=0;i<sepcnt;i++)
    first_part_needs_b64 += !noit_metric_tagset_is_taggable_key_char(decoded_tag[i]);
  int first_part_len = sepcnt;
  if(first_part_needs_b64) first_part_len = mtev_b64_encode_len(first_part_len) + 3;
 
  int second_part_needs_b64 = 0; 
  for(i=sepcnt+1;i<decoded_len;i++)
	second_part_needs_b64 += !noit_metric_tagset_is_taggable_value_char(decoded_tag[i]);
  int second_part_len = decoded_len - sepcnt - 1;
  if(second_part_needs_b64) second_part_len = mtev_b64_encode_len(second_part_len) + 3;

  if(first_part_len + second_part_len + 2 > max_len) return -1;
  char *cp = scratch;
  if(first_part_needs_b64) {
    *cp++ = 'b';
    *cp++ = '"';
    int len = mtev_b64_encode((unsigned char *)decoded_tag, sepcnt,
                              cp, sizeof(scratch) - (cp - scratch));
    if(len <= 0) return -1;
    cp += len;
    *cp++ = '"';
  } else {
    memcpy(cp, decoded_tag, sepcnt);
    cp += sepcnt;
  }
  *cp++ = ':';
  if(second_part_needs_b64) {
    *cp++ = 'b';
    *cp++ = '"';
    int len = mtev_b64_encode((unsigned char *)decoded_tag + sepcnt + 1,
                              decoded_len - sepcnt - 1, cp, sizeof(scratch) - (cp - scratch));
    if(len <= 0) return -1;
    cp += len;
    *cp++ = '"';
  } else {
    memcpy(cp, decoded_tag + sepcnt + 1, decoded_len - sepcnt - 1);
    cp += decoded_len - sepcnt - 1;
  }
  *cp = '\0';
  memcpy(encoded_tag, scratch, cp - scratch + 1);
  return cp - scratch;
}
size_t
noit_metric_tagset_decode_tag(char *decoded_tag, size_t max_len, const char *encoded_tag, size_t encoded_size)
{
  const char *colon = (const char *)memchr(encoded_tag, ':', encoded_size);
  if (!colon) return 0;

  const char *encoded = encoded_tag;
  const char *encoded_end = encoded + encoded_size;
  char *decoded = decoded_tag;
  if (memcmp(encoded, "b\"", 2) == 0) {
    encoded += 2;
    const char *eend = colon - 1;
    if (*eend != '"') return 0;
    size_t elen = eend - encoded;
    int len = mtev_b64_decode(encoded, elen, (unsigned char *)decoded, max_len);
    if (len < 0) return 0;
    decoded += len;
    encoded += elen + 1; // skip enclosing quote
  }
  else {
    memcpy(decoded, encoded, colon - encoded);
    decoded += (colon - encoded);
    encoded += (colon - encoded);
  }
  encoded++; // skip over the colon
  *decoded = 0x1f; //replace colon with ascii unit sep
  decoded++;
  if (memcmp(encoded, "b\"", 2) == 0) {
    encoded += 2;
    const char *eend = encoded_end - 1;
    if (*eend != '"') return 0;
    size_t elen = eend - encoded;
    int len = mtev_b64_decode(encoded, elen, (unsigned char *)decoded, max_len - (decoded - decoded_tag));
    if (len < 0) return 0;
    decoded += len;
    encoded += elen + 1; // skip enclosing quote
  }
  else {
    memcpy(decoded, encoded, encoded_size - (encoded - encoded_tag));
    decoded += encoded_size - (encoded - encoded_tag);
  }
  *decoded = '\0';
  return decoded - decoded_tag;
}

/* This is a reimplementation of the stuff in noit_message_decoder b/c allocations are
 * rampant there and it's basically impossible to change the API.
 */
static mtev_boolean
build_tags(const char *input, size_t len, noit_metric_tag_t *tags, int max_tags, int *ntags) {
  
  while(len > 0 && *ntags < max_tags) {
    const char *next = noit_metric_tags_parse_one(input, len, &tags[*ntags]);
    if(!next) return mtev_false;
    
    len -= (next - input);
    input = next;
    (*ntags)++;
    if(len > 0) {
      /* there's string left, we require it to be ",<next_tag>". */
      if(*input != ',' || len == 1) return mtev_false;
      input++;
      len--;
    }
  }
  return mtev_true;
}
static mtev_boolean
eat_up_tags(const char *input, size_t *input_len, noit_metric_tag_t *tags, int max_tags,
            int *ntags, const char *prefix, const char *suffix) {
  int slen = strlen(suffix);
  int plen = strlen(prefix);
  const char *end = input + *input_len;
  if(*input_len < slen + plen) return mtev_false;
  if(memcmp(input+*input_len-slen, suffix, slen)) return mtev_false;
  const char *next_start = memchr(input, prefix[0], *input_len);
  const char *block_start = NULL;
  while(next_start) {
    if(end - next_start > plen+slen &&
       memcmp(next_start, prefix, plen) == 0) {
      block_start = next_start;
    }
    next_start = memchr(next_start+1, prefix[0], (end - next_start - 1));
  }
  if(!block_start) return mtev_false;
  if(build_tags(block_start + plen, *input_len - (block_start + plen - input) - slen,
                  tags, max_tags, ntags)) {
    *input_len = (block_start - input);
    return mtev_true;
  }
  return mtev_false;
}
static int
tags_sort_dedup(noit_metric_tag_t *tags, int n_tags) {
  int i;
  qsort((void *) tags, n_tags, sizeof(noit_metric_tag_t), noit_metric_tags_decoded_compare);
  for(i=0; i<n_tags - 1; i++) {
    if(noit_metric_tags_decoded_compare(&tags[i], &tags[i+1]) == 0) {
      memmove(&tags[i], &tags[i+1], sizeof(*tags) * (n_tags-i+1));
      i--;
      n_tags--;
    }
  }
  return n_tags;
}
static void
decode_tags(noit_metric_tag_t *tags, int ntags) {
  for(int i=0; i<ntags; i++) {
    tags[i].total_size = noit_metric_tagset_decode_tag((char *)tags[i].tag, tags[i].total_size, tags[i].tag, tags[i].total_size);
  }
}
mtev_boolean
noit_metric_name_is_clean(const char *m, size_t len) {
  if(len<1) return mtev_true;
  if(!isprint(m[0]) || isspace(m[0]) ||
     !isprint(m[len-1]) || isspace(m[len-1])) return mtev_false;
  for(int i=1; i<len-1; i++) {
    if(m[i] != ' ' && (!isprint(m[i]) || isspace(m[i]))) {
      return mtev_false;
    }
  }
  return mtev_true;
}
size_t
noit_metric_clean_name(char *m, size_t len) {
  char *ip = m;
  char *op = m;
  char *end = m + len;
  /* eat leading junk */
  while(ip < end && (!isprint(*ip) || isspace(*ip))) ip++;
  while(ip < end) {
    *op++ = (!isprint(*ip) || isspace(*ip)) ? ' ' : *ip;
    ip++;
  }
  while(op-1 > m && *(op-1) == ' ') op--;
  return op - m;
}
ssize_t
noit_metric_canonicalize(const char *input, size_t input_len, char *output, size_t output_len,
                         mtev_boolean null_term) {
  int i = 0, ntags = 0;
  noit_metric_tag_t stags[MAX_TAGS], mtags[MAX_TAGS];
  int n_stags = 0, n_mtags = 0;
  char buff[MAX_METRIC_TAGGED_NAME];
  if(output_len < input_len) return -1;
  if(input_len > MAX_METRIC_TAGGED_NAME) return -1;
  if(input != output) memcpy(output, input, input_len);

  for(i=0; i<input_len; i++) ntags += (input[i] == ',');
  if(ntags > MAX_TAGS) return -1;

  while(eat_up_tags(output, &input_len, stags, MAX_TAGS, &n_stags, "|ST[", "]") ||
        eat_up_tags(output, &input_len, mtags, MAX_TAGS, &n_mtags, "|MT{", "}"));

  if(strnstrn("|ST[", 4, output, input_len) || strnstrn("|MT{", 4, output, input_len))
    return -1;

  decode_tags(stags, n_stags);
  decode_tags(mtags, n_mtags);
  n_stags = tags_sort_dedup(stags, n_stags);
  n_mtags = tags_sort_dedup(mtags, n_mtags);
  if(output_len > MAX_METRIC_TAGGED_NAME) output_len = MAX_METRIC_TAGGED_NAME;

  /* write to buff then copy to allow for output and input to be the same */
  char *out = buff;
  int len;

  input_len = noit_metric_clean_name(output, input_len);
  if(input_len < 1) return -1;
  memcpy(out, output, input_len);
  out += input_len;
  if(n_stags) {
    memcpy(out, "|ST[", 4); out += 4;
    len = noit_metric_tags_canonical(stags, n_stags, out, (output_len - (out-buff)), mtev_true);
    if(len < 0) return -1;
    out += len;
    *out++ = ']';
  }
  if(n_mtags) {
    memcpy(out, "|MT{", 4); out += 4;
    len = noit_metric_tags_canonical(mtags, n_mtags, out, (output_len - (out-buff)), mtev_true);
    if(len < 0) return -1;
    out += len;
    *out++ = '}';
  }
  memcpy(output, buff, (out-buff));
  if(null_term) output[out-buff] = '\0';
  return (out - buff);
}
