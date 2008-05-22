/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include "utils/noit_hash.h"
#include "noit_conf.h"
#include "noit_check.h"
#include "noit_filters.h"

#include <pcre.h>

static noit_hash_table *filtersets = NULL;

typedef enum { NOIT_FILTER_ACCEPT, NOIT_FILTER_DENY } noit_ruletype_t;
typedef struct _filterrule {
  noit_ruletype_t type;
  pcre *target;
  pcre_extra *target_e;
  pcre *module;
  pcre_extra *module_e;
  pcre *name;
  pcre_extra *name_e;
  pcre *metric;
  pcre_extra *metric_e;
  struct _filterrule *next;
} filterrule_t;

typedef struct {
  char *name;
  filterrule_t *rules;
} filterset_t;

void
noit_refresh_filtersets(noit_console_closure_t ncct,
                        noit_conf_t_userdata_t *info) {
  noit_filters_init();
  nc_printf(ncct, "Reloaded %d filtersets.\n",
            filtersets ? filtersets->size : 0);
}

#define FRF(r,a) do { \
  if(r->a) pcre_free(r->a); \
  if(r->a##_e) pcre_free(r->a##_e); \
} while(0)

static void
filterrule_free(void *vp) {
  filterrule_t *r = vp;
  FRF(r,target);
  FRF(r,module);
  FRF(r,name);
  FRF(r,metric);
  free(r);
}
static void
filterset_free(void *vp) {
  filterset_t *fs = vp;
  filterrule_t *r;
  while(fs->rules) {
    r = fs->rules->next;
    filterrule_free(fs->rules);
    fs->rules = r;
  }
  if(fs->name) free(fs->name);
  free(fs);
}
void
noit_filters_init() {
  noit_hash_table *newsets = NULL, *cleanup;
  noit_conf_section_t *sets;
  int i, cnt;

  sets = noit_conf_get_sections(NULL, "/noit/filtersets//filterset", &cnt);
  newsets = calloc(1, sizeof(noit_hash_table));
  for(i=0; i<cnt; i++) {
    noit_conf_section_t *rules;
    int j, fcnt;
    char filterset_name[256];
    filterset_t *set;
    if(!noit_conf_get_stringbuf(sets[i], "@name",
                                filterset_name, sizeof(filterset_name))) {
      noitL(noit_error,
            "filterset with no name, skipping as it cannot be referenced.\n");
      continue;
    }
    set = calloc(1, sizeof(*set));
    set->name = strdup(filterset_name);

    rules = noit_conf_get_sections(sets[i], "rule", &fcnt);
    /* Here we will work through the list backwards pushing the rules on
     * the front of the list.  That way we can simply walk them in order
     * for the application process.
     */
    noitL(noit_debug, "Compiling filterset '%s'\n", set->name);
    for(j=fcnt-1; j>=0; j--) {
      filterrule_t *rule;
      char buffer[256];
      if(!noit_conf_get_stringbuf(rules[j], "@type", buffer, sizeof(buffer)) ||
         (strcmp(buffer, "accept") && strcmp(buffer, "deny"))) {
        noitL(noit_error, "rule must have type 'accept' or 'deny'\n");
        continue;
      }
      noitL(noit_debug, "Prepending %s into %s\n", buffer, set->name);
      rule = calloc(1, sizeof(*rule));
      rule->type = (!strcmp(buffer, "accept")) ?
                     NOIT_FILTER_ACCEPT : NOIT_FILTER_DENY;

      /* Compile our rules */
#define RULE_COMPILE(rname) do { \
  if(noit_conf_get_stringbuf(rules[j], "@" #rname, buffer, sizeof(buffer))) { \
    const char *error; \
    int erroffset; \
    rule->rname = pcre_compile(buffer, 0, &error, &erroffset, NULL); \
    if(!rule->rname) { \
      noitL(noit_error, "set '%s' rule '%s: %s' compile failed: %s\n", \
            set->name, #rname, buffer, error ? error : "???"); \
    } \
    else { \
      rule->rname##_e = pcre_study(rule->rname, 0, &error); \
    } \
  } \
} while(0)

      RULE_COMPILE(target);
      RULE_COMPILE(module);
      RULE_COMPILE(name);
      RULE_COMPILE(metric);
      rule->next = set->rules;
      set->rules = rule;
    }
    free(rules);
    noit_hash_replace(newsets, set->name, strlen(set->name), (void *)set,
                      NULL, filterset_free);
  }
  free(sets);
  cleanup = filtersets;
  filtersets = newsets;
  if(cleanup) noit_hash_destroy(cleanup, NULL, filterset_free);
}

static noit_conf_boolean
noit_apply_filterrule(pcre *p, pcre_extra *pe, const char *subj) {
  int rc, ovector[30];
  if(!p) return noit_true;
  rc = pcre_exec(p, pe, subj, strlen(subj), 0, 0, ovector, 30);
  if(rc >= 0) return noit_true;
  return noit_false;
}
noit_conf_boolean
noit_apply_filterset(const char *filterset,
                     noit_check_t *check,
                     metric_t *metric) {
  /* We pass in filterset here just in case someone wants to apply
   * a filterset other than check->filterset.. You never know.
   */
  filterset_t *fs;
  if(!filtersets || !filterset) return noit_true;

  if(noit_hash_retrieve(filtersets, filterset, strlen(filterset),
                        (void **)&fs)) {
    filterrule_t *r;
#define MATCHES(rname, value) noit_apply_filterrule(r->rname, r->rname##_e, value)
    for(r = fs->rules; r; r = r->next) {
      if(MATCHES(target, check->target) &&
         MATCHES(module, check->module) &&
         MATCHES(name, check->name) &&
         MATCHES(metric, metric->metric_name))
        return (r->type == NOIT_FILTER_ACCEPT) ? noit_true : noit_false;
    }
  }
  return noit_true;
}


