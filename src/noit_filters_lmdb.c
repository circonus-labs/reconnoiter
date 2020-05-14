/* Copyright (c) 2020, Circonus, Inc. All rights reserved.
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

#include "noit_filters_lmdb.h"
#include "noit_filters.h"
#include "noit_lmdb_tools.h"
#include "noit_fb.h"
#include "flatbuffers/filterset_builder.h"
#include "flatbuffers/filterset_verifier.h"
#include "flatbuffers/filterset_rule_builder.h"
#include "flatbuffers/filterset_rule_verifier.h"
#include <mtev_conf.h>
#include <mtev_watchdog.h>

static int32_t global_default_filter_flush_period_ms = DEFAULT_FILTER_FLUSH_PERIOD_MS;
static pcre *fallback_no_match = NULL;

typedef struct noit_filter_lmdb_rule_filterset {
  noit_ruletype_t type;
  char *skipto;
  char *ruleid;
  int32_t filter_flush_period;
  mtev_boolean filter_flush_period_present;
  int32_t target_auto_add;
  mtev_boolean target_auto_add_present;
  int32_t module_auto_add;
  mtev_boolean module_auto_add_present;
  int32_t name_auto_add;
  mtev_boolean name_auto_add_present;
  int32_t metric_auto_add;
  mtev_boolean metric_auto_add_present;
  mtev_hash_table *target_hash_rules;
  mtev_hash_table *module_hash_rules;
  mtev_hash_table *name_hash_rules;
  mtev_hash_table *metric_hash_rules;
  char *target_attribute;
  char *module_attribute;
  char *name_attribute;
  char *metric_attribute;
  char *stream_tags_tag_str;
  char *measurement_tags_tag_str;
} noit_filter_lmdb_filterset_rule_t;

typedef enum {
  FILTERSET_RULE_UNKNOWN_TYPE = 0,
  FILTERSET_RULE_TARGET_TYPE,
  FILTERSET_RULE_MODULE_TYPE,
  FILTERSET_RULE_NAME_TYPE,
  FILTERSET_RULE_METRIC_TYPE
} noit_filter_lmdb_rule_type_e;

static void *get_aligned_fb(mtev_dyn_buffer_t *aligned, void *d, uint32_t size)
{
  mtev_dyn_buffer_init(aligned);
  void *fb_data = d;
  /* If d is unaligned, then align the data within the mtev_dyn_buffer_t aligned. */
  if((uintptr_t)d & 15) {
    if(size >= mtev_dyn_buffer_size(aligned)-16) {
      mtev_dyn_buffer_ensure(aligned, size+16);
    }
    size_t oa = (size_t)(uintptr_t)mtev_dyn_buffer_data(aligned) & 15;
    if(oa) mtev_dyn_buffer_advance(aligned, 16-oa);
    fb_data = mtev_dyn_buffer_write_pointer(aligned);
    mtev_dyn_buffer_add(aligned, d, size);
  }
  return fb_data;
}

static void
noit_filters_lmdb_free_filterset_rule(noit_filter_lmdb_filterset_rule_t *rule) {
  if (rule) {
    free(rule->skipto);
    free(rule->ruleid);
    free(rule);
  }
}

static int
noit_filters_lmdb_write_finalized_fb_to_lmdb(char *filterset_name, void *buffer, size_t buffer_size) {
  int rc;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  MDB_val mdb_key, mdb_data;
  size_t key_size;
  char *key = NULL;
  noit_lmdb_instance_t *instance = noit_filters_get_lmdb_instance();
  
  mtevAssert(instance != NULL);

  do { 
    key  = noit_lmdb_make_filterset_key(filterset_name, &key_size);
    mtevAssert(key);

    mdb_key.mv_data = key;
    mdb_key.mv_size = key_size;
    mdb_data.mv_data = buffer;
    mdb_data.mv_size = buffer_size;

    pthread_rwlock_rdlock(&instance->lock);

    rc = mdb_txn_begin(instance->env, NULL, 0, &txn);
    if (rc != 0) {
      pthread_rwlock_unlock(&instance->lock);
      return rc;
    }
    mdb_cursor_open(txn, instance->dbi, &cursor);
    rc = mdb_cursor_put(cursor, &mdb_key, &mdb_data, 0);
    if (rc) {
      mdb_cursor_close(cursor);
      mdb_txn_abort(txn);
      free(key);
      pthread_rwlock_unlock(&instance->lock);
      if (rc == MDB_MAP_FULL) {
        noit_lmdb_resize_instance(instance);
        continue;
      }
      else {
        return rc;
      }
    }
    mdb_cursor_close(cursor);
    rc = mdb_txn_commit(txn);
    if (rc) {
      free(key);
      pthread_rwlock_unlock(&instance->lock);
      if (rc == MDB_MAP_FULL) {
        noit_lmdb_resize_instance(instance);
        continue;
      }
      else {
        return rc;
      }
    }
    pthread_rwlock_unlock(&instance->lock);
    free(key);
  } while(0);

  return 0;
}

