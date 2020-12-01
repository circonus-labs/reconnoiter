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

#include <mtev_defines.h>
#include <mtev_b64.h>
#include <mtev_str.h>
#include <mtev_log.h>
#include <mtev_dyn_buffer.h>
#include <mtev_memory.h>
#include "noit_metric_tag_search.h"

#include <stdio.h>
#include <ck_pr.h>

void
noit_metric_tag_search_free(noit_metric_tag_search_ast_t *node) {
  if(node == NULL) return;
  if(!ck_pr_dec_32_is_zero(&node->refcnt)) return;
  noit_metric_tag_search_reset(node);
  free(node);
}

void
noit_metric_tag_search_reset(noit_metric_tag_search_ast_t *node) {
  switch(node->operation) {
    case OP_MATCH:
      free(node->contents.spec.cat.str);
      free(node->contents.spec.name.str);
      if(node->contents.spec.cat.re) pcre_free(node->contents.spec.cat.re);
      if(node->contents.spec.cat.re_e) 
#ifdef PCRE_STUDY_JIT_COMPILE
        pcre_free_study(node->contents.spec.cat.re_e);
#else
        pcre_free(node->contents.spec.cat.re_e);
#endif
      if(node->contents.spec.name.re) pcre_free(node->contents.spec.name.re);
      if(node->contents.spec.name.re_e)
#ifdef PCRE_STUDY_JIT_COMPILE
        pcre_free_study(node->contents.spec.name.re_e);
#else
        pcre_free(node->contents.spec.name.re_e);
#endif
      break;
    /* All ARGS */
    default:
      for(int i=0;i<node->contents.args.cnt; i++) {
        noit_metric_tag_search_free(node->contents.args.node[i]);
      }
      free(node->contents.args.node);
  }
  if(node->user_data_free) node->user_data_free(node->user_data);
}

noit_metric_tag_search_ast_t *
noit_metric_tag_search_ref(noit_metric_tag_search_ast_t *node) {
  ck_pr_inc_32(&node->refcnt);
  return node;
}

static char *
build_regex_from_expansion(const char *expansion) {
  /* brace expansion has already been done, all we need to do here
 *    * is translate '*' to '[^.]*', '?' to '[^.]', and '.' to '\.',
 *       */
  size_t ridx = 0;
  char *regex = (char *)calloc(1, strlen(expansion) * 2 + 3); /* 2 == '.*' */
  regex[ridx++] = '^';
  for (size_t i = 0; i < strlen(expansion); i++) {
    if (expansion[i] == '*') {
      regex[ridx++] = '.';
      regex[ridx++] = '*';
    } else if (expansion[i] == '?') {
      regex[ridx++] = '.';
    } else if (expansion[i] == '.') {
      regex[ridx++] = '\\';
      regex[ridx++] = '.';
    } else {
      regex[ridx++] = expansion[i];
    }
  }
  regex[ridx++] = '$';
  return regex;
}

static mtev_boolean
noit_metric_tag_match_compile(struct noit_var_match_t *m, const char **endq, int part) {
  char decoded_tag[512];
  const char *query = *endq;
  const char *error;
  int erroffset;
  int is_encoded_match = memcmp(query, "b!", 2) == 0;
  int is_encoded = is_encoded_match || memcmp(query, "b\"", 2) == 0 || memcmp(query, "b/", 2) == 0;
  if (is_encoded) {
    (*endq)++; // skip the 'b'
    query = *endq;
  }
  if(*query == '/') {
    *endq = query+1;
    while(**endq && **endq != ',' && **endq != ')' && (part == 2 || **endq != ':')) (*endq)++;
    if(**endq != ',' && **endq != ')' && (part == 2 || **endq != ':')) return mtev_false;
    (*endq)--;
    if(*endq <= query) return mtev_false;
    if(**endq != '/') {
      /* not a regex */
      *endq = query;
      goto not_a_regex;
    }
    if (is_encoded) {
      int len = mtev_b64_decode(query + 1, *endq - query - 1, (unsigned char *)decoded_tag, 
				sizeof(decoded_tag));
      if (len == 0) return mtev_false;
      m->str = mtev_strndup(decoded_tag, len);
    } else {
      m->str = mtev_strndup(query + 1, *endq - query - 1);
    }
    m->re = pcre_compile(m->str,
                         0, &error, &erroffset, NULL);
    if(!m->re) {
      if (!is_encoded) *endq = query+1+erroffset;
      else *endq = query+1; // we can't easily know where in the encoded query the problem lies, so best to point to beginning of query
      return mtev_false;
    }
    (*endq)++;
  }
  else {
    not_a_regex:
    if (is_encoded) {
      if (!is_encoded_match && *query != '"') return mtev_false;
      if (is_encoded_match && *query != '!') return mtev_false;
      *endq = query + 1;
      query = *endq;
    }

    while(**endq &&
          (is_encoded ?
           noit_metric_tagset_is_taggable_b64_char(**endq) :
           (
            (part == 2 ?
               noit_metric_tagset_is_taggable_value(*endq, 1) :
               noit_metric_tagset_is_taggable_key(*endq, 1)
            ) || **endq == '*' || **endq == '?'
           )
          )
         )
    {
      (*endq)++;
    }

    if(*endq == query && part == 1) return mtev_false;
    if (is_encoded) {
      int len = mtev_b64_decode(query, *endq - query, (unsigned char *)decoded_tag, 
				sizeof(decoded_tag));
      if (len == 0 && part == 1) return mtev_false;
      m->str = mtev_strndup(decoded_tag, len);
      (*endq)++; // skip the trailing quotation mark
    } else {
      m->str = mtev_strndup(query, *endq - query);
    }
    if((strchr(m->str, '*') || strchr(m->str, '?')) && !is_encoded_match) {
      char *previous = m->str;
      m->str = build_regex_from_expansion(m->str);
      free(previous);
      m->re = pcre_compile(m->str,
                           0, &error, &erroffset, NULL);
      if(!m->re) {
        *endq = query; /* We don't know where in the original string */
        return mtev_false;
      }
    }
  }
  return mtev_true;
}

