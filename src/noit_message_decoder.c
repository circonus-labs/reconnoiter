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
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "noit_message_decoder.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <mtev_uuid.h>
#include <ctype.h>
#include <mtev_log.h>
#include <mtev_str.h>

#include "noit_metric.h"
#include "noit_metric_private.h"

#define MOVE_TO_NEXT_TAB(cp, lvalue) do { \
  lvalue = memchr(cp, '\t', strlen(cp)); \
  if(lvalue){\
    ++lvalue; \
    cp = lvalue; \
    } \
} while(0)

mtev_boolean
noit_metric_extract_tags(const char *in, int *inlen,
                         const char *start_marker, char end_marker,
                         noit_metric_tagset_builder_t *builder) {
  /* This function will take a metric name (in) and extract a
   * <start_marker><taglist><end_marker> from it
   * and union that with any existing tags.
   * If successful, it will shorten inlen accordingly.
   */
  int st_off = strlen(start_marker);
  const char *end = in + *inlen - 1;
  const char *match = mtev_memmem(in, *inlen, start_marker, st_off);
  /* we wannt to find the last one */
  const char *tag_start = match;
  while(match) {
    match = mtev_memmem(tag_start+1, (*inlen) - (tag_start + 1 - in),
                        start_marker, st_off);
    if(match) tag_start = match;
  }
  if(!tag_start || *end != end_marker) return mtev_false;
  if(tag_start + st_off == end) { /* empty */
    *inlen -= st_off + 1;
    return mtev_true;
  }

  if(!noit_metric_tagset_builder_add_many(builder, tag_start + st_off, end - (tag_start + st_off)))
    return mtev_false;

  *inlen = tag_start - in;
  return mtev_true;
}
static void
relative_tags_adjust(noit_metric_tagset_t *tags, const char *oldbase, const char *newbase) {
  /* This is evil, this takes a set of stream tags points pointing somewhere into oldbase
   * and rewrites them pointing into newbase.  This is useful when you've memcpyd the underlying
   * string and need to smudge all the pointers. */
  for(int i=0; i<tags->tag_count; i++) {
    mtevAssert(tags->tags[i].tag >= oldbase && tags->tags[i].tag - oldbase < MAX_METRIC_TAGGED_NAME);
    size_t offset = tags->tags[i].tag - oldbase;
    tags->tags[i].tag = newbase + offset;
  }
}
static int
noit_metric_process_tags_phase(noit_metric_message_t *metric, int phase) {
  noit_metric_tagset_builder_t stream_builder, measurement_builder;
  noit_metric_tagset_builder_start(&stream_builder);
  noit_metric_tagset_builder_start(&measurement_builder);
  int starting_name_len = metric->id.name_len;
  char buff[MAX_METRIC_TAGGED_NAME];
  metric->id.name_len_with_tags = metric->id.name_len;
  if(starting_name_len > sizeof(buff)) return -1;
  while(
    noit_metric_extract_tags(metric->id.name, &metric->id.name_len,
                             "|ST[", ']', &stream_builder) ||
    noit_metric_extract_tags(metric->id.name, &metric->id.name_len,
                             "|MT{", '}', &measurement_builder)
  );
  if(phase == 1) {
    const char *stag_start = NULL, *mtag_start = NULL;
    char *stagnm = NULL, *mtagnm = NULL;
    noit_metric_tagset_builder_end(&stream_builder, &metric->id.stream, &stagnm);
    noit_metric_tagset_builder_end(&measurement_builder, &metric->id.measurement, &mtagnm);
    int stagnmlen = stagnm ? strlen(stagnm) : 0;
    int mtagnmlen = mtagnm ? strlen(mtagnm) : 0;
    if((stagnm ? stagnmlen + 5 : 0) +
       (mtagnm ? mtagnmlen + 5 : 0) +
       metric->id.name_len > starting_name_len) return -1;
    char *out = buff;
    memcpy(out, metric->id.name, metric->id.name_len);
    out += metric->id.name_len;
    if(stagnm) {
      memcpy(out, "|ST[", 4);
      out += 4;
      memcpy(out, stagnm, stagnmlen);
      stag_start = out;
      out += stagnmlen;
      *out++ = ']';
    }
    if(mtagnm) {
      memcpy(out, "|MT{", 4);
      out += 4;
      memcpy(out, mtagnm, mtagnmlen);
      mtag_start = out;
      out += mtagnmlen;
      *out++ = '}';
    }
    free(stagnm);
    free(mtagnm);
    if(out - buff >= MAX_METRIC_TAGGED_NAME - 1) return -1;
    *out = '\0';

    if(!noit_metric_name_is_clean(metric->id.name, metric->id.name_len) ||
       out-buff != metric->id.name_len_with_tags ||
       memcmp(buff, metric->id.name, metric->id.name_len_with_tags)) {
      metric->id.alloc_name = strdup(buff);
      size_t initial_just_name_len = metric->id.name_len;
      metric->id.name_len = noit_metric_clean_name(metric->id.alloc_name, metric->id.name_len);
      if(metric->id.name_len > initial_just_name_len) return -1;
      if(initial_just_name_len != metric->id.name_len) {
        size_t bump = initial_just_name_len - metric->id.name_len;
        memmove(metric->id.alloc_name + metric->id.name_len,
                metric->id.alloc_name + initial_just_name_len,
                (out-buff) - initial_just_name_len);
        out -= bump;
      }
      metric->id.name_len = out-buff;
      metric->id.name = metric->id.alloc_name;
      metric->id.name_len_with_tags = metric->id.name_len;
      free(metric->id.stream.tags);
      metric->id.stream.tags = NULL;
      metric->id.stream.tag_count = 0;
      free(metric->id.measurement.tags);
      metric->id.measurement.tags = NULL;
      metric->id.measurement.tag_count = 0;
      return noit_metric_process_tags_phase(metric, 2);
    }

    /* name was canonical, so fix up out tags to point back into that */
    if(stag_start) {
      /* coverity[USE_AFTER_FREE] */
      relative_tags_adjust(&metric->id.stream, stagnm, metric->id.name + (stag_start - buff));
    }
    if(mtag_start) {
      /* coverity[USE_AFTER_FREE] */
      relative_tags_adjust(&metric->id.measurement, mtagnm, metric->id.name + (mtag_start - buff));
    }
  }
  else {
    noit_metric_tagset_builder_end(&stream_builder, &metric->id.stream, NULL);
    noit_metric_tagset_builder_end(&measurement_builder, &metric->id.measurement, NULL);
  }
  return metric->id.name_len == 0 ||
         mtev_memmem(metric->id.name, metric->id.name_len, "|ST[", 4) ||
         mtev_memmem(metric->id.name, metric->id.name_len, "|MT{", 4);
}
int
noit_metric_process_tags(noit_metric_message_t *metric) {
  return noit_metric_process_tags_phase(metric, 1);
}
int noit_is_timestamp(const char *line, int len) {
  int is_ts = 0;
  int state = 0, cnt[2] = { 0, 0 };
  const char *end = line + len - 1;
  const char *possible = line;
  while(possible < end && *possible != '\t') {
    if(state == 0 && *possible == '.') {
      state = 1;
    }
    else if(isdigit(*possible)) {
      cnt[state]++;
    }
    else break;
    possible++;
    if(*possible == '\t') {
      if(cnt[1] == 3 && cnt[0] > 0) is_ts = 1;
    }
  }
  return is_ts;
}
int noit_message_decoder_parse_line(noit_metric_message_t *message, int has_noit) {
  const char *cp, *metric_type_str, *time_str, *check_id_str;
  char *value_str;
  char *dp, id_str_copy[UUID_PRINTABLE_STRING_LENGTH];
  cp = message->original_message;

  // Go to the timestamp column
  MOVE_TO_NEXT_TAB(cp, time_str);
  if(time_str == NULL)
    return -1;
  message->noit.name = NULL;
  message->noit.name_len = 0;

  if(has_noit == -1) {
    /* auto-detect */
    const char *end = message->original_message + message->original_message_len - 1;
    const char *possible = time_str;
    has_noit = !noit_is_timestamp(possible, (end-possible));
  }

  if(has_noit == 1) { // non bundled messages store the source IP in the second column
    const char *nname = time_str;
    message->noit.name = nname;
    MOVE_TO_NEXT_TAB(cp, time_str);
    if(time_str == NULL)
      return -1;
    message->noit.name_len = time_str - nname - 1;
  }

  /* extract time */
  message->value.whence_ms = strtoull(time_str, &dp, 10);
  message->value.whence_ms *= 1000; /* s -> ms */
  if(dp && *dp == '.')
    message->value.whence_ms += (int) (1000.0 * atof(dp));

  MOVE_TO_NEXT_TAB(cp, check_id_str);
  if(!check_id_str)
    return -2;
  MOVE_TO_NEXT_TAB(cp, message->id.name);
  if(!message->id.name)
    return -3;

  const char *acctid_str = check_id_str;
  acctid_str = memchr(acctid_str, '`', message->id.name - acctid_str);
  if(acctid_str) acctid_str = memchr(acctid_str+1, '`', message->id.name - acctid_str - 1);
  if(acctid_str && acctid_str < message->id.name - 6 && memcmp(acctid_str, "`c_", 3) == 0) {
    acctid_str += 3;
    const char *fe = memchr(acctid_str, '_', message->id.name - acctid_str);
    if(fe) message->id.account_id = strtoull(acctid_str, NULL, 10);
  }

  memcpy(id_str_copy, message->id.name - UUID_STR_LEN - 1, UUID_STR_LEN);
  id_str_copy[UUID_STR_LEN] = '\0';

  if(mtev_uuid_parse(id_str_copy, message->id.id) != 0) {
    mtevL(mtev_error, "mtev_uuid_parse(%s) failed\n", id_str_copy);
    return -7;
  }

  if(message->id.name - check_id_str < UUID_PRINTABLE_STRING_LENGTH)
    return -6;

  if(*message->original_message == 'M') {
    MOVE_TO_NEXT_TAB(cp, metric_type_str);
    if(!metric_type_str)
      return -4;
    MOVE_TO_NEXT_TAB(cp, value_str);
    if(!value_str)
      return -5;

    message->id.name_len = metric_type_str - message->id.name - 1;
    if(message->id.name_len > MAX_METRIC_TAGGED_NAME) {
      return -8;
    }
    if(noit_metric_process_tags(message) != 0) {
      return -9;
    }

    message->value.type = *metric_type_str;

    int vlen = (uintptr_t)message->original_message +
               (uintptr_t)message->original_message_len -
               (uintptr_t)value_str;

    if((vlen == 8 && !memcmp(value_str, "[[null]]", 8)) ||
       (vlen == 9 && !memcmp(value_str, "[[null]]\n", 9))) {
      message->value.is_null = mtev_true;
    } else {
      char osnum[512]; /* that's a big number! */
      int nlen = (vlen >= sizeof(osnum)) ? (sizeof(osnum)-1) : vlen;
      memcpy(osnum, value_str, nlen);
      osnum[nlen] = '\0';
      switch (*metric_type_str) {
      case METRIC_INT32:
        message->value.value.v_int32 = strtol(osnum, NULL, 10);
        break;
      case METRIC_UINT32:
        message->value.value.v_uint32 = strtoul(osnum, NULL, 10);
        break;
      case METRIC_INT64:
        message->value.value.v_int64 = strtoll(osnum, NULL, 10);
        break;
      case METRIC_UINT64:
        message->value.value.v_uint64 = strtoull(osnum, NULL, 10);
        break;
      case METRIC_DOUBLE:
        message->value.value.v_double = strtod(osnum, NULL);
        break;
      case METRIC_STRING:
        /* It's possible for M records that the \n is included, it should not be. */
        if(vlen > 0 && value_str[vlen-1] == '\n') vlen--;
        message->value.value.v_string = mtev_strndup(value_str, vlen);
        break;
      default:
        return -9;
      }
    }
    return 1;
  }

  if(*message->original_message == 'H') {
    MOVE_TO_NEXT_TAB(cp, value_str);
    if(!value_str)
      return -4;

    message->id.name_len = value_str - message->id.name - 1;
    if(noit_metric_process_tags(message) != 0) {
      return -7;
    }

    int vstrlen = strlen(value_str);

    while ((vstrlen > 1) && (value_str[vstrlen - 1] == '\n'))
      vstrlen--;
    message->value.type = METRIC_STRING;
    if(vstrlen == 0)
      message->value.value.v_string = NULL;
    else {
      message->value.value.v_string = mtev_strndup(value_str, vstrlen);
    }
    return 1;
  }

  if(*message->original_message == 'S' ||
     *message->original_message == 'C' ||
     *message->original_message == 'D') {
    message->value.type = METRIC_GUESS;
    message->value.value.v_string = NULL;
    return 1;
  }

  return 0;
}

