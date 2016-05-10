/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2015, Circonus, Inc. All rights reserved.
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

#include <mtev_defines.h>

#include <mtev_hash.h>
#include <mtev_atomic.h>
#include <mtev_watchdog.h>
#include <mtev_capabilities_listener.h>
#include <mtev_conf.h>

#include "noit_mtev_bridge.h"
#include "noit_check.h"
#include "noit_conf_checks.h"
#include "noit_filters.h"

#include <pcre.h>
#include <assert.h>
#include <libxml/tree.h>

static mtev_hash_table *filtersets = NULL;
static pthread_mutex_t filterset_lock;
static pcre *fallback_no_match = NULL;
#define LOCKFS() pthread_mutex_lock(&filterset_lock)
#define UNLOCKFS() pthread_mutex_unlock(&filterset_lock)

typedef enum { NOIT_FILTER_ACCEPT, NOIT_FILTER_DENY, NOIT_FILTER_SKIPTO } noit_ruletype_t;
typedef struct _filterrule {
  char *ruleid;
  char *skipto;
  struct _filterrule *skipto_rule;
  noit_ruletype_t type;
  pcre *target_override;
  pcre *target;
  pcre_extra *target_e;
  mtev_hash_table *target_ht;
  int target_auto_hash_max;
  pcre *module_override;
  pcre *module;
  pcre_extra *module_e;
  mtev_hash_table *module_ht;
  int module_auto_hash_max;
  pcre *name_override;
  pcre *name;
  pcre_extra *name_e;
  mtev_hash_table *name_ht;
  int name_auto_hash_max;
  pcre *metric_override;
  pcre *metric;
  pcre_extra *metric_e;
  mtev_hash_table *metric_ht;
  int metric_auto_hash_max;
  struct _filterrule *next;
} filterrule_t;

typedef struct {
  mtev_atomic32_t ref_cnt;
  char *name;
  filterrule_t *rules;
} filterset_t;

