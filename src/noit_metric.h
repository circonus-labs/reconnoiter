/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
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

#ifndef _NOIT_METRIC_H
#define _NOIT_METRIC_H

#include <mtev_defines.h>
#include <mtev_hooks.h>
#include <mtev_uuid.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_METRIC_TAGGED_NAME 4096
#define MAX_TAGS 256

typedef enum {
  NOIT_METRIC_TAGSET_CHECK = 1,
  NOIT_METRIC_TAGSET_STREAM = 2,
  NOIT_METRIC_TAGSET_MEASUREMENT = 3,
} noit_metric_tagset_class_t;

typedef enum {
  METRIC_ABSENT = 0,
  METRIC_GUESS = '0',
  METRIC_INT32 = 'i',
  METRIC_UINT32 = 'I',
  METRIC_INT64 = 'l',
  METRIC_UINT64 = 'L',
  METRIC_DOUBLE = 'n',
  METRIC_STRING = 's',
  METRIC_HISTOGRAM = 'h',
  METRIC_HISTOGRAM_CUMULATIVE = 'H'
} metric_type_t;

typedef enum {
  NOIT_METRIC_ENCODE_DEFAULT = '"',
  NOIT_METRIC_ENCODE_EXACT = '!',
  NOIT_METRIC_ENCODE_REGEX = '/'
} noit_metric_encode_type_t;

#define IS_METRIC_TYPE_NUMERIC(t) \
  ((t) == METRIC_INT32 || (t) == METRIC_UINT32 || \
   (t) == METRIC_INT64 || (t) == METRIC_UINT64 || (t) == METRIC_DOUBLE)

typedef struct {
  char *metric_name;
  metric_type_t metric_type;
  union {
    double *n;
    int32_t *i;
    uint32_t *I;
    int64_t *l;
    uint64_t *L;
    char *s;
    void *vp; /* used for clever assignments */
  } metric_value;
  mtev_boolean logged;
  unsigned long accumulator; /* used to track divisor of averages */
  struct timeval whence; /* if non-zero, specifies a time */
  char *expanded_metric_name;
} metric_t;

typedef enum {
  MESSAGE_TYPE_C = 'C',
  MESSAGE_TYPE_D = 'D',
  MESSAGE_TYPE_S = 'S',
  MESSAGE_TYPE_H = 'H',
  MESSAGE_TYPE_M = 'M'
} noit_message_type;

#define NOIT_TAG_MAX_PAIR_LEN 256
/* category_size is uint8_t (255), but includes the : */
#define NOIT_TAG_MAX_CAT_LEN  254
#define NOIT_TAG_DECODED_SEPARATOR 0x1f

typedef struct {
  uint16_t total_size;
  uint8_t category_size;
  const char *tag;
} noit_metric_tag_t;

typedef struct noit_metric_tagset_t {
  noit_metric_tag_t *tags;
  int tag_count;
  int canonical_size;
} noit_metric_tagset_t;

typedef struct {
  uuid_t id;
  const char *name;
  int name_len;
  int name_len_with_tags;
  uint64_t account_id;
  noit_metric_tagset_t check;
  noit_metric_tagset_t stream;
  noit_metric_tagset_t measurement;
  char *alloc_name;
} noit_metric_id_t;

typedef struct {
  const char *name;
  int name_len;
} noit_noit_t;

typedef struct {
  uint64_t whence_ms; /* when this was recieved */
  metric_type_t type; /* the type of the following data item */
  mtev_boolean is_null;
  union {
    int32_t v_int32;
    uint32_t v_uint32;
    int64_t v_int64;
    uint64_t v_uint64;
    double v_double;
    char *v_string;
    metric_type_t v_type_if_absent;
  } value; /* the data itself */
} noit_metric_value_t;

typedef struct {
  noit_metric_id_t id;
  noit_metric_value_t value;
  noit_message_type type;
  mtev_boolean original_allocated;
  char* original_message;
  size_t original_message_len;
  uint32_t refcnt;
  noit_noit_t noit;
} noit_metric_message_t;

void noit_metric_to_json(noit_metric_message_t *metric, char **json, size_t *len, mtev_boolean include_original);


/* If possible coerce the metric to a double, return success */
API_EXPORT(mtev_boolean) noit_metric_as_double(metric_t *m, double *);

typedef struct noit_metric_tagset_builder_el_t {
  struct noit_metric_tagset_builder_el_t *next;
  noit_metric_tag_t tag;
} noit_metric_tagset_builder_el_t;

typedef struct noit_metric_tagset_builder_t {
  noit_metric_tagset_builder_el_t *list;
  /* gets set to -1 if we attempt to add an invalid tag. */
  int tag_count;
  int sum_tag_size;
} noit_metric_tagset_builder_t;

API_EXPORT(mtev_boolean)
  noit_metric_tagset_is_taggable_b64_char(char c);

API_EXPORT(mtev_boolean)
  noit_metric_tagset_is_taggable_key(const char *key, size_t len);