void noit_metric_message_clear(noit_metric_message_t* message) {
  if(message->original_message) {
    if(message->value.type == METRIC_STRING &&
       !message->value.is_null && message->value.value.v_string) {
      free(message->value.value.v_string);
    }
    if(message->original_allocated) free(message->original_message);
    message->original_message = NULL;
  }
  free(message->id.alloc_name);
  free(message->id.stream.tags);
  message->id.stream.tag_count = 0;
  free(message->id.measurement.tags);
  message->id.measurement.tag_count = 0;
}
void noit_metric_message_free(noit_metric_message_t* message) {
  noit_metric_message_clear(message);
  free(message);
}


size_t
noit_metric_tags_count(const char *str, size_t strlen) {
  if(!strlen) return 0;
  size_t rval = 1;
  while(strlen > 0) {
    if(*str == ',') rval++;
    str++;
    strlen--;
  }
  return rval;
}

const char *
noit_metric_tags_parse_one(const char *tagnm, size_t tagnmlen,
                           noit_metric_tag_t *output, mtev_boolean *toolong) {
  size_t colon_pos = 0;
  size_t cur_size = 0;
  *toolong = mtev_false;
  while(cur_size < tagnmlen) {
    char test_char = tagnm[cur_size];
    if(test_char == ':' && !colon_pos) {
      if(!cur_size) {
        /* need at least one byte for category name. */
        return 0;
      }
      colon_pos = cur_size;
      if(!noit_metric_tagset_is_taggable_key(tagnm, cur_size)) return 0;
    }
    else if(test_char == ',') {
      /* tag-separation char, terminates this loop. */
      if(!(cur_size>colon_pos) ||
         !noit_metric_tagset_is_taggable_value(&tagnm[colon_pos+1], cur_size-colon_pos-1)) return 0;
      break;
    }
    cur_size++;
  }
  /* make sure we covered everything */
  if(colon_pos == 0) return 0;
  /* tag category and name must combined be <= NOIT_TAG_MAX_PAIR_LEN */
  if(cur_size > NOIT_TAG_MAX_PAIR_LEN) {
    *toolong = mtev_true;
    return tagnm + cur_size;
  }
  output->total_size = cur_size;
  output->category_size = colon_pos + 1;
  output->tag = tagnm;
  return tagnm + cur_size;
}