static noit_metric_tag_search_ast_t *
noit_metric_tag_part_parse(const char *query, const char **endq, mtev_boolean allow_match) {
  noit_metric_tag_search_ast_t *node = NULL;
  *endq = query;
  if(!strncmp(query, "and(", 4) ||
     !strncmp(query, "or(", 3)) {
    *endq = strchr(query, '(');
    if(*endq == NULL) goto error; /* This is not possible, but coverity */
    node = calloc(1, sizeof(*node));
    node->operation = (*query == 'a') ? OP_AND_ARGS : OP_OR_ARGS;
    noit_metric_tag_search_ast_t *arg = NULL;
    do {
      (*endq)++;
      arg = noit_metric_tag_part_parse(*endq, endq, mtev_true);
      if((**endq != ',' && **endq != ')') || !arg) goto error;
      node->contents.args.cnt++;
      node->contents.args.node = realloc(node->contents.args.node, sizeof(arg) * node->contents.args.cnt);
      node->contents.args.node[node->contents.args.cnt - 1] = arg;
    } while(**endq == ',');
    (*endq)++;
  }
  else if(!strncmp(query, "not(", 4)) {
    *endq = query + 4;
    node = calloc(1, sizeof(*node));
    node->operation = OP_NOT_ARGS;
    noit_metric_tag_search_ast_t *arg = noit_metric_tag_part_parse(*endq, endq, mtev_true);
    if(**endq != ')' || !arg) goto error;
    node->contents.args.cnt++;
    node->contents.args.node = realloc(node->contents.args.node, sizeof(arg) * node->contents.args.cnt);
    node->contents.args.node[node->contents.args.cnt - 1] = arg;
    (*endq)++;
  }
  else if(allow_match) {
    node = calloc(1, sizeof(*node));
    node->operation = OP_MATCH;
    if(!noit_metric_tag_match_compile(&node->contents.spec.cat, endq, 1)) goto error;
    if(**endq == ':') {
      (*endq)++;
      if(!noit_metric_tag_match_compile(&node->contents.spec.name, endq, 2)) goto error;
    }
  }
  if(node) node->refcnt = 1;
  return node;
 error:
  noit_metric_tag_search_free(node);
  return NULL;
}