#define FRF(r,a) do { \
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
  free(r->ruleid);
  free(r->skipto);
  free(r);
}
static void
filterset_free(void *vp) {
  filterset_t *fs = vp;
  filterrule_t *r;
  if(mtev_atomic_dec32(&fs->ref_cnt) != 0) return;
  mtevL(noit_debug, "Freeing filterset [%d]: %s\n", fs->ref_cnt, fs->name);
  while(fs->rules) {
    r = fs->rules->next;
    filterrule_free(fs->rules);
    fs->rules = r;
  }
  if(fs->name) free(fs->name);
  free(fs);
}
void
noit_filter_compile_add(mtev_conf_section_t setinfo) {
  mtev_conf_section_t *rules;
  int j, fcnt;
  char filterset_name[256];
  filterset_t *set;
  if(!mtev_conf_get_stringbuf(setinfo, "@name",
                              filterset_name, sizeof(filterset_name))) {
    mtevL(noit_error,
          "filterset with no name, skipping as it cannot be referenced.\n");
    return;
  }
  set = calloc(1, sizeof(*set));
  set->ref_cnt = 1;
  set->name = strdup(filterset_name);

  rules = mtev_conf_get_sections(setinfo, "rule", &fcnt);
  /* Here we will work through the list backwards pushing the rules on
   * the front of the list.  That way we can simply walk them in order
   * for the application process.
   */
  mtevL(noit_debug, "Compiling filterset '%s'\n", set->name);
  for(j=fcnt-1; j>=0; j--) {
    filterrule_t *rule;
    char buffer[256];
    if(!mtev_conf_get_stringbuf(rules[j], "@type", buffer, sizeof(buffer)) ||
       (strcmp(buffer, "accept") && strcmp(buffer, "allow") && strcmp(buffer, "deny") &&
        strncmp(buffer, "skipto:", strlen("skipto:")))) {
      mtevL(noit_error, "rule must have type 'accept' or 'allow' or 'deny' or 'skipto:'\n");
      continue;
    }
    mtevL(noit_debug, "Prepending %s into %s\n", buffer, set->name);
    rule = calloc(1, sizeof(*rule));
    if(!strncasecmp(buffer, "skipto:", strlen("skipto:"))) {
      rule->type = NOIT_FILTER_SKIPTO;
      rule->skipto = strdup(buffer+strlen("skipto:"));
    }
    else {
      rule->type = (!strcmp(buffer, "accept") || !strcmp(buffer, "allow")) ?
                     NOIT_FILTER_ACCEPT : NOIT_FILTER_DENY;
    }

    if(mtev_conf_get_stringbuf(rules[j], "@id", buffer, sizeof(buffer))) {
      rule->ruleid = strdup(buffer);
    }
    /* Compile any hash tables, should they exist */
#define HT_COMPILE(rname) do { \
    mtev_conf_section_t *htentries; \
    int hte_cnt, hti, tablesize = 2, auto_max = 0; \
    char *htstr; \
    htentries = mtev_conf_get_sections(rules[j], #rname, &hte_cnt); \
    mtev_conf_get_int(rules[j], "@" #rname "_auto_add", &auto_max); \
    if(hte_cnt || auto_max > 0) { \
      rule->rname##_auto_hash_max = auto_max; \
      rule->rname##_ht = calloc(1, sizeof(*(rule->rname##_ht))); \
      while(tablesize < hte_cnt) tablesize <<= 1; \
      mtev_hash_init_size(rule->rname##_ht, tablesize); \
      for(hti=0; hti<hte_cnt; hti++) { \
        if(!mtev_conf_get_string(htentries[hti], "self::node()", &htstr)) \
          mtevL(noit_error, "Error fetching text content from filter match.\n"); \
        else \
          mtev_hash_replace(rule->rname##_ht, htstr, strlen(htstr), NULL, free, NULL); \
      } \
    } \
    free(htentries); \
} while(0);
    HT_COMPILE(target);
    HT_COMPILE(module);
    HT_COMPILE(name);
    HT_COMPILE(metric);
    
    /* Compile our rules */
#define RULE_COMPILE(rname) do { \
  char *longre = NULL; \
  if(mtev_conf_get_string(rules[j], "@" #rname, &longre)) { \
    const char *error; \
    int erroffset; \
    rule->rname = pcre_compile(longre, 0, &error, &erroffset, NULL); \
    if(!rule->rname) { \
      mtevL(noit_debug, "set '%s' rule '%s: %s' compile failed: %s\n", \
            set->name, #rname, longre, error ? error : "???"); \
      rule->rname##_override = fallback_no_match; \
    } \
    else { \
      rule->rname##_e = pcre_study(rule->rname, 0, &error); \
    } \
    free(longre); \
  } \
} while(0)

    if(rule->target_ht == NULL)
      RULE_COMPILE(target);
    if(rule->module_ht == NULL)
      RULE_COMPILE(module);
    if(rule->name_ht == NULL)
      RULE_COMPILE(name);
    if(rule->metric_ht == NULL)
      RULE_COMPILE(metric);
    rule->next = set->rules;
    set->rules = rule;
  }

  filterrule_t *cursor;
  for(cursor = set->rules; cursor->next; cursor = cursor->next) {
    if(cursor->skipto) {
      filterrule_t *target;
      for(target = cursor->next; target; target = target->next) {
        if(target->ruleid && !strcmp(cursor->skipto, target->ruleid)) {
          cursor->skipto_rule = target;
          break;
        }
      }
      if(!cursor->skipto_rule)
        mtevL(noit_error, "filterset %s skipto:%s not found\n",
              set->name, cursor->skipto);
    }
  }
  free(rules);
  LOCKFS();
  mtev_hash_replace(filtersets, set->name, strlen(set->name), (void *)set,
                    NULL, filterset_free);
  UNLOCKFS();
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
noit_filter_remove(mtev_conf_section_t vnode) {
  int removed;
  char *name = (char *)xmlGetProp(vnode, (xmlChar *)"name");
  if(!name) return 0;
  LOCKFS();
  removed = mtev_hash_delete(filtersets, name, strlen(name),
                             NULL, filterset_free);
  UNLOCKFS();
  return removed;
}
void
noit_filters_from_conf() {
  mtev_conf_section_t *sets;
  int i, cnt;

  sets = mtev_conf_get_sections(NULL, "/noit/filtersets//filterset", &cnt);
  for(i=0; i<cnt; i++) {
    mtev_watchdog_child_heartbeat();
    noit_filter_compile_add(sets[i]);
  }
  free(sets);
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
static int
noit_filter_update_conf_rule(const char *fname, int idx, const char *rname, const char *value) {
  char xpath[1024];
  xmlNodePtr rulenode, child;

  snprintf(xpath, sizeof(xpath), "//filtersets/filterset[@name=\"%s\"]/rule[%d]", fname, idx);
  rulenode = mtev_conf_get_section(NULL, xpath);
  if(!rulenode) return -1;
  child = xmlNewNode(NULL, (xmlChar *)rname);
  xmlNodeAddContent(child, (xmlChar *)value);
  xmlAddChild(rulenode, child);
  CONF_DIRTY(rulenode);
  mtev_conf_mark_changed();
  mtev_conf_request_write();
  return 0;
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

  LOCKFS();
  if(mtev_hash_retrieve(filtersets, filterset, strlen(filterset), &vfs)) {
    filterset_t *fs = (filterset_t *)vfs;
    filterrule_t *r, *skipto_rule = NULL;
    int idx = 0;
    mtev_atomic_inc32(&fs->ref_cnt);
    UNLOCKFS();
#define MATCHES(rname, value) noit_apply_filterrule(r->rname##_ht, r->rname ? r->rname : r->rname##_override, r->rname ? r->rname##_e : NULL, value)
    for(r = fs->rules; r; r = r->next) {
      int need_target, need_module, need_name, need_metric;
      /* If we're targeting a skipto rule, match or continue */
      idx++;
      if(skipto_rule && skipto_rule != r) continue;
      skipto_rule = NULL;

      need_target = !MATCHES(target, check->target);
      need_module = !MATCHES(module, check->module);
      need_name = !MATCHES(name, check->name);
      need_metric = !MATCHES(metric, metric->metric_name);
      if(!need_target && !need_module && !need_name && !need_metric) {
        if(r->type == NOIT_FILTER_SKIPTO) {
          skipto_rule = r->skipto_rule;
          continue;
        }
        return (r->type == NOIT_FILTER_ACCEPT) ? mtev_true : mtev_false;
      }
      /* If we need some of these and we have an auto setting that isn't fulfilled for each of them, we can add and succeed */
#define CHECK_ADD(rname) (!need_##rname || (r->rname##_auto_hash_max > 0 && r->rname##_ht && mtev_hash_size(r->rname##_ht) < r->rname##_auto_hash_max))
      if(CHECK_ADD(target) && CHECK_ADD(module) && CHECK_ADD(name) && CHECK_ADD(metric)) {
#define UPDATE_FILTER_RULE(rnum, rname, value) do { \
  mtev_hash_replace(r->rname##_ht, strdup(value), strlen(value), NULL, free, NULL); \
  if(noit_filter_update_conf_rule(fs->name, rnum, #rname, value) < 0) { \
    mtevL(noit_error, "Error updating configuration for new filter auto_add on %s=%s\n", #rname, value); \
  } \
} while(0)
        if(need_target) UPDATE_FILTER_RULE(idx, target, check->target);
        if(need_module) UPDATE_FILTER_RULE(idx, module, check->module);
        if(need_name) UPDATE_FILTER_RULE(idx, name, check->name);
        if(need_metric) UPDATE_FILTER_RULE(idx, metric, metric->metric_name);
        noit_filterset_log_auto_add(fs->name, check, metric, r->type == NOIT_FILTER_ACCEPT);
        if(r->type == NOIT_FILTER_SKIPTO) {
          skipto_rule = r->skipto_rule;
          continue;
        }
        return (r->type == NOIT_FILTER_ACCEPT) ? mtev_true : mtev_false;
      }
    }
    filterset_free(fs);
    return mtev_false;
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
  xmlNodePtr fsnode;
  mtev_conf_section_t *rules;
  int i, rulecnt;

  info = mtev_console_userdata_get(ncct, MTEV_CONF_T_USERDATA);
  snprintf(xpath, sizeof(xpath), "/%s",
           info->path);
  fsnode = mtev_conf_get_section(NULL, xpath);
  if(!fsnode) {
    nc_printf(ncct, "internal error\n");
    return -1;
  }
  rules = mtev_conf_get_sections(fsnode, "rule", &rulecnt);
  for(i=0; i<rulecnt; i++) {
    char val[256];
    val[0] = '\0';
    mtev_conf_get_stringbuf(rules[i], "@type", val, sizeof(val));
    nc_printf(ncct, "Rule %d [%s]:\n", i+1, val);
#define DUMP_ATTR(a) do { \
  char *vstr; \
  mtev_conf_section_t ht; \
  int cnt; \
  ht = mtev_conf_get_sections(rules[i], #a, &cnt); \
  if(ht && cnt) { \
    nc_printf(ncct, "\t%s: hash match of %d items\n", #a, cnt); \
  } \
  else if(mtev_conf_get_string(rules[i], "@" #a, &vstr)) { \
    nc_printf(ncct, "\t%s: /%s/\n", #a, val); \
    free(vstr); \
  } \
  free(ht); \
} while(0)
    DUMP_ATTR(target);
    DUMP_ATTR(module);
    DUMP_ATTR(name);
    DUMP_ATTR(metric);
    DUMP_ATTR(id);
    DUMP_ATTR(skipto);
  }
  if(rules) free(rules);
  return 0;
}
static int
noit_console_rule_configure(mtev_console_closure_t ncct,
                            int argc, char **argv,
                            mtev_console_state_t *state,
                            void *closure) {
  xmlNodePtr fsnode = NULL;
  mtev_conf_t_userdata_t *info;
  char xpath[1024];

  info = mtev_console_userdata_get(ncct, MTEV_CONF_T_USERDATA);
  snprintf(xpath, sizeof(xpath), "/%s",
           info->path);
  fsnode = mtev_conf_get_section(NULL, xpath);
  if(!fsnode) {
    nc_printf(ncct, "internal error");
    return -1;
  }
  if(closure) {
    int rulenum;
    xmlNodePtr byebye;
    /* removing a rule */
    if(argc != 1) {
      nc_printf(ncct, "requires one argument\n");
      return -1;
    }
    rulenum = atoi(argv[0]);
    snprintf(xpath, sizeof(xpath), "rule[%d]", rulenum);
    byebye = mtev_conf_get_section(fsnode, xpath);
    if(!byebye) {
      nc_printf(ncct, "cannot find rule\n");
      return -1;
    }
    xmlUnlinkNode(byebye);
    xmlFreeNode(byebye);
    nc_printf(ncct, "removed\n");
  }
  else {
    xmlNodePtr (*add_func)(xmlNodePtr, xmlNodePtr);
    xmlNodePtr add_arg, new_rule;
    int i, needs_type = 1;
    if(argc < 1 || argc % 2) {
      nc_printf(ncct, "even number of arguments required\n");
      return -1;
    }
    if(!strcmp(argv[0], "before") || !strcmp(argv[0], "after")) {
      int rulenum = atoi(argv[1]);
      snprintf(xpath, sizeof(xpath), "rule[%d]", rulenum);
      add_arg = mtev_conf_get_section(fsnode, xpath);
      if(!add_arg) {
        nc_printf(ncct, "%s rule not found\n", xpath);
        return -1;
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
    for(i=0;i<argc;i+=2) {
      if(!strcmp(argv[i], "type")) needs_type = 0;
      else if(strcmp(argv[i], "target") && strcmp(argv[i], "module") &&
              strcmp(argv[i], "name") && strcmp(argv[i], "metric") &&
              strcmp(argv[i], "id")) {
        nc_printf(ncct, "unknown attribute '%s'\n", argv[i]);
        return -1;
      }
    }
    if(needs_type) {
      nc_printf(ncct, "type <allow|deny> is required\n");
      return -1;
    }
    new_rule = xmlNewNode(NULL, (xmlChar *)"rule");
    for(i=0;i<argc;i+=2)
      xmlSetProp(new_rule, (xmlChar *)argv[i], (xmlChar *)argv[i+1]);
    add_func(add_arg, new_rule);
    noit_filter_compile_add((mtev_conf_section_t *)fsnode);
  }
  return 0;
}
static int
noit_console_filter_configure(mtev_console_closure_t ncct,
                              int argc, char **argv,
                              mtev_console_state_t *state,
                              void *closure) {
  xmlNodePtr parent, fsnode = NULL;
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
  parent = mtev_conf_get_section(NULL, xpath);
  if(!parent) {
    nc_printf(ncct, "internal error, can't final current working path\n");
    goto cleanup;
  }
  snprintf(xpath, sizeof(xpath), "filterset[@name=\"%s\"]", argv[0]);
  fsnode = mtev_conf_get_section(parent, xpath);
  if(closure) {
    int removed;
    removed = noit_filter_remove(fsnode);
    nc_printf(ncct, "%sremoved filterset '%s'\n",
              removed ? "" : "failed to ", argv[0]);
    if(removed) {
      CONF_REMOVE(fsnode);
      xmlUnlinkNode(fsnode);
      xmlFreeNode(fsnode);
    }
    rv = !removed;
    goto cleanup;
  }
  if(!fsnode) {
    void *vfs;
    nc_printf(ncct, "Cannot find filterset '%s'\n", argv[0]);
    LOCKFS();
    if(mtev_hash_retrieve(filtersets, argv[0], strlen(argv[0]), &vfs)) {
      UNLOCKFS();
      nc_printf(ncct, "filter of the same name already exists\n");
      goto cleanup;
    }
    UNLOCKFS();
    /* Fine the parent path */
    fsnode = xmlNewNode(NULL, (xmlChar *)"filterset");
    xmlSetProp(fsnode, (xmlChar *)"name", (xmlChar *)argv[0]);
    xmlAddChild(parent, fsnode);
    nc_printf(ncct, "created new filterset\n");
  }

  if(info) {
    char *xmlpath = NULL;
    free(info->path);
    xmlpath = (char *)xmlGetNodePath(fsnode);
    info->path = strdup(xmlpath + strlen("/noit"));
    free(xmlpath);
    strlcpy(info->filter_name, argv[0], sizeof(info->filter_name));
    if(state) {
      mtev_console_state_push_state(ncct, state);
      mtev_console_state_init(ncct);
    }
  }
 cleanup:
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

int
noit_filtersets_cull_unused() {
  mtev_hash_table active = MTEV_HASH_EMPTY;
  char *buffer = NULL;
  mtev_conf_section_t *declares;
  int i, n_uses = 0, n_declares = 0, removed = 0;
  const char *declare_xpath = "//filterset[@name and not (@cull='false')]";

  declares = mtev_conf_get_sections(NULL, declare_xpath, &n_declares);
  if(declares) {
    /* store all unit filtersets used */
    for(i=0;i<n_declares;i++) {
      if(!buffer) buffer = malloc(128);
      if(mtev_conf_get_stringbuf(declares[i], "@name", buffer, 128)) {
        if(mtev_hash_store(&active, buffer, strlen(buffer), declares[i])) {
          buffer = NULL;
        }
        else {
          void *vnode = NULL;
          /* We've just hit a duplicate.... check to see if there's an existing
           * entry and if there is, load the latest one and delete the old
           * one. */
          mtev_hash_retrieve(&active, buffer, strlen(buffer), &vnode);
          if (vnode) {
            noit_filter_compile_add(declares[i]);
            CONF_REMOVE(vnode);
            xmlUnlinkNode(vnode);
            xmlFreeNode(vnode);
            removed++;
            if(mtev_hash_replace(&active, buffer, strlen(buffer), declares[i], free, NULL)) {
              buffer = NULL;
            }
          }
        }
      }
    }
    if(buffer) free(buffer);
    free(declares);
  }

  n_uses = noit_poller_do(filterset_accum, &active);

  if(n_uses > 0 && mtev_hash_size(&active) > 0) {
    mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
    const char *filter_name;
    int filter_name_len;
    void *vnode;
    while(mtev_hash_next(&active, &iter, &filter_name, &filter_name_len,
                         &vnode)) {
      if(noit_filter_remove(vnode)) {
        CONF_REMOVE(vnode);
        xmlUnlinkNode(vnode);
        xmlFreeNode(vnode);
        removed++;
      }
    }
  }

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
  rv = noit_filtersets_cull_unused();
  nc_printf(ncct, "Culled %d unused filtersets\n", rv);
  return 0;
}
static void
register_console_filter_commands() {
  mtev_console_state_t *tl, *filterset_state, *nostate;
  cmd_info_t *confcmd, *conf_t_cmd, *no_cmd;

  tl = mtev_console_state_initial();
  confcmd = mtev_console_state_get_cmd(tl, "configure");
  assert(confcmd && confcmd->dstate);

  conf_t_cmd = mtev_console_state_get_cmd(confcmd->dstate, "terminal");
  assert(conf_t_cmd && conf_t_cmd->dstate);

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
  assert(no_cmd && no_cmd->dstate);
  mtev_console_state_add_cmd(no_cmd->dstate,
    NCSCMD("filterset", noit_console_filter_configure, NULL, NULL, (void *)1));
}

void
noit_filters_init() {
  const char *error;
  int erroffset;
  pthread_mutex_init(&filterset_lock, NULL);
  filtersets = calloc(1, sizeof(mtev_hash_table));
  fallback_no_match = pcre_compile("^(?=a)b", 0, &error, &erroffset, NULL);
  if(!fallback_no_match) {
    mtevL(noit_error, "Filter initialization failed (nomatch filter)\n");
    exit(-1);
  }
  mtev_capabilities_add_feature("filterset:hash", NULL);
  register_console_filter_commands();
  noit_filters_from_conf();
}
