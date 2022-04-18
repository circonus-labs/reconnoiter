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
#include <mtev_maybe_alloc.h>
#include "noit_metric_tag_search.h"

#include <stdio.h>
#include <ck_pr.h>
#include <ctype.h>

typedef struct noit_var_match_t {
  char *str;
  void *impl_data;
  const noit_var_match_impl_t *impl;
} noit_var_match_t;

static mtev_boolean noit_match_str(const char *subj, int subj_len, const struct noit_var_match_t *m);

static pthread_mutex_t noit_tags_search_tls_jit_init_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread pcre_jit_stack *noit_tag_search_tls_jit_stack;
static bool noit_tags_search_tls_jit_init = false;
static pthread_key_t noit_tag_search_tls_jit_stack_key;

static void
noit_tag_search_free_tls_pcre_jit_stack(void *stack)
{
  pcre_jit_stack_free((pcre_jit_stack *)stack);
}
static pcre_jit_stack *
noit_tag_search_get_tls_pcre_jit_stack(void *arg)
{
  (void)arg;
  if (noit_tags_search_tls_jit_init && noit_tag_search_tls_jit_stack == NULL) {
    noit_tag_search_tls_jit_stack = (pcre_jit_stack *)pthread_getspecific(noit_tag_search_tls_jit_stack_key);
    if (noit_tag_search_tls_jit_stack == NULL) {
      noit_tag_search_tls_jit_stack = (pcre_jit_stack *)pcre_jit_stack_alloc(1024 * 32, 512 * 1024);
      pthread_setspecific(noit_tag_search_tls_jit_stack_key, noit_tag_search_tls_jit_stack);
    }
  }
  return noit_tag_search_tls_jit_stack;
}

void
__attribute__((constructor))
noit_tag_search_init(void) {
  pthread_mutex_lock(&noit_tags_search_tls_jit_init_lock);
  if(!noit_tags_search_tls_jit_init) {
    pthread_key_create(&noit_tag_search_tls_jit_stack_key, noit_tag_search_free_tls_pcre_jit_stack);
    noit_tags_search_tls_jit_init = true;
  }
  pthread_mutex_unlock(&noit_tags_search_tls_jit_init_lock);
}

struct graphite_impl {
  int count;
  int allocd;
  bool has_wildcards;
  size_t fixed_prefix_len;
  struct {
    char *str;
    size_t str_len;
    char *re_str;
    pcre *re;
    pcre_extra *re_e;
  } *results;
};

static mtev_boolean
var_graphite_match(void *impl_data, const char *pattern, const char *in, size_t in_len) {
  (void)pattern;
  int ovector[30], rv;
  struct graphite_impl *g = (struct graphite_impl *)impl_data;
  /* If we have a fixed prefix, if it is longer than the subject or mismatched, bail early */
  if(g->fixed_prefix_len &&
     (g->fixed_prefix_len > in_len || 0 != memcmp(in, pattern, g->fixed_prefix_len))) {
    return mtev_false;
  }
  /* If the fixed_prefix len is the subject len, we know we have an exact match */
  if(g->fixed_prefix_len == in_len) return mtev_true;
  for(int i=0; i<g->count; i++) {
    if(g->results[i].re) {
      rv = pcre_exec(g->results[i].re, g->results[i].re_e, in, in_len, 0, 0, ovector, 30);
      if(rv >= 0) return mtev_true;
    } else {
      if(in_len == g->results[i].str_len && 0 == memcmp(in, g->results[i].str, in_len)) return mtev_true;
    }
  }
  return mtev_false;
}

void var_graphite_free(void *vi) {
  struct graphite_impl *g = vi;
  if(g == NULL) return;
  for(int i=0; i<g->count; i++) {
    free(g->results[i].str);
    free(g->results[i].re_str);
    if(g->results[i].re) pcre_free(g->results[i].re);
    if(g->results[i].re_e) {
#ifdef PCRE_STUDY_JIT_COMPILE
      pcre_free_study(g->results[i].re_e);
#else
      pcre_free(g->results[i].re_e);
#endif
    }
  }
  free(g->results);
  free(g);
}

static char *
build_regex_from_graphite(const char *expansion) {
  mtev_dyn_buffer_t out;
  mtev_dyn_buffer_init(&out);
#undef AC
#define AC(a) mtev_dyn_buffer_add(&out, (uint8_t *)(a), sizeof(a)-1)
#undef AC1
#define AC1(a) mtev_dyn_buffer_add(&out, (uint8_t *)&(a), 1)
  /* brace expansion has already been done, all we need to do here
   * is translate '*' to '[^.]*', '?' to '[^.]', and '.' to '\.',
   */
  AC("^");
  for (size_t i = 0; i < strlen(expansion); i++) {
    /* Here we account for needing what expand to, plus "$\0" */
    if (expansion[i] == '*') {
      if (expansion[i+1] == '*') {
        i++;
        AC(".*");
      } else {
        AC("[^.]*");
      }
    } else if (expansion[i] == '?') {
      AC("[^.]{1}");
    } else if (expansion[i] == '.') {
      AC("\\.");
    } else {
      AC1(expansion[i]);
    }
  }
  AC1("$");
  char *rv = mtev_strndup((char *)mtev_dyn_buffer_data(&out), mtev_dyn_buffer_used(&out));
  mtev_dyn_buffer_destroy(&out);
  return rv;
}