static int
tag_canonical_size(noit_metric_tag_t *tag) {
  int len;
  char dbuff[NOIT_TAG_MAX_PAIR_LEN];
  len = noit_metric_tagset_decode_tag(dbuff, sizeof(dbuff), tag->tag, tag->total_size);
  if(len < 0) return 0;
  len = noit_metric_tagset_encode_tag(dbuff, sizeof(dbuff), dbuff, len);
  if(len < 0) return 0;
  return len;
}
size_t
noit_metric_tags_compact(noit_metric_tag_t *tags, size_t tag_count,
                         size_t *canonical_size_out) {
  if(tag_count <= 0) {
    /* no compaction necessary */
    *canonical_size_out = 0;
    return tag_count;
  }
  else if(tag_count == 1) {
    *canonical_size_out = tag_canonical_size(&tags[0]);
    return tag_count;
  }
  /* output should be in lexically-sorted form */
  qsort((void *) tags, tag_count, sizeof(noit_metric_tag_t), noit_metric_tags_compare);

  /* and no tags should repeat */
  size_t squash_index_left = 0;
  size_t squash_index_right = 1;
  size_t sum_tags_len = 0;
  while(squash_index_right < tag_count) {
    sum_tags_len += tag_canonical_size(&tags[squash_index_left]);

    while(noit_metric_tags_compare(&tags[squash_index_left], &tags[squash_index_right]) == 0) {
      squash_index_right++;
      if(squash_index_right >= tag_count) {
        *canonical_size_out = sum_tags_len + squash_index_left;
        return squash_index_left + 1;
      }
    }
    squash_index_left++;
    if(squash_index_left != squash_index_right)
      memcpy((void *) &tags[squash_index_left], &tags[squash_index_right], sizeof(noit_metric_tag_t));
    else
      squash_index_right++;
  }
  *canonical_size_out = sum_tags_len + tag_canonical_size(&tags[squash_index_left]) + squash_index_left;
  return squash_index_left + 1;
}