static void
noit_filters_lmdb_add_filterset_rule_info(flatcc_builder_t *B,
                                          noit_filter_lmdb_filterset_rule_t *rule) {
#define FILTERSET_RULE_INFO_ADD_HASH(rtype) do { \
  if ((rule->rtype##_hash_rules) || ((rule->rtype##_auto_add_present) && (rule->rtype##_auto_add > 0))) { \
    ns(FiltersetRuleInfo_data_FiltersetRuleHashValue_start(B)); \
    if ((rule->rtype##_auto_add_present) && (rule->rtype##_auto_add > 0)) { \
      ns(FiltersetRuleHashValue_auto_add_max_add(B, rule->rtype##_auto_add)); \
    } \
    ns(FiltersetRuleHashValue_values_start(B)); \
    if (rule->rtype##_hash_rules) { \
      mtev_hash_iter iter = MTEV_HASH_ITER_ZERO; \
      const char *k; \
      int klen; \
      void *data; \
      while(mtev_hash_next(rule->rtype##_hash_rules, &iter, &k, &klen, \
        &data)) { \
        char *tmp = (char *)calloc(1, klen+1); \
        memcpy(tmp, k, klen); \
        ns(FiltersetRuleHashValue_values_push_create_str(B, tmp)); \
        free(tmp); \
      } \
    } \
    ns(FiltersetRuleHashValue_values_end(B)); \
    ns(FiltersetRuleInfo_data_FiltersetRuleHashValue_end(B)); \
  } \
} while (0);

#define FILTERSET_RULE_INFO_ADD_ATTRIBUTE(rtype) do { \
  if ((!rule->rtype##_hash_rules) && ((!rule->rtype##_auto_add_present) || (rule->rtype##_auto_add == 0))) { \
    if (rule->rtype##_attribute) { \
      ns(FiltersetRuleInfo_data_FiltersetRuleAttributeValue_start(B)); \
      ns(FiltersetRuleAttributeValue_regex_create_str(B, rule->rtype##_attribute)); \
      ns(FiltersetRuleInfo_data_FiltersetRuleAttributeValue_end(B)); \
    } \
  } \
} while (0);

#define FILTERSET_RULE_ADD_TAG(rtype) do { \
  if (rule->rtype##_tag_str) { \
    ns(FiltersetRule_tags_push_start(B)); \
    ns(FiltersetRuleTagInfo_type_create_str(B, #rtype)); \
    ns(FiltersetRuleTagInfo_value_create_str(B, rule->rtype##_tag_str)); \
    ns(FiltersetRule_tags_push_end(B)); \
  } \
} while (0);

  ns(FiltersetRule_info_start(B));
  if (rule->target_hash_rules || rule->target_attribute || (rule->target_auto_add_present && rule->target_auto_add)) {
    ns(FiltersetRule_info_push_start(B));
    ns(FiltersetRuleInfo_type_create_str(B, FILTERSET_TARGET_STRING));
    FILTERSET_RULE_INFO_ADD_HASH(target);
    FILTERSET_RULE_INFO_ADD_ATTRIBUTE(target);
    ns(FiltersetRule_info_push_end(B));
  }
  if (rule->module_hash_rules || rule->module_attribute || (rule->module_auto_add_present && rule->module_auto_add)) {
    ns(FiltersetRule_info_push_start(B));
    ns(FiltersetRuleInfo_type_create_str(B, FILTERSET_MODULE_STRING));
    FILTERSET_RULE_INFO_ADD_HASH(module);
    FILTERSET_RULE_INFO_ADD_ATTRIBUTE(module);
    ns(FiltersetRule_info_push_end(B));
  }
  if (rule->name_hash_rules || rule->name_attribute || (rule->name_auto_add_present && rule->name_auto_add)) {
    ns(FiltersetRule_info_push_start(B));
    ns(FiltersetRuleInfo_type_create_str(B, FILTERSET_NAME_STRING));
    FILTERSET_RULE_INFO_ADD_HASH(name);
    FILTERSET_RULE_INFO_ADD_ATTRIBUTE(name);
    ns(FiltersetRule_info_push_end(B));
  }
  if (rule->metric_hash_rules || rule->metric_attribute || (rule->metric_auto_add_present && rule->metric_auto_add)) {
    ns(FiltersetRule_info_push_start(B));
    ns(FiltersetRuleInfo_type_create_str(B, FILTERSET_METRIC_STRING));
    FILTERSET_RULE_INFO_ADD_HASH(metric);
    FILTERSET_RULE_INFO_ADD_ATTRIBUTE(metric);
    ns(FiltersetRule_info_push_end(B));
  }
  ns(FiltersetRule_info_end(B));
  ns(FiltersetRule_tags_start(B));
  FILTERSET_RULE_ADD_TAG(stream_tags);
  FILTERSET_RULE_ADD_TAG(measurement_tags);
  ns(FiltersetRule_tags_end(B));
}

static int
noit_filters_lmdb_write_flatbuffer_to_db(char *filterset_name,
                                         int64_t *sequence,
                                         mtev_boolean *cull,
                                         int32_t *filter_flush_global,
                                         noit_filter_lmdb_filterset_rule_t **rules,
                                         int64_t rule_cnt,
                                         char **error) {
  int i = 0, ret = 0;
  size_t buffer_size;
  flatcc_builder_t builder;
  flatcc_builder_t *B = &builder;

  mtevAssert(error != NULL);
  if (*error) {
    free(*error);
    *error = NULL;
  }
  if (!filterset_name) {
    *error = strdup("no filterset name provided");
    return -1;
  }
  flatcc_builder_init(B);
  ns(Filterset_start_as_root(B));
  ns(Filterset_name_create_str(B, filterset_name));
  if (sequence) {
    ns(Filterset_seq_add)(B, *sequence);
  }
  if (cull) {
    ns(Filterset_cull_add)(B, *cull);
  }
  else {
    ns(Filterset_cull_add)(B, true);
  }
  ns(Filterset_filterset_flush_period_start(B));
  if (filter_flush_global) {
    ns(FiltersetRuleFlushPeriod_present_add(B, true));
    ns(FiltersetRuleFlushPeriod_value_add(B, *filter_flush_global));
  }
  else {
    ns(FiltersetRuleFlushPeriod_present_add(B, false));
  }
  ns(Filterset_filterset_flush_period_end(B));
  ns(Filterset_rules_start(B));
  for (i = 0; i < rule_cnt; i++) {
    noit_filter_lmdb_filterset_rule_t *rule = rules[i];
    if (!rule) {
      continue;
    }
    ns(Filterset_rules_push_start(B));
    if (rule->ruleid) {
      ns(FiltersetRule_id_create_str(B, rule->ruleid));
    }
    ns(FiltersetRule_filterset_flush_period_start(B));
    if (rule->filter_flush_period_present) {
      ns(FiltersetRuleFlushPeriod_present_add(B, true));
      ns(FiltersetRuleFlushPeriod_value_add(B, rule->filter_flush_period));
    }
    else {
      ns(FiltersetRuleFlushPeriod_present_add(B, false));
    }
    ns(FiltersetRule_filterset_flush_period_end(B));
    switch(rule->type) {
      case NOIT_FILTER_ACCEPT:
        ns(FiltersetRule_rule_type_create_str(B, FILTERSET_ACCEPT_STRING));
        break;
      case NOIT_FILTER_DENY:
        ns(FiltersetRule_rule_type_create_str(B, FILTERSET_DENY_STRING));
        break;
      case NOIT_FILTER_SKIPTO:
        ns(FiltersetRule_rule_type_create_str(B, FILTERSET_SKIPTO_STRING_NO_COLON));
        if (rule->skipto) {
          ns(FiltersetRule_skipto_value_create_str(B, rule->skipto));
        }
        break;
      default:
        mtevFatal(mtev_error, "noit_filters_lmdb_write_flatbuffer_to_db: undefined type in db (%d) for %s\n", (int)rule->type, filterset_name);
        break;
    }

    noit_filters_lmdb_add_filterset_rule_info(B, rule);

    ns(Filterset_rules_push_end(B));
  }
  ns(Filterset_rules_end(B));
  ns(Filterset_end_as_root(B));
  void *buffer = flatcc_builder_finalize_buffer(B, &buffer_size);
  flatcc_builder_clear(B);

  if ((ret = ns(Filterset_verify_as_root(buffer, buffer_size)))) {
    if(asprintf(error, "could not verify Filterset flatbuffer (name %s, len %zd)", filterset_name, buffer_size) < 0)
      *error = strdup("could not verify flatbuffer");
    return ret;
  }
  else {
    mtevL(mtev_debug, "noit_filters_lmdb_write_flatbuffer_to_db: successfully verified Filterset flatbuffer (name %s, len %zd)\n", filterset_name, buffer_size);
  }
  ret = noit_filters_lmdb_write_finalized_fb_to_lmdb(filterset_name, buffer, buffer_size);
  free(buffer);
  if (ret) {
    if(asprintf(error, "failed to write filterset %s to the database: %d (%s)", filterset_name, ret, mdb_strerror(ret)) < 0)
      *error = strdup("failed to write");
    return ret;
  }
  else {
    mtevL(mtev_debug, "noit_filters_lmdb_write_flatbuffer_to_db: successfully wrote filterset %s to the database\n", filterset_name);
  }
  return 0;
}

static noit_filter_lmdb_filterset_rule_t *
noit_filters_lmdb_one_xml_rule_to_memory(mtev_conf_section_t rule_conf) {
#define GET_RULE_AUTO_ADD(rtype) do { \
  if (mtev_conf_get_int32(rule_conf, "@" #rtype "_auto_add", &rule->rtype##_auto_add)) { \
    if (rule->rtype##_auto_add < 0) { \
      rule->rtype##_auto_add = 0; \
    } \
    else { \
      rule->rtype##_auto_add_present = mtev_true; \
    } \
  } \
} while(0);

#define GET_RULE_HASH_VALUES(rtype) do { \
  int hte_cnt, hti, tablesize = 2; \
  char *htstr = NULL; \
  mtev_conf_section_t *htentries = mtev_conf_get_sections_read(rule_conf, #rtype, &hte_cnt); \
  if(hte_cnt) { \
    rule->rtype##_hash_rules = calloc(1, sizeof(*(rule->rtype##_hash_rules))); \
    while(tablesize < hte_cnt) { \
      tablesize <<= 1; \
    } \
    mtev_hash_init_size(rule->rtype##_hash_rules, tablesize); \
    for(hti=0; hti<hte_cnt; hti++) { \
      if(!mtev_conf_get_string(htentries[hti], "self::node()", &htstr)) { \
        mtevL(mtev_error, "Error fetching text content from filter match.\n"); \
      } \
      else { \
        mtev_hash_replace(rule->rtype##_hash_rules, htstr, strlen(htstr), NULL, free, NULL); \
        htstr = NULL; \
      } \
    } \
  } \
  mtev_conf_release_sections_read(htentries, hte_cnt); \
} while (0);

#define GET_RULE_ATTRIBUTES(rtype) do { \
  char *longre = NULL; \
  if(mtev_conf_get_string(rule_conf, "@" #rtype, &longre)) { \
    rule->rtype##_attribute = longre; \
  } \
} while (0);

#define GET_RULE_TAGS(rtype) do { \
  char *expr = NULL; \
  if(mtev_conf_get_string(rule_conf, "@" #rtype, &expr)) { \
    rule->rtype##_tag_str = expr; \
  } \
} while (0);

  char buffer[MAX_METRIC_TAGGED_NAME];
  noit_filter_lmdb_filterset_rule_t *rule =
    (noit_filter_lmdb_filterset_rule_t *)calloc(1, sizeof(noit_filter_lmdb_filterset_rule_t));

  if(!mtev_conf_get_stringbuf(rule_conf, "@type", buffer, sizeof(buffer)) ||
    (strcmp(buffer, FILTERSET_ACCEPT_STRING) && strcmp(buffer, FILTERSET_ALLOW_STRING) && strcmp(buffer, FILTERSET_DENY_STRING) &&
    strncmp(buffer, FILTERSET_SKIPTO_STRING, strlen(FILTERSET_SKIPTO_STRING)))) {
    mtevL(mtev_error, "rule must have type 'accept' or 'allow' or 'deny' or 'skipto:'\n");
    free(rule);
    return NULL;
  }
  if(!strncasecmp(buffer, FILTERSET_SKIPTO_STRING, strlen(FILTERSET_SKIPTO_STRING))) {
    rule->type = NOIT_FILTER_SKIPTO;
    rule->skipto = strdup(buffer+strlen(FILTERSET_SKIPTO_STRING));
  }
  else {
    /* NOTE: With XML filtersets, "accept" and "allow" were both accepted and did the same thing. With
     * LMDB filtersets, we coerce both into ACCEPT in the DB */
    rule->type = (!strcmp(buffer, FILTERSET_ACCEPT_STRING) || !strcmp(buffer, FILTERSET_ALLOW_STRING)) ?
      NOIT_FILTER_ACCEPT : NOIT_FILTER_DENY;
  }
  if(mtev_conf_get_int32(rule_conf, "self::node()/@filter_flush_period", &rule->filter_flush_period)) {
    if (rule->filter_flush_period < 0) {
      rule->filter_flush_period = 0;
    }
    else {
      rule->filter_flush_period_present = mtev_true;
    }
  }
  if(mtev_conf_get_stringbuf(rule_conf, "@id", buffer, sizeof(buffer))) {
    rule->ruleid = strdup(buffer);
  }

  GET_RULE_AUTO_ADD(target);
  GET_RULE_AUTO_ADD(module);
  GET_RULE_AUTO_ADD(name);
  GET_RULE_AUTO_ADD(metric);

  GET_RULE_HASH_VALUES(target);
  GET_RULE_HASH_VALUES(module);
  GET_RULE_HASH_VALUES(name);
  GET_RULE_HASH_VALUES(metric);

  GET_RULE_ATTRIBUTES(target);
  GET_RULE_ATTRIBUTES(module);
  GET_RULE_ATTRIBUTES(name);
  GET_RULE_ATTRIBUTES(metric);

  GET_RULE_TAGS(stream_tags);
  GET_RULE_TAGS(measurement_tags);

  return rule;
}

static int
noit_filters_lmdb_convert_one_xml_filterset_to_lmdb(mtev_conf_section_t fs_section, char **error) {
  int i = 0, rv = -1;
  char *filterset_name = NULL;
  int64_t sequence = 0;
  int32_t filter_flush_period = 0;
  mtev_boolean cull = mtev_false;
  mtev_boolean sequence_present = mtev_false, cull_present = mtev_false, filter_flush_period_present = mtev_false;
  noit_filter_lmdb_filterset_rule_t **rules = NULL;
  mtev_conf_section_t *rules_conf;
  int rule_cnt = 0;
  noit_lmdb_instance_t *instance = noit_filters_get_lmdb_instance();
  
  mtevAssert(instance != NULL);

  mtevAssert(error != NULL);
  if (*error) {
    free(*error);
    *error = NULL;
  }

  /* We want to heartbeat here... otherwise, if a lot of checks are 
   * configured or if we're running on a slower system, we could 
   * end up getting watchdog killed before we get a chance to run 
   * any checks */
  mtev_watchdog_child_heartbeat();

  if (!mtev_conf_get_string(fs_section, "self::node()/@name", &filterset_name)) {
    /* Filterset must have a name */
    *error = strdup("no name found for filterset");
    return -1;
  }
  if (mtev_conf_get_int64(fs_section, "self::node()/@seq", &sequence)) {
    sequence_present = mtev_true;
  }
  if (mtev_conf_get_boolean(fs_section, "self::node()/@cull", &cull)) {
    cull_present = mtev_true;
  }
  if(mtev_conf_get_int32(fs_section, "self::node()/@filter_flush_period", &filter_flush_period)) {
    filter_flush_period_present = mtev_true;
  }

  rules_conf = mtev_conf_get_sections_read(fs_section, "rule", &rule_cnt);

  rules = (noit_filter_lmdb_filterset_rule_t **)calloc(rule_cnt, sizeof(noit_filter_lmdb_filterset_rule_t *));

  for (i = 0; i < rule_cnt; i++) {
    rules[i] = noit_filters_lmdb_one_xml_rule_to_memory(rules_conf[i]);
  }

  mtev_conf_release_sections_read(rules_conf, rule_cnt);

  rv = noit_filters_lmdb_write_flatbuffer_to_db(filterset_name,
               (sequence_present) ? &sequence : NULL,
               (cull_present) ? &cull : NULL,
               (filter_flush_period_present) ? &filter_flush_period : NULL,
               rules,
               (int64_t)rule_cnt,
               error);

  free(filterset_name);
  for (i = 0; i < rule_cnt; i++) {
    noit_filters_lmdb_free_filterset_rule(rules[i]);
  }
  free(rules);

  return rv;
}

static mtev_boolean
noit_filters_lmdb_load_one_from_db_locked(void *fb_data, size_t fb_size) {

#define LMDB_HT_COMPILE(rtype, canon) do { \
  int32_t auto_max = 0; \
  int hte_cnt = 0, hti, tablesize = 2; \
  flatbuffers_string_vec_t values_vec = NULL; \
  ns(FiltersetRuleHashValue_table_t) v = ns(FiltersetRuleInfo_data(rule_info_table)); \
  auto_max = ns(FiltersetRuleHashValue_auto_add_max(v)); \
  if (auto_max < 0) { \
    auto_max = 0; \
  } \
  if (ns(FiltersetRuleHashValue_values_is_present(v))) { \
    values_vec = ns(FiltersetRuleHashValue_values(v)); \
    hte_cnt = flatbuffers_string_vec_len(values_vec); \
  } \
  if (auto_max || hte_cnt) { \
    rule->rtype##_auto_hash_max = auto_max; \
    rule->rtype##_ht = calloc(1, sizeof(*(rule->rtype##_ht))); \
    while(tablesize < hte_cnt) tablesize <<= 1; \
    mtev_hash_init_size(rule->rtype##_ht, tablesize); \
    for(hti=0; hti<hte_cnt; hti++) { \
      flatbuffers_string_t r = flatbuffers_string_vec_at(values_vec, hti); \
      char *htstr = strdup(r); \
      if(canon) { \
        char tgt[MAX_METRIC_TAGGED_NAME]; \
        if(noit_metric_canonicalize(htstr, strlen(htstr), tgt, sizeof(tgt), mtev_true) > 0) { \
          free(htstr); \
          htstr = strdup(tgt); \
        } \
      } \
      mtevL(mtev_debug, "LMDB_HT_COMPILE(%p) -> (%s)\n", rule->rtype##_ht, htstr); \
      mtev_hash_replace(rule->rtype##_ht, htstr, strlen(htstr), NULL, free, NULL); \
    } \
  } \
} while (0);

#define LMDB_RULE_COMPILE(rtype) do { \
  char *longre = NULL; \
  const char *error; \
  int erroffset; \
  ns(FiltersetRuleAttributeValue_table_t) v = ns(FiltersetRuleInfo_data(rule_info_table)); \
  flatbuffers_string_t r = ns(FiltersetRuleAttributeValue_regex(v)); \
  longre = strdup(r); \
  rule->rtype = pcre_compile(longre, 0, &error, &erroffset, NULL); \
  if(!rule->rtype) { \
    mtevL(mtev_debug, "set '%s' rule '%s: %s' compile failed: %s\n", \
          set->name, #rtype, longre, error ? error : "???"); \
    rule->rtype##_override = fallback_no_match; \
    mtevAssert(asprintf(&rule->rtype##_re, "/%s/ failed to compile", longre)); \
  } \
  else { \
    rule->rtype##_re = strdup(longre); \
    rule->rtype##_e = pcre_study(rule->rtype, 0, &error); \
  } \
  free(longre); \
} while (0);

#define LMDB_TAGS_COMPILE(rtype, search) do { \
  if (rule->metric_ht == NULL) { \
    flatbuffers_string_t tag_value = ns(FiltersetRuleTagInfo_value(rule_tag_info_table)); \
    char *expr = strdup(tag_value); \
    int erroffset; \
    rule->rtype = strdup(expr); \
    rule->search = noit_metric_tag_search_parse(rule->rtype, &erroffset); \
    if(!rule->search) { \
      mtevL(mtev_error, "set '%s' rule '%s: %s' compile failed at offset %d\n", \
            set->name, #rtype, expr, erroffset); \
      rule->metric_override = fallback_no_match; \
    } \
    free(expr); \
  } \
} while(0)

  int i = 0, j = 0;
  filterset_t *set = NULL;;
  int64_t seq = 0;
  size_t num_rules = 0;
  /* We need to align the flatbuffer */
  mtev_dyn_buffer_t aligned;
  void *aligned_fb_data = get_aligned_fb(&aligned, fb_data, fb_size);
  int fb_ret = ns(Filterset_verify_as_root(aligned_fb_data, fb_size));
  if(fb_ret != 0) {
    mtevL(mtev_error, "Corrupt filterset flatbuffer: %s\n", flatcc_verify_error_string(fb_ret));
    mtev_dyn_buffer_destroy(&aligned);
    return mtev_false;
  }
  ns(Filterset_table_t) filterset = ns(Filterset_as_root(aligned_fb_data));

  set = (filterset_t *)calloc(1, sizeof(filterset_t));
  set->ref_cnt = 1;
  set->name = strdup(ns(Filterset_name(filterset)));

  seq = ns(Filterset_seq(filterset));
  mtevAssert (seq >= 0);
  set->seq = seq;

  int local_default_filter_flush_period_ms = global_default_filter_flush_period_ms;
  if (ns(Filterset_filterset_flush_period_is_present(filterset))) {
    ns(FiltersetRuleFlushPeriod_table_t) fpt = ns(Filterset_filterset_flush_period(filterset));
    if (ns(FiltersetRuleFlushPeriod_present(fpt))) {
      local_default_filter_flush_period_ms = ns(FiltersetRuleFlushPeriod_value(fpt));
    }
  }
  if (local_default_filter_flush_period_ms < 0) {
    local_default_filter_flush_period_ms = 0;
  }

  ns(FiltersetRule_vec_t) rule_vec = ns(Filterset_rules(filterset));
  num_rules = ns(FiltersetRule_vec_len(rule_vec));
  for (i=num_rules-1; i >= 0; i--) {
    filterrule_t *rule = NULL;
    rule = (filterrule_t *)calloc(1, sizeof(filterrule_t));
    ns(FiltersetRule_table_t) fs_rule = ns(FiltersetRule_vec_at(rule_vec, i));
    flatbuffers_string_t ruleid = ns(FiltersetRule_id(fs_rule));
    if (ruleid != NULL) {
      rule->ruleid = strdup(ns(FiltersetRule_id(fs_rule)));
    }
    int32_t ffp = local_default_filter_flush_period_ms;
    if (ns(FiltersetRule_filterset_flush_period_is_present(fs_rule))) {
      ns(FiltersetRuleFlushPeriod_table_t) fpt = ns(FiltersetRule_filterset_flush_period(fs_rule));
      if (ns(FiltersetRuleFlushPeriod_present(fpt))) {
        ffp = ns(FiltersetRuleFlushPeriod_value(fpt));
      }
    }
    if(ffp < 0) {
      ffp = 0;
    }
    rule->flush_interval.tv_sec = ffp/1000;
    rule->flush_interval.tv_usec = ffp%1000;

    flatbuffers_string_t rule_type = ns(FiltersetRule_rule_type(fs_rule));

    if (!strcmp(rule_type, FILTERSET_ACCEPT_STRING)) {
      rule->type = NOIT_FILTER_ACCEPT;
    }
    else if (!strcmp(rule_type, FILTERSET_DENY_STRING)) {
      rule->type = NOIT_FILTER_DENY;
    }
    else if (!strcmp(rule_type, FILTERSET_SKIPTO_STRING_NO_COLON)) {
      flatbuffers_string_t skipto = NULL;
      rule->type = NOIT_FILTER_SKIPTO;
      skipto = ns(FiltersetRule_skipto_value(fs_rule));
      if (skipto != NULL) {
        rule->skipto = strdup(skipto);
      }
      else {
        mtevL(mtev_error, "noit_filters_lmdb_load_one_from_db: skipto type with no value\n");
      }
    }
    else {
      mtevL(mtev_error, "noit_filters_lmdb_load_one_from_db: unknown type - %s - setting to deny\n", rule_type);
      rule->type = NOIT_FILTER_DENY;
    }

    ns(FiltersetRuleInfo_vec_t)rule_info_vec = ns(FiltersetRule_info(fs_rule));
    size_t num_run_info = ns(FiltersetRuleInfo_vec_len(rule_info_vec));
    circonus_FiltersetRuleHashValue_table_t v;
    for (j = 0; j < num_run_info; j++) {
      ns(FiltersetRuleInfo_table_t)rule_info_table = ns(FiltersetRuleInfo_vec_at(rule_info_vec, j));
      flatbuffers_string_t info_type = ns(FiltersetRuleInfo_type(rule_info_table));
      noit_filter_lmdb_rule_type_e local_type = FILTERSET_RULE_UNKNOWN_TYPE;

      if (!strcmp(info_type, FILTERSET_TARGET_STRING)) {
        local_type = FILTERSET_RULE_TARGET_TYPE;
      }
      else if (!strcmp(info_type, FILTERSET_MODULE_STRING)) {
        local_type = FILTERSET_RULE_MODULE_TYPE;
      }
      else if (!strcmp(info_type, FILTERSET_NAME_STRING)) {
        local_type = FILTERSET_RULE_NAME_TYPE;
      }
      else if (!strcmp(info_type, FILTERSET_METRIC_STRING)) {
        local_type = FILTERSET_RULE_METRIC_TYPE;
      }
      else {
        mtevL(mtev_error, "noit_filters_lmdb_load_one_from_db: unknown type (%s), skipping...\n", info_type);
        free(rule);
        continue;
      }
      switch(ns(FiltersetRuleInfo_data_type(rule_info_table))) {
        case ns(FiltersetRuleValueUnion_FiltersetRuleHashValue):
        {
          switch(local_type) {
            case FILTERSET_RULE_TARGET_TYPE:
              LMDB_HT_COMPILE(target, mtev_false);
              break;
            case FILTERSET_RULE_MODULE_TYPE:
              LMDB_HT_COMPILE(module, mtev_false);
              break;
            case FILTERSET_RULE_NAME_TYPE:
              LMDB_HT_COMPILE(name, mtev_false);
              break;
            case FILTERSET_RULE_METRIC_TYPE:
              LMDB_HT_COMPILE(metric, mtev_true);
              break;
            default:
              /* Should be impossible */
              break;
          }
          break;
        }
        case ns(FiltersetRuleValueUnion_FiltersetRuleAttributeValue):
        {
          switch(local_type) {
            case FILTERSET_RULE_TARGET_TYPE:
              LMDB_RULE_COMPILE(target);
              break;
            case FILTERSET_RULE_MODULE_TYPE:
              LMDB_RULE_COMPILE(module);
              break;
            case FILTERSET_RULE_NAME_TYPE:
              LMDB_RULE_COMPILE(name);
              break;
            case FILTERSET_RULE_METRIC_TYPE:
              LMDB_RULE_COMPILE(metric);
              break;
            default:
              /* Should be impossible */
              break;
          }
          break;
        }
        default:
          mtevL(mtev_error, "noit_filters_lmdb_load_one_from_db: got unexpected FiltersetRuleValueUnion type\n");
          break;
      }
    }
    if (ns(FiltersetRule_tags_is_present(fs_rule))) {
      int ii = 0;
      ns(FiltersetRuleTagInfo_vec_t)rule_tag_info_vec = ns(FiltersetRule_tags(fs_rule));
      size_t num_tags_info = ns(FiltersetRuleTagInfo_vec_len(rule_tag_info_vec));
      for (ii = 0; ii < num_tags_info; ii++) {
        ns(FiltersetRuleTagInfo_table_t)rule_tag_info_table = ns(FiltersetRuleTagInfo_vec_at(rule_tag_info_vec, ii));
        flatbuffers_string_t tag_type = ns(FiltersetRuleTagInfo_type(rule_tag_info_table));
        if (!strcmp(tag_type, FILTERSET_TAG_STREAM_TAGS_STRING)) {
          LMDB_TAGS_COMPILE(stream_tags, stsearch);
        }
        else if (!strcmp(tag_type, FILTERSET_TAG_MEASUREMENT_TAGS_STRING)) {
          LMDB_TAGS_COMPILE(measurement_tags, mtsearch);
        }
        else {
          mtevL(mtev_error, "noit_filters_lmdb_load_one_from_db: got unexpected tag type in db: %s\n", tag_type);
        }
      }
    }
    rule->next = set->rules;
    set->rules = rule;
  }
  mtev_dyn_buffer_destroy(&aligned);

  filterrule_t *cursor;
  for(cursor = set->rules; cursor && cursor->next; cursor = cursor->next) {
    if(cursor->skipto) {
      filterrule_t *target;
      for(target = cursor->next; target; target = target->next) {
        if(target->ruleid && !strcmp(cursor->skipto, target->ruleid)) {
          cursor->skipto_rule = target;
          break;
        }
      }
      if(!cursor->skipto_rule) {
        mtevL(mtev_error, "filterset %s skipto:%s not found\n",
              set->name, cursor->skipto);
      }
    }
  }

  mtev_boolean used_new_one = noit_filter_compile_add_load_set(set);
  return used_new_one;
}

static int
noit_filters_lmdb_remove_from_db(char *name) {
  int rc = MDB_SUCCESS;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  MDB_val mdb_key;
  char *key = NULL;
  size_t key_size;
  noit_lmdb_instance_t *instance = noit_filters_get_lmdb_instance();

  mtevAssert(instance != NULL);
  if (!noit_filters_lmdb_already_in_db(name)) {
    return MDB_NOTFOUND;
  }

  key = noit_lmdb_make_filterset_key(name, &key_size);
  mtevAssert(key);

  mdb_key.mv_data = key;
  mdb_key.mv_size = key_size;

  pthread_rwlock_rdlock(&instance->lock);

  mdb_txn_begin(instance->env, NULL, 0, &txn);
  rc = mdb_del(txn, instance->dbi, &mdb_key, NULL);
  if (rc != MDB_SUCCESS) {
    mtevL(mtev_error, "failed to delete key in filters: %d (%s)\n", rc, mdb_strerror(rc));
    mdb_txn_abort(txn);
    pthread_rwlock_unlock(&instance->lock);
    free(key);
    return rc;
  }

  rc = mdb_txn_commit(txn);
  if (rc != MDB_SUCCESS) {
    mtevL(mtev_error, "failed to delete key in filters: %d (%s)\n", rc, mdb_strerror(rc));
    pthread_rwlock_unlock(&instance->lock);
    free(key);
    return rc;
  }

  pthread_rwlock_unlock(&instance->lock);
  free(key);

  return MDB_SUCCESS;
}

int
noit_filters_lmdb_populate_filterset_xml_from_lmdb(xmlNodePtr root, char *fs_name) {
  int rc;
  char buffer[65535];
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  MDB_val mdb_key, mdb_data;
  char *key = NULL;
  size_t key_size;
  noit_lmdb_instance_t *instance = noit_filters_get_lmdb_instance();

  mtevAssert(instance != NULL);

  key = noit_lmdb_make_filterset_key(fs_name, &key_size);
  mtevAssert(key);

  mdb_key.mv_data = key;
  mdb_key.mv_size = key_size;

  pthread_rwlock_rdlock(&instance->lock);

  mdb_txn_begin(instance->env, NULL, MDB_RDONLY, &txn);
  mdb_cursor_open(txn, instance->dbi, &cursor);
  rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_SET_KEY);
  if (rc != 0) {
    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    pthread_rwlock_unlock(&instance->lock);
    free(key);
    return -1;
  }

  mtev_dyn_buffer_t aligned;
  void *aligned_fb_data = get_aligned_fb(&aligned, mdb_data.mv_data, mdb_data.mv_size);
  int fb_ret = ns(Filterset_verify_as_root(aligned_fb_data, mdb_data.mv_size));
  if(fb_ret != 0) {
    mtevL(mtev_error, "Corrupt filterset flatbuffer: %s\n", flatcc_verify_error_string(fb_ret));
    mtev_dyn_buffer_destroy(&aligned);
    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    pthread_rwlock_unlock(&instance->lock);
    free(key);
    return -1;
  }
  ns(Filterset_table_t) filterset = ns(Filterset_as_root(aligned_fb_data));
  flatbuffers_string_t name = ns(Filterset_name(filterset));
  if (name != NULL) {
    xmlSetProp(root, (xmlChar *)"name", (xmlChar *)name);
  }

  int64_t seq = ns(Filterset_seq(filterset));
  if (seq > 0) {
    snprintf(buffer, sizeof(buffer), "%" PRId64 "", seq);
    xmlSetProp(root, (xmlChar *)"seq", (xmlChar *)buffer);
  }

  mtev_boolean cull = ns(Filterset_cull(filterset));
  snprintf(buffer, sizeof(buffer), "%s", (cull == mtev_true) ? "true" : "false");
  xmlSetProp(root, (xmlChar *)"cull", (xmlChar *)buffer);

  if (ns(Filterset_filterset_flush_period_is_present(filterset))) {
    ns(FiltersetRuleFlushPeriod_table_t) fpt = ns(Filterset_filterset_flush_period(filterset));
    if (ns(FiltersetRuleFlushPeriod_present(fpt))) {
      int32_t ffp = ns(FiltersetRuleFlushPeriod_value(fpt));
      snprintf(buffer, sizeof(buffer), "%" PRId32 "", ffp);
      xmlSetProp(root, (xmlChar *)"filter_flush_period", (xmlChar *)buffer);
    }
  }

  if (ns(Filterset_rules_is_present(filterset))) {
    ns(FiltersetRule_vec_t) rule_vec = ns(Filterset_rules(filterset));
    size_t num_rules = ns(FiltersetRule_vec_len(rule_vec));
    size_t i = 0;
    for (i=0; i < num_rules; i++) {
      xmlNodePtr rule = xmlNewNode(NULL, (xmlChar *)"rule");
      ns(FiltersetRule_table_t) fs_rule = ns(FiltersetRule_vec_at(rule_vec, i));
      flatbuffers_string_t id = ns(FiltersetRule_id(fs_rule));
      if (id != NULL) {
        xmlSetProp(rule, (xmlChar *)"id", (xmlChar *)id);
      }
      if (ns(FiltersetRule_filterset_flush_period_is_present(fs_rule))) {
        ns(FiltersetRuleFlushPeriod_table_t) fpt = ns(FiltersetRule_filterset_flush_period(fs_rule));
        if (ns(FiltersetRuleFlushPeriod_present(fpt))) {
          int64_t ffs = ns(FiltersetRuleFlushPeriod_value(fpt));
          snprintf(buffer, sizeof(buffer), "%" PRId64 "", ffs);
          xmlSetProp(rule, (xmlChar *)"filter_flush_period", (xmlChar *)buffer);
        }
      }
      flatbuffers_string_t type = ns(FiltersetRule_rule_type(fs_rule));
      if (type != NULL) {
        if (!strcmp(type, FILTERSET_SKIPTO_STRING_NO_COLON)) {
          flatbuffers_string_t skipto = ns(FiltersetRule_skipto_value(fs_rule));
          if (skipto) {
            snprintf(buffer, sizeof(buffer), "%s:%s", type, skipto);
            xmlSetProp(rule, (xmlChar *)"type", (xmlChar *)buffer);
          }
          else {
            xmlSetProp(rule, (xmlChar *)"type", (xmlChar *)type);
          }
        }
        else {
          xmlSetProp(rule, (xmlChar *)"type", (xmlChar *)type);
        }
      }
      if(ns(FiltersetRule_info_is_present(fs_rule))) {
        ns(FiltersetRuleInfo_vec_t) rule_info_vec = ns(FiltersetRule_info(fs_rule));
        size_t num_rule_info = ns(FiltersetRuleInfo_vec_len(rule_info_vec));
        size_t j = 0;
        for (j=0; j < num_rule_info; j++) {
          ns(FiltersetRuleInfo_table_t) fs_rule_info = ns(FiltersetRuleInfo_vec_at(rule_info_vec, j));
          flatbuffers_string_t type = ns(FiltersetRuleInfo_type(fs_rule_info));
          if (type == NULL) {
            mtevL(mtev_error, "noit_filters_lmdb_populate_filterset_xml_from_lmdb: FiltersetRuleInfo_type missing\n");
            continue;
          }
          if (!ns(FiltersetRuleInfo_data_is_present(fs_rule_info))) {
            mtevL(mtev_error, "noit_filters_lmdb_populate_filterset_xml_from_lmdb: FiltersetRuleInfo_data missing\n");
            continue;
          }
          switch(ns(FiltersetRuleInfo_data_type(fs_rule_info))) {
            case ns(FiltersetRuleValueUnion_FiltersetRuleHashValue):
            {
              ns(FiltersetRuleHashValue_table_t) v = ns(FiltersetRuleInfo_data(fs_rule_info));
              int64_t auto_add = ns(FiltersetRuleHashValue_auto_add_max(v));
              if (auto_add > 0) {
                char key_buffer[65535];
                snprintf(key_buffer, sizeof(key_buffer), "%s_auto_add", type);
                snprintf(buffer, sizeof(buffer), "%" PRId64 "", auto_add);
                xmlSetProp(rule, (xmlChar *)key_buffer, (xmlChar *)buffer);
              }
              if (ns(FiltersetRuleHashValue_values_is_present(v))) {
                flatbuffers_string_vec_t values_vec = ns(FiltersetRuleHashValue_values(v));
                size_t hte_cnt = flatbuffers_string_vec_len(values_vec);
                size_t ii = 0;
                for (ii = 0; ii < hte_cnt; ii++) {
                  flatbuffers_string_t value = flatbuffers_string_vec_at(values_vec, ii);
                  xmlNodePtr node = xmlNewNode(NULL, (xmlChar *)type);
                  xmlNodeAddContent(node, (xmlChar *)value);
                  xmlAddChild(rule, node);
                }
              }
              break;
            }
            case ns(FiltersetRuleValueUnion_FiltersetRuleAttributeValue):
            {
              ns(FiltersetRuleAttributeValue_table_t) v = ns(FiltersetRuleInfo_data(fs_rule_info));
              flatbuffers_string_t regex = ns(FiltersetRuleAttributeValue_regex(v));
              xmlSetProp(rule, (xmlChar *)type, (xmlChar *)regex);
              break;
            }
            default:
            {
              /* Shouldn't happen */
              mtevL(mtev_error, "noit_filters_lmdb_populate_filterset_xml_from_lmdb: Uknown FiltersetRuleInfo_data type\n");
              break;
            }
          }
        }
      }
      if(ns(FiltersetRule_tags_is_present(fs_rule))) {
        ns(FiltersetRuleTagInfo_vec_t) rule_tag_info_vec = ns(FiltersetRule_tags(fs_rule));
        size_t num_rule_tag_info = ns(FiltersetRuleTagInfo_vec_len(rule_tag_info_vec));
        size_t j = 0;
        for (j=0; j < num_rule_tag_info; j++) {
          ns(FiltersetRuleTagInfo_table_t) fs_rule_tag_info = ns(FiltersetRuleTagInfo_vec_at(rule_tag_info_vec, j));
          if (ns(FiltersetRuleTagInfo_type_is_present(fs_rule_tag_info)) &&
              ns(FiltersetRuleTagInfo_value_is_present(fs_rule_tag_info))) {
            flatbuffers_string_t type = ns(FiltersetRuleTagInfo_type(fs_rule_tag_info));
            flatbuffers_string_t value = ns(FiltersetRuleTagInfo_value(fs_rule_tag_info));
            xmlSetProp(rule, (xmlChar *)type, (xmlChar *)value);
          }
        }
      }
      xmlAddChild(root, rule);
    }
  }


  mtev_dyn_buffer_destroy(&aligned);
  mdb_cursor_close(cursor);
  mdb_txn_abort(txn);

  pthread_rwlock_unlock(&instance->lock);
  free(key);
  return 0;
}

mtev_boolean
noit_filters_lmdb_already_in_db(char *name) {
  int rc;
  mtev_boolean toRet = mtev_false;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  MDB_val mdb_key;
  char *key = NULL;
  size_t key_size;
  noit_lmdb_instance_t *instance = noit_filters_get_lmdb_instance();

  mtevAssert(instance != NULL);

  key = noit_lmdb_make_filterset_key(name, &key_size);
  mtevAssert(key);

  mdb_key.mv_data = key;
  mdb_key.mv_size = key_size;

  pthread_rwlock_rdlock(&instance->lock);

  mdb_txn_begin(instance->env, NULL, MDB_RDONLY, &txn);
  mdb_cursor_open(txn, instance->dbi, &cursor);
  rc = mdb_cursor_get(cursor, &mdb_key, NULL, MDB_SET_KEY);
  if (rc == 0) {
    toRet = mtev_true;
  }

  mdb_cursor_close(cursor);
  mdb_txn_abort(txn);

  pthread_rwlock_unlock(&instance->lock);
  free(key);
  return toRet;
}

int64_t
noit_filters_lmdb_get_seq(char *name) {
  int rc;
  int toRet = 0;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  MDB_val mdb_key, mdb_data;
  char *key = NULL;
  size_t key_size;
  noit_lmdb_instance_t *instance = noit_filters_get_lmdb_instance();

  mtevAssert(instance != NULL);

  key = noit_lmdb_make_filterset_key(name, &key_size);
  mtevAssert(key);

  mdb_key.mv_data = key;
  mdb_key.mv_size = key_size;

  pthread_rwlock_rdlock(&instance->lock);

  mdb_txn_begin(instance->env, NULL, MDB_RDONLY, &txn);
  mdb_cursor_open(txn, instance->dbi, &cursor);
  rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_SET_KEY);
  if (rc == 0) {
    mtev_dyn_buffer_t aligned;
    void *aligned_fb_data = get_aligned_fb(&aligned, mdb_data.mv_data, mdb_data.mv_size);
    int fb_ret = ns(Filterset_verify_as_root(aligned_fb_data, mdb_data.mv_size));
    if(fb_ret != 0) {
      mtevL(mtev_error, "Corrupt filterset flatbuffer: %s\n", flatcc_verify_error_string(fb_ret));
    }
    else {
      ns(Filterset_table_t) filterset = ns(Filterset_as_root(aligned_fb_data));
      toRet = ns(Filterset_seq(filterset));
      if (toRet < 0) {
        toRet = 0;
      }
    }
    mtev_dyn_buffer_destroy(&aligned);
  }

  mdb_cursor_close(cursor);
  mdb_txn_abort(txn);

  pthread_rwlock_unlock(&instance->lock);
  free(key);
  return toRet;
}

void
noit_filters_lmdb_filters_from_lmdb() {
  int rc;
  uint32_t cnt = 0;
  uint64_t start, end, diff;
  double per_record;

  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  MDB_val mdb_key, mdb_data;
  noit_lmdb_instance_t *instance = noit_filters_get_lmdb_instance();
  
  mtevAssert(instance != NULL);

  mdb_key.mv_data = NULL;
  mdb_key.mv_size = 0;
  pthread_rwlock_rdlock(&instance->lock);

  mtevL(mtev_error, "begin loading filtersets from db\n");
  start = mtev_now_us();

  rc = mdb_txn_begin(instance->env, NULL, 0, &txn);
  if (rc != 0) {
    pthread_rwlock_unlock(&instance->lock);
    mtevL(mtev_error, "failed to create transaction for processing all filtersets: %d (%s)\n", rc, mdb_strerror(rc));
    return;
  }
  mdb_cursor_open(txn, instance->dbi, &cursor);
  rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_FIRST);

  while (rc == 0) {
    noit_filters_lmdb_load_one_from_db_locked(mdb_data.mv_data, mdb_data.mv_size);
    rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_NEXT);
    cnt++;
  }
  mdb_cursor_close(cursor);
  mdb_txn_abort(txn);
  pthread_rwlock_unlock(&instance->lock);
  end = mtev_now_us();
  diff = (end - start) / 1000;
  if (cnt) {
    per_record = ((double)(end-start) / (double)cnt) / 1000.0;
  }
  else {
    per_record = 0;
  }

  mtevL(mtev_error, "finished loading %" PRIu32 " filtersets from db - took %" PRIu64 " ms (average %0.4f ms per filterset)\n",
    cnt, diff, per_record);
}

void
noit_filters_lmdb_migrate_xml_filtersets_to_lmdb() {
  int cnt, i;
  const char *xpath = "/noit/filtersets//filterset";
  mtev_conf_section_t *sec = mtev_conf_get_sections_write(MTEV_CONF_ROOT, xpath, &cnt);
  if (cnt) {
    mtevL(mtev_error, "converting %d xml filtersets to lmdb\n", cnt);
  }
  for(i=0; i<cnt; i++) {
    char *error = NULL;
    int rv = noit_filters_lmdb_convert_one_xml_filterset_to_lmdb(sec[i], &error);
    if (rv || error) {
      mtevL(mtev_error, "noit_filters_lmdb_migrate_xml_filtersets_to_lmdb: error converting filterset from xml to lmdb: %s\n", error ? error : "(unknown error)");
      free(error);
    }
    else {
      CONF_REMOVE(sec[i]);
      xmlUnlinkNode(mtev_conf_section_to_xmlnodeptr(sec[i]));
      xmlFreeNode(mtev_conf_section_to_xmlnodeptr(sec[i]));
    }
  }
  mtev_conf_release_sections_write(sec, cnt);
  mtev_conf_mark_changed();
  if(mtev_conf_write_file(NULL) != 0) {
    mtevL(mtev_error, "local config write failed\n");
  }
}

int
noit_filters_lmdb_process_repl(xmlDocPtr doc) {
  int i = 0;
  xmlNodePtr root, child, next = NULL;

  if(!noit_filter_initialized()) {
    mtevL(mtev_debug, "filterset replication pending initialization\n");
    return -1;
  }

  root = xmlDocGetRootElement(doc);
  for(child = xmlFirstElementChild(root); child; child = next) {
    next = xmlNextElementSibling(child);

    char filterset_name[MAX_METRIC_TAGGED_NAME];
    mtevAssert(mtev_conf_get_stringbuf(mtev_conf_section_from_xmlnodeptr(child), "@name",
                                       filterset_name, sizeof(filterset_name)));
    if(noit_filter_compile_add(mtev_conf_section_from_xmlnodeptr(child))) {
      char *error = NULL;
      int rv = noit_filters_lmdb_convert_one_xml_filterset_to_lmdb(mtev_conf_section_from_xmlnodeptr(child), &error);
      if (rv || error) {
        mtevL(mtev_error, "noit_filters_lmdb_process_repl: error converting filterset from xml to lmdb: %s\n", error ? error : "(unknown error)");
        free(error);
      }
    }
    i++;
  }
  return i;
}

int
noit_filters_lmdb_rest_show_filter(mtev_http_rest_closure_t *restc,
                                   int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  xmlDocPtr doc = NULL;
  xmlNodePtr root;
  noit_lmdb_instance_t *instance = noit_filters_get_lmdb_instance();

  mtevAssert(instance != NULL);

  if(npats != 2) {
    goto not_found;
  }

  if (!noit_filters_lmdb_already_in_db(pats[1])) {
    goto not_found;
  }

  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlNewDocNode(doc, NULL, (xmlChar *)"filterset", NULL);
  xmlDocSetRootElement(doc, root);

  if (noit_filters_lmdb_populate_filterset_xml_from_lmdb(root, pats[1])) {
    goto not_found;
  }

  mtev_http_response_ok(ctx, "text/xml");
  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);
  goto cleanup;

 not_found:
  mtev_http_response_not_found(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if(doc) {
    xmlFreeDoc(doc);
  }
  return 0;
}

int
noit_filters_lmdb_rest_delete_filter(mtev_http_rest_closure_t *restc,
                                     int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  int rc = 0;
  MDB_txn *txn = NULL;
  MDB_val mdb_key;
  char *key = NULL;
  size_t key_size;
  noit_lmdb_instance_t *instance = noit_filters_get_lmdb_instance();

  mtevAssert(instance != NULL);

  if(npats != 2) {
    goto not_found;
  }

  rc = noit_filters_lmdb_remove_from_db(pats[1]);
  if (rc == MDB_NOTFOUND) {
    goto not_found;
  }
  else if (rc != MDB_SUCCESS) {
    goto error;
  }

  mtev_http_response_ok(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 error:
  mtev_http_response_standard(ctx, 500, mdb_strerror(rc), "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 not_found:
  mtev_http_response_not_found(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  return 0;
}

int
noit_filters_lmdb_cull_unused() {
  int rc;
  mtev_hash_table active;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  MDB_val mdb_key, mdb_data;
  int i, n_uses = 0, n_declares = 0, removed = 0;
  noit_lmdb_instance_t *instance = noit_filters_get_lmdb_instance();
  
  mtevAssert(instance != NULL);
  mtev_hash_init(&active);

  mdb_key.mv_data = NULL;
  mdb_key.mv_size = 0;
  pthread_rwlock_rdlock(&instance->lock);

  rc = mdb_txn_begin(instance->env, NULL, 0, &txn);
  if (rc != 0) {
    pthread_rwlock_unlock(&instance->lock);
    mtevL(mtev_error, "failed to create transaction for cull: %d (%s)\n", rc, mdb_strerror(rc));
    return -1;
  }
  mdb_cursor_open(txn, instance->dbi, &cursor);
  rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_FIRST);

  while (rc == 0) {
    mtev_dyn_buffer_t aligned;
    void *aligned_fb_data = get_aligned_fb(&aligned, mdb_data.mv_data, mdb_data.mv_size);
    int fb_ret = ns(Filterset_verify_as_root(aligned_fb_data, mdb_data.mv_size));
    if(fb_ret != 0) {
      mtevL(mtev_error, "Corrupt filterset flatbuffer: %s\n", flatcc_verify_error_string(fb_ret));
      mtev_dyn_buffer_destroy(&aligned);
      rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_NEXT);
      continue;
    }
    ns(Filterset_table_t) filterset = ns(Filterset_as_root(aligned_fb_data));

    mtev_boolean cull = ns(Filterset_cull(filterset));
    if (cull == mtev_false) {
      mtev_dyn_buffer_destroy(&aligned);
      rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_NEXT);
      continue;
    }
    flatbuffers_string_t name = ns(Filterset_name(filterset));
    mtev_hash_store(&active, strdup(name), strlen(name), NULL);
    mtev_dyn_buffer_destroy(&aligned);
    rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_NEXT);
  }
  mdb_cursor_close(cursor);
  mdb_txn_abort(txn);

  n_uses = noit_poller_do(noit_filters_filterset_accum, &active);

  if(n_uses > 0 && mtev_hash_size(&active) > 0) {
    mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
    const char *filter_name = NULL;
    int filter_name_len = 0;
    void *unused = NULL;
    while(mtev_hash_next(&active, &iter, &filter_name, &filter_name_len, &unused)) {
      char *name = (char *)calloc(1, filter_name_len + 1);
      memcpy(name, filter_name, filter_name_len);
      if(noit_filter_remove_from_name(name)) {
        noit_filters_lmdb_remove_from_db(name);
      }
      free(name);
    }
  }

  mtev_hash_destroy(&active, free, NULL);

  pthread_rwlock_unlock(&instance->lock);
  return removed;
}

int
noit_filters_lmdb_rest_set_filter(mtev_http_rest_closure_t *restc,
                                  int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  xmlDocPtr doc = NULL, indoc = NULL;
  xmlNodePtr parent, root, newfilter;
  char xpath[1024];
  int error_code = 500, complete = 0, mask = 0;
  mtev_boolean exists;
  int64_t seq = 0;
  int64_t old_seq = 0;
  const char *error = "internal error";
  char *error_str = NULL;
  mtev_boolean allocated_error = mtev_false;
  noit_lmdb_instance_t *instance = noit_filters_get_lmdb_instance();
  int rv = 0;

  mtevAssert(instance != NULL);

  if(npats != 2) {
    error = "invalid URI";
    error_code = 404;
    goto error;
  }

  indoc = rest_get_xml_upload(restc, &mask, &complete);
  if(!complete) {
    return mask;
  }
  if(indoc == NULL) {
    error = "xml parse error";
    error_code = 406;
    goto error;
  }
  
  exists = noit_filters_lmdb_already_in_db(pats[1]);
  if (exists) {
    old_seq = noit_filters_lmdb_get_seq(pats[1]);
  }

  if((newfilter = noit_filter_validate_filter(indoc, pats[1], &seq, &error)) == NULL) {
    goto error;
  }

  if(exists && (old_seq >= seq && seq != 0)) {
    error = "sequencing error";
    error_code = 409;
    goto error;
  }
  error_str = NULL;
  rv = noit_filters_lmdb_convert_one_xml_filterset_to_lmdb(mtev_conf_section_from_xmlnodeptr(newfilter), &error_str);
  if (rv || error_str) {
    mtevL(mtev_error, "noit_filters_lmdb_process_repl: error converting filterset from xml to lmdb: %s\n", error_str ? error_str : "(unknown error)");
    error_code = 500;
    error = error_str;
    allocated_error = mtev_true;
    goto error;
  }
  noit_filter_compile_add(mtev_conf_section_from_xmlnodeptr(newfilter));

  if(restc->call_closure_free) {
    restc->call_closure_free(restc->call_closure);
  }
  restc->call_closure_free = NULL;
  restc->call_closure = NULL;
  restc->fastpath = noit_filters_lmdb_rest_show_filter;
  return restc->fastpath(restc, restc->nparams, restc->params);

 error:
  mtev_http_response_standard(ctx, error_code, "ERROR", "text/html");
  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlNewDocNode(doc, NULL, (xmlChar *)"error", NULL);
  xmlDocSetRootElement(doc, root);
  xmlNodeAddContent(root, (xmlChar *)error);
  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);
  if (allocated_error) {
    free((char *)error);
  }
  goto cleanup;

 cleanup:
  if(doc) {
    xmlFreeDoc(doc);
  }
  return 0;
}

void
noit_filters_lmdb_init() {
  const char *error;
  int erroffset;
  const char *xpath = "/noit/filtersets";
  mtev_conf_section_t sec = mtev_conf_get_section_read(MTEV_CONF_ROOT, xpath);
  if(!mtev_conf_section_is_empty(sec)) {
    if (mtev_conf_get_int32(sec, "ancestor-or-self::node()/@filter_flush_period", &global_default_filter_flush_period_ms)) {
      if (global_default_filter_flush_period_ms < 0) {
        global_default_filter_flush_period_ms = DEFAULT_FILTER_FLUSH_PERIOD_MS;
      }
    }
  }
  mtev_conf_release_section_read(sec);
  fallback_no_match = pcre_compile("^(?=a)b", 0, &error, &erroffset, NULL);
  if(!fallback_no_match) {
    mtevL(mtev_error, "Filter initialization failed (nomatch filter)\n");
    exit(-1);
  }
}
