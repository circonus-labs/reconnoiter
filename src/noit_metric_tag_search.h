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

#ifndef NOIT_METRIC_TAG_SEARCH_H_
#define NOIT_METRIC_TAG_SEARCH_H_

/* This implement filtering based on a set of tags where tags are of the form key:value.
 *
 * Tag keys and values may contain only characters [a-zA-Z0-9._-].
 *
 * The grammar for matching a set of tags supports and/or/not.
 *
 * stmt        :=  and(stmt-list)
 *              |  or(stmt-list)
 *              |  not(stmt-single)
 * stmt-list   :=  stmt-single,stmt-list
 *              |  stmt-single
 * stmt-single :=  stmt
 *              |  match
 * match       :=  partmatch
 *              |  partmatch : partmatch
 * partmatch   :=  [a-zA-Z0-9._-*]  // tag character plus wildcard
 *              |  /<re>/           // re is a regular expression
 *
 * example: and(env:prod,or(service:web,team:/^(red|blue)$/),not(special))
 *
 * This requires that one of the tags is 'env:prod',
 *                    none of the tags have the key 'special',
 *                    and at least one of the 'service:web', 'team:red', or 'team:blue' tags exist
 *
 *
 * The AST (abstract syntax tree) is exposes with user_data visible to that
 * one could compile a search and then leverage the AST in another context
 * (such as building the metadata to execute a database query plan).
 */

#include <pcre.h>
#include <noit_metric.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  OP_AND_ARGS,
  OP_OR_ARGS,
  OP_NOT_ARGS,
  OP_MATCH,
  OP_HINT_ARGS
} noit_metric_tag_search_op_t;

typedef struct noit_metric_tag_search_ast_t noit_metric_tag_search_ast_t;
typedef struct noit_var_match_t noit_var_match_t;

typedef struct noit_var_match_impl_t {
  char *impl_name;
  void *(*init)(const char *in);
  bool (*compile)(void *impl_data, int *errpos);
  mtev_boolean (*match)(void *impl_data, const char *pattern, const char *in, size_t in_len);
  void (*free)(void *impl_data);
  int (*append_fixed_prefix)(void *impl_data, const char *pattern, char *prefix, size_t len, mtev_boolean *all);
} noit_var_match_impl_t;

API_EXPORT(noit_metric_tag_search_ast_t *)
  noit_metric_tag_search_clone(const noit_metric_tag_search_ast_t *);

API_EXPORT(noit_metric_tag_search_ast_t *)
  noit_metric_tag_search_clone_lazy(const noit_metric_tag_search_ast_t *);

API_EXPORT(noit_metric_tag_search_ast_t *)
  noit_metric_tag_search_ref(noit_metric_tag_search_ast_t *);

API_EXPORT(noit_metric_tag_search_ast_t *)
  noit_metric_tag_search_alloc(noit_metric_tag_search_op_t op);

API_EXPORT(noit_metric_tag_search_ast_t *)
  noit_metric_tag_search_alloc_match(const char *cat_impl, const char *cat_pat,
                                     const char *name_impl, const char *name_pat);

API_EXPORT(void)
  noit_metric_tag_search_free(noit_metric_tag_search_ast_t *);

API_EXPORT(void)
  noit_metric_tag_search_resize_args(noit_metric_tag_search_ast_t *node, int cnt);

API_EXPORT(void *)
  noit_metric_tag_search_get_udata(const noit_metric_tag_search_ast_t *node);

API_EXPORT(void)
  noit_metric_tag_search_set_udata(noit_metric_tag_search_ast_t *node, void *, void (*)(void *));

API_EXPORT(noit_metric_tag_search_ast_t *)
  noit_metric_tag_search_get_arg(const noit_metric_tag_search_ast_t *node, int idx);

API_EXPORT(void)
  noit_metric_tag_search_set_arg(noit_metric_tag_search_ast_t *node, int idx, noit_metric_tag_search_ast_t *r);

API_EXPORT(void)
  noit_metric_tag_search_add_arg(noit_metric_tag_search_ast_t *node, noit_metric_tag_search_ast_t *r);

API_EXPORT(int)
  noit_metric_tag_search_get_nargs(const noit_metric_tag_search_ast_t *node);

API_EXPORT(void)
  noit_metric_tag_search_reset(noit_metric_tag_search_ast_t *);

API_EXPORT(noit_metric_tag_search_ast_t *)
  noit_metric_tag_search_parse(const char *query, int *erroff);

API_EXPORT(noit_metric_tag_search_ast_t *)
  noit_metric_tag_search_parse_lazy(const char *query, int *erroff);

API_EXPORT(void)
  noit_metric_tag_search_set_op(noit_metric_tag_search_ast_t *node, noit_metric_tag_search_op_t);

API_EXPORT(noit_metric_tag_search_op_t)
  noit_metric_tag_search_get_op(const noit_metric_tag_search_ast_t *node);

API_EXPORT(const noit_var_match_t *)
  noit_metric_tag_search_get_cat(const noit_metric_tag_search_ast_t *node);

API_EXPORT(const noit_var_match_t *)
  noit_metric_tag_search_get_name(const noit_metric_tag_search_ast_t *node);

API_EXPORT(mtev_boolean)
  noit_var_match(const noit_var_match_t *node, const char *subj, size_t subj_len);

API_EXPORT(int)
  noit_var_strlcat_fixed_prefix(const noit_var_match_t *node, char *out, size_t len, mtev_boolean *all);

API_EXPORT(const char *)
  noit_var_val(const noit_var_match_t *node);

API_EXPORT(const char *)
  noit_var_impl_name(const noit_var_match_t *node);

API_EXPORT(mtev_boolean)
  noit_metric_tag_search_evaluate_against_tags(const noit_metric_tag_search_ast_t *search,
                                               const noit_metric_tagset_t *tags);

API_EXPORT(mtev_boolean)
  noit_metric_tag_search_evaluate_against_tags_multi(const noit_metric_tag_search_ast_t *search,
                                                     const noit_metric_tagset_t **tags, const int ntagsets);

API_EXPORT(mtev_boolean)
  noit_metric_tag_search_evaluate_against_metric_id(const noit_metric_tag_search_ast_t *search,
                                                    const noit_metric_id_t *id);

API_EXPORT(mtev_boolean)
  noit_metric_tag_search_has_hint(const noit_metric_tag_search_ast_t *search, const char *cat, const char *name);

API_EXPORT(char *)
  noit_metric_tag_search_unparse(const noit_metric_tag_search_ast_t *);

API_EXPORT(int)
  noit_metric_tag_search_swap(noit_metric_tag_search_ast_t *, int, int);

API_EXPORT(void)
  noit_var_matcher_register(const noit_var_match_impl_t *matcher);

#ifdef __cplusplus
}
#endif
#endif
