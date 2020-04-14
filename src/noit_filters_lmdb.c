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
#include <mtev_conf.h>
#include <mtev_watchdog.h>

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
} noit_filter_lmdb_filterset_rule_t;

static void
noit_filters_lmdb_free_filterset_rule(noit_filter_lmdb_filterset_rule_t *rule) {
  if (rule) {
    free(rule->skipto);
    free(rule->ruleid);
    free(rule);
  }
}

static int
noit_filters_lmdb_write_flatbuffer_to_db(char *filterset_name,
                                         int64_t *sequence,
                                         mtev_boolean *cull,
                                         noit_filter_lmdb_filterset_rule_t **rules,
                                         int64_t rule_cnt) {
  int i = 0;
  if (!filterset_name) {
    return -1;
  }
  for (i = 0; i < rule_cnt; i++) {
    noit_filter_lmdb_filterset_rule_t *rule = rules[i];
    if (!rule) {
      continue;
    }
  }
  return 0;
}

static noit_filter_lmdb_filterset_rule_t *
noit_filters_lmdb_one_xml_rule_to_memory(mtev_conf_section_t rule_conf) {
#define GET_AUTO_ADD(rtype) do { \
  if (mtev_conf_get_int32(rule_conf, "@" #rtype "_auto_add", &rule->rtype##_auto_add)) { \
    if (rule->rtype##_auto_add < 0) { \
      rule->rtype##_auto_add = 0; \
    } \
    else { \
      rule->rtype##_auto_add_present = mtev_true; \
    } \
  } \
} while(0)

  char buffer[MAX_METRIC_TAGGED_NAME];
  noit_filter_lmdb_filterset_rule_t *rule =
    (noit_filter_lmdb_filterset_rule_t *)calloc(1, sizeof(noit_filter_lmdb_filterset_rule_t));

  if(!mtev_conf_get_stringbuf(rule_conf, "@type", buffer, sizeof(buffer)) ||
    (strcmp(buffer, "accept") && strcmp(buffer, "allow") && strcmp(buffer, "deny") &&
    strncmp(buffer, "skipto:", strlen("skipto:")))) {
    mtevL(mtev_error, "rule must have type 'accept' or 'allow' or 'deny' or 'skipto:'\n");
    free(rule);
    return NULL;
  }
  if(!strncasecmp(buffer, "skipto:", strlen("skipto:"))) {
    rule->type = NOIT_FILTER_SKIPTO;
    rule->skipto = strdup(buffer+strlen("skipto:"));
  }
  else {
    rule->type = (!strcmp(buffer, "accept") || !strcmp(buffer, "allow")) ?
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

  GET_AUTO_ADD(target);
  GET_AUTO_ADD(module);
  GET_AUTO_ADD(name);
  GET_AUTO_ADD(metric);

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