static inline bool
valid_non_re_char(char c) {
  switch(c) {
    case '{':
    case '}':
    case '(':
    case ')':
    case '[':
    case ']':
    case '.':
    case '*':
    case '?':
    case '+':
    case '^':
    case '$':
    case '\\':
      return false;
    default:
      break;
  }
  return true;
}

/* This extracts the maximal "fixed" memcmp()able prefix and tacks it onto prefix */
static inline int
append_prefix(char *prefix, size_t len, const char *re_str, pcre *re, mtev_boolean *all) {
  int starting_len = strlen(prefix);
  *all = false;
  if(!re_str) return starting_len;
  if(re == NULL) {
    size_t str_len = strlen(re_str);
    strlcat(prefix, re_str, len);
    size_t final_len = strlen(prefix);
    if(starting_len + str_len == final_len) *all = true;
    return final_len;
  }
  /* The re case... perhaps it is a front-achored prefix */
  const char *cp = re_str;
  char *outcp = prefix + starting_len;
  size_t remaining_len = len - starting_len;
  /* We can only prefix match is we're front anchored */
  if(*cp++ == '^') {
    while(*cp && remaining_len > 1) {
      if(cp[0] == '$' && cp[1] == '\0') {
        *all = true;
        break;
      }
      if(cp[0] == '\\' && cp[1] != '\0' && !isalnum(cp[1])) {
        cp++;
      }
      else if(!valid_non_re_char(*cp)) break;
      /* ? and * mean 0 or more, so the current character can't be included */
      if(cp[1] == '?' || cp[1] == '*') break;
      *outcp++ = *cp++;
      remaining_len--;
    }
  }
  *outcp = '\0';
  return strlen(prefix);
}

static inline int
find_char(const char *s, size_t len, int c) {
  const char *loc = (const char *)memchr(s, c, len);
  if(loc == NULL) return -1;
  return loc - s;
}
static bool
graphite_has_wildcards(const char *s, size_t *fixed_prefix_len) {
  size_t len = strlen(s);
  int fixed = 1;
  size_t fplen = 0;
  for(size_t i=0; i<len; i++) {
    if(s[i] == '*' || s[i] == '[' || s[i] == '?') {
      if(fixed_prefix_len) *fixed_prefix_len = fplen;
      return true;
    }
    if(s[i] == '\\' || s[i] == '{') fixed = 0;
    fplen += fixed;
  }
  if(fixed_prefix_len) *fixed_prefix_len = fplen;
  return false;
}
static void
graphite_add_expansion(const char *s, struct graphite_impl *g) {
  if(g->count + 1 > g->allocd) {
    g->allocd += 20;
    g->results = realloc(g->results, g->allocd * sizeof(*g->results));
    memset(&g->results[g->count], 0, sizeof(*g->results) * (g->allocd - g->count));
  }
  g->results[g->count].str = strdup(s);
  g->results[g->count].str_len = strlen(s);
  if(g->has_wildcards) {
    if(graphite_has_wildcards(s, NULL)) {
      const char *error;
      int eoff;
      char *tmpstr = build_regex_from_graphite(g->results[g->count].str);
      g->results[g->count].re_str = tmpstr;
      g->results[g->count].re = pcre_compile(tmpstr, 0, &error, &eoff, NULL);
      if(g->results[g->count].re) {
#ifdef PCRE_STUDY_JIT_COMPILE
        g->results[g->count].re_e = pcre_study(g->results[g->count].re, PCRE_STUDY_JIT_COMPILE, &error);
        if(g->results[g->count].re_e) {
          pcre_assign_jit_stack(g->results[g->count].re_e, noit_tag_search_get_tls_pcre_jit_stack, NULL);
        }
#else
        g->results[g->count].re_e = pcre_study(g->results[g->count].re, 0, &error);
#endif
      }
    }
  }
  g->count++;
}
static void
graphite_expand_braces(const char *pre, const char *s, const char *suf, struct graphite_impl *g) {
  MTEV_MAYBE_DECL_VARS(char, copy, 256);
  int start_brace = -1, end_brace = 0;
  size_t len_s = strlen(s);
  size_t len_pre = strlen(pre);
  size_t len_suf = strlen(suf);
  MTEV_MAYBE_REALLOC(copy, len_s + 1);

  while ((start_brace = find_char(s + start_brace + 1, len_s - start_brace - 1, '{')) != -1) {
    end_brace = start_brace + 1;
    strncpy(copy, s, MTEV_MAYBE_SIZE(copy));
    for (int depth = 1; end_brace < (int)len_s && depth > 0; end_brace++) {
      char c = s[end_brace];
      depth = (c == '{') ? depth + 1 : depth;
      depth = (c == '}') ? depth - 1 : depth;
      if (c == '}' && depth == 0) {
        goto done;
      }
    }
  }
 done:
  if (start_brace == -1) {
    if (len_suf > 0) {
      MTEV_MAYBE_DECL_VARS(char, prefix, 256);
      MTEV_MAYBE_REALLOC(prefix, len_s + len_pre + 2);
      snprintf(prefix, MTEV_MAYBE_SIZE(prefix), "%s%s", pre, s);
      graphite_expand_braces(prefix, suf, "", g);
      MTEV_MAYBE_FREE(prefix);
    }
    else {
      MTEV_MAYBE_DECL_VARS(char, expstr, 256);
      MTEV_MAYBE_REALLOC(expstr, len_s + len_pre + len_suf + 2);
      snprintf(expstr, MTEV_MAYBE_SIZE(expstr), "%s%s%s", pre, s, suf);
      graphite_add_expansion(expstr, g);
      MTEV_MAYBE_FREE(expstr);
    }
  } else {
    MTEV_MAYBE_DECL_VARS(char, sub, 256);
    MTEV_MAYBE_REALLOC(sub, len_s + 1);
    char *lasts;
    char *token;
    memcpy(sub, copy + start_brace + 1, end_brace);
    sub[end_brace - start_brace - 1] = '\0';
    if ((token = strtok_r(sub, ",", &lasts)) != NULL) {
      MTEV_MAYBE_DECL_VARS(char, x, 256);
      MTEV_MAYBE_REALLOC(x, len_pre + start_brace + 2);
      snprintf(x, MTEV_MAYBE_SIZE(x), "%s", pre);
      memcpy(x + len_pre, s, start_brace);
      x[len_pre + start_brace] = '\0';
      size_t len_begin = strlen(s + end_brace + 1);
      MTEV_MAYBE_DECL_VARS(char, y, 256);
      MTEV_MAYBE_REALLOC(y, len_begin + len_suf + 2);
      memcpy(y, s + end_brace + 1, len_begin);
      snprintf(y + len_begin, len_suf, "%s", suf);
      y[len_begin + len_suf] = '\0';
      graphite_expand_braces(x, token, y, g);
      while((token = strtok_r(NULL, ",", &lasts))) {
        graphite_expand_braces(x, token, y, g);
      }
      MTEV_MAYBE_FREE(x);
      MTEV_MAYBE_FREE(y);
    }
    MTEV_MAYBE_FREE(sub);
  }
  MTEV_MAYBE_FREE(copy);
}
static void
graphite_expand(const char *s, struct graphite_impl *g) {
  g->has_wildcards = graphite_has_wildcards(s, &g->fixed_prefix_len);
  graphite_expand_braces("", s, "", g);
}