noit_metric_tag_search_ast_t *
noit_metric_tag_search_clone(const noit_metric_tag_search_ast_t *in) {
  if(!in) return NULL;
  noit_metric_tag_search_ast_t *out = malloc(sizeof(*out));
  memcpy(out, in, sizeof(*out));
  if(out->operation == OP_MATCH) {
    int erroffset;
    const char *error;
    if(out->contents.spec.cat.str)
      out->contents.spec.cat.str = strdup(out->contents.spec.cat.str);
    if(out->contents.spec.cat.re)
      out->contents.spec.cat.re = pcre_compile(out->contents.spec.cat.str,
                                               0, &error, &erroffset, NULL);
    out->contents.spec.cat.re_e = NULL;
    if(out->contents.spec.name.str)
      out->contents.spec.name.str = strdup(out->contents.spec.name.str);
    if(out->contents.spec.name.re)
      out->contents.spec.name.re = pcre_compile(out->contents.spec.name.str,
                                               0, &error, &erroffset, NULL);
    out->contents.spec.name.re_e = NULL;
  }
  else {
    noit_metric_tag_search_ast_t **nodes = calloc(out->contents.args.cnt, sizeof(*nodes));
    for(int i=0; i<out->contents.args.cnt; i++)
      nodes[i] = noit_metric_tag_search_clone(out->contents.args.node[i]);
    out->contents.args.node = nodes;
  }

  out->user_data = NULL;
  out->user_data_free = NULL;
  out->refcnt = 1;
  return out;
}
noit_metric_tag_search_ast_t *
noit_metric_tag_search_parse(const char *query, int *erroff) {
  noit_metric_tag_search_ast_t *tree;
  const char *eop;
  if(NULL == (tree = noit_metric_tag_part_parse(query, &eop, mtev_false))) {
    *erroff = eop - query;
    return NULL;
  }
  if(*eop != '\0') {
    noit_metric_tag_search_free(tree);
    *erroff = eop - query;
    return NULL;
  }
  *erroff = -1;
  return tree;
}

static mtev_boolean
noit_match_str(const char *subj, int subj_len, struct noit_var_match_t *m) {
  char decoded_tag[512];
  const char *ssubj = subj;
  int ssubj_len = subj_len;
  if (memcmp(subj, "b\"", 2) == 0) {
    const char *start = subj + 2;
    const char *end = memchr(start, '"', subj_len - 2);
    if (!end) return mtev_false; // not decodable, no match
    int len = mtev_b64_decode(start, end - start, (unsigned char *)decoded_tag, 
			      sizeof(decoded_tag));
    if (len == 0) return mtev_false; // decode failed, no match
    ssubj_len = len;
    ssubj = decoded_tag;
  }
  if(m->re) {
    int ovector[30], rv;
    if((rv = pcre_exec(m->re, NULL, ssubj, ssubj_len, 0, 0, ovector, 30)) >= 0) return mtev_true;
    return mtev_false;
  }
  if(m->str == NULL) return mtev_true;
  if(strlen(m->str) == ssubj_len && !memcmp(m->str, ssubj, ssubj_len)) return mtev_true;
  return mtev_false;
}
static mtev_boolean
noit_metric_tag_match_evaluate_against_tags_multi(struct noit_metric_tag_match_t *match,
                                                  noit_metric_tagset_t **sets, int set_cnt) {
  for(int s=0; s<set_cnt; s++) {
    noit_metric_tagset_t *set = sets[s];
    for(int i=0; i<set->tag_count; i++) {
      if(set->tags[i].total_size >= set->tags[i].category_size &&
         noit_match_str(set->tags[i].tag, set->tags[i].category_size - 1, &match->cat) &&
         noit_match_str(set->tags[i].tag + set->tags[i].category_size,
                        set->tags[i].total_size - set->tags[i].category_size, &match->name)) {
        return mtev_true;
      }
    }
  }
  return mtev_false;
}
mtev_boolean
noit_metric_tag_search_evaluate_against_tags_multi(noit_metric_tag_search_ast_t *search,
                                                   noit_metric_tagset_t **set, int set_cnt) {
  switch(search->operation) {
    case OP_MATCH: return noit_metric_tag_match_evaluate_against_tags_multi(&search->contents.spec, set, set_cnt);
    case OP_NOT_ARGS:
      mtevAssert(search->contents.args.cnt == 1);
      return !noit_metric_tag_search_evaluate_against_tags_multi(search->contents.args.node[0], set, set_cnt);
    case OP_AND_ARGS:
      for(int i=0; i<search->contents.args.cnt; i++) {
        if(!noit_metric_tag_search_evaluate_against_tags_multi(search->contents.args.node[i], set, set_cnt)) {
          return mtev_false;
        }
      }
      return mtev_true;
    case OP_OR_ARGS:
      for(int i=0; i<search->contents.args.cnt; i++) {
        if(noit_metric_tag_search_evaluate_against_tags_multi(search->contents.args.node[i], set, set_cnt)) {
          return mtev_true;
        }
      }
      return mtev_false;
  }
  return mtev_false;
}
mtev_boolean
noit_metric_tag_search_evaluate_against_tags(noit_metric_tag_search_ast_t *search,
                                             noit_metric_tagset_t *set) {
  return noit_metric_tag_search_evaluate_against_tags_multi(search, &set, 1);
}