ssize_t
noit_metric_tags_parse(const char *tagnm, size_t tagnmlen,
                       noit_metric_tag_t *tags, size_t tag_count,
                       size_t *canonical_size_out) {
  if(tagnmlen == 0) return 0;

  size_t rval = 0;
  while(tagnmlen > 0 && rval < tag_count) {
    mtev_boolean toolong = mtev_false;
    const char *next = noit_metric_tags_parse_one(tagnm, tagnmlen, &tags[rval], &toolong);
    if(!next) return -1;

    tagnmlen -= (next - tagnm);
    tagnm = next;
    if(!toolong) rval++;
    if(tagnmlen > 0) {
      /* there's string left, we require it to be ",<next_tag>". */
      if(*tagnm != ',' || tagnmlen == 1) return -1;
      tagnm++;
      tagnmlen--;
    }
  }

  return (ssize_t) noit_metric_tags_compact(tags, rval, canonical_size_out);
}

ssize_t
noit_metric_tags_canonical(const noit_metric_tag_t *tags,
                           size_t tag_count, char *tagnm, size_t tagnmlen,
                           mtev_boolean already_decoded) {
  char dbuff[NOIT_TAG_MAX_PAIR_LEN];
  if(!tag_count) return 0;
  size_t rval = 0;
  while(tag_count > 0) {
    int dlen;
    if(already_decoded) {
      dlen = noit_metric_tagset_encode_tag(dbuff, sizeof(dbuff), tags->tag, tags->total_size);
      if(dlen < 0) return -1;
    } else {
      dlen = noit_metric_tagset_decode_tag(dbuff, sizeof(dbuff), tags->tag, tags->total_size);
      if(dlen < 0) return -1;
      dlen = noit_metric_tagset_encode_tag(dbuff, sizeof(dbuff), dbuff, dlen);
      if(dlen < 0) return -1;
    }
    if(tagnmlen < dlen) return -1;
    memcpy(tagnm, dbuff, dlen);
    tagnm += dlen;
    tagnmlen -= dlen;
    rval += dlen;
    tags++;
    tag_count--;
    if(tag_count > 0) {
      if (tagnmlen <= 0) return -1;
      *tagnm++ = ',';
      rval++;
      tagnmlen--;
    }
  }
  return rval;
}

