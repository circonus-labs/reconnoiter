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
#include "noit_metric.h"

typedef enum {
  OP_AND_ARGS,
  OP_OR_ARGS,
  OP_NOT_ARGS,
  OP_MATCH
} noit_metric_tag_search_op_t;

typedef struct noit_var_match_t {
  char *str;
  pcre *re;
} noit_var_match_t;

typedef struct noit_metric_tag_match_t {
  noit_var_match_t cat;
  noit_var_match_t name;
} noit_metric_tag_match_t;

typedef struct noit_metric_tag_search_ast_t {
  noit_metric_tag_search_op_t operation;
  union {
    struct {
      int cnt;
      struct noit_metric_tag_search_ast_t **node;
    } args;
    noit_metric_tag_match_t spec;
  } contents;
  void *user_data;
  void (*user_data_free)(void *);
} noit_metric_tag_search_ast_t;

API_EXPORT(noit_metric_tag_search_ast_t *)
  noit_metric_tag_search_clone(const noit_metric_tag_search_ast_t *);

API_EXPORT(void)
  noit_metric_tag_search_free(noit_metric_tag_search_ast_t *);

API_EXPORT(noit_metric_tag_search_ast_t *)
  noit_metric_tag_search_parse(const char *query, int *erroff);

API_EXPORT(mtev_boolean)
  noit_metric_tag_search_evaluate_against_tags(noit_metric_tag_search_ast_t *search,
                                               noit_metric_tagset_t *tags);

API_EXPORT(char *)
  noit_metric_tag_search_unparse(noit_metric_tag_search_ast_t *);

#endif
