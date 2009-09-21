/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
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

#include "noit_defines.h"

#include "utils/noit_hash.h"
#include "utils/noit_atomic.h"
#include "noit_conf.h"
#include "noit_check.h"
#include "noit_conf_checks.h"
#include "noit_filters.h"

#include <pcre.h>
#include <assert.h>
#include <libxml/tree.h>

static noit_hash_table *filtersets = NULL;
static pthread_mutex_t filterset_lock;
#define LOCKFS() pthread_mutex_lock(&filterset_lock)
#define UNLOCKFS() pthread_mutex_unlock(&filterset_lock)

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
  noit_atomic32_t ref_cnt;
  char *name;
  filterrule_t *rules;
} filterset_t;

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
  if(noit_atomic_dec32(&fs->ref_cnt) != 0) return;
  noitL(noit_error, "Freeing filterset [%d]: %s\n", fs->ref_cnt, fs->name);
  while(fs->rules) {
    r = fs->rules->next;
    filterrule_free(fs->rules);
    fs->rules = r;
  }
  if(fs->name) free(fs->name);
  free(fs);
}
void
noit_filter_compile_add(noit_conf_section_t setinfo) {
  noit_conf_section_t *rules;
  int j, fcnt;
  char filterset_name[256];
  filterset_t *set;
  if(!noit_conf_get_stringbuf(setinfo, "@name",
                              filterset_name, sizeof(filterset_name))) {
    noitL(noit_error,
          "filterset with no name, skipping as it cannot be referenced.\n");
    return;
  }
  set = calloc(1, sizeof(*set));
  set->ref_cnt = 1;
  set->name = strdup(filterset_name);

  rules = noit_conf_get_sections(setinfo, "rule", &fcnt);
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
  LOCKFS();
  noit_hash_replace(filtersets, set->name, strlen(set->name), (void *)set,
                    NULL, filterset_free);
  UNLOCKFS();
}
int
noit_filter_exists(const char *name) {
  int removed;
  void *v;
  LOCKFS();
  removed = noit_hash_retrieve(filtersets, name, strlen(name), &v);
  UNLOCKFS();
  return removed;
}
int
noit_filter_remove(noit_conf_section_t vnode) {
  int removed;
  xmlNodePtr node = vnode;
  char *name = (char *)node->name;
  LOCKFS();
  removed = noit_hash_delete(filtersets, name, strlen(name),
                             NULL, filterset_free);
  UNLOCKFS();
  return removed;
}
void
noit_filters_from_conf() {
  noit_conf_section_t *sets;
  int i, cnt;

  sets = noit_conf_get_sections(NULL, "/noit/filtersets//filterset", &cnt);
  for(i=0; i<cnt; i++) {
    noit_filter_compile_add(sets[i]);
  }
  free(sets);
}

void
noit_refresh_filtersets(noit_console_closure_t ncct,
                        noit_conf_t_userdata_t *info) {
  noit_filters_from_conf();
  nc_printf(ncct, "Reloaded %d filtersets.\n",
            filtersets ? filtersets->size : 0);
}

static noit_boolean
noit_apply_filterrule(pcre *p, pcre_extra *pe, const char *subj) {
  int rc, ovector[30];
  if(!p) return noit_true;
  rc = pcre_exec(p, pe, subj, strlen(subj), 0, 0, ovector, 30);
  if(rc >= 0) return noit_true;
  return noit_false;
}
noit_boolean
noit_apply_filterset(const char *filterset,
                     noit_check_t *check,
                     metric_t *metric) {
  /* We pass in filterset here just in case someone wants to apply
   * a filterset other than check->filterset.. You never know.
   */
  void *vfs;
  if(!filtersets || !filterset) return noit_true;

  LOCKFS();
  if(noit_hash_retrieve(filtersets, filterset, strlen(filterset), &vfs)) {
    filterset_t *fs = (filterset_t *)vfs;
    filterrule_t *r;
    noit_atomic_inc32(&fs->ref_cnt);
    UNLOCKFS();
#define MATCHES(rname, value) noit_apply_filterrule(r->rname, r->rname##_e, value)
    for(r = fs->rules; r; r = r->next) {
      if(MATCHES(target, check->target) &&
         MATCHES(module, check->module) &&
         MATCHES(name, check->name) &&
         MATCHES(metric, metric->metric_name))
        return (r->type == NOIT_FILTER_ACCEPT) ? noit_true : noit_false;
    }
    filterset_free(fs);
  }
  UNLOCKFS();
  return noit_true;
}