mtev_boolean
noit_metric_tag_search_evaluate_against_metric_id(noit_metric_tag_search_ast_t *search,
                                                  noit_metric_id_t *id) {
  mtev_memory_begin();

#define MKTAGSETCOPY(name) \
  noit_metric_tag_t name##_tags[MAX_TAGS]; \
  memcpy(&name##_tags, name.tags, name.tag_count * sizeof(noit_metric_tag_t)); \
  name.tags = name##_tags

  // setup check tags
  noit_metric_tagset_t tagset_check = id->check;
  // Add in extra tags: __uuid
  if ( tagset_check.tag_count > MAX_TAGS - 1 ) { return 0; }
  MKTAGSETCOPY(tagset_check);
  char uuid_str[13 + UUID_STR_LEN + 1];
  strcpy(uuid_str, "__check_uuid:");
  mtev_uuid_unparse_lower(id->id, uuid_str + 13);
  noit_metric_tag_t uuid_tag = { .tag = uuid_str, .total_size = strlen(uuid_str), .category_size = 13 };
  tagset_check.tags[tagset_check.tag_count++] = uuid_tag;
  if(noit_metric_tagset_fixup_hook_invoke(NOIT_METRIC_TAGSET_CHECK, &tagset_check) == MTEV_HOOK_ABORT) {
    mtev_memory_end();
    return mtev_false;
  }

  // setup stream tags
  noit_metric_tagset_t tagset_stream = id->stream;
  // Add in extra tags: __name
  if ( tagset_stream.tag_count > MAX_TAGS - 1 ) { return 0; }
  MKTAGSETCOPY(tagset_stream);
  char name_str[NOIT_TAG_MAX_PAIR_LEN + 1];
  snprintf(name_str, sizeof(name_str), "__name:%.*s", id->name_len, id->name);
  noit_metric_tag_t name_tag = { .tag = name_str, .total_size = strlen(name_str), .category_size = 7 };
  tagset_stream.tags[tagset_stream.tag_count++] = name_tag;
  if(noit_metric_tagset_fixup_hook_invoke(NOIT_METRIC_TAGSET_STREAM, &tagset_stream) == MTEV_HOOK_ABORT) {
    mtev_memory_end();
    return mtev_false;
  }

  // setup measurement tags
  noit_metric_tagset_t tagset_measurement = id->measurement;
  MKTAGSETCOPY(tagset_measurement);
  if(noit_metric_tagset_fixup_hook_invoke(NOIT_METRIC_TAGSET_MEASUREMENT, &tagset_measurement) == MTEV_HOOK_ABORT) {
    mtev_memory_end();
    return mtev_false;
  }

  noit_metric_tagset_t *tagsets[3] = { &tagset_check, &tagset_stream, &tagset_measurement };
  mtev_boolean ok = noit_metric_tag_search_evaluate_against_tags_multi(search, tagsets, 3);
  mtev_memory_end();
  return ok;
}

static void
noit_metric_tag_search_unparse_part(noit_metric_tag_search_ast_t *search, mtev_dyn_buffer_t *buf) {
  switch(search->operation) {
    case OP_MATCH: {
      noit_metric_tag_match_t *spec = &search->contents.spec;
      mtev_dyn_buffer_add_printf(buf, "%s", spec->cat.str);
      if (spec->name.str) mtev_dyn_buffer_add_printf(buf, ":%s", spec->name.str);
      break;
    }
    case OP_NOT_ARGS:
      mtevAssert(search->contents.args.cnt == 1);
      mtev_dyn_buffer_add_printf(buf, "not(");
      noit_metric_tag_search_unparse_part(search->contents.args.node[0], buf);
      mtev_dyn_buffer_add_printf(buf, ")");
      break;
    case OP_AND_ARGS:
      mtev_dyn_buffer_add_printf(buf, "and(");
      for(int i=0; i<search->contents.args.cnt; i++) {
        noit_metric_tag_search_unparse_part(search->contents.args.node[i], buf);
        if (i != search->contents.args.cnt - 1) mtev_dyn_buffer_add_printf(buf, ",");
      }
      mtev_dyn_buffer_add_printf(buf, ")");
      break;
    case OP_OR_ARGS:
      mtev_dyn_buffer_add_printf(buf, "or(");
      for(int i=0; i<search->contents.args.cnt; i++) {
        noit_metric_tag_search_unparse_part(search->contents.args.node[i], buf);
        if (i != search->contents.args.cnt - 1) mtev_dyn_buffer_add_printf(buf, ",");
      }
      mtev_dyn_buffer_add_printf(buf, ")");
      break;
  }
}

char *
noit_metric_tag_search_unparse(noit_metric_tag_search_ast_t *search) {
  char *res;
  mtev_dyn_buffer_t buf;
  mtev_dyn_buffer_init(&buf);
  noit_metric_tag_search_unparse_part(search, &buf);
  res = strdup((const char *)mtev_dyn_buffer_data(&buf));
  mtev_dyn_buffer_destroy(&buf);
  return res;
}
