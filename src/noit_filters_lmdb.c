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
  char *stream_tags_str;
  char *measurement_tags_str;
} noit_filter_lmdb_filterset_rule_t;

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
  } \
} while (0);

#define FILTERSET_RULE_INFO_ADD_ATTRIBUTE(rtype) do { \
  if ((!rule->rtype##_hash_rules) && ((!rule->rtype##_auto_add_present) || (rule->rtype##_auto_add == 0))) { \
  } \
} while (0);

  ns(FiltersetRule_info_start(B));
  if (rule->target_hash_rules || rule->target_attribute) {
    ns(FiltersetRule_info_push_start(B));
    ns(FiltersetRuleInfo_type_create_str(B, FILTERSET_TARGET_STRING));
    FILTERSET_RULE_INFO_ADD_HASH(target);
    FILTERSET_RULE_INFO_ADD_ATTRIBUTE(target);
    ns(FiltersetRule_info_push_end(B));
  }
  if (rule->module_hash_rules || rule->module_attribute) {
    ns(FiltersetRule_info_push_start(B));
    ns(FiltersetRuleInfo_type_create_str(B, FILTERSET_MODULE_STRING));
    FILTERSET_RULE_INFO_ADD_HASH(module);
    FILTERSET_RULE_INFO_ADD_ATTRIBUTE(module);
    ns(FiltersetRule_info_push_end(B));
  }
  if (rule->name_hash_rules || rule->name_attribute) {
    ns(FiltersetRule_info_push_start(B));
    ns(FiltersetRuleInfo_type_create_str(B, FILTERSET_NAME_STRING));
    FILTERSET_RULE_INFO_ADD_HASH(name);
    FILTERSET_RULE_INFO_ADD_ATTRIBUTE(name);
    ns(FiltersetRule_info_push_end(B));
  }
  if (rule->metric_hash_rules || rule->metric_attribute) {
    ns(FiltersetRule_info_push_start(B));
    ns(FiltersetRuleInfo_type_create_str(B, FILTERSET_METRIC_STRING));
    FILTERSET_RULE_INFO_ADD_HASH(metric);
    FILTERSET_RULE_INFO_ADD_ATTRIBUTE(metric);
    ns(FiltersetRule_info_push_end(B));
  }
  ns(FiltersetRule_info_end(B));
}