char *
noit_metric_tagset_from_tags(noit_metric_tagset_t *lookup,
                             noit_metric_tag_t *tags,
                             size_t tag_count,
                             size_t canonical_size) {
  /* the canonical representation does not include a trailing NULL, but we make room for one
   * anyway... helps our display logic. */
  char *canonical = malloc(canonical_size + 1);
  if(!canonical) return NULL;
  if(noit_metric_tags_canonical(tags, tag_count, canonical, canonical_size, mtev_false) != canonical_size) {
    free((void *) canonical);
    return NULL;
  }
  canonical[canonical_size] = '\0';
  noit_metric_tags_parse(canonical, canonical_size, tags, tag_count, &canonical_size);

  return canonical;
}

int
noit_metric_tagset_init(noit_metric_tagset_t *lookup,
                        const char *tagsetstr, size_t tagsetstrlen) {
  memset((void *) lookup, '\0', sizeof(*lookup));
  size_t tag_count = noit_metric_tags_count(tagsetstr, tagsetstrlen);
  if(tag_count == 0) {
    lookup->tags = 0;
    lookup->tag_count = 0;
    lookup->canonical_size = 0;
    return 0;
  }
  noit_metric_tag_t *tags = (noit_metric_tag_t *) calloc(tag_count, sizeof(noit_metric_tag_t));
  size_t canonical_size;
  ssize_t parsed_tag_count = noit_metric_tags_parse(tagsetstr, tagsetstrlen, tags, tag_count, &canonical_size);
  if(parsed_tag_count <= 0) {
    free((void *) tags);
    return -1;
  }
  lookup->tags = realloc(tags, tag_count * sizeof(noit_metric_tag_t));
  lookup->tag_count = tag_count;
  lookup->canonical_size = canonical_size;
  return 0;
}

void
noit_metric_tagset_cleanup(noit_metric_tagset_t *lookup) {
  free((void *) lookup->tags);
  memset((void *) lookup, '\0', sizeof(*lookup));
}