static void *var_graphite_compile(const char *in, int *erroffset) {
  if(erroffset) *erroffset = 0;
  struct graphite_impl *impl_data = calloc(1, sizeof(*impl_data));
  graphite_expand(in, impl_data);
  return impl_data;
}

static int var_graphite_afp(void *impl_data, const char *pattern, char *out, size_t out_len, mtev_boolean *all) {
  struct graphite_impl *g = (struct graphite_impl *)impl_data;
  if(g->count == 1) return append_prefix(out, out_len, g->results[0].re_str, g->results[0].re, all);
  int pattern_len = strlen(pattern);
  int out_has = strlen(out);
  int fpl = g->fixed_prefix_len;
  if(fpl > out_len - out_has - 1) fpl = out_len - out_has - 1;
  memcpy(out + out_has, pattern, fpl);
  out[out_has + fpl] = '\0';
  *all = pattern_len == fpl;
  return strlen(out);
}

struct re_matcher {
  char *re_str;
  pcre *re;
  pcre_extra *re_e;
};

static void *var_re_compile(const char *in, int *erroffset) {
  int eoff;
  const char *error;
  pcre *re = pcre_compile(in, 0, &error, &eoff, NULL);
  if(!re) {
    if(erroffset) *erroffset = eoff;
    return NULL;
  }
  struct re_matcher *impl_data = calloc(1, sizeof(*impl_data));
  impl_data->re_str = strdup(in);
  impl_data->re = re;
  if(re) {
#ifdef PCRE_STUDY_JIT_COMPILE
    impl_data->re_e = pcre_study(re, PCRE_STUDY_JIT_COMPILE, &error);
    if(impl_data->re_e) {
      pcre_assign_jit_stack(impl_data->re_e, noit_tag_search_get_tls_pcre_jit_stack, NULL);
    }
#else
    impl_data->re_e = pcre_study(re, 0, &error);
#endif
  }
  return impl_data;
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

static void *var_default_compile(const char *in, int *erroffset) {
  int eoff;
  const char *error;
  char *tmpstr;
  tmpstr = build_regex_from_expansion(in);
  pcre *re = pcre_compile(tmpstr, 0, &error, &eoff, NULL);
  if(!re) {
    // translation means no offset.
    if(erroffset) *erroffset = 0;
    free(tmpstr);
    return NULL;
  }
  struct re_matcher *impl_data = calloc(1, sizeof(*impl_data));
  impl_data->re = re;
  if(re) {
#ifdef PCRE_STUDY_JIT_COMPILE
    impl_data->re_e = pcre_study(re, PCRE_STUDY_JIT_COMPILE, &error);
    if(impl_data->re_e) {
      pcre_assign_jit_stack(impl_data->re_e, noit_tag_search_get_tls_pcre_jit_stack, NULL);
    }
#else
    impl_data->re_e = pcre_study(re, 0, &error);
#endif
  }
  impl_data->re_str = tmpstr;
  return impl_data;
}

static mtev_boolean var_re_match(void *impl_data, const char *pattern, const char *in, size_t in_len) {
  (void)pattern;
  struct re_matcher *m = (struct re_matcher *)impl_data;
  if(m == NULL) return mtev_false;
  int ovector[30], rv;
  rv = pcre_exec(m->re, m->re_e, in, in_len, 0, 0, ovector, 30);
  if(rv >= 0) return mtev_true;
  return mtev_false;
}

static void var_re_free(void *impl_data) {
  struct re_matcher *m = (struct re_matcher *)impl_data;
  if(m == NULL) return;
  free(m->re_str);
  if(m->re) pcre_free(m->re);
  if(m->re_e) {
#ifdef PCRE_STUDY_JIT_COMPILE
    pcre_free_study(m->re_e);
#else
    pcre_free(m->re_e);
#endif
  }
  free(m);
}

static int var_re_afp(void *impl_data, const char *pattern, char *out, size_t out_len, mtev_boolean *all) {
  struct re_matcher *m = (struct re_matcher *)impl_data;
  return append_prefix(out, out_len, m ? m->re_str : "", m ? m->re : NULL, all);
}

static mtev_boolean var_exact_match(void *impl_data, const char *pattern, const char *in, size_t in_len) {
  (void)impl_data;
  if(!pattern) return mtev_false;
  size_t len = strlen(pattern);
  if(len != in_len) return mtev_false;
  return (0 == memcmp(in, pattern, in_len));
}

static int var_exact_afp(void *impl_data, const char *pattern, char *out, size_t out_len, mtev_boolean *all) {
  return append_prefix(out, out_len, pattern, NULL, all);
}

static const noit_var_match_impl_t var_re_matcher = {
  .impl_name = "re",
  .compile = var_re_compile,
  .match = var_re_match,
  .free = var_re_free,
  .append_fixed_prefix = var_re_afp
};
static const noit_var_match_impl_t var_re_expansion_matcher = {
  .impl_name = "default",
  .compile = var_default_compile,
  .match = var_re_match,
  .free = var_re_free,
  .append_fixed_prefix = var_re_afp
};
static const noit_var_match_impl_t var_graphite_matcher = {
  .impl_name = "graphite",
  .compile = var_graphite_compile,
  .match = var_graphite_match,
  .free = var_graphite_free,
  .append_fixed_prefix = var_graphite_afp
};
static const noit_var_match_impl_t var_exact_matcher = {
  .impl_name = "exact",
  .compile = NULL,
  .match = var_exact_match,
  .free = NULL,
  .append_fixed_prefix = var_exact_afp
};

static mtev_hash_table *matcher_impls;
static pthread_mutex_t  matcher_impls_write_lock = PTHREAD_MUTEX_INITIALIZER;

void noit_var_matcher_register(const noit_var_match_impl_t *matcher) {
  pthread_mutex_lock(&matcher_impls_write_lock);
  if(!matcher_impls) {
    matcher_impls = calloc(1, sizeof(*matcher_impls));
    mtev_hash_init(matcher_impls);
  }
  mtev_hash_replace(matcher_impls, matcher->impl_name, strlen(matcher->impl_name), matcher, NULL, NULL);
  pthread_mutex_unlock(&matcher_impls_write_lock);
}

static const noit_var_match_impl_t *
noit_var_matcher_impl(const char *name, size_t name_len) {
  void *vimpl;
  if(matcher_impls && mtev_hash_retrieve(matcher_impls, name, name_len, &vimpl)) {
    return vimpl;
  }
  if(name_len == 7 && memcmp(name, "default", 7) == 0) return &var_re_expansion_matcher;
  if(name_len == 5 && memcmp(name, "exact", 5) == 0) return &var_exact_matcher;
  if(name_len == 2 && memcmp(name, "re", 2) == 0) return &var_re_matcher;
  if(name_len == 8 && memcmp(name, "graphite", 8) == 0) return &var_graphite_matcher;
  return &var_exact_matcher;
}

typedef struct noit_metric_tag_match_t {
  noit_var_match_t cat;
  noit_var_match_t name;
} noit_metric_tag_match_t;

#define DEFAULT_CHILDREN_ALLOC 16

typedef struct noit_metric_tag_search_ast_t {
  noit_metric_tag_search_op_t operation;
  union {
    struct {
      int cnt;
      int nallocd;
      struct noit_metric_tag_search_ast_t **node;
    } args;
    noit_metric_tag_match_t spec;
  } contents;
  void *user_data;
  void (*user_data_free)(void *);
  uint32_t refcnt;
} noit_metric_tag_search_ast_t;

void
noit_metric_tag_search_resize_args(noit_metric_tag_search_ast_t *node, int new_size) {
  assert(node->operation == OP_NOT_ARGS ||
         node->operation == OP_OR_ARGS ||
         node->operation == OP_AND_ARGS ||
         node->operation == OP_HINT_ARGS);
  if(new_size == 0) new_size = DEFAULT_CHILDREN_ALLOC;
  if(node->contents.args.nallocd >= new_size) return;
  int target_size = MAX(node->contents.args.nallocd, DEFAULT_CHILDREN_ALLOC);
  while(target_size < new_size) target_size *= 2;
  new_size = target_size;
  noit_metric_tag_search_ast_t **new_nodes = calloc(new_size, sizeof(*new_nodes));
  for(size_t i=0; i<node->contents.args.cnt; i++) {
    new_nodes[i] = node->contents.args.node[i];
  }
  free(node->contents.args.node);
  node->contents.args.node = new_nodes;
  node->contents.args.nallocd = new_size;
}

noit_metric_tag_search_op_t
noit_metric_tag_search_get_op(const noit_metric_tag_search_ast_t *node) {
  return node->operation;
}

void
noit_metric_tag_search_set_op(noit_metric_tag_search_ast_t *node, noit_metric_tag_search_op_t op) {
  node->operation = op;
}

const noit_var_match_t *
noit_metric_tag_search_get_cat(const noit_metric_tag_search_ast_t *node) {
  return node->operation == OP_MATCH ? &node->contents.spec.cat : NULL;
}

const noit_var_match_t *
noit_metric_tag_search_get_name(const noit_metric_tag_search_ast_t *node) {
  return node->operation == OP_MATCH ? &node->contents.spec.name : NULL;
}

const char *
noit_var_val(const noit_var_match_t *node) {
  return node ? node->str : NULL;
}

mtev_boolean
noit_var_match(const noit_var_match_t *node, const char *subj, size_t subj_len) {
  if((node == NULL) || (node->impl == NULL)) return mtev_false;
  return node->impl->match(node->impl_data, node->str, subj, subj_len);
}

int
noit_var_strlcat_fixed_prefix(const noit_var_match_t *node, char *out, size_t len, mtev_boolean *all) {
  if((node == NULL) || (node->impl == NULL)) return strlen(out);
  return node->impl->append_fixed_prefix(node->impl_data, node->str, out, len, all);
}

const char *
noit_var_impl_name(const noit_var_match_t *node) {
  if (node && node->impl)
    return node->impl->impl_name;
  return NULL;
}

int
noit_metric_tag_search_get_nargs(const noit_metric_tag_search_ast_t *node) {
  return node->operation == OP_MATCH ? 0 : node->contents.args.cnt;
}

void *
noit_metric_tag_search_get_udata(const noit_metric_tag_search_ast_t *node) {
  return node->user_data;
}

void
noit_metric_tag_search_set_udata(noit_metric_tag_search_ast_t *node, void *udata, void (*ufree)(void *)) {
  node->user_data = udata;
  node->user_data_free = ufree;
}

noit_metric_tag_search_ast_t *
noit_metric_tag_search_get_arg(const noit_metric_tag_search_ast_t *node, int idx) {
  if(node->operation == OP_MATCH) return NULL;
  if(idx < 0 || idx >= node->contents.args.cnt) return NULL;
  return node->contents.args.node[idx];
}

void
noit_metric_tag_search_set_arg(noit_metric_tag_search_ast_t *node, int idx, noit_metric_tag_search_ast_t *r) {
  assert(node->operation != OP_MATCH);
  assert(node->operation != OP_NOT_ARGS || idx == 0);

  noit_metric_tag_search_resize_args(node, idx+1);
  assert(node->contents.args.nallocd > idx);
  node->contents.args.node[idx] = r;
  assert(node->contents.args.cnt >= idx); // can't insert with gaps.
  if(node->contents.args.cnt == idx) node->contents.args.cnt++;
}
void
noit_metric_tag_search_add_arg(noit_metric_tag_search_ast_t *node, noit_metric_tag_search_ast_t *r) {
  noit_metric_tag_search_set_arg(node, node->contents.args.cnt, r);
}

noit_metric_tag_search_ast_t *
noit_metric_tag_search_alloc(noit_metric_tag_search_op_t op) {
  noit_metric_tag_search_ast_t *node = calloc(1, sizeof(*node));
  node->operation = op;
  node->refcnt = 1;
  return node;
}

noit_metric_tag_search_ast_t *
noit_metric_tag_search_alloc_match(const char *cat_impl, const char *cat_pat,
                                   const char *name_impl, const char *name_pat) {
  noit_metric_tag_search_ast_t *node = noit_metric_tag_search_alloc(OP_MATCH);
  node->contents.spec.cat.impl = noit_var_matcher_impl(cat_impl, strlen(cat_impl));
  node->contents.spec.cat.str = cat_pat ? strdup(cat_pat) : NULL;
  if(node->contents.spec.cat.impl->compile)
    node->contents.spec.cat.impl_data = node->contents.spec.cat.impl->compile(cat_pat, NULL);
  node->contents.spec.name.impl = noit_var_matcher_impl(name_impl, strlen(name_impl));
  node->contents.spec.name.str = name_pat ? strdup(name_pat) : NULL;
  if(node->contents.spec.name.impl->compile)
    node->contents.spec.name.impl_data = node->contents.spec.name.impl->compile(name_pat, NULL);
  return node;
}

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
      if(node->contents.spec.cat.impl && node->contents.spec.cat.impl->free)
        node->contents.spec.cat.impl->free(node->contents.spec.cat.impl_data);
      if(node->contents.spec.name.impl && node->contents.spec.name.impl->free)
        node->contents.spec.name.impl->free(node->contents.spec.name.impl_data);
      break;
    /* All ARGS */
    default:
      for(int i=0;i<node->contents.args.cnt; i++) {
        noit_metric_tag_search_free(node->contents.args.node[i]);
      }
      free(node->contents.args.node);
  }
  if(node->user_data_free) node->user_data_free(node->user_data);
  memset(node, 0, sizeof(*node));
}

