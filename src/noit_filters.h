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

#ifndef _NOIT_FILTERS_H
#define _NOIT_FILTERS_H

#include <mtev_defines.h>
#include <mtev_hash.h>
#include <mtev_console.h>
#include <mtev_conf.h>
#include "noit_check.h"
#include "noit_metric_tag_search.h"

/* TODO: Remove this before merging */
#define ENABLE_LMDB_FILTERSETS 0
#define DEFAULT_FILTER_FLUSH_PERIOD_MS 300000 /* 5 minutes */

#define FILTERSET_ACCEPT_STRING "accept"
#define FILTERSET_ALLOW_STRING "allow"
#define FILTERSET_DENY_STRING "deny"
#define FILTERSET_SKIPTO_STRING "skipto:"
#define FILTERSET_SKIPTO_STRING_NO_COLON "skipto"

#define FILTERSET_TARGET_STRING "target"
#define FILTERSET_MODULE_STRING "module"
#define FILTERSET_NAME_STRING "name"
#define FILTERSET_METRIC_STRING "metric"

typedef enum { NOIT_FILTER_ACCEPT, NOIT_FILTER_DENY, NOIT_FILTER_SKIPTO } noit_ruletype_t;

typedef struct _filterrule {
  char *ruleid;
  char *skipto;
  struct _filterrule *skipto_rule;
  noit_ruletype_t type;
  char *target_re;
  pcre *target_override;
  pcre *target;
  pcre_extra *target_e;
  mtev_hash_table *target_ht;
  int target_auto_hash_max;
  char *module_re;
  pcre *module_override;
  pcre *module;
  pcre_extra *module_e;
  mtev_hash_table *module_ht;
  int module_auto_hash_max;
  char *name_re;
  pcre *name_override;
  pcre *name;
  pcre_extra *name_e;
  mtev_hash_table *name_ht;
  int name_auto_hash_max;
  char *metric_re;
  pcre *metric_override;
  pcre *metric;
  pcre_extra *metric_e;
  mtev_hash_table *metric_ht;
  int metric_auto_hash_max;
  char *stream_tags;
  noit_metric_tag_search_ast_t *stsearch;
  char *measurement_tags;
  noit_metric_tag_search_ast_t *mtsearch;
  struct _filterrule *next;
  uint32_t executions;
  uint32_t matches;
  struct timeval last_flush;
  struct timeval flush_interval;
} filterrule_t;

typedef struct {
  uint32_t ref_cnt;
  char *name;
  int64_t seq;
  filterrule_t *rules;
  uint32_t executions;
  uint32_t denies;
} filterset_t;

API_EXPORT(void)
  noit_filters_init();

API_EXPORT(noit_lmdb_instance_t *)
  noit_filters_get_lmdb_instance();

API_EXPORT(void)
  noit_refresh_filtersets();

API_EXPORT(mtev_boolean)
  noit_apply_filterset(const char *filterset,
                       noit_check_t *check,
                       metric_t *metric);

API_EXPORT(mtev_boolean)
  noit_filter_compile_add(mtev_conf_section_t setinfo);

API_EXPORT(int)
  noit_filter_remove(mtev_conf_section_t setinfo);

API_EXPORT(int)
  noit_filter_exists(const char *name);

API_EXPORT(int)
  noit_filter_get_seq(const char *name, int64_t *seq);

API_EXPORT(void)
  noit_filters_rest_init();

API_EXPORT(int)
  noit_filtersets_cull_unused();

API_EXPORT(void)
  noit_filters_init_globals(void);

API_EXPORT(int)
  noit_filters_process_repl(xmlDocPtr);

API_EXPORT(void)
  noit_filtersets_build_cluster_changelog(void *);

#endif
