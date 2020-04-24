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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#undef _GNU_SOURCE

#include <mtev_defines.h>

#include <mtev_log.h>
#include <mtev_hash.h>
#include <mtev_watchdog.h>
#include <mtev_capabilities_listener.h>
#include <mtev_conf.h>

#include "noit_mtev_bridge.h"
#include "noit_check.h"
#include "noit_conf_checks.h"
#include "noit_filters.h"
#include "noit_filters_lmdb.h"
#include "noit_clustering.h"
#include "noit_metric.h"

#include <pcre.h>
#include <libxml/tree.h>

static mtev_log_stream_t nf_error;
static mtev_log_stream_t nf_debug;

static mtev_hash_table *filtersets = NULL;
static pthread_mutex_t filterset_lock;
static pcre *fallback_no_match = NULL;
#define LOCKFS() pthread_mutex_lock(&filterset_lock)
#define UNLOCKFS() pthread_mutex_unlock(&filterset_lock)
static char* filtersets_replication_path = NULL;
static noit_lmdb_instance_t *lmdb_instance = NULL;
static bool initialized = false;

#define FRF(r,a) do { \
  free(r->a##_re); \
  if(r->a) pcre_free(r->a); \
  if(r->a##_e) pcre_free(r->a##_e); \
  if(r->a##_ht) { \
    mtev_hash_destroy(r->a##_ht, free, NULL); \
    free(r->a##_ht); \
  } \
} while(0)

static void
filterrule_free(void *vp) {
  filterrule_t *r = vp;
  FRF(r,target);
  FRF(r,module);
  FRF(r,name);
  FRF(r,metric);
  free(r->stream_tags);
  noit_metric_tag_search_free(r->stsearch);
  free(r->measurement_tags);
  noit_metric_tag_search_free(r->mtsearch);
  free(r->ruleid);
  free(r->skipto);
  free(r);
}
noit_lmdb_instance_t *noit_filters_get_lmdb_instance() {
  return lmdb_instance;
}
static void
filterset_free(void *vp) {
  filterset_t *fs = vp;
  filterrule_t *r;
  bool zero;
  ck_pr_dec_32_zero(&fs->ref_cnt, &zero);
  if(!zero) return;
  mtevL(nf_debug, "Freeing filterset [%d]: %s\n", fs->ref_cnt, fs->name);
  while(fs->rules) {
    r = fs->rules->next;
    filterrule_free(fs->rules);
    fs->rules = r;
  }
  if(fs->name) free(fs->name);
  free(fs);
}
xmlNodePtr
noit_filter_validate_filter(xmlDocPtr doc, char *name, int64_t *seq, const char **err) {
  xmlNodePtr root, r, previous_child;
  char *old_name;
  *err = "data validation error";

  if(seq) *seq = 0;
  root = xmlDocGetRootElement(doc);
  if(!root) return NULL;
  if(strcmp((char *)root->name, "filterset")) {
    *err = "bad root node";
    return NULL;
  }

  old_name = (char *)xmlGetProp(root, (xmlChar *)"name");
  if(old_name == NULL) {
    xmlSetProp(root, (xmlChar *)"name", (xmlChar *)name);
  } else if(name == NULL || strcmp(old_name, name)) {
    xmlFree(old_name);
    *err = "name mismatch";
    return NULL;
  }
  if(old_name) xmlFree(old_name);

  if(!root->children) {
    *err = "no rules";
    return NULL;
  }

  xmlChar *seqstr = xmlGetProp(root, (xmlChar *)"seq");
  if(seqstr && seq) {
    *seq = strtoll((const char *)seqstr, NULL, 10);
  }
  if(seqstr) xmlFree(seqstr);

  previous_child = root;
  int rulecnt = 0;
  for(r = root->children; r; r = r->next) {
#define CHECK_N_SET(a) if(!strcmp((char *)r->name, #a))
    char *type;
    CHECK_N_SET(rule) {
      type = (char *)xmlGetProp(r, (xmlChar *)"type");
      if(!type || (strcmp(type, "deny") && strcmp(type, "accept") && strcmp(type, "allow") &&
                   strncmp(type, "skipto:", strlen("skipto:")))) {
        if(type) xmlFree(type);
        *err = "unknown type";
        return NULL;
      }
      rulecnt++;
      if(type) {
        xmlFree(type);
      }
    }
    else CHECK_N_SET(seq) {
      xmlChar *v = xmlNodeGetContent(r);
      if(v) {
        xmlSetProp(root, r->name, v);
      }
      else {
        xmlUnsetProp(root, r->name);
      }
      xmlUnlinkNode(r);
      xmlFreeNode(r);
      r = previous_child;

      if (seq && v) {
        *seq = strtoll((const char *)v, NULL, 10);
      }

      xmlFree(v);
    }
    else if(strcmp((char *)r->name, "text") != 0) {
      /* ignore text nodes */
      *err = "unknown attribute";
      return NULL;
    }
    previous_child = r;
  }

  if(rulecnt == 0) {
    *err = "no rules";
    return NULL;
  }
  return root;
}

mtev_boolean
noit_filter_compile_add_load_set(filterset_t *set) {
  mtev_boolean used_new_one = mtev_false;
  void *vset;
  LOCKFS();
  if(mtev_hash_retrieve(filtersets, set->name, strlen(set->name), &vset)) {
    filterset_t *oldset = vset;
    if(oldset->seq >= set->seq) { /* no update */
      filterset_free(set);
    } else {
      mtev_hash_replace(filtersets, set->name, strlen(set->name), (void *)set,
                        NULL, filterset_free);
      used_new_one = mtev_true;
    }
  }
  else {
    mtev_hash_store(filtersets, set->name, strlen(set->name), (void *)set);
    used_new_one = mtev_true;
  }
  UNLOCKFS();
  if(used_new_one && set->seq >= 0) {
    noit_cluster_mark_filter_changed(set->name, NULL);
  }
  return used_new_one;
}
mtev_boolean
noit_filter_compile_add(mtev_conf_section_t setinfo) {
  mtev_conf_section_t *rules;
  int j, fcnt;
  char filterset_name[MAX_METRIC_TAGGED_NAME];
  filterset_t *set;
  int64_t seq = 0;
  if(!mtev_conf_get_stringbuf(setinfo, "@name",
                              filterset_name, sizeof(filterset_name))) {
    mtevL(nf_error,
          "filterset with no name, skipping as it cannot be referenced.\n");
    return mtev_false;
  }
  set = calloc(1, sizeof(*set));
  set->ref_cnt = 1;
  set->name = strdup(filterset_name);
  mtev_conf_get_int64(setinfo, "@seq", &seq);
  assert(seq>=0);
  set->seq = seq;

  rules = mtev_conf_get_sections_read(setinfo, "rule", &fcnt);
  /* Here we will work through the list backwards pushing the rules on
   * the front of the list.  That way we can simply walk them in order
   * for the application process.
   */
  mtevL(nf_debug, "Compiling filterset '%s'\n", set->name);
  for(j=fcnt-1; j>=0; j--) {
    filterrule_t *rule;
    char buffer[MAX_METRIC_TAGGED_NAME];
    if(!mtev_conf_get_stringbuf(rules[j], "@type", buffer, sizeof(buffer)) ||
       (strcmp(buffer, FILTERSET_ACCEPT_STRING) && strcmp(buffer, FILTERSET_ALLOW_STRING) && strcmp(buffer, FILTERSET_DENY_STRING) &&
        strncmp(buffer, FILTERSET_SKIPTO_STRING, strlen(FILTERSET_SKIPTO_STRING)))) {
      mtevL(nf_error, "rule must have type 'accept' or 'allow' or 'deny' or 'skipto:'\n");
      continue;
    }
    mtevL(nf_debug, "Prepending %s into %s\n", buffer, set->name);
    rule = calloc(1, sizeof(*rule));
    if(!strncasecmp(buffer, FILTERSET_SKIPTO_STRING, strlen(FILTERSET_SKIPTO_STRING))) {
      rule->type = NOIT_FILTER_SKIPTO;
      rule->skipto = strdup(buffer+strlen(FILTERSET_SKIPTO_STRING));
    }
    else {
      rule->type = (!strcmp(buffer, FILTERSET_ACCEPT_STRING) || !strcmp(buffer, FILTERSET_ALLOW_STRING)) ?
                     NOIT_FILTER_ACCEPT : NOIT_FILTER_DENY;
    }
    int32_t ffp = DEFAULT_FILTER_FLUSH_PERIOD_MS;
    if(!mtev_conf_get_int32(rules[j], "ancestor-or-self::node()/@filter_flush_period", &ffp))
      ffp = DEFAULT_FILTER_FLUSH_PERIOD_MS;
    if(ffp < 0) ffp = 0;
    rule->flush_interval.tv_sec = ffp/1000;
    rule->flush_interval.tv_usec = ffp%1000;
    if(mtev_conf_get_stringbuf(rules[j], "@id", buffer, sizeof(buffer))) {
      rule->ruleid = strdup(buffer);
    }
    /* Compile any hash tables, should they exist */
#define HT_COMPILE(rname, canon) do { \
    mtev_conf_section_t *htentries; \
    int hte_cnt, hti, tablesize = 2; \
    int32_t auto_max = 0; \
    char *htstr; \
    htentries = mtev_conf_get_sections_read(rules[j], #rname, &hte_cnt); \
    mtev_conf_get_int32(rules[j], "@" #rname "_auto_add", &auto_max); \
    if(hte_cnt || auto_max > 0) { \
      rule->rname##_auto_hash_max = auto_max; \
      rule->rname##_ht = calloc(1, sizeof(*(rule->rname##_ht))); \
      while(tablesize < hte_cnt) tablesize <<= 1; \
      mtev_hash_init_size(rule->rname##_ht, tablesize); \
      for(hti=0; hti<hte_cnt; hti++) { \
        if(!mtev_conf_get_string(htentries[hti], "self::node()", &htstr)) \
          mtevL(nf_error, "Error fetching text content from filter match.\n"); \
        else { \
          if(canon) { \
            char tgt[MAX_METRIC_TAGGED_NAME]; \
            if(noit_metric_canonicalize(htstr, strlen(htstr), tgt, sizeof(tgt), mtev_true) > 0) { \
              free(htstr); \
              htstr = strdup(tgt); \
            } \
          } \
          mtevL(nf_debug, "HT_COMPILE(%p) -> (%s)\n", rule->rname##_ht, htstr); \
          mtev_hash_replace(rule->rname##_ht, htstr, strlen(htstr), NULL, free, NULL); \
        } \
      } \
    } \
    mtev_conf_release_sections_read(htentries, hte_cnt); \
} while(0);
    HT_COMPILE(target, mtev_false);
    HT_COMPILE(module, mtev_false);
    HT_COMPILE(name, mtev_false);
    HT_COMPILE(metric, mtev_true);
    
    /* Compile our rules */
#define RULE_COMPILE(rname) do { \
  char *longre = NULL; \
  if(mtev_conf_get_string(rules[j], "@" #rname, &longre)) { \
    const char *error; \
    int erroffset; \
    rule->rname = pcre_compile(longre, 0, &error, &erroffset, NULL); \
    if(!rule->rname) { \
      mtevL(nf_debug, "set '%s' rule '%s: %s' compile failed: %s\n", \
            set->name, #rname, longre, error ? error : "???"); \
      rule->rname##_override = fallback_no_match; \
      mtevAssert(asprintf(&rule->rname##_re, "/%s/ failed to compile", longre)); \
    } \
    else { \
      rule->rname##_re = strdup(longre); \
      rule->rname##_e = pcre_study(rule->rname, 0, &error); \
    } \
    free(longre); \
  } \
} while(0)
#define TAGS_COMPILE(rname, search) do { \
  char *expr = NULL; \
  if(mtev_conf_get_string(rules[j], "@" #rname, &expr)) { \
    int erroffset; \
    rule->rname = strdup(expr); \
    rule->search = noit_metric_tag_search_parse(rule->rname, &erroffset); \
    if(!rule->search) { \
      mtevL(nf_error, "set '%s' rule '%s: %s' compile failed at offset %d\n", \
            set->name, #rname, expr, erroffset); \
      rule->metric_override = fallback_no_match; \
    } \
    free(expr); \
  } \
} while(0)

    if(rule->target_ht == NULL)
      RULE_COMPILE(target);
    if(rule->module_ht == NULL)
      RULE_COMPILE(module);
    if(rule->name_ht == NULL)
      RULE_COMPILE(name);
    if(rule->metric_ht == NULL) {
      RULE_COMPILE(metric);
      TAGS_COMPILE(stream_tags, stsearch);
      TAGS_COMPILE(measurement_tags, mtsearch);
    }
    rule->next = set->rules;
    set->rules = rule;
  }

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
      if(!cursor->skipto_rule)
        mtevL(nf_error, "filterset %s skipto:%s not found\n",
              set->name, cursor->skipto);
    }
  }
  mtev_conf_release_sections_read(rules, fcnt);

  mtev_boolean used_new_one = noit_filter_compile_add_load_set(set);

  return used_new_one;
}
int
noit_filter_exists(const char *name) {
  int exists;
  void *v;
  LOCKFS();
  exists = mtev_hash_retrieve(filtersets, name, strlen(name), &v);
  UNLOCKFS();
  return exists;
}

int
noit_filter_get_seq(const char *name, int64_t *seq) {
  int exists;
  void *v;
  LOCKFS();
  exists = mtev_hash_retrieve(filtersets, name, strlen(name), &v);
  UNLOCKFS();
  if(exists && seq != NULL) {
    *seq = ((filterset_t*)v)->seq;
  }
  return exists;
}

int
noit_filter_remove(mtev_conf_section_t vnode) {
  int removed;
  char *name = (char *)xmlGetProp(mtev_conf_section_to_xmlnodeptr(vnode), (xmlChar *)"name");
  if(!name) return 0;
  LOCKFS();
  removed = mtev_hash_delete(filtersets, name, strlen(name),
                             NULL, filterset_free);
  UNLOCKFS();
  xmlFree(name);

  return removed;
}
void
noit_filters_from_conf() {
  mtev_conf_section_t *sets;
  int i, cnt;

  sets = mtev_conf_get_sections_read(MTEV_CONF_ROOT, "/noit/filtersets//filterset", &cnt);
  for(i=0; i<cnt; i++) {
    mtev_watchdog_child_heartbeat();
    noit_filter_compile_add(sets[i]);
  }
  mtev_conf_release_sections_read(sets, cnt);
}

int
noit_filters_process_repl(xmlDocPtr doc) {
  int i = 0;
  xmlNodePtr root, child, next = NULL;
  if(!initialized) {
    mtevL(nf_debug, "filterset replication pending initialization\n");
    return -1;
  }
  root = xmlDocGetRootElement(doc);
  mtev_conf_section_t filtersets = mtev_conf_get_section_write(MTEV_CONF_ROOT, filtersets_replication_path);
  mtevAssert(!mtev_conf_section_is_empty(filtersets));
  for(child = xmlFirstElementChild(root); child; child = next) {
    next = xmlNextElementSibling(child);

    char filterset_name[MAX_METRIC_TAGGED_NAME];
    mtevAssert(mtev_conf_get_stringbuf(mtev_conf_section_from_xmlnodeptr(child), "@name",
                                       filterset_name, sizeof(filterset_name)));
    if(noit_filter_compile_add(mtev_conf_section_from_xmlnodeptr(child))) {
      char xpath[MAX_METRIC_TAGGED_NAME+128];
      snprintf(xpath, sizeof(xpath), "/noit/filtersets//filterset[@name=\"%s\"]",
               filterset_name);
      mtev_conf_section_t oldsection = mtev_conf_get_section_write(MTEV_CONF_ROOT, xpath);
      if(!mtev_conf_section_is_empty(oldsection)) {
        CONF_REMOVE(oldsection);
        xmlNodePtr node = mtev_conf_section_to_xmlnodeptr(oldsection);
        xmlUnlinkNode(node);
        xmlFreeNode(node);
      }
      mtev_conf_release_section_write(oldsection);
      xmlUnlinkNode(child);
      xmlAddChild(mtev_conf_section_to_xmlnodeptr(filtersets), child);
      CONF_DIRTY(mtev_conf_section_from_xmlnodeptr(child));
    }
    i++;
  }
  mtev_conf_release_section_write(filtersets);
  mtev_conf_mark_changed();
  if(mtev_conf_write_file(NULL) != 0)
    mtevL(nf_error, "local config write failed\n");
  return i;
}

void
noit_refresh_filtersets(mtev_console_closure_t ncct,
                        mtev_conf_t_userdata_t *info) {
  noit_filters_from_conf();
  nc_printf(ncct, "Reloaded %d filtersets.\n",
            filtersets ? mtev_hash_size(filtersets) : 0);
}

static mtev_boolean
noit_apply_filterrule(mtev_hash_table *m,
                      pcre *p, pcre_extra *pe, const char *subj) {
  int rc, ovector[30];
  if(m) {
    void *vptr;
    return mtev_hash_retrieve(m, subj, strlen(subj), &vptr);
  }
  if(!p) return mtev_true;
  rc = pcre_exec(p, pe, subj, strlen(subj), 0, 0, ovector, 30);
  if(rc >= 0) return mtev_true;
  return mtev_false;
}
static mtev_boolean
noit_apply_filterrule_metric(filterrule_t *r,
                             const char *subj, int subj_len,
                             noit_metric_tagset_t *stset, noit_metric_tagset_t *mtset) {
  int rc, ovector[30];
  char canonicalcheck[MAX_METRIC_TAGGED_NAME];
  canonicalcheck[0] = '\0';
  mtev_boolean canonical = (subj[subj_len] == '|');
  if(r->metric_ht) {
    void *vptr;
    if(mtev_hash_retrieve(r->metric_ht, subj, subj_len, &vptr)) {
      mtevL(nf_debug, "HT_MATCH(%p) -> (%.*s) -> true\n", r->metric_ht, subj_len, subj);
      return mtev_true;
    }
    /* it's not there, but if this wasn't a tagged metric, someone could have explicitly
     * matched on <name>|ST[], so we should check that too. */
    if(!canonical && subj[subj_len] == '\0') {
      strlcat(canonicalcheck, subj, sizeof(canonicalcheck));
      strlcat(canonicalcheck, "|ST[]", sizeof(canonicalcheck));
      subj = canonicalcheck;
    }
    mtev_boolean rv = mtev_hash_retrieve(r->metric_ht, subj, strlen(subj), &vptr);
    mtevL(nf_debug, "HT_MATCH(%p) -> (%s) -> %s\n", r->metric_ht, subj, rv ? "true" : "false");
    return rv;
  }
  if(r->metric || r->metric_override) {
    rc = pcre_exec(r->metric ? r->metric : r->metric_override, r->metric ? r->metric_e : NULL,
                   subj, subj_len, 0, 0, ovector, 30);
    if(rc < 0) return mtev_false;
  }
  if(r->stsearch) {
    if(!noit_metric_tag_search_evaluate_against_tags(r->stsearch, stset)) {
      return mtev_false;
    }
  }
  if(r->mtsearch) {
    if(!noit_metric_tag_search_evaluate_against_tags(r->mtsearch, mtset)) {
      return mtev_false;
    }
  }
  return mtev_true;
}

mtev_boolean
noit_apply_filterset(const char *filterset,
                     noit_check_t *check,
                     metric_t *metric) {
  /* We pass in filterset here just in case someone wants to apply
   * a filterset other than check->filterset.. You never know.
   */
  void *vfs;
  if(!filterset) return mtev_true;   /* No filter */
  if(!filtersets) return mtev_false; /* Couldn't possibly match */
  struct timeval now;
  mtev_gettimeofday(&now, NULL);

  char encoded_nametag[NOIT_TAG_MAX_PAIR_LEN+1];
  char decoded_nametag[NOIT_TAG_MAX_PAIR_LEN+1];
  noit_metric_tag_t stags[MAX_TAGS], mtags[MAX_TAGS];
  noit_metric_tagset_t stset = { .tags = stags, .tag_count = MAX_TAGS };
  noit_metric_tagset_t mtset = { .tags = mtags, .tag_count = MAX_TAGS };
  int mlen = noit_metric_parse_tags(metric->metric_name, strlen(metric->metric_name), &stset, &mtset);
  if(mlen < 0) {
    stset.tag_count = mtset.tag_count = 0;
    mlen = strlen(metric->metric_name);
  }
  if(stset.tag_count < MAX_TAGS-1) {
    /* add __name */
    snprintf(decoded_nametag, sizeof(decoded_nametag), "__name%c%.*s",
             NOIT_TAG_DECODED_SEPARATOR, mlen, metric->metric_name);
    size_t nlen = noit_metric_tagset_encode_tag(encoded_nametag, sizeof(encoded_nametag),
                                                decoded_nametag, strlen(decoded_nametag));
    stset.tags[stset.tag_count].category_size = 7;
    stset.tags[stset.tag_count].total_size = nlen;
    stset.tags[stset.tag_count].tag = encoded_nametag;
    stset.tag_count++;
  }
  LOCKFS();
  if(mtev_hash_retrieve(filtersets, filterset, strlen(filterset), &vfs)) {
    filterset_t *fs = (filterset_t *)vfs;
    filterrule_t *r, *skipto_rule = NULL;
    mtev_boolean ret = mtev_false;
    ck_pr_inc_32(&fs->ref_cnt);
    ck_pr_inc_32(&fs->executions);
    UNLOCKFS();
#define MATCHES(rname, value) noit_apply_filterrule(r->rname##_ht, r->rname ? r->rname : r->rname##_override, r->rname ? r->rname##_e : NULL, value)
    for(r = fs->rules; r; r = r->next) {
      int need_target, need_module, need_name, need_metric;
      /* If we're targeting a skipto rule, match or continue */
      if(skipto_rule && skipto_rule != r) continue;
      skipto_rule = NULL;

      ck_pr_inc_32(&r->executions);

      need_target = !MATCHES(target, check->target);
      need_module = !MATCHES(module, check->module);
      need_name = !MATCHES(name, check->name);
      need_metric = !noit_apply_filterrule_metric(r, metric->metric_name, mlen, &stset, &mtset);

      if(!need_target && !need_module && !need_name && !need_metric) {
        if(r->type == NOIT_FILTER_SKIPTO) {
          skipto_rule = r->skipto_rule;
          continue;
        }
        ck_pr_inc_32(&r->matches);
        ret = (r->type == NOIT_FILTER_ACCEPT) ? mtev_true : mtev_false;
        break;
      }
      /* If we need some of these and we have an auto setting that isn't fulfilled for each of them, we can add and succeed */
#define CHECK_ADD(rname) (!need_##rname || (r->rname##_auto_hash_max > 0 && r->rname##_ht && mtev_hash_size(r->rname##_ht) < r->rname##_auto_hash_max))
#define UPDATE_FILTER_RULE(rname, value) mtev_hash_replace(r->rname##_ht, strdup(value), strlen(value), NULL, free, NULL)

      /* flush if required */
      if(r->flush_interval.tv_sec || r->flush_interval.tv_usec) {
        struct timeval reset;
        add_timeval(r->last_flush, r->flush_interval, &reset);
        if(compare_timeval(now, reset) >= 0) {
          mtev_boolean did_work = mtev_false;
          if(r->target_auto_hash_max) {
            mtev_hash_delete_all(r->target_ht, free, NULL);
            did_work = mtev_true;
          }
          if(r->module_auto_hash_max) {
            mtev_hash_delete_all(r->module_ht, free, NULL);
            did_work = mtev_true;
          }
          if(r->name_auto_hash_max) {
            mtev_hash_delete_all(r->name_ht, free, NULL);
            did_work = mtev_true;
          }
          if(r->metric_auto_hash_max) {
            mtev_hash_delete_all(r->metric_ht, free, NULL);
            did_work = mtev_true;
          }
          memcpy(&r->last_flush, &now, sizeof(now));
          if(did_work) {
            mtevL(nf_debug, "flushed auto_add rule %s%s%s\n", fs->name, r->ruleid ? ":" : "", r->ruleid ? r->ruleid : "");
          }
        }
      }

      if(CHECK_ADD(target) && CHECK_ADD(module) && CHECK_ADD(name) && CHECK_ADD(metric)) {
        if(need_target) UPDATE_FILTER_RULE(target, check->target);
        if(need_module) UPDATE_FILTER_RULE(module, check->module);
        if(need_name) UPDATE_FILTER_RULE(name, check->name);
        if(need_metric) UPDATE_FILTER_RULE(metric, metric->metric_name);
        if(r->type == NOIT_FILTER_SKIPTO) {
          skipto_rule = r->skipto_rule;
          continue;
        }
        ck_pr_inc_32(&r->matches);
        ret = (r->type == NOIT_FILTER_ACCEPT) ? mtev_true : mtev_false;
        break;
      }
    }
    filterset_free(fs);
    if(!ret) ck_pr_inc_32(&fs->denies);
    return ret;
  }
  UNLOCKFS();
  return mtev_false;
}

static char *
conf_t_filterset_prompt(EditLine *el) {
  mtev_console_closure_t ncct;
  mtev_conf_t_userdata_t *info;
  static char *tl = "noit(conf)# ";
  static char *pfmt = "noit(conf:filterset:%.*s%s)# ";
  int max_space, namelen;
  el_get(el, EL_USERDATA, (void *)&ncct);
  if(!ncct) return tl;
  info = mtev_console_userdata_get(ncct, MTEV_CONF_T_USERDATA);
  if(!info) return tl;
  max_space = sizeof(info->prompt) - strlen(pfmt) + 6 - 1;
  namelen = strlen(info->filter_name);
  if(namelen > max_space)
    snprintf(info->prompt, sizeof(info->prompt), pfmt,
             max_space - 3, info->filter_name, "...");
  else
    snprintf(info->prompt, sizeof(info->prompt), pfmt,
             namelen, info->filter_name, "");
  return info->prompt;
}

static int
noit_console_filter_show(mtev_console_closure_t ncct,
                         int argc, char **argv,
                         mtev_console_state_t *state,
                         void *closure) {
  mtev_conf_t_userdata_t *info;
  char xpath[1024];
  mtev_conf_section_t fsnode;
  mtev_conf_section_t *rules;
  int i, rulecnt;

  info = mtev_console_userdata_get(ncct, MTEV_CONF_T_USERDATA);
  snprintf(xpath, sizeof(xpath), "/%s",
           info->path);
  fsnode = mtev_conf_get_section_read(MTEV_CONF_ROOT, xpath);
  if(mtev_conf_section_is_empty(fsnode)) {
    nc_printf(ncct, "internal error\n");
    mtev_conf_release_section_read(fsnode);
    return -1;
  }
  rules = mtev_conf_get_sections_read(fsnode, "rule", &rulecnt);
  for(i=0; i<rulecnt; i++) {
    char val[MAX_METRIC_TAGGED_NAME];
    if(!mtev_conf_get_stringbuf(rules[i], "@type", val, sizeof(val)))
      val[0] = '\0';
    nc_printf(ncct, "Rule %d [%s]:\n", i+1, val);
#define DUMP_ATTR(a) do { \
  char *vstr; \
  mtev_conf_section_t *ht; \
  int cnt; \
  ht = mtev_conf_get_sections_read(rules[i], #a, &cnt); \
  if(ht && cnt) { \
    nc_printf(ncct, "\t%s: hash match of %d items\n", #a, cnt); \
  } \
  else if(mtev_conf_get_string(rules[i], "@" #a, &vstr)) { \
    nc_printf(ncct, "\t%s: /%s/\n", #a, val); \
    free(vstr); \
  } \
  mtev_conf_release_sections_read(ht, cnt); \
} while(0)
    DUMP_ATTR(target);
    DUMP_ATTR(module);
    DUMP_ATTR(name);
    DUMP_ATTR(metric);
    DUMP_ATTR(stream_tags);
    DUMP_ATTR(measurement_tags);
    DUMP_ATTR(id);
    DUMP_ATTR(skipto);
  }
  mtev_conf_release_sections_write(rules, rulecnt);
  mtev_conf_release_section_write(fsnode);
  return 0;
}
static int
noit_console_rule_configure(mtev_console_closure_t ncct,
                            int argc, char **argv,
                            mtev_console_state_t *state,
                            void *closure) {
  mtev_conf_section_t fsnode = MTEV_CONF_EMPTY,
                      sec2 = MTEV_CONF_EMPTY,
                      byebye = MTEV_CONF_EMPTY;
  mtev_conf_t_userdata_t *info;
  mtev_conf_section_t add_arg;
  mtev_boolean add_arg_initialized = mtev_false;
  char xpath[1024];
  int rv = -1;

  info = mtev_console_userdata_get(ncct, MTEV_CONF_T_USERDATA);
  snprintf(xpath, sizeof(xpath), "/%s",
           info->path);
  fsnode = mtev_conf_get_section_write(MTEV_CONF_ROOT, xpath);
  if(mtev_conf_section_is_empty(fsnode)) {
    nc_printf(ncct, "internal error");
    goto bail;
  }
  if(closure) {
    int rulenum;
    /* removing a rule */
    if(argc != 1) {
      nc_printf(ncct, "requires one argument\n");
      goto bail;
    }
    rulenum = atoi(argv[0]);
    snprintf(xpath, sizeof(xpath), "rule[%d]", rulenum);
    byebye = mtev_conf_get_section_write(fsnode, xpath);
    if(mtev_conf_section_is_empty(byebye)) {
      nc_printf(ncct, "cannot find rule\n");
      goto bail;
    }
    xmlUnlinkNode(mtev_conf_section_to_xmlnodeptr(byebye));
    xmlFreeNode(mtev_conf_section_to_xmlnodeptr(byebye));
    nc_printf(ncct, "removed\n");
  }
  else {
    xmlNodePtr (*add_func)(xmlNodePtr, xmlNodePtr);
    xmlNodePtr add_arg_node, new_rule;
    int i, needs_type = 1;
    if(argc < 1 || argc % 2) {
      nc_printf(ncct, "even number of arguments required\n");
      goto bail;
    }
    if(!strcmp(argv[0], "before") || !strcmp(argv[0], "after")) {
      int rulenum = atoi(argv[1]);
      snprintf(xpath, sizeof(xpath), "rule[%d]", rulenum);
      add_arg = mtev_conf_get_section_write(fsnode, xpath);
      add_arg_initialized = mtev_true;
      if(mtev_conf_section_is_empty(add_arg)) {
        nc_printf(ncct, "%s rule not found\n", xpath);
        goto bail;
      }
      if(*argv[0] == 'b') add_func = xmlAddPrevSibling;
      else add_func = xmlAddNextSibling;
      argc -= 2;
      argv += 2;
    }
    else {
      add_func = xmlAddChild;
      add_arg = fsnode;
    }
    add_arg_node = mtev_conf_section_to_xmlnodeptr(add_arg);
    for(i=0;i<argc;i+=2) {
      if(!strcmp(argv[i], "type")) needs_type = 0;
      else if(strcmp(argv[i], "target") && strcmp(argv[i], "module") &&
              strcmp(argv[i], "name") && strcmp(argv[i], "metric") &&
              strcmp(argv[i], "stream_tags") && strcmp(argv[i], "measurement_tags") &&
              strcmp(argv[i], "id")) {
        nc_printf(ncct, "unknown attribute '%s'\n", argv[i]);
        goto bail;
      }
    }
    if(needs_type) {
      nc_printf(ncct, "type <allow|deny> is required\n");
      goto bail;
    }
    new_rule = xmlNewNode(NULL, (xmlChar *)"rule");
    for(i=0;i<argc;i+=2)
      xmlSetProp(new_rule, (xmlChar *)argv[i], (xmlChar *)argv[i+1]);
    add_func(add_arg_node, new_rule);
  }
  rv = 0;
 bail:
  if (add_arg_initialized) {
    mtev_conf_release_section_write(add_arg);
  }
  mtev_conf_release_section_write(fsnode);
  mtev_conf_release_section_write(sec2);
  mtev_conf_release_section_write(byebye);
  return rv;
}
static int
noit_console_filter_configure(mtev_console_closure_t ncct,
                              int argc, char **argv,
                              mtev_console_state_t *state,
                              void *closure) {
  mtev_conf_section_t parent = MTEV_CONF_EMPTY, fsnode = MTEV_CONF_EMPTY;
  int rv = -1;
  mtev_conf_t_userdata_t *info;
  char xpath[1024];

  info = mtev_console_userdata_get(ncct, MTEV_CONF_T_USERDATA);
  if(!info) {
    nc_printf(ncct, "internal error\n");
    goto cleanup;
  }
  if(strncmp(info->path, "/filtersets/", strlen("/filtersets/")) &&
     strcmp(info->path, "/filtersets")) {
    nc_printf(ncct, "filterset only allows inside /filtersets (not %s)\n",
              info->path);
    goto cleanup;
  }
  if(argc != 1) {
    nc_printf(ncct, "filterset requires one argument\n");
    goto cleanup;
  }
  snprintf(xpath, sizeof(xpath), "/%s", info->path);
  parent = mtev_conf_get_section_write(MTEV_CONF_ROOT, xpath);
  if(mtev_conf_section_is_empty(parent)) {
    nc_printf(ncct, "internal error, can't final current working path\n");
    goto cleanup;
  }
  snprintf(xpath, sizeof(xpath), "filterset[@name=\"%s\"]", argv[0]);
  fsnode = mtev_conf_get_section_write(parent, xpath);
  if(closure) {
    int removed;
    removed = noit_filter_remove(fsnode);
    nc_printf(ncct, "%sremoved filterset '%s'\n",
              removed ? "" : "failed to ", argv[0]);
    if(removed) {
      CONF_REMOVE(fsnode);
      xmlUnlinkNode(mtev_conf_section_to_xmlnodeptr(fsnode));
      xmlFreeNode(mtev_conf_section_to_xmlnodeptr(fsnode));
    }
    rv = !removed;
    goto cleanup;
  }
  if(mtev_conf_section_is_empty(fsnode)) {
    void *vfs;
    xmlNodePtr newfsxml;
    nc_printf(ncct, "Cannot find filterset '%s'\n", argv[0]);
    LOCKFS();
    if(mtev_hash_retrieve(filtersets, argv[0], strlen(argv[0]), &vfs)) {
      UNLOCKFS();
      nc_printf(ncct, "filter of the same name already exists\n");
      goto cleanup;
    }
    UNLOCKFS();
    /* Fine the parent path */
    newfsxml = xmlNewNode(NULL, (xmlChar *)"filterset");
    xmlSetProp(newfsxml, (xmlChar *)"name", (xmlChar *)argv[0]);
    xmlAddChild(mtev_conf_section_to_xmlnodeptr(parent), newfsxml);
    nc_printf(ncct, "created new filterset\n");
  }

  if(info && !mtev_conf_section_is_empty(fsnode)) {
    char *xmlpath = NULL;
    free(info->path);
    xmlpath = (char *)xmlGetNodePath(mtev_conf_section_to_xmlnodeptr(fsnode));
    info->path = strdup(xmlpath + strlen("/noit"));
    free(xmlpath);
    strlcpy(info->filter_name, argv[0], sizeof(info->filter_name));
    if(state) {
      mtev_console_state_push_state(ncct, state);
      mtev_console_state_init(ncct);
    }
  }
 cleanup:
  mtev_conf_release_section_write(parent);
  mtev_conf_release_section_write(fsnode);
  return rv;
}

static int
filterset_accum(noit_check_t *check, void *closure) {
  mtev_hash_table *active = closure;
  if(!check->filterset) return 0;
  if(mtev_hash_delete(active, check->filterset, strlen(check->filterset), free, NULL))
    return 1;
  return 0;
}

void
noit_filtersets_build_cluster_changelog(void *vpeer) {
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  LOCKFS();
  while(mtev_hash_adv(filtersets, &iter)) {
    filterset_t *set = iter.value.ptr;
    if(set->seq >= 0) noit_cluster_mark_filter_changed(set->name, vpeer);
  }
  UNLOCKFS();
}
int
noit_filtersets_cull_unused() {
  mtev_hash_table active;
  char *buffer = NULL;
  mtev_conf_section_t *declares;
  int i, n_uses = 0, n_declares = 0, removed = 0;
  const char *declare_xpath = "//filterset[@name and not (@cull='false')]";

  mtev_hash_init(&active);

  declares = mtev_conf_get_sections_write(MTEV_CONF_ROOT, declare_xpath, &n_declares);
  for(i=0;i<n_declares;i++) {
    if(!buffer) buffer = malloc(128);
    if(mtev_conf_get_stringbuf(declares[i], "@name", buffer, 128)) {
      if(mtev_hash_store(&active, buffer, strlen(buffer), &declares[i])) {
        buffer = NULL;
      }
      else {
        void *vnode = NULL;
        /* We've just hit a duplicate.... check to see if there's an existing
         * entry and if there is, load the latest one and delete the old
         * one. */
        if(!mtev_hash_retrieve(&active, buffer, strlen(buffer), &vnode))
          vnode = NULL;
        if (vnode) {
          mtev_conf_section_t *sptr = vnode;
          xmlNodePtr node = mtev_conf_section_to_xmlnodeptr(*sptr);
          noit_filter_compile_add(declares[i]);
          CONF_REMOVE(*sptr);
          xmlUnlinkNode(node);
          xmlFreeNode(node);
          removed++;
          if(mtev_hash_replace(&active, buffer, strlen(buffer), &declares[i], free, NULL)) {
            buffer = NULL;
          }
        }
      }
    }
  }
  free(buffer);
  

  n_uses = noit_poller_do(filterset_accum, &active);

  if(n_uses > 0 && mtev_hash_size(&active) > 0) {
    mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
    const char *filter_name;
    int filter_name_len;
    void *vnode;
    while(mtev_hash_next(&active, &iter, &filter_name, &filter_name_len,
                         &vnode)) {
      mtev_conf_section_t *sptr = vnode;
      if(noit_filter_remove(*sptr)) {
        CONF_REMOVE(*sptr);
        xmlNodePtr node = mtev_conf_section_to_xmlnodeptr(*sptr);
        xmlUnlinkNode(node);
        xmlFreeNode(node);
        removed++;
      }
    }
  }
  mtev_conf_release_sections_write(declares, n_declares);

  mtev_hash_destroy(&active, free, NULL);
  return removed;
}

static int
noit_console_filter_cull(mtev_console_closure_t ncct,
                         int argc, char **argv,
                         mtev_console_state_t *state,
                         void *closure) {
  int rv = 0;
  mtev_conf_t_userdata_t *info;

  info = mtev_console_userdata_get(ncct, MTEV_CONF_T_USERDATA);
  if(!info) {
    nc_printf(ncct, "internal error\n");
    return -1;
  }
  if(strncmp(info->path, "/filtersets/", strlen("/filtersets/")) &&
     strcmp(info->path, "/filtersets")) {
    nc_printf(ncct, "filterset only allows inside /filtersets (not %s)\n",
              info->path);
    return -1;
  }
  if (ENABLE_LMDB_FILTERSETS && noit_filters_get_lmdb_instance()) {
    rv = noit_filters_lmdb_cull_unused();
  }
  else {
    rv = noit_filtersets_cull_unused();
  }
  nc_printf(ncct, "Culled %d unused filtersets\n", rv);
  return 0;
}

static int
noit_console_filter_show_running(mtev_console_closure_t ncct,
                                 int argc, char **argv,
                                 mtev_console_state_t *state,
                                 void *closure) {
  struct timeval now;
  if(argc != 1) {
    nc_printf(ncct, "filterset name required\n");
    return 0;
  }
  char *filterset = argv[0];
  void *vfs;

  mtev_gettimeofday(&now, NULL);

  LOCKFS();
  if(mtev_hash_retrieve(filtersets, filterset, strlen(filterset), &vfs)) {
    filterset_t *fs = (filterset_t *)vfs;
    filterrule_t *r;
    ck_pr_inc_32(&fs->ref_cnt);
    UNLOCKFS();

    nc_printf(ncct, "filterset name: %s\n", fs->name);
    nc_printf(ncct, "executions: %u\n", ck_pr_load_32(&fs->executions));
    nc_printf(ncct, "denies: %u\n", ck_pr_load_32(&fs->denies));
    int i = 1;
    for(r=fs->rules; r; r=r->next) {
      if(r->type == NOIT_FILTER_SKIPTO) {
        nc_printf(ncct, "[%d:%s] skipto %s", i, r->ruleid ? r->ruleid : "", r->skipto);
      }
      else {
        nc_printf(ncct, "[%d:%s] %s", i, r->ruleid ? r->ruleid : "", (r->type == NOIT_FILTER_ACCEPT) ? FILTERSET_ACCEPT_STRING : FILTERSET_DENY_STRING);
      }
      nc_printf(ncct, " matched: %u/%u\n", ck_pr_load_32(&r->matches), ck_pr_load_32(&r->executions));

#define DESCRIBE(rname) do { \
  if(r->rname##_ht) { \
    char bb[128]; \
    struct timeval diff; \
    sub_timeval(now, r->last_flush, &diff); \
    if(r->last_flush.tv_sec == 0) snprintf(bb, sizeof(bb), "at boot"); \
    else snprintf(bb, sizeof(bb), "%u.%03us ago", (unsigned)diff.tv_sec, (unsigned)diff.tv_usec/1000); \
    if(r->rname##_auto_hash_max) { \
      nc_printf(ncct, "  %s hash-based [%d/%d] flushed %s\n", #rname, mtev_hash_size(r->rname##_ht), r->rname##_auto_hash_max, bb); \
    } else { \
      nc_printf(ncct, "  %s hash-based [%d entries] flushed %s\n", #rname, mtev_hash_size(r->rname##_ht), bb); \
    } \
  } else if(r->rname##_re) { \
    nc_printf(ncct, "  %s =~ %s\n", #rname, r->rname##_re); \
  } \
} while(0)

      DESCRIBE(target);
      DESCRIBE(module);
      DESCRIBE(name);
      DESCRIBE(metric);
      if(r->stream_tags) {
        nc_printf(ncct, "  stream tag filter: %s\n", r->stream_tags);
      }
      i++;
    }

    filterset_free(fs);
  } else {
    nc_printf(ncct, "no such filterset\n");
  }
  return 0;
}

static char *
noit_console_filter_opts(mtev_console_closure_t ncct,
                         mtev_console_state_stack_t *stack,
                         mtev_console_state_t *dstate,
                         int argc, char **argv, int idx) {
  if(argc == 1) {
    int i = 0;
    mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
    LOCKFS();
    while(mtev_hash_adv(filtersets, &iter)) {
      if(!argv[0] || !strncmp(argv[0], iter.key.str, strlen(argv[0]))) {
        if(idx == i) {
          char *copy = strdup(iter.key.str);
          UNLOCKFS();
          return copy;
        }
        i++;
      }
    }
    UNLOCKFS();
    return NULL;
  }
  if(argc == 2) {
    return mtev_console_opt_delegate(ncct, stack, dstate, argc-1, argv+1, idx);
  }
  return NULL;
}

static void
register_console_filter_commands() {
  mtev_console_state_t *tl, *filterset_state, *nostate;
  cmd_info_t *confcmd, *conf_t_cmd, *no_cmd, *show_cmd;

  tl = mtev_console_state_initial();
  show_cmd = mtev_console_state_get_cmd(tl, "show");
  mtevAssert(show_cmd && show_cmd->dstate);

  mtev_console_state_add_cmd(show_cmd->dstate,
    NCSCMD("filterset", noit_console_filter_show_running,
           noit_console_filter_opts, NULL, NULL));

  confcmd = mtev_console_state_get_cmd(tl, "configure");
  mtevAssert(confcmd && confcmd->dstate);

  conf_t_cmd = mtev_console_state_get_cmd(confcmd->dstate, "terminal");
  mtevAssert(conf_t_cmd && conf_t_cmd->dstate);

  nostate = mtev_console_state_alloc();
  mtev_console_state_add_cmd(nostate,
    NCSCMD("rule", noit_console_rule_configure, NULL, NULL, (void *)1));

  filterset_state = mtev_console_state_alloc();
  mtev_console_state_add_cmd(filterset_state,
    NCSCMD("exit", mtev_console_config_cd, NULL, NULL, ".."));
  mtev_console_state_add_cmd(filterset_state,
    NCSCMD("status", noit_console_filter_show, NULL, NULL, NULL));
  mtev_console_state_add_cmd(filterset_state,
    NCSCMD("rule", noit_console_rule_configure, NULL, NULL, NULL));
  mtev_console_state_add_cmd(filterset_state,
    NCSCMD("no", mtev_console_state_delegate, mtev_console_opt_delegate,
           nostate, NULL));

  filterset_state->console_prompt_function = conf_t_filterset_prompt;

  mtev_console_state_add_cmd(conf_t_cmd->dstate,
    NCSCMD("filterset", noit_console_filter_configure,
           NULL, filterset_state, NULL));

  mtev_console_state_add_cmd(conf_t_cmd->dstate,
    NCSCMD("cull", noit_console_filter_cull,
           NULL, NULL, NULL));

  no_cmd = mtev_console_state_get_cmd(conf_t_cmd->dstate, "no");
  mtevAssert(no_cmd && no_cmd->dstate);
  mtev_console_state_add_cmd(no_cmd->dstate,
    NCSCMD("filterset", noit_console_filter_configure, NULL, NULL, (void *)1));
}

void
noit_filters_init() {
  const char *error;
  int erroffset;

  nf_error = mtev_log_stream_find("error/noit/filters");
  nf_debug = mtev_log_stream_find("debug/noit/filters");

  /* lmdb_path dictates where an LMDB backing store for checks would live.
   * use_lmdb defaults to the existence of an lmdb_path...
   *   explicit true will use one, creating if needed
   *   explicit false will not use it, if even if it exists
   *   otherwise, it will use it only if it already exists
   */
  char *lmdb_path = NULL;
  bool lmdb_path_exists = false;
  (void)mtev_conf_get_string(MTEV_CONF_ROOT, "//filtersets/@lmdb_path", &lmdb_path);
  if(lmdb_path) {
    struct stat sb;
    int rv = -1;
    while((rv = stat(lmdb_path, &sb)) == -1 && errno == EINTR);
    if(rv == 0 && (S_IFDIR == (sb.st_mode & S_IFMT))) {
      lmdb_path_exists = true;
    }
  }
  mtev_boolean use_lmdb = (lmdb_path && lmdb_path_exists);
  mtev_conf_get_boolean(MTEV_CONF_ROOT, "//filtersets/@use_lmdb", &use_lmdb);

  if (ENABLE_LMDB_FILTERSETS && (use_lmdb == mtev_true)) {
    noit_filters_lmdb_init();
    if (lmdb_path == NULL) {
      mtevFatal(mtev_error, "noit_filters: use_lmdb specified, but no path provided\n");
    }
    lmdb_instance = noit_lmdb_tools_open_instance(lmdb_path);
    if (!lmdb_instance) {
      mtevFatal(mtev_error, "noit_filters: couldn't create lmdb instance - %s\n", strerror(errno));
    }
    noit_filters_lmdb_migrate_xml_filtersets_to_lmdb();
  }
  free(lmdb_path);

  pthread_mutex_init(&filterset_lock, NULL);
  fallback_no_match = pcre_compile("^(?=a)b", 0, &error, &erroffset, NULL);
  if(!fallback_no_match) {
    mtevL(nf_error, "Filter initialization failed (nomatch filter)\n");
    exit(-1);
  }
  mtev_capabilities_add_feature("filterset:hash", NULL);
  register_console_filter_commands();

  if (ENABLE_LMDB_FILTERSETS && (use_lmdb == mtev_true)) {
    noit_filters_lmdb_filters_from_lmdb();
  }
  else {
    noit_filters_from_conf();
  }

  // The replication_prefix attribute instructs noit to put replicated filtersets into a sub-node of
  // of /noit/filtersets in noit.conf. This was introduced to for situations filtersets should be
  // placed in a sub-tree which is "shattered" into a backingstore.
  char *replication_prefix;
  if(mtev_conf_get_string(MTEV_CONF_ROOT, "/noit/filtersets/@replication_prefix", &replication_prefix)) {
    mtevL(nf_debug, "Using filterset replication prefix: %s\n", replication_prefix);
    mtevAssert(asprintf(&filtersets_replication_path, "/noit/filtersets/%s", replication_prefix));
  }
  else {
    filtersets_replication_path = "/noit/filtersets";
  }
  initialized = true;
}

void
noit_filters_init_globals(void) {
  filtersets = calloc(1, sizeof(mtev_hash_table));
  mtev_hash_init(filtersets);
}