noit_metric_tag_search_ast_t *
noit_metric_tag_search_ref(noit_metric_tag_search_ast_t *node) {
  ck_pr_inc_32(&node->refcnt);
  return node;
}

static void
unescape_regex(char *inout) {
  char *in = inout, *out = inout;
  while(*in) {
    if(*in == '\\') {
      if(in[1] == '/' || in[1] == '\\') {
        in++;
      }
    }
    *out++ = *in++;
  }
  *out = '\0';
}

static inline mtev_boolean is_allowable_escape(char in) {
  return in == '\\' || in == '"';
}
static inline void unescape_tag_string(char *inout) {
  char *in = inout, *out = inout;
  while(*in) {
    if(*in == '\\') {
      if(in[1] == '"' || in[1] == '\\') {
        in++;
      }
    }
    *out++ = *in++;
  }
  *out = '\0';
}

static mtev_boolean
noit_metric_tag_match_compile(struct noit_var_match_t *m, const char **endq, int part) {
  const char *query = *endq;
  int is_escaped = mtev_false;
  int is_encoded_match = memcmp(query, "b!", 2) == 0;
  int is_encoded = is_encoded_match || memcmp(query, "b\"", 2) == 0 || memcmp(query, "b/", 2) == 0;
  int is_alt = memcmp(query, "b[", 2) == 0 || *query == '[';

  if(is_encoded_match) m->impl = &var_exact_matcher;
  if(is_alt) {
   if(*query == 'b') is_encoded = 1;
    while(**endq && **endq != ']') {
     (*endq)++;
    }
    if(**endq != ']') {
      *endq = query+1;
      return mtev_false;
    }
    m->impl = noit_var_matcher_impl(query+1+is_encoded, *endq - query - 1 - is_encoded);
    if(!m->impl) {
      *endq = query + 2;
      return mtev_false;
    }
    (*endq)++; // skip the ']'
    query = *endq;
  }
  else if (is_encoded) {
    (*endq)++; // skip the 'b'
    query = *endq;
  }
  if(*query == '/' && !m->impl) {
    *endq = query+1;
    while(**endq && **endq != '/') {
      if((*endq)[0] == '\\') {
        if((*endq)[1]) (*endq)++; // escapes
        else break;
      }
      (*endq)++;
    }
    if(**endq != '/') {
      *endq = query;
      goto not_a_regex;
    }
    (*endq)++;
    if(**endq != ',' && **endq != ')' && (part == 2 || **endq != ':')) {
      *endq = query;
      goto not_a_regex;
    }
    m->impl = &var_re_matcher;
    (*endq)--;
    if(*endq <= query) return mtev_false;
    if (is_encoded) {
      MTEV_MAYBE_DECL_VARS(char, decoded_tag, 512);
      MTEV_MAYBE_REALLOC(decoded_tag, mtev_b64_max_decode_len(*endq - query - 1) + 1);
      int len = mtev_b64_decode(query + 1, *endq - query - 1, (unsigned char *)decoded_tag, 
				MTEV_MAYBE_SIZE(decoded_tag));
      if (len == 0) {
        MTEV_MAYBE_FREE(decoded_tag);
        return mtev_false;
      }
      m->str = mtev_strndup(decoded_tag, len);
      MTEV_MAYBE_FREE(decoded_tag);
    } else {
      m->str = mtev_strndup(query + 1, *endq - query - 1);
      unescape_regex(m->str);
    }
    if(m->impl) {
      int erroffset = 0;
      if(m->impl->compile) {
        m->impl_data = m->impl->compile(m->str, &erroffset);
        if(!m->impl_data) {
          if (!is_encoded) *endq = query+1+erroffset;
          else *endq = query+1; // we can't easily know where in the encoded query the problem lies, so best to point to beginning of query
          return mtev_false;
        }
      }
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
    } else {
      if(!is_encoded && *query == '"') {
        is_escaped = mtev_true;
        *endq = query + 1;
        query = *endq;
      }
    }

    if(is_encoded) {
      while(**endq && noit_metric_tagset_is_taggable_b64_char(**endq)) (*endq)++;
    } else if(is_escaped) {
      while(**endq && **endq != '"') {
        if(**endq == '\\') {
          if(is_allowable_escape((*endq)[1])) (*endq)++;
          else return mtev_false;
        }
        (*endq)++;
      }
    } else {
      while(**endq &&
           (
            (part == 2 ?
               noit_metric_tagset_is_taggable_value(*endq, 1) :
               noit_metric_tagset_is_taggable_key(*endq, 1)
            ) || **endq == '*' || **endq == '?'
           )) (*endq)++;
    }

    if(*endq == query && part == 1) return mtev_false;
    if (is_encoded) {
      MTEV_MAYBE_DECL_VARS(char, decoded_tag, 512);
      MTEV_MAYBE_REALLOC(decoded_tag, mtev_b64_max_decode_len(*endq - query) + 1);
      int len = mtev_b64_decode(query, *endq - query, (unsigned char *)decoded_tag, 
				MTEV_MAYBE_SIZE(decoded_tag));
      if (len == 0 && part == 1) {
        MTEV_MAYBE_FREE(decoded_tag);
        return mtev_false;
      }
      m->str = mtev_strndup(decoded_tag, len);
      MTEV_MAYBE_FREE(decoded_tag);
      if (!is_encoded_match && **endq != '"') return mtev_false;
      if (is_encoded_match && **endq != '!') return mtev_false;
      (*endq)++; // skip the trailing quotation mark
    } else if(is_escaped) {
      m->str = mtev_strndup(query, *endq - query);
      unescape_tag_string(m->str);
      if (**endq != '"') return mtev_false;
      (*endq)++; // skip the trailing quotation mark
    } else {
      m->str = mtev_strndup(query, *endq - query);
    }
    if(m->impl == NULL) {
      if(strchr(m->str, '*') || strchr(m->str, '?')) {
        m->impl = &var_re_expansion_matcher;
      } else {
        m->impl = &var_exact_matcher;
      }
    }
    if(m->impl) {
      int erroffset;
      if(m->impl->compile) {
        m->impl_data = m->impl->compile(m->str, &erroffset);
        if(!m->impl_data) {
          if (!is_encoded) *endq = query+1+erroffset;
          else *endq = query+1; // we can't easily know where in the encoded query the problem lies, so best to point to beginning of query
          return mtev_false;
        }
      }
    }
  }
  assert(m->impl);
  return mtev_true;
}