API_EXPORT(mtev_boolean)
  noit_metric_tagset_is_taggable_value(const char *val, size_t len);
API_EXPORT(ssize_t)
  noit_metric_tagset_encode_tag(char *encoded_tag, size_t max_len, 
                                const char *decoded_tag, size_t decoded_len);
API_EXPORT(ssize_t)
  noit_metric_tagset_encode_tag_for_search(char *encoded_tag, size_t max_len, 
                                           const char *decoded_tag, size_t decoded_len,
                                           noit_metric_encode_type_t left,
                                           noit_metric_encode_type_t right);

/*!
 * \brief Decode a single tag from \a encoded_tag and outputs it to
 * \a decoded_tag
 *
 * \param decoded_tag Receives the value of the tag once decoded.  This
 * string will be null terminated if and only if there is an extra byte
 * available in the buffer to write the null terminator
 * \param max_len The number of bytes available to write in \a decoded_tag
 * \param encoded_tag Contains the encoded tag name to decode
 * \param encoded_size The length in bytes of \a encoded_tag
 * \return The size of the output of \a decoded_tag, this string is only
 * null terminate if an extra byte is available at the end of the output
 * buffer to facilitate logging.  It should not be considered a null
 * terminated string
 */
API_EXPORT(ssize_t)
  noit_metric_tagset_decode_tag(char *decoded_tag, size_t max_len, 
                                const char *encoded_tag, size_t encoded_size);
API_EXPORT(int)
  noit_metric_tagset_init(noit_metric_tagset_t *lookup, const char *tagstr, size_t tagstr_len);
API_EXPORT(void)
  noit_metric_tagset_cleanup(noit_metric_tagset_t *lookup);
API_EXPORT(mtev_boolean)
  noit_metric_tagset_is_populated(const noit_metric_tagset_t *tagset);

API_EXPORT(void)
  noit_metric_tagset_builder_start(noit_metric_tagset_builder_t *builder);
API_EXPORT(mtev_boolean)
  noit_metric_tagset_builder_add_many(noit_metric_tagset_builder_t *builder,
                                      const char *tagset,
                                      size_t tagstr_len);
API_EXPORT(mtev_boolean)
  noit_metric_tagset_builder_add_one(noit_metric_tagset_builder_t *builder,
                                     const char *tagset,
                                     size_t tagstr_len);
API_EXPORT(mtev_boolean)
  noit_metric_tagset_builder_end(noit_metric_tagset_builder_t *builder, noit_metric_tagset_t *out,
                                 char **canonical);

API_EXPORT(ssize_t)
  noit_metric_tags_canonical(const noit_metric_tag_t *tags,
                             size_t tag_count, char *tagnm, size_t tagnmlen,
                             mtev_boolean already_decoded);

API_EXPORT(mtev_boolean)
  noit_metric_name_is_clean(const char *m, size_t len);

API_EXPORT(size_t)
  noit_metric_clean_name(char *m, size_t len);

API_EXPORT(ssize_t)
  noit_metric_canonicalize_ex(const char *input, size_t input_len, char *output, size_t output_len,
                              mtev_boolean null_term, mtev_boolean include_stream_tags,
                              mtev_boolean include_measurement_tags);

API_EXPORT(ssize_t)
  noit_metric_canonicalize(const char *input, size_t input_len, char *output, size_t output_len,
                           mtev_boolean null_term);

API_EXPORT(ssize_t)
  noit_metric_parse_tags(const char *input, size_t input_len,
                         noit_metric_tagset_t *stset, noit_metric_tagset_t *mtset);


MTEV_HOOK_PROTO(noit_metric_tagset_fixup,
                (noit_metric_tagset_class_t, noit_metric_tagset_t *),
                void *, closure,
                (void *closure, noit_metric_tagset_class_t cls, noit_metric_tagset_t *tagset))

API_EXPORT(const char *)
  noit_metric_tags_parse_one(const char *tagnm, size_t tagnmlen,
                             noit_metric_tag_t *output, mtev_boolean *toolong);

API_EXPORT(const char *)
  noit_metric_get_full_metric_name(metric_t *m);

static inline int
noit_metric_tags_compare(const void *v_l, const void *v_r) {
  const noit_metric_tag_t *l = (noit_metric_tag_t *) v_l;
  const noit_metric_tag_t *r = (noit_metric_tag_t *) v_r;
  char lb[NOIT_TAG_MAX_PAIR_LEN], rb[NOIT_TAG_MAX_PAIR_LEN];
  if(l->total_size > NOIT_TAG_MAX_PAIR_LEN) return -1;
  if(r->total_size > NOIT_TAG_MAX_PAIR_LEN) return -1;
  int llen = noit_metric_tagset_decode_tag(lb, sizeof(lb), l->tag, l->total_size);
  if(llen < 0) return -1;
  int rlen = noit_metric_tagset_decode_tag(rb, sizeof(rb), r->tag, r->total_size);
  if(rlen < 0) return -1;
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

#ifdef __cplusplus
}
#endif
#endif