static char *
conf_t_filterset_prompt(EditLine *el) {
  noit_console_closure_t ncct;
  noit_conf_t_userdata_t *info;
  static char *tl = "noit(conf)# ";
  static char *pfmt = "noit(conf:filterset:%.*s%s)# ";
  int max_space, namelen;
  el_get(el, EL_USERDATA, (void *)&ncct);
  if(!ncct) return tl;
  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
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
noit_console_filter_show(noit_console_closure_t ncct,
                         int argc, char **argv,
                         noit_console_state_t *state,
                         void *closure) {
  noit_conf_t_userdata_t *info;
  char xpath[1024];
  xmlNodePtr fsnode;
  noit_conf_section_t *rules;
  int i, rulecnt;

  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
  snprintf(xpath, sizeof(xpath), "/%s",
           info->path);
  fsnode = noit_conf_get_section(NULL, xpath);
  if(!fsnode) {
    nc_printf(ncct, "internal error\n");
    return -1;
  }
  rules = noit_conf_get_sections(fsnode, "rule", &rulecnt);
  for(i=0; i<rulecnt; i++) {
    char val[256];
    val[0] = '\0';
    noit_conf_get_stringbuf(rules[i], "@type", val, sizeof(val));
    nc_printf(ncct, "Rule %d [%s]:\n", i+1, val);
#define DUMP_ATTR(a) do { \
  if(noit_conf_get_stringbuf(rules[i], "@" #a, val, sizeof(val))) { \
    nc_printf(ncct, "\t%s: /%s/\n", #a, val); \
  } \
} while(0)
    DUMP_ATTR(target);
    DUMP_ATTR(module);
    DUMP_ATTR(name);
    DUMP_ATTR(metric);
  }
  if(rules) free(rules);
  return 0;
}
static int
noit_console_rule_configure(noit_console_closure_t ncct,
                            int argc, char **argv,
                            noit_console_state_t *state,
                            void *closure) {
  xmlNodePtr fsnode = NULL;
  noit_conf_t_userdata_t *info;
  char xpath[1024];

  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
  snprintf(xpath, sizeof(xpath), "/%s",
           info->path);
  fsnode = noit_conf_get_section(NULL, xpath);
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
    byebye = noit_conf_get_section(fsnode, xpath);
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
      add_arg = noit_conf_get_section(fsnode, xpath);
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
              strcmp(argv[i], "name") && strcmp(argv[i], "metric")) {
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
    noit_filter_compile_add((noit_conf_section_t *)fsnode);
  }
  return 0;
}
static int
noit_console_filter_configure(noit_console_closure_t ncct,
                              int argc, char **argv,
                              noit_console_state_t *state,
                              void *closure) {
  xmlNodePtr parent, fsnode = NULL;
  int rv = -1;
  noit_conf_t_userdata_t *info;
  char xpath[1024];

  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
  if(!info ||
     (strncmp(info->path, "/filtersets/", strlen("/filtersets/")) &&
      strcmp(info->path, "/filtersets"))) {
    nc_printf(ncct, "filterset only allows inside /filtersets (not %s)\n",
              info->path);
    goto cleanup;
  }
  if(argc != 1) {
    nc_printf(ncct, "filterset requires one argument\n");
    goto cleanup;
  }
  snprintf(xpath, sizeof(xpath), "/%s", info->path);
  parent = noit_conf_get_section(NULL, xpath);
  if(!parent) {
    nc_printf(ncct, "internal error, can't final current working path\n");
    goto cleanup;
  }
  snprintf(xpath, sizeof(xpath), "filterset[@name=\"%s\"]", argv[0]);
  fsnode = noit_conf_get_section(parent, xpath);
  if(closure) {
    int removed;
    removed = noit_filter_remove(fsnode);
    nc_printf(ncct, "%sremoved filterset '%s'\n",
              removed ? "" : "failed to ", argv[0]);
    if(removed) {
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
    if(noit_hash_retrieve(filtersets, argv[0], strlen(argv[0]), &vfs)) {
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
    if(info->path) free(info->path);
    info->path = strdup((char *)xmlGetNodePath(fsnode) + strlen("/noit"));
    strlcpy(info->filter_name, argv[0], sizeof(info->filter_name));
    if(state) {
      noit_console_state_push_state(ncct, state);
      noit_console_state_init(ncct);
    }
  }
 cleanup:
  return rv;
}

static void
register_console_filter_commands() {
  noit_console_state_t *tl, *filterset_state, *nostate;
  cmd_info_t *confcmd, *conf_t_cmd, *no_cmd;

  tl = noit_console_state_initial();
  confcmd = noit_console_state_get_cmd(tl, "configure");
  assert(confcmd && confcmd->dstate);

  conf_t_cmd = noit_console_state_get_cmd(confcmd->dstate, "terminal");
  assert(conf_t_cmd && conf_t_cmd->dstate);

  nostate = noit_console_state_alloc();
  noit_console_state_add_cmd(nostate,
    NCSCMD("rule", noit_console_rule_configure, NULL, NULL, (void *)1));

  filterset_state = noit_console_state_alloc();
  noit_console_state_add_cmd(filterset_state,
    NCSCMD("exit", noit_console_config_cd, NULL, NULL, ".."));
  noit_console_state_add_cmd(filterset_state,
    NCSCMD("status", noit_console_filter_show, NULL, NULL, NULL));
  noit_console_state_add_cmd(filterset_state,
    NCSCMD("rule", noit_console_rule_configure, NULL, NULL, NULL));
  noit_console_state_add_cmd(filterset_state,
    NCSCMD("no", noit_console_state_delegate, noit_console_opt_delegate,
           nostate, NULL));

  filterset_state->console_prompt_function = conf_t_filterset_prompt;

  noit_console_state_add_cmd(conf_t_cmd->dstate,
    NCSCMD("filterset", noit_console_filter_configure,
           NULL, filterset_state, NULL));

  no_cmd = noit_console_state_get_cmd(conf_t_cmd->dstate, "no");
  assert(no_cmd && no_cmd->dstate);
  noit_console_state_add_cmd(no_cmd->dstate,
    NCSCMD("filterset", noit_console_filter_configure, NULL, NULL, (void *)1));
}

void
noit_filters_init() {
  pthread_mutex_init(&filterset_lock, NULL);
  filtersets = calloc(1, sizeof(noit_hash_table));
  register_console_filter_commands();
  noit_filters_from_conf();
}