static noit_metric_tag_search_ast_t *
noit_metric_tag_part_parse(const char *query, const char **endq, mtev_boolean allow_match) {
  noit_metric_tag_search_ast_t *node = NULL;
  *endq = query;
  while(*query && isspace(*query)) query++;
  if(!strncmp(query, "and(", 4) ||
     !strncmp(query, "or(", 3)) {
    *endq = strchr(query, '(');
    if(*endq == NULL) goto error; /* This is not possible, but coverity */
    node = calloc(1, sizeof(*node));
    node->operation = (*query == 'a') ? OP_AND_ARGS : OP_OR_ARGS;
    noit_metric_tag_search_ast_t *arg = NULL;
    do {
      (*endq)++;
      while(**endq && isspace(**endq)) (*endq)++;
      arg = noit_metric_tag_part_parse(*endq, endq, mtev_true);
      while(**endq && isspace(**endq)) (*endq)++;
      if((**endq != ',' && **endq != ')') || !arg) goto error;
      noit_metric_tag_search_add_arg(node, arg);
    } while(**endq == ',');
    (*endq)++;
  }
  else if(!strncmp(query, "hint(", 5)) {
    *endq = strchr(query, '(');
    if(*endq == NULL) goto error; /* This is not possible, but coverity */
    node = calloc(1, sizeof(*node));
    node->operation = OP_HINT_ARGS;
    noit_metric_tag_search_ast_t *arg = NULL;
    do {
      (*endq)++;
      while(**endq && isspace(**endq)) (*endq)++;
      arg = noit_metric_tag_part_parse(*endq, endq, mtev_true);
      while(**endq && isspace(**endq)) (*endq)++;
      if((**endq != ',' && **endq != ')') || !arg) goto error;
      noit_metric_tag_search_add_arg(node, arg);
    } while(**endq == ',');
    (*endq)++;
  }
  else if(!strncmp(query, "not(", 4)) {
    *endq = query + 4;
    while(**endq && isspace(**endq)) (*endq)++;
    node = calloc(1, sizeof(*node));
    node->operation = OP_NOT_ARGS;
    noit_metric_tag_search_ast_t *arg = noit_metric_tag_part_parse(*endq, endq, mtev_true);
    if(**endq != ')' || !arg) goto error;
    noit_metric_tag_search_add_arg(node, arg);
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
    if(out->contents.spec.cat.str)
      out->contents.spec.cat.str = strdup(out->contents.spec.cat.str);
    if(out->contents.spec.cat.impl && out->contents.spec.cat.impl->compile)
      out->contents.spec.cat.impl_data = out->contents.spec.cat.impl->compile(out->contents.spec.cat.str, NULL);
    if(out->contents.spec.name.str)
      out->contents.spec.name.str = strdup(out->contents.spec.name.str);
    if(out->contents.spec.name.impl && out->contents.spec.name.impl->compile)
      out->contents.spec.name.impl_data = out->contents.spec.name.impl->compile(out->contents.spec.name.str, NULL);
  }
  else {
    noit_metric_tag_search_ast_t **nodes = calloc(out->contents.args.cnt, sizeof(*nodes));
    out->contents.args.nallocd = out->contents.args.cnt;
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
noit_match_str(const char *subj, int subj_len, const struct noit_var_match_t *m) {
  MTEV_MAYBE_DECL_VARS(char, decoded_tag, 512);
  mtev_boolean rv = mtev_false;
  const char *ssubj = subj;
  int ssubj_len = subj_len;
  if (memcmp(subj, "b\"", 2) == 0) {
    const char *start = subj + 2;
    const char *end = memchr(start, '"', subj_len - 2);
    if (!end) goto out; // not decodable, no match
    MTEV_MAYBE_REALLOC(decoded_tag, mtev_b64_max_decode_len(end-start) + 1);
    int len = mtev_b64_decode(start, end - start, (unsigned char *)decoded_tag, 
			      MTEV_MAYBE_SIZE(decoded_tag));
    if (len == 0) goto out; // decode failed, no match
    ssubj_len = len;
    ssubj = decoded_tag;
  }
  if(m->impl) {
    if(m->impl->match(m->impl_data, m->str, ssubj, ssubj_len)) {
      rv = mtev_true;
    }
  }
  else if(m->str == NULL) {
    rv = true;
  }
  else if(strlen(m->str) == ssubj_len && !memcmp(m->str, ssubj, ssubj_len)) {
    rv = true;
  }
out:
  MTEV_MAYBE_FREE(decoded_tag);
  return rv;
}
static mtev_boolean
noit_metric_tag_match_evaluate_against_tags_multi(const struct noit_metric_tag_match_t *match,
                                                  const noit_metric_tagset_t **sets, const int set_cnt) {
  for(int s=0; s<set_cnt; s++) {
    const noit_metric_tagset_t *set = sets[s];
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
noit_metric_tag_search_evaluate_against_tags_multi(const noit_metric_tag_search_ast_t *search,
                                                   const noit_metric_tagset_t **set, const int set_cnt) {
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
    case OP_HINT_ARGS:
      if(!noit_metric_tag_search_evaluate_against_tags_multi(search->contents.args.node[0], set, set_cnt)) {
        return mtev_false;
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
noit_metric_tag_search_evaluate_against_tags(const noit_metric_tag_search_ast_t *search,
                                             const noit_metric_tagset_t *set) {
  return noit_metric_tag_search_evaluate_against_tags_multi(search, &set, 1);
}

mtev_boolean
noit_metric_tag_search_evaluate_against_metric_id(const noit_metric_tag_search_ast_t *search,
                                                  const noit_metric_id_t *id) {
  mtev_memory_begin();

#define MKTAGSETCOPY(name) \
  noit_metric_tag_t name##_tags[MAX_TAGS]; \
  memcpy(&name##_tags, name.tags, name.tag_count * sizeof(noit_metric_tag_t)); \
  name.tags = name##_tags

  // setup check tags
  noit_metric_tagset_t tagset_check = id->check;
  // Add in extra tags: __uuid
  if (tagset_check.tag_count > MAX_TAGS - 1) {
    mtev_memory_end();
    return 0;
  }
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
  if (tagset_stream.tag_count > MAX_TAGS - 1) {
    mtev_memory_end();
    return 0;
  }
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

  const noit_metric_tagset_t *tagsets[3] = { &tagset_check, &tagset_stream, &tagset_measurement };
  mtev_boolean ok = noit_metric_tag_search_evaluate_against_tags_multi(search, tagsets, 3);
  mtev_memory_end();
  return ok;
}

mtev_boolean
noit_metric_tag_search_has_hint(const noit_metric_tag_search_ast_t *search, const char *cat, const char *name) {
  if(cat == NULL || search == NULL || search->operation != OP_HINT_ARGS) return mtev_false;
  for(int i=1; i<search->contents.args.cnt; i++) {
    if(search->contents.args.node[i]->operation == OP_MATCH) {
      const noit_metric_tag_search_ast_t *node = search->contents.args.node[i];
      if(!strcmp(node->contents.spec.cat.str, cat)) {
        if(node->contents.spec.name.str == NULL && name == NULL) return mtev_true;
        if(!(node->contents.spec.name.str == NULL || name == NULL)) {
          if(!strcmp(node->contents.spec.name.str, name)) return mtev_true;
        }
      }
    }
  }
  return mtev_false;
}

static void
noit_metric_tag_search_unparse_part(const noit_metric_tag_search_ast_t *search, mtev_dyn_buffer_t *buf) {
  switch(search->operation) {
    case OP_MATCH: {
      const noit_metric_tag_match_t *spec = &search->contents.spec;
      mtev_dyn_buffer_add_printf(buf, "[%s]%s", spec->cat.impl->impl_name, spec->cat.str);
      if (spec->name.str) mtev_dyn_buffer_add_printf(buf, ":[%s]%s", spec->name.impl->impl_name, spec->name.str);
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
    case OP_HINT_ARGS:
      mtev_dyn_buffer_add_printf(buf, "hint(");
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
noit_metric_tag_search_unparse(const noit_metric_tag_search_ast_t *search) {
  char *res;
  mtev_dyn_buffer_t buf;
  mtev_dyn_buffer_init(&buf);
  noit_metric_tag_search_unparse_part(search, &buf);
  res = strdup((const char *)mtev_dyn_buffer_data(&buf));
  mtev_dyn_buffer_destroy(&buf);
  return res;
}

int
noit_metric_tag_search_swap(noit_metric_tag_search_ast_t *search, int idx1, int idx2) {
  if (!search) {
    return -1;
  }
  if (idx1 < 0 || idx2 < 0) {
    return -1;
  }
  int nargs = noit_metric_tag_search_get_nargs(search);
  if (idx1 >= nargs || idx2 >= nargs) {
    return -1;
  }
  if (idx1 == idx2) {
    /* nothing to do */
    return 0;
  }

  noit_metric_tag_search_ast_t *arg1 = noit_metric_tag_search_get_arg(search, idx1);
  noit_metric_tag_search_ast_t *arg2 = noit_metric_tag_search_get_arg(search, idx2);

  if (!arg1 || !arg2) {
    return -1;
  }
  noit_metric_tag_search_set_arg(search, idx1, arg2);
  noit_metric_tag_search_set_arg(search, idx2, arg1);
  return 0;
}
