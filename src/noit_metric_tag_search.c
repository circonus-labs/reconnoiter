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
#include <mtev_str.h>
#include <mtev_log.h>
#include "noit_metric_tag_search.h"

void
noit_metric_tag_search_free(noit_metric_tag_search_ast_t *node) {
  if(node == NULL) return;
  switch(node->operation) {
    case OP_MATCH:
      free(node->contents.spec.cat.str);
      free(node->contents.spec.name.str);
      if(node->contents.spec.cat.re) pcre_free(node->contents.spec.cat.re);
      if(node->contents.spec.name.re) pcre_free(node->contents.spec.name.re);
      break;
    /* All ARGS */
    default:
      for(int i=0;i<node->contents.args.cnt; i++) {
        noit_metric_tag_search_free(node->contents.args.node[i]);
      }
  }
  if(node->user_data_free) node->user_data_free(node->user_data);
  free(node);
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
noit_metric_tag_match_compile(struct noit_var_match_t *m, const char **endq) {
  const char *query = *endq;
  const char *error;
  int erroffset;
  if(*query == '/') {
    *endq = query+1;
    while(**endq && **endq != '/') (*endq)++;
    if(**endq != '/') return mtev_false;
    m->str = mtev__strndup(query + 1, *endq - query - 1);
    m->re = pcre_compile(m->str,
                         0, &error, &erroffset, NULL);
    if(!m->re) {
      *endq = query+1+erroffset;
      return mtev_false;
    }
    (*endq)++;
  }
  else {
    while(**endq && (noit_metric_tagset_is_taggable_char(**endq) || **endq == '*')) (*endq)++;
    if(*endq == query) return mtev_false;
    m->str = mtev__strndup(query, *endq - query);
    if(strchr(m->str, '*') || strchr(m->str, '?')) {
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
    if(!noit_metric_tag_match_compile(&node->contents.spec.cat, endq)) goto error;
    if(**endq == ':') {
      (*endq)++;
      if(!noit_metric_tag_match_compile(&node->contents.spec.name, endq)) goto error;
    }
  }
  return node;
 error:
  noit_metric_tag_search_free(node);
  return NULL;
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
  if(m->re) {
    int ovector[30], rv;
    if((rv = pcre_exec(m->re, NULL, subj, subj_len, 0, 0, ovector, 30)) >= 0) return mtev_true;
    return mtev_false;
  }
  if(m->str == NULL) return mtev_true;
  if(strlen(m->str) == subj_len && !memcmp(m->str, subj, subj_len)) return mtev_true;
  return mtev_false;
}
static mtev_boolean
noit_metric_tag_match_evaluate_against_tags(struct noit_metric_tag_match_t *match, noit_metric_tagset_t *set) {
  for(int i=0; i<set->tag_count; i++) {
    if(set->tags[i].total_size > set->tags[i].category_size &&
       noit_match_str(set->tags[i].tag, set->tags[i].category_size - 1, &match->cat) &&
       noit_match_str(set->tags[i].tag + set->tags[i].category_size,
                      set->tags[i].total_size - set->tags[i].category_size, &match->name)) {
      return mtev_true;
    }
  }
  return mtev_false;
}
mtev_boolean
noit_metric_tag_search_evaluate_against_tags(noit_metric_tag_search_ast_t *search,
                                      noit_metric_tagset_t *set) {
  switch(search->operation) {
    case OP_MATCH: return noit_metric_tag_match_evaluate_against_tags(&search->contents.spec, set);
    case OP_NOT_ARGS:
      mtevAssert(search->contents.args.cnt == 1);
      return !noit_metric_tag_search_evaluate_against_tags(search->contents.args.node[0], set);
    case OP_AND_ARGS:
      for(int i=0; i<search->contents.args.cnt; i++) {
        if(!noit_metric_tag_search_evaluate_against_tags(search->contents.args.node[i], set)) {
          return mtev_false;
        }
      }
      return mtev_true;
    case OP_OR_ARGS:
      for(int i=0; i<search->contents.args.cnt; i++) {
        if(noit_metric_tag_search_evaluate_against_tags(search->contents.args.node[i], set)) {
          return mtev_true;
        }
      }
      return mtev_false;
  }
  return mtev_false;
}