static int
noit_filters_lmdb_write_flatbuffer_to_db(char *filterset_name,
                                         int64_t *sequence,
                                         mtev_boolean *cull,
                                         noit_filter_lmdb_filterset_rule_t **rules,
                                         int64_t rule_cnt) {
#define AUTO_ADD_TO_FB(rtype) do { \
  if (rule->rtype##_auto_add_present == mtev_true) { \
    ns(FiltersetRule_auto_add_push_start(B)); \
    ns(FiltersetAutoAddValue_type_create_str(B, #rtype)); \
    ns(FiltersetAutoAddValue_max_add(B, rule->rtype##_auto_add)); \
    ns(FiltersetRule_auto_add_push_end(B)); \
  } \
} while (0);

  int i = 0, ret = 0;
  size_t buffer_size;
  flatcc_builder_t builder;
  flatcc_builder_t *B = &builder;
  if (!filterset_name) {
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
    if (rule->filter_flush_period_present) {
      ns(FiltersetRule_filterset_flush_period_add(B, rule->filter_flush_period));
    }
    switch(rule->type) {
      case NOIT_FILTER_ACCEPT:
        ns(FiltersetRule_rule_type_create_str(B, ACCEPT_STRING));
        break;
      case NOIT_FILTER_DENY:
        ns(FiltersetRule_rule_type_create_str(B, DENY_STRING));
        break;
      case NOIT_FILTER_SKIPTO:
        ns(FiltersetRule_rule_type_create_str(B, SKIPTO_STRING_NO_COLON));
        if (rule->skipto) {
          ns(FiltersetRule_skipto_value_create_str(B, rule->skipto));
        }
        break;
      default:
        mtevFatal(mtev_error, "noit_filters_lmdb_write_flatbuffer_to_db: undefined type in db (%d) for %s\n", (int)rule->type, filterset_name);
        break;
    }

    noit_filters_lmdb_add_filterset_rule_info(B, rule);

    ns(FiltersetRule_auto_add_start(B));
    AUTO_ADD_TO_FB(target);
    AUTO_ADD_TO_FB(module);
    AUTO_ADD_TO_FB(name);
    AUTO_ADD_TO_FB(metric);
    ns(FiltersetRule_auto_add_end(B));

    ns(Filterset_rules_push_end(B));
  }
  ns(Filterset_rules_end(B));
  ns(Filterset_end_as_root(B));
  void *buffer = flatcc_builder_finalize_buffer(B, &buffer_size);
  flatcc_builder_clear(B);

  if ((ret = ns(Filterset_verify_as_root(buffer, buffer_size)))) {
    mtevL(mtev_error, "noit_filters_lmdb_write_flatbuffer_to_db: could not verify Filterset flatbuffer (name %s, len %zd)\n", filterset_name, buffer_size);
    return ret;
  }
  else {
    mtevL(mtev_debug, "noit_filters_lmdb_write_flatbuffer_to_db: successfully verified Filterset flatbuffer (name %s, len %zd)\n", filterset_name, buffer_size);
  }
  ret = noit_filters_lmdb_write_finalized_fb_to_lmdb(filterset_name, buffer, buffer_size);
  free(buffer);
  if (ret) {
    mtevL(mtev_error, "failed to write filterset %s to the database: %d (%s)\n", filterset_name, ret, mdb_strerror(ret));
    return ret;
  }
  else {
    mtevL(mtev_debug, "successfully wrote filterset %s to the database\n", filterset_name);
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
    rule->rtype##_str = expr; \
  } \
} while (0);

  char buffer[MAX_METRIC_TAGGED_NAME];
  noit_filter_lmdb_filterset_rule_t *rule =
    (noit_filter_lmdb_filterset_rule_t *)calloc(1, sizeof(noit_filter_lmdb_filterset_rule_t));

  if(!mtev_conf_get_stringbuf(rule_conf, "@type", buffer, sizeof(buffer)) ||
    (strcmp(buffer, ACCEPT_STRING) && strcmp(buffer, ALLOW_STRING) && strcmp(buffer, DENY_STRING) &&
    strncmp(buffer, SKIPTO_STRING, strlen(SKIPTO_STRING)))) {
    mtevL(mtev_error, "rule must have type 'accept' or 'allow' or 'deny' or 'skipto:'\n");
    free(rule);
    return NULL;
  }
  if(!strncasecmp(buffer, SKIPTO_STRING, strlen(SKIPTO_STRING))) {
    rule->type = NOIT_FILTER_SKIPTO;
    rule->skipto = strdup(buffer+strlen(SKIPTO_STRING));
  }
  else {
    rule->type = (!strcmp(buffer, ACCEPT_STRING) || !strcmp(buffer, ALLOW_STRING)) ?
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
noit_filters_lmdb_convert_one_xml_filterset_to_lmdb(mtev_conf_section_t fs_section) {
  int i = 0, rv = -1;
  char *filterset_name = NULL;
  int64_t sequence = 0;
  mtev_boolean cull = mtev_false;
  mtev_boolean sequence_present = mtev_false, cull_present = mtev_false;
  noit_filter_lmdb_filterset_rule_t **rules = NULL;
  mtev_conf_section_t *rules_conf;
  int rule_cnt = 0;
  noit_lmdb_instance_t *instance = noit_filters_get_lmdb_instance();
  
  mtevAssert(instance != NULL);

  /* We want to heartbeat here... otherwise, if a lot of checks are 
   * configured or if we're running on a slower system, we could 
   * end up getting watchdog killed before we get a chance to run 
   * any checks */
  mtev_watchdog_child_heartbeat();

  /* TODO: Finish this */
  if (!mtev_conf_get_string(fs_section, "@name", &filterset_name)) {
    /* Filterset must have a name */
    return -1;
  }
  if (mtev_conf_get_int64(fs_section, "@seq", &sequence)) {
    sequence_present = mtev_true;
  }
  if (mtev_conf_get_boolean(fs_section, "@cull", &cull)) {
    cull_present = mtev_true;
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
               rules,
               (int64_t)rule_cnt);

  free(filterset_name);
  for (i = 0; i < rule_cnt; i++) {
    noit_filters_lmdb_free_filterset_rule(rules[i]);
  }
  free(rules);

  return rv;
}

static int
noit_filters_lmdb_load_one_from_db(void *fb_data, size_t fb_size) {
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
    return -1;
  }
  set = (filterset_t *)calloc(1, sizeof(filterset_t));
  ns(Filterset_table_t) filterset = ns(Filterset_as_root(aligned_fb_data));
  set->name = strdup(ns(Filterset_name(filterset)));
  if (ns(Filterset_seq_is_present(filterset))) {
    seq = ns(Filterset_seq(filterset));
    mtevAssert (seq >= 0);
  }
  set->seq = seq;
  if (ns(Filterset_cull_is_present(filterset))) {
    /* TODO: Read cull */
  }
  ns(FiltersetRule_vec_t) rule_vec = ns(Filterset_rules(filterset));
  num_rules = ns(FiltersetRule_vec_len(rule_vec));
  for (i=num_rules-1; i >= 0; i--) {
    mtev_boolean target_auto_add_set = mtev_false,
                 module_auto_add_set = mtev_false,
                 name_auto_add_set = mtev_false,
                 metric_auto_add_set = mtev_false;
    filterrule_t *rule = NULL;
    rule = (filterrule_t *)calloc(1, sizeof(filterrule_t));
    ns(FiltersetRule_table_t) fs_rule = ns(FiltersetRule_vec_at(rule_vec, i));
    if (ns(FiltersetRule_id_is_present(fs_rule))) {
      rule->ruleid = strdup(ns(FiltersetRule_id(fs_rule)));
    }
    int32_t ffp = global_default_filter_flush_period_ms;
    if (ns(FiltersetRule_filterset_flush_period_is_present(fs_rule))) {
      ffp = ns(FiltersetRule_filterset_flush_period(fs_rule));
    }
    if(ffp < 0) {
      ffp = 0;
    }
    rule->flush_interval.tv_sec = ffp/1000;
    rule->flush_interval.tv_usec = ffp%1000;

    ns(FiltersetAutoAddValue_vec_t) auto_add_vec = ns(FiltersetRule_auto_add(fs_rule));
    size_t num_auto_add = ns(FiltersetAutoAddValue_vec_len(auto_add_vec));
    for (j = 0; j < num_auto_add; j++) {
      ns(FiltersetAutoAddValue_table_t) auto_add_table = ns(FiltersetAutoAddValue_vec_at(auto_add_vec, j));
      flatbuffers_string_t type = ns(FiltersetAutoAddValue_type(auto_add_table));
      int32_t auto_add_max_value = ns(FiltersetAutoAddValue_max(auto_add_table));
      if (auto_add_max_value < 0) {
        auto_add_max_value = 0;
      }
      if (!strcmp(type, "target")) {
        if (target_auto_add_set) {
          mtevL(mtev_error, "noit_filters_lmdb_load_one_from_db: duplicate target auto add found in rule for %s, skipping\n",
            set->name);
        }
        else {
          rule->target_auto_hash_max = auto_add_max_value;
          target_auto_add_set = mtev_true;
        }
      }
      else if (!strcmp(type, "module")) {
        if (module_auto_add_set) {
          mtevL(mtev_error, "noit_filters_lmdb_load_one_from_db: duplicate module auto add found in rule for %s, skipping\n",
            set->name);
        }
        else {
          rule->module_auto_hash_max = auto_add_max_value;
          module_auto_add_set = mtev_true;
        }
      }
      else if (!strcmp(type, "name")) {
        if (name_auto_add_set) {
          mtevL(mtev_error, "noit_filters_lmdb_load_one_from_db: duplicate name auto add found in rule for %s, skipping\n",
            set->name);
        }
        else {
          rule->name_auto_hash_max = auto_add_max_value;
          name_auto_add_set = mtev_true;
        }
      }
      else if (!strcmp(type, "metric")) {
        if (metric_auto_add_set) {
          mtevL(mtev_error, "noit_filters_lmdb_load_one_from_db: duplicate metric auto add found in rule for %s, skipping\n",
            set->name);
        }
        else {
          rule->metric_auto_hash_max = auto_add_max_value;
          metric_auto_add_set = mtev_true;
        }
      }
      else {
        mtevL(mtev_error, "noit_filters_lmdb_load_one_from_db: unknown type for auto_add (%s) found in rule, skipping\n", type);
      }
    }
    rule->next = set->rules;
    set->rules = rule;
  }
  mtev_dyn_buffer_destroy(&aligned);
  /* TODO: Handle the set - just free it for now for testing */
  free(set->name);
  free(set);
  return 0;
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
    noit_filters_lmdb_load_one_from_db(mdb_data.mv_data, mdb_data.mv_size);
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
    noit_filters_lmdb_convert_one_xml_filterset_to_lmdb(sec[i]);
    /* TODO: Remove filtersets once converted
    CONF_REMOVE(sec[i]);
    xmlUnlinkNode(mtev_conf_section_to_xmlnodeptr(sec[i]));
    xmlFreeNode(mtev_conf_section_to_xmlnodeptr(sec[i]));
    */
  }
  mtev_conf_release_sections_write(sec, cnt);
}

void
noit_filters_lmdb_init() {
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
}