mtev_boolean
noit_metric_tagset_is_populated(const noit_metric_tagset_t *tagset) {
  return (tagset->tags != 0) ? mtev_true : mtev_false;
}

void
noit_metric_tagset_builder_start(noit_metric_tagset_builder_t *builder) {
  builder->list = 0;
  builder->tag_count = 0;
  builder->sum_tag_size = 0;
}

void
noit_metric_tagset_builder_clean(noit_metric_tagset_builder_t *builder) {
  noit_metric_tagset_builder_el_t *iter = builder->list;
  while(iter) {
    noit_metric_tagset_builder_el_t *to_free = iter;
    iter = iter->next;
    free((void *) to_free);
  }
  builder->list = 0;
  builder->tag_count = 0;
}

mtev_boolean
noit_metric_tagset_builder_add_many(noit_metric_tagset_builder_t *builder,
                                    const char *tagstr, size_t tagstr_len) {
  if(builder->tag_count < 0) return mtev_false;
  noit_metric_tag_t tag;
  while(tagstr_len > 0) {
    mtev_boolean toolong = mtev_false;
    const char *next = noit_metric_tags_parse_one(tagstr, tagstr_len, &tag, &toolong);
    if(toolong) {
      /* do nothing at all, the tag must be dropped */
    } else if(next && (tag.total_size == tagstr_len || *next == ',')) {
      noit_metric_tagset_builder_el_t *el =
        (noit_metric_tagset_builder_el_t *) calloc(1, sizeof(noit_metric_tagset_builder_el_t));
      el->tag = tag;
      el->next = builder->list;
      builder->list = el;
      builder->sum_tag_size += tag.total_size;
      builder->tag_count++;
    } else {
      noit_metric_tagset_builder_clean(builder);
      builder->tag_count = -1;
      return mtev_false;
    }
    tagstr += tag.total_size;
    tagstr_len -= tag.total_size;
    if(*next == ',') {
      tagstr++;
      tagstr_len--;
    }
  }
  return mtev_true;
}

mtev_boolean
noit_metric_tagset_builder_add_one(noit_metric_tagset_builder_t *builder,
                                   const char *tagstr, size_t tagstr_len) {
  if(builder->tag_count < 0) return mtev_false;
  noit_metric_tag_t tag;
  mtev_boolean toolong = mtev_false;
  if(!noit_metric_tags_parse_one(tagstr, tagstr_len, &tag, &toolong) || tag.total_size != tagstr_len) {
    noit_metric_tagset_builder_clean(builder);
    builder->tag_count = -1;
    return mtev_false;
  }
  if(toolong) return mtev_true;
  noit_metric_tagset_builder_el_t *el =
    (noit_metric_tagset_builder_el_t *) calloc(1, sizeof(noit_metric_tagset_builder_el_t));
  el->tag = tag;
  el->next = builder->list;
  builder->list = el;
  builder->sum_tag_size += tag.total_size;
  builder->tag_count++;
  return mtev_true;
}

mtev_boolean
noit_metric_tagset_builder_end(noit_metric_tagset_builder_t *builder,
                               noit_metric_tagset_t *out, char **canon) {
  if(builder->tag_count <= 0) {
    return builder->tag_count == 0 ? mtev_true : mtev_false;
  }
  mtev_boolean success = mtev_true;

  if(out) {
    out->tags = (noit_metric_tag_t *) realloc(out->tags, (out->tag_count + builder->tag_count) * sizeof(noit_metric_tag_t));
    size_t tag_index = out->tag_count;
    noit_metric_tagset_builder_el_t *iter = builder->list;
    while (iter) {
      out->tags[out->tag_count] = iter->tag;
      iter = iter->next;
      out->tag_count++;
    }
    size_t canonical_size;
    out->tag_count = noit_metric_tags_compact(out->tags, out->tag_count, &canonical_size);
    out->canonical_size = canonical_size;
    if(builder->tag_count != tag_index)
      out->tags = (noit_metric_tag_t *) realloc((void *)out->tags, out->tag_count * sizeof(noit_metric_tag_t));
    if(canon) {
      *canon = noit_metric_tagset_from_tags(out, out->tags, out->tag_count, canonical_size);
      if(*canon == NULL) success = mtev_false;
    }
  }

  noit_metric_tagset_builder_clean(builder);
  return success;
}
