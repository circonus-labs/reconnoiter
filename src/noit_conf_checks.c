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

#include <mtev_defines.h>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <inttypes.h>

#include <mtev_conf.h>
#include <mtev_conf_private.h>
#include <mtev_console.h>
#include <mtev_hash.h>
#include <mtev_log.h>
#include <mtev_memory.h>
#include <mtev_uuid.h>

#include "noit_filters.h"
#include "noit_conf_checks.h"
#include "noit_conf_checks_lmdb.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_clustering.h"

#define NCLOCK mtev_conf_section_t nc__lock = mtev_conf_get_section_write(MTEV_CONF_ROOT, "/noit")
#define NCUNLOCK mtev_conf_release_section_write(nc__lock)

static void register_console_config_check_commands();
static mtev_hash_table check_attrs;

static struct _valid_attr_t {
  const char *scope;
  const char *name;
  const char *xpath;
  int checks_fixate;
} valid_attrs[] = {
  { "/checks", "name", "@name", 0 },
  { "/checks", "target", "@target", 0 },
  { "/checks", "seq", "@seq", 0 },
  { "/checks", "period", "@period", 0 },
  { "/checks", "timeout", "@timeout", 0 },
  { "/checks", "oncheck", "@oncheck", 0 },
  { "/checks", "disable", "@disable", 0 },
  { "/checks", "resolve_rtype", "@resolve_rtype", 0 },
  { "/checks", "filterset", "@filterset", 0 },
  { "/checks", "module", "@module", 1 },
  { "/filtersets", "target", "@target", 0 },
  { "/filtersets", "module", "@module", 0 },
  { "/filtersets", "name", "@name", 0 },
  { "/filtersets", "metric", "@metric", 0 },
};

void
mtev_console_state_add_check_attrs(mtev_console_state_t *state,
                                   console_cmd_func_t f,
                                   const char *scope) {
  int i;
  for(i = 0;
      i < sizeof(valid_attrs)/sizeof(valid_attrs[0]);
      i++) {
    if(strcmp(valid_attrs[i].scope, scope)) continue;
    mtev_console_state_add_cmd(state,
      NCSCMD(valid_attrs[i].name, f, NULL,
             NULL, &valid_attrs[i]));
  }
}

void noit_console_conf_checks_init() {
  int i;
  for(i=0;i<sizeof(valid_attrs)/sizeof(*valid_attrs);i++) {
    mtev_hash_store(&check_attrs,
                    valid_attrs[i].name, strlen(valid_attrs[i].name),
                    &valid_attrs[i]);
  }
  register_console_config_check_commands();
}

int64_t
noit_conf_check_bump_seq(xmlNodePtr node) {
  int64_t seq = 0;
  xmlChar *seq_str;
  seq_str = xmlGetProp(node, (xmlChar *)"seq");
  if(!seq_str) return seq;
  seq = strtoll((const char *)seq_str, NULL, 10);
  if(seq != 0) {
    char new_seq_str[64];
    /* negatve -> 0, positive ++ */
    seq = (seq < 0) ? 0 : (seq + 1);
    snprintf(new_seq_str, sizeof(new_seq_str), "%" PRIi64, seq);
    xmlUnsetProp(node, (xmlChar *)"seq");
    xmlSetProp(node, (xmlChar *)"seq", (xmlChar *)new_seq_str);
  }
  xmlFree(seq_str);
  return seq;
}
static int
noit_console_mkcheck_xpath(char *xpath, int len,
                           mtev_conf_t_userdata_t *info,
                           const char *arg) {
  int rv;
  rv = noit_check_xpath(xpath, len, "/", arg);
  if(rv == -1) return -1;
  if(rv == 0) {
    char *path = (!info || !strcmp(info->path, "/")) ? "" : info->path;
    snprintf(xpath, len, "/noit%s%s%s[@uuid]",
             path, arg ? "/" : "", arg ? arg : "");
  }
  return 0;
}
static void
nc_attr_show(mtev_console_closure_t ncct, const char *name, xmlNodePtr cnode,
             xmlNodePtr anode, const char *value) {
  char *cpath, *apath;
  cpath = cnode ? (char *)xmlGetNodePath(cnode) : strdup("");
  apath = anode ? (char *)xmlGetNodePath(anode) : strdup("");
  nc_printf(ncct, " %s: %s", name, value ? value : "[undef]");
  if(value && cpath && apath) {
    int clen = strlen(cpath);
    int plen = strlen("/noit/checks/");
    if(!strncmp(cpath, apath, clen) && apath[clen] == '/') {
      /* we have a match, which means it isn't inherited */
    }
    else {
      nc_printf(ncct, " [inherited from %s]",
                strlen(apath) > plen ? apath + plen : apath);
    }
  }
  nc_write(ncct, "\n", 1);
  if(cpath) free(cpath);
  if(apath) free(apath);
}
static void 
refresh_subchecks(mtev_console_closure_t ncct,
                  mtev_conf_t_userdata_t *info) {
  char *path;
  char xpath[1024];
 
  path = info->path;
  if(!strcmp(path, "/")) path = "";

  /* The first one is just a process_checks, the second is the reload.
   * Reload does a lot of work and there is no need to do it twice.
   */
  snprintf(xpath, sizeof(xpath), "/noit/%s[@uuid]", path);
  noit_poller_process_checks(xpath);
  snprintf(xpath, sizeof(xpath), "/noit/%s//check[@uuid]", path);
  noit_poller_reload(xpath);
}
static int
noit_config_check_update_attrs(xmlNodePtr node, int argc, char **argv) {
  int i, error = 0;
  if(argc % 2) return -1;

  for(i=0; i<argc; i+=2) {
    void *vattrinfo;
    struct _valid_attr_t *attrinfo;
    char *attr = argv[i], *val = NULL;
    if(!strcasecmp(argv[i], "no")) attr = argv[i+1];
    else val = argv[i+1];
    if(!mtev_hash_retrieve(&check_attrs, attr, strlen(attr),
                           &vattrinfo)) {
      error = 1;
      break;
    }
    attrinfo = vattrinfo;
    /* The fixation stuff doesn't matter here, this check is brand-new */
    xmlUnsetProp(node, (xmlChar *)attrinfo->name);
    if(val)
      xmlSetProp(node, (xmlChar *)attrinfo->name, (xmlChar *)val);
    CONF_DIRTY(mtev_conf_section_from_xmlnodeptr(node));
    mtev_conf_mark_changed();
  }
  return error;
}

static int
noit_conf_mkcheck_under(const char *ppath, int argc, char **argv, uuid_t out) {
  int rv = -1;
  const char *path;
  char xpath[1024];
  xmlXPathContextPtr xpath_ctxt = NULL;
  xmlXPathObjectPtr pobj = NULL;
  xmlNodePtr node = NULL, newnode;

  NCLOCK;

  /* attr val [or] no attr (sets of two) */
  if(argc % 2) goto out;

  mtev_conf_xml_xpath(NULL, &xpath_ctxt);
  path = strcmp(ppath, "/") ? ppath : "";
  snprintf(xpath, sizeof(xpath), "/noit%s", path);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET ||
     xmlXPathNodeSetGetLength(pobj->nodesetval) != 1) {
    goto out;
  }
  node = xmlXPathNodeSetItem(pobj->nodesetval, 0);
  if((newnode = xmlNewChild(node, NULL, (xmlChar *)"check", NULL)) != NULL) {
    char outstr[37];
    mtev_uuid_generate(out);
    mtev_uuid_unparse_lower(out, outstr);
    xmlSetProp(newnode, (xmlChar *)"uuid", (xmlChar *)outstr);
    xmlSetProp(newnode, (xmlChar *)"disable", (xmlChar *)"true");

    /* No risk of running off the end (we checked this above) */
    if(noit_config_check_update_attrs(newnode, argc, argv)) {
      /* Something went wrong, remove the node */
      xmlUnlinkNode(newnode);
    }
    else {
      CONF_DIRTY(mtev_conf_section_from_xmlnodeptr(newnode));
      mtev_conf_mark_changed();
      rv = 0;
    }
  }
 out:
  if(pobj) xmlXPathFreeObject(pobj);
  NCUNLOCK;
  return rv;
}

static int
noit_console_check(mtev_console_closure_t ncct,
                   int argc, char **argv,
                   mtev_console_state_t *state, void *closure) {
  int cnt;
  mtev_conf_t_userdata_t *info;
  char xpath[1024], newuuid_str[37];
  char *uuid_conf = NULL, *wanted;
  uuid_t checkid;
  xmlXPathContextPtr xpath_ctxt = NULL;
  xmlXPathObjectPtr pobj = NULL;
  xmlNodePtr node = NULL;
  mtev_boolean creating_new = mtev_false;

  if(closure) {
    char *fake_argv[1] = { ".." };
    mtev_console_state_pop(ncct, 0, argv, NULL, NULL);
    mtev_console_config_cd(ncct, 1, fake_argv, NULL, NULL);
  }

  if(argc < 1) {
    nc_printf(ncct, "requires at least one argument\n");
    return -1;
  }
  if(argc % 2 == 0) {
    nc_printf(ncct, "wrong number of arguments\n");
    return -1;
  } 

  info = mtev_console_userdata_get(ncct, MTEV_CONF_T_USERDATA);
  wanted = strcmp(argv[0], "new") ? argv[0] : NULL;
  if(info && !wanted) {
    /* We are creating a new node */
    uuid_t out;
    creating_new = mtev_true;
    if(strncmp(info->path, "/checks/", strlen("/checks/")) &&
       strcmp(info->path, "/checks")) {
      nc_printf(ncct, "New checks must be under /checks/\n");
      return -1;
    }
    if(noit_conf_mkcheck_under(info->path, argc - 1, argv + 1, out)) {
      nc_printf(ncct, "Error creating new check\n");
      return -1;
    }
    newuuid_str[0] = '\0';
    mtev_uuid_unparse_lower(out, newuuid_str);
    wanted = newuuid_str;
  }
  /* We many not be in conf-t mode -- that's fine */
  if(noit_console_mkcheck_xpath(xpath, sizeof(xpath), info, wanted)) {
    nc_printf(ncct, "could not find check '%s'\n", wanted);
    return -1;
  }

  NCLOCK;
  mtev_conf_xml_xpath(NULL, &xpath_ctxt);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET ||
     xmlXPathNodeSetIsEmpty(pobj->nodesetval)) {
    nc_printf(ncct, "no checks found for '%s'\n", wanted);
    goto out;
  }
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  if(info && cnt != 1) {
    nc_printf(ncct, "Ambiguous check specified\n");
    goto out;
  }
  node = xmlXPathNodeSetItem(pobj->nodesetval, 0);
  uuid_conf = (char *)xmlGetProp(node, (xmlChar *)"uuid");
  if(!node || !uuid_conf || mtev_uuid_parse(uuid_conf, checkid)) {
    nc_printf(ncct, "%s has invalid or missing UUID!\n",
              (char *)xmlGetNodePath(node) + strlen("/noit"));
    goto out;
  }
  if(argc > 1 && !creating_new) {
    if(noit_config_check_update_attrs(node, argc - 1, argv + 1))
      nc_printf(ncct, "Partially successful, error setting some attributes\n");
    noit_conf_check_bump_seq(node);
  }

  if(info) {
    char *xmlpath;
    if(info->path) free(info->path);
    xmlpath = (char *)xmlGetNodePath(node);
    info->path = strdup(xmlpath + strlen("/noit"));
    free(xmlpath);
    mtev_uuid_copy(info->current_check, checkid);
    if(argc > 1) refresh_subchecks(ncct, info);
    if(state) {
      mtev_console_state_push_state(ncct, state);
      mtev_console_state_init(ncct);
    }
    goto out;
  }
 out:
  if(uuid_conf) free(uuid_conf);
  if(pobj) xmlXPathFreeObject(pobj);
  NCUNLOCK;
  return 0;
}
static int
noit_console_watch_check(mtev_console_closure_t ncct,
                         int argc, char **argv,
                         mtev_console_state_t *state, void *closure) {
  int i, cnt;
  int adding = (int)(intptr_t)closure;
  int period = 0;
  char xpath[1024];
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL;

  if (noit_check_get_lmdb_instance()) {
    return noit_conf_checks_lmdb_console_watch_check(ncct, argc, argv, state, closure);
  }

  if(argc < 1 || argc > 2) {
    nc_printf(ncct, "requires one or two arguments\n");
    return -1;
  }
  /* An alternate period */
  if(argc == 2) period = atoi(argv[1]);

  if(noit_console_mkcheck_xpath(xpath, sizeof(xpath), NULL, argv[0])) {
    nc_printf(ncct, "ERROR: could not find check '%s'\n", argv[0]);
    return -1;
  }

  NCLOCK;
  mtev_conf_xml_xpath(NULL, &xpath_ctxt);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET ||
     xmlXPathNodeSetIsEmpty(pobj->nodesetval)) {
    nc_printf(ncct, "no checks found\n");
    goto out;
  }
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  for(i=0; i<cnt; i++) {
    uuid_t checkid;
    noit_check_t *check;
    xmlNodePtr node;
    char *uuid_conf;

    node = xmlXPathNodeSetItem(pobj->nodesetval, i);
    uuid_conf = (char *)xmlGetProp(node, (xmlChar *)"uuid");
    if(!uuid_conf || mtev_uuid_parse(uuid_conf, checkid)) {
      nc_printf(ncct, "%s has invalid or missing UUID!\n",
                (char *)xmlGetNodePath(node) + strlen("/noit"));
      continue;
    }
    xmlFree(uuid_conf);
    if(adding) {
      check = noit_check_watch(checkid, period);
      /* This check must be watched from the console */
      noit_check_transient_add_feed(check, ncct->feed_path);
      /* Note the check */
      noit_check_log_check(check);
      /* kick it off, if it isn't running already */
      if(!NOIT_CHECK_LIVE(check)) noit_check_activate(check);
    }
    else {
      check = noit_check_get_watch(checkid, period);
      if(check) noit_check_transient_remove_feed(check, ncct->feed_path);
    }
  }
 out:
  if(pobj) xmlXPathFreeObject(pobj);
  NCUNLOCK;
  return 0;
}
static int
_qsort_string_compare(const void *i1, const void *i2) {
        const char *s1 = ((const char **)i1)[0];
        const char *s2 = ((const char **)i2)[0];
        return strcasecmp(s1, s2);
}
static void
nc_print_stat_metrics(mtev_console_closure_t ncct,
                      noit_check_t *check, stats_t *c) {
  int mcount=0, cnt=0;
  const char **sorted_keys;
  char buff[256];
  mtev_boolean filtered;
  mtev_hash_table *metrics;
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  const char *k;
  int klen;
  void *data;

  metrics = noit_check_stats_metrics(c);
  memset(&iter, 0, sizeof(iter));
  while(mtev_hash_next(metrics, &iter, &k, &klen, &data)) cnt++;
  sorted_keys = malloc(cnt * sizeof(*sorted_keys));
  memset(&iter, 0, sizeof(iter));
  while(mtev_hash_next(metrics, &iter, &k, &klen, &data)) {
    if(sorted_keys && mcount < cnt) sorted_keys[mcount++] = k;
    else {
      noit_stats_snprint_metric(buff, sizeof(buff), (metric_t *)data);
      filtered = !noit_apply_filterset(check->filterset, check, (metric_t *)data);
      nc_printf(ncct, "  %c%s\n", filtered ? '*' : ' ', buff);
    }
  }
  if(sorted_keys) {
    int j;
    qsort(sorted_keys, mcount, sizeof(*sorted_keys),
          _qsort_string_compare);
    for(j=0;j<mcount;j++) {
      if(mtev_hash_retrieve(metrics,
                            sorted_keys[j], strlen(sorted_keys[j]),
                            &data)) {
        noit_stats_snprint_metric(buff, sizeof(buff), (metric_t *)data);
        filtered = !noit_apply_filterset(check->filterset, check, (metric_t *)data);
        nc_printf(ncct, "  %c%s\n", filtered ? '*' : ' ', buff);
      }
    }
    free(sorted_keys);
  }
}
void
noit_console_get_running_stats(mtev_console_closure_t ncct,
                               uuid_t checkid) {
  noit_check_t *check;

  check = noit_poller_lookup(checkid);
  if(!check) {
    nc_printf(ncct, " ERROR: not in running system\n");
  }
  else {
    int idx = 0;
    stats_t *c;
    struct timeval *whence;
    mtev_hash_table *metrics;
    nc_printf(ncct, " target_ip: %s\n", check->target_ip);
    nc_printf(ncct, " currently: %08x ", check->flags);
    if(NOIT_CHECK_RUNNING(check)) { nc_printf(ncct, "running"); idx++; }
    if(NOIT_CHECK_KILLED(check)) nc_printf(ncct, "%skilled", idx++?",":"");
    if(!NOIT_CHECK_CONFIGURED(check)) nc_printf(ncct, "%sunconfig", idx++?",":"");
    if(NOIT_CHECK_DISABLED(check)) nc_printf(ncct, "%sdisabled", idx++?",":"");
    if(NOIT_CHECK_DELETED(check)) nc_printf(ncct, "%sdeleted", idx++?",":"");
    if(!idx) nc_printf(ncct, "idle");
    nc_write(ncct, "\n", 1);
    if(mtev_cluster_enabled()) {
      mtev_cluster_node_t *where = NULL;
      mtev_boolean mine = noit_should_run_check(check, &where);
      nc_printf(ncct, " clustered running on %s%s\n",
                where ? mtev_cluster_node_get_cn(where) : "...",
                mine ? " (locally)" : "");
    }
    if(NOIT_CHECK_DELETED(check)) {
      /* Nothing more to do here */
      return;
    }

    if (check->fire_event != NULL) {
      struct timeval then, now, diff;
      mtev_gettimeofday(&now, NULL);
      then = eventer_get_whence(check->fire_event);
      sub_timeval(then, now, &diff);
      nc_printf(ncct, " next run: %0.3f seconds\n",
                diff.tv_sec + (diff.tv_usec / 1000000.0));
    }
    else {
      nc_printf(ncct, " next run: unscheduled\n");
    }

    c = noit_check_get_stats_current(check);
    whence = noit_check_stats_whence(c, NULL);
    if(whence->tv_sec == 0) {
      nc_printf(ncct, " last run: never\n");
    }
    else {
      const char *status;
      struct timeval now, *then, diff;
      mtev_gettimeofday(&now, NULL);
      then = noit_check_stats_whence(c, NULL);
      sub_timeval(now, *then, &diff);
      nc_printf(ncct, " last run: %0.3f seconds ago\n",
                diff.tv_sec + (diff.tv_usec / 1000000.0));
      nc_printf(ncct, " availability/state: %s/%s\n",
                noit_check_available_string(noit_check_stats_available(c, NULL)),
                noit_check_state_string(noit_check_stats_state(c, NULL)));
      status = noit_check_stats_status(c, NULL);
      nc_printf(ncct, " status: %s\n", status);
      nc_printf(ncct, " feeds: %d\n", check->feeds ? mtev_skiplist_size(check->feeds) : 0);
    }

    mtev_memory_begin();
    c = noit_check_get_stats_inprogress(check);
    metrics = noit_check_stats_metrics(c);
    if(mtev_hash_size(metrics) > 0) {
      nc_printf(ncct, " metrics (inprogress):\n");
      nc_print_stat_metrics(ncct, check, c);
    }
    c = noit_check_get_stats_current(check);
    metrics = noit_check_stats_metrics(c);
    if(mtev_hash_size(metrics)) {
      nc_printf(ncct, " metrics (current):\n");
      nc_print_stat_metrics(ncct, check, c);
    }
    c = noit_check_get_stats_previous(check);
    metrics = noit_check_stats_metrics(c);
    if(mtev_hash_size(metrics) > 0) {
      nc_printf(ncct, " metrics (previous):\n");
      nc_print_stat_metrics(ncct, check, c);
    }
    mtev_memory_end();
    noit_check_deref(check);
  }
}
static int
noit_console_show_check(mtev_console_closure_t ncct,
                        int argc, char **argv,
                        mtev_console_state_t *state, void *closure) {
  int i, cnt;
  mtev_conf_t_userdata_t *info;
  char xpath[1024];
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL;

  if (noit_check_get_lmdb_instance()) {
    return noit_conf_checks_lmdb_console_show_check(ncct, argc, argv, state, closure);
  }

  if(argc > 1) {
    nc_printf(ncct, "requires zero or one arguments\n");
    return -1;
  }

  info = mtev_console_userdata_get(ncct, MTEV_CONF_T_USERDATA);
  /* We many not be in conf-t mode -- that's fine */
  if(noit_console_mkcheck_xpath(xpath, sizeof(xpath), info,
                                argc ? argv[0] : NULL)) {
    nc_printf(ncct, "could not find check '%s'\n", argv[0]);
    return -1;
  }

  NCLOCK;
  mtev_conf_xml_xpath(NULL, &xpath_ctxt);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET ||
     xmlXPathNodeSetIsEmpty(pobj->nodesetval)) {
    nc_printf(ncct, "no checks found\n");
    goto out;
  }
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  if(info && cnt != 1) {
    nc_printf(ncct, "Ambiguous check specified\n");
    goto out;
  }
  for(i=0; i<cnt; i++) {
    mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
    const char *k;
    int klen;
    void *data;
    uuid_t checkid;
    mtev_hash_table *config;
    xmlNodePtr node, anode, mnode = NULL;
    mtev_conf_section_t section;
    char *uuid_conf;
    char *module, *value;

    node = xmlXPathNodeSetItem(pobj->nodesetval, i);
    section = mtev_conf_section_from_xmlnodeptr(node);
    uuid_conf = (char *)xmlGetProp(node, (xmlChar *)"uuid");
    if(!uuid_conf || mtev_uuid_parse(uuid_conf, checkid)) {
      nc_printf(ncct, "%s has invalid or missing UUID!\n",
                (char *)xmlGetNodePath(node) + strlen("/noit"));
      continue;
    }
    nc_printf(ncct, "==== %s ====\n", uuid_conf);
    xmlFree(uuid_conf);

#define MYATTR(a,n,b) _mtev_conf_get_string(section, &(n), "@" #a, &(b))
#define INHERIT(a,n,b) \
  _mtev_conf_get_string(section, &(n), "ancestor-or-self::node()/@" #a, &(b))
#define SHOW_ATTR(a) do { \
  anode = NULL; \
  value = NULL; \
  INHERIT(a, anode, value); \
  nc_attr_show(ncct, #a, node, anode, value); \
  if(value != NULL) free(value); \
} while(0)

    if(!INHERIT(module, mnode, module)) module = NULL;
    if(MYATTR(name, anode, value)) {
      nc_printf(ncct, " name: %s\n", value);
      free(value);
    }
    else
      nc_printf(ncct, " name: %s [from module]\n", module ? module : "[undef]");
    nc_attr_show(ncct, "module", node, mnode, module);
    if(module) free(module);
    SHOW_ATTR(target);
    SHOW_ATTR(seq);
    SHOW_ATTR(resolve_rtype);
    SHOW_ATTR(period);
    SHOW_ATTR(timeout);
    SHOW_ATTR(oncheck);
    SHOW_ATTR(filterset);
    SHOW_ATTR(disable);
    /* Print out all the config settings */
    config = mtev_conf_get_hash(section, "config");
    while(mtev_hash_next(config, &iter, &k, &klen, &data)) {
      nc_printf(ncct, " config::%s: %s\n", k, (const char *)data);
    }
    mtev_hash_destroy(config, free, free);
    free(config);
    noit_console_get_running_stats(ncct, checkid);
  }
 out:
  if(pobj) xmlXPathFreeObject(pobj);
  NCUNLOCK;
  return 0;
}
static int
noit_console_config_nocheck(mtev_console_closure_t ncct,
                            int argc, char **argv,
                            mtev_console_state_t *state, void *closure) {
  int i, cnt;
  const char *err = "internal error";
  mtev_conf_t_userdata_t *info;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL;
  char xpath[1024];
  uuid_t checkid;

  if(argc < 1) {
    nc_printf(ncct, "requires one argument\n");
    return -1;
  }

  info = mtev_console_userdata_get(ncct, MTEV_CONF_T_USERDATA);
  if(noit_console_mkcheck_xpath(xpath, sizeof(xpath), info, argv[0])) {
    nc_printf(ncct, "could not find check '%s'\n", argv[0]);
    return -1;
  }

  NCLOCK;
  mtev_conf_xml_xpath(NULL, &xpath_ctxt);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET ||
     xmlXPathNodeSetIsEmpty(pobj->nodesetval)) {
    err = "no checks found";
    goto bad;
  }
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  mtev_boolean removed = mtev_false;
  for(i=0; i<cnt; i++) {
    xmlNodePtr node;
    char *uuid_conf;
    node = xmlXPathNodeSetItem(pobj->nodesetval, i);
    uuid_conf = (char *)xmlGetProp(node, (xmlChar *)"uuid");
    if(!uuid_conf || mtev_uuid_parse(uuid_conf, checkid)) {
      nc_printf(ncct, "%s has invalid or missing UUID!\n",
                (char *)xmlGetNodePath(node) + strlen("/noit"));
    }
    else {
      if(argc > 1) {
        int j;
        for(j=1;j<argc;j++)
          xmlUnsetProp(node, (xmlChar *)argv[j]);
        noit_conf_check_bump_seq(node);
        CONF_DIRTY(mtev_conf_section_from_xmlnodeptr(node));
      } else {
        nc_printf(ncct, "descheduling %s\n", uuid_conf);
        if(noit_poller_deschedule(checkid, mtev_true, mtev_false)) {
          CONF_REMOVE(mtev_conf_section_from_xmlnodeptr(node));
          xmlUnlinkNode(node);
          removed = mtev_true;
        }
        else {
          xmlSetProp(node, (xmlChar *)"deleted", (xmlChar *)"deleted");
          noit_conf_check_bump_seq(node);
          CONF_DIRTY(mtev_conf_section_from_xmlnodeptr(node));
        }
      }
      mtev_conf_mark_changed();
    }
    xmlFree(uuid_conf);
  }
  if(!removed) {
    noit_poller_process_checks(xpath);
    noit_poller_reload(xpath);
  }
  nc_printf(ncct, "rebuilding causal map...\n");
  noit_poller_make_causal_map();
  if(pobj) xmlXPathFreeObject(pobj);
  NCUNLOCK;
  return 0;
 bad:
  if(pobj) xmlXPathFreeObject(pobj);
  nc_printf(ncct, "%s\n", err);
  NCUNLOCK;
  return -1;
}
static int
noit_console_config_show(mtev_console_closure_t ncct,
                         int argc, char **argv,
                         mtev_console_state_t *state, void *closure) {
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  const char *k;
  int klen;
  void *data;
  int i, cnt, titled = 0, cliplen = 0;
  const char *path = "", *basepath = NULL;
  char xpath[1024];
  mtev_conf_t_userdata_t *info = NULL;
  mtev_hash_table *config;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL, current_ctxt;
  xmlDocPtr master_config = NULL;
  xmlNodePtr node = NULL;

  if(argc > 1) {
    nc_printf(ncct, "too many arguments\n");
    return -1;
  }

  info = mtev_console_userdata_get(ncct, MTEV_CONF_T_USERDATA);
  if(info && info->path) path = basepath = info->path;
  if(!info && argc == 0) {
    nc_printf(ncct, "argument required when not in configuration mode\n");
    return -1;
  }

  if(argc == 1) path = argv[0];
  if(!basepath) basepath = path;

  /* { / } is a special case */
  if(!strcmp(basepath, "/")) basepath = "";
  if(!strcmp(path, "/")) path = "";


  NCLOCK;
  mtev_conf_xml_xpath(&master_config, &xpath_ctxt);
  if(!master_config) {
    NCUNLOCK;
    nc_printf(ncct, "no config\n");
    return -1;
  }

  /* { / } is the only path that will end with a /
   * in XPath { / / * } means something _entirely different than { / * }
   * Ever notice how it is hard to describe xpath in C comments?
   */
  /* We don't want to show the root node */
  cliplen = strlen("/noit/");

  /* If we are in configuration mode
   * and we are without an argument or the argument is absolute,
   * clip the current path off */
  if(info && (argc == 0 || path[0] != '/')) cliplen += strlen(basepath);
  if(!path[0] || path[0] == '/') /* base only, or absolute path requested */
    snprintf(xpath, sizeof(xpath), "/noit%s/@*", path);
  else
    snprintf(xpath, sizeof(xpath), "/noit%s/%s/@*", basepath, path);

  current_ctxt = xpath_ctxt;
  pobj = xmlXPathEval((xmlChar *)xpath, current_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET) {
    nc_printf(ncct, "no such object\n");
    goto bad;
  }
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  titled = 0;
  for(i=0; i<cnt; i++) {
    node = xmlXPathNodeSetItem(pobj->nodesetval, i);
    if(!strcmp((char *)node->name, "check")) continue;
    if(node->children && node->children == xmlGetLastChild(node) &&
      xmlNodeIsText(node->children)) {
      char *node_str, *xmlpath;
      node_str = (char *)xmlXPathCastNodeToString(node->children);
      xmlpath = (char *)xmlGetNodePath(node);
      if(!titled++) nc_printf(ncct, "== Section Settings ==\n");
      nc_printf(ncct, "%s: %s\n", xmlpath + cliplen, node_str);
      free(xmlpath);
      free(node_str);
    }
  }
  xmlXPathFreeObject(pobj);

  /* Print out all the config settings */
  if(!path[0] || path[0] == '/') /* base only, or absolute path requested */
    snprintf(xpath, sizeof(xpath), "/noit%s", path);
  else
    snprintf(xpath, sizeof(xpath), "/noit%s/%s", basepath, path);
  pobj = xmlXPathEval((xmlChar *)xpath, current_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET) {
    nc_printf(ncct, "no such object\n");
    goto bad;
  }
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  if(cnt > 0) {
    node = xmlXPathNodeSetItem(pobj->nodesetval, 0);
    titled = 0;
    config = mtev_conf_get_hash(mtev_conf_section_from_xmlnodeptr(node), "config");
    while(mtev_hash_next(config, &iter, &k, &klen, &data)) {
      if(!titled++) nc_printf(ncct, "== Section [Aggregated] Config ==\n");
      nc_printf(ncct, "config::%s: %s\n", k, (const char *)data);
    }
    mtev_hash_destroy(config, free, free);
    free(config);
  }
  xmlXPathFreeObject(pobj);

  /* _shorten string_ turning last { / @ * } to { / * } */
  if(!path[0] || path[0] == '/') /* base only, or absolute path requested */
    snprintf(xpath, sizeof(xpath), "/noit%s/*", path);
  else
    snprintf(xpath, sizeof(xpath), "/noit%s/%s/*", basepath, path);
  pobj = xmlXPathEval((xmlChar *)xpath, current_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET) {
    nc_printf(ncct, "no such object\n");
    goto bad;
  }
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  titled = 0;
  for(i=0; i<cnt; i++) {
    char *xmlpath;
    node = xmlXPathNodeSetItem(pobj->nodesetval, i);
    if(!strcmp((char *)node->name, "check")) continue;
    if(!strcmp((char *)node->name, "filterset")) continue;
    xmlpath = (char *)xmlGetNodePath(node);
    if(strcmp(xmlpath + cliplen, "config")) {
      if(!(node->children && node->children == xmlGetLastChild(node) &&
           xmlNodeIsText(node->children))) {
        if(!titled++) nc_printf(ncct, "== Subsections ==\n");
        nc_printf(ncct, "%s\n", xmlpath + cliplen);
      }
    }
    free(xmlpath);
  }

  titled = 0;
  for(i=0; i<cnt; i++) {
    node = xmlXPathNodeSetItem(pobj->nodesetval, i);
    if(!strcmp((char *)node->name, "filterset")) {
      xmlAttr *attr;
      char *filter_name = NULL;
      for(attr=node->properties; attr; attr = attr->next) {
        if(!strcmp((char *)attr->name, "name"))
          filter_name = (char *)xmlXPathCastNodeToString(attr->children);
      }
      if(filter_name) {
        nc_printf(ncct, "filterset[@name=\"%s\"]\n", filter_name);
        xmlFree(filter_name);
      }
      else nc_printf(ncct, "fitlerset\n");
    }
    else if(!strcmp((char *)node->name, "check")) {
      int busted = 1;
      xmlAttr *attr;
      char *uuid_str = NULL;
      uuid_t checkid;

      if(!titled++) nc_printf(ncct, "== Checks ==\n");

      for(attr=node->properties; attr; attr = attr->next) {
        if(!strcmp((char *)attr->name, "uuid")) {
          uuid_str = (char *)xmlXPathCastNodeToString(attr->children);
          break;
        }
      }
      nc_printf(ncct, "check[@uuid=\"%s\"] ", uuid_str ? uuid_str : "undefined");
      if(uuid_str && mtev_uuid_parse(uuid_str, checkid) == 0) {
        noit_check_t *check = noit_poller_lookup(checkid);
        if(check) {
          busted = 0;
          nc_printf(ncct, "%s`%s`%s", check->target, check->module, check->name);
          noit_check_deref(check);
        }
      }
      if(uuid_str) free(uuid_str);
      if(busted) nc_printf(ncct, "[check not in running system]");
      nc_write(ncct, "\n", 1);
    }
  }
  xmlXPathFreeObject(pobj);
  NCUNLOCK;
  return 0;
 bad:
  if(pobj) xmlXPathFreeObject(pobj);
  NCUNLOCK;
  return -1;
}

static char *
conf_t_check_prompt(EditLine *el) {
  mtev_console_closure_t ncct;
  mtev_conf_t_userdata_t *info;
  noit_check_t *check;
  static char *tl = "noit(conf)# ";
  static char *pfmt = "noit(conf:%s%s%s)# ";

  el_get(el, EL_USERDATA, (void *)&ncct);
  if(!ncct) return tl;
  info = mtev_console_userdata_get(ncct, MTEV_CONF_T_USERDATA);
  if(!info) return tl;

  check = noit_poller_lookup(info->current_check);
  if(check &&
     check->target && check->target[0] &&
     check->name && check->name[0]) {
    snprintf(info->prompt, sizeof(info->prompt),
             pfmt, check->target, "`", check->name);
    noit_check_deref(check);
  }
  else {
    char uuid_str[37];
    mtev_uuid_unparse_lower(info->current_check, uuid_str);
    snprintf(info->prompt, sizeof(info->prompt), pfmt, "[", uuid_str, "]");
  }
  return info->prompt;
}
static int
noit_conf_checks_reload(mtev_console_closure_t ncct,
                        int argc, char **argv,
                        mtev_console_state_t *state, void *closure) {
  if(mtev_conf_reload(ncct, argc, argv, state, closure)) {
    return -1;
  }
  if (noit_check_get_lmdb_instance()) {
    noit_poller_reload_lmdb(NULL, 0);
  }
  else {
    noit_poller_reload(NULL);
  }
  return 0;
}

static int
validate_attr_set_scope(mtev_conf_t_userdata_t *info,
                        struct _valid_attr_t *attrinfo) {
  int len;
  len = strlen(attrinfo->scope);
  if(strncmp(info->path, attrinfo->scope, len) ||
     (info->path[len] != '\0' && info->path[len] != '/')) {
    return -1;
  }
  return 0;
}
static int
replace_config(mtev_console_closure_t ncct,
               mtev_conf_t_userdata_t *info, const char *name,
               const char *value) {
  int i, cnt, rv = -1, active = 0;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL;
  xmlNodePtr node, confignode;
  char xpath[1024], *path;

  path = info->path;
  if(!strcmp(path, "/")) path = "";

  NCLOCK;

  mtev_conf_xml_xpath(NULL, &xpath_ctxt);

  /* Only if checks will fixate this attribute shall we check for
   * child <check> nodes.
   * NOTE: this return nothing and "seems" okay if we are _in_
   *       a <check> node.  That case is handled below.
   */
  snprintf(xpath, sizeof(xpath), "/noit/%s//check[@uuid]", path);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET) goto out;
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  for(i=0; i<cnt; i++) {
    uuid_t checkid;
    node = xmlXPathNodeSetItem(pobj->nodesetval, i);
    if(mtev_conf_get_uuid(mtev_conf_section_from_xmlnodeptr(node), "@uuid", checkid)) {
      noit_check_t *check = noit_poller_lookup(checkid);
      if (check) {
        if (NOIT_CHECK_LIVE(check)) {
          active++;
        }
        noit_check_deref(check);
      }
    }
  }
  if(pobj) xmlXPathFreeObject(pobj);

#ifdef UNSAFE_RECONFIG
  snprintf(xpath, sizeof(xpath), "/noit/%s", path);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET) goto out;
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  if(cnt != 1) {
    nc_printf(ncct, "Internal error: context node disappeared\n");
    goto out;
  }
  node = (mtev_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, 0);
  if(strcmp((const char *)node->name, "check")) {
    uuid_t checkid;
    /* Detect if  we are actually a <check> node and attempting to
     * change something we shouldn't.
     * This is the counterpart noted above.
     */
    if(mtev_conf_get_uuid(node, "@uuid", checkid)) {
      noit_check_t *check;
      check = noit_poller_lookup(checkid);
      if(NOIT_CHECK_LIVE(check)) active++;
      noit_check_deref(check);
    }
  }
  if(active) {
    nc_printf(ncct, "Cannot set '%s', it would effect %d live check(s)\n",
              name, active);
    goto out;
  }
  if(pobj) xmlXPathFreeObject(pobj);
#endif

  /* Here we want to remove /noit/path/config/name */
  snprintf(xpath, sizeof(xpath), "/noit/%s/config/%s", path, name);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET) goto out;
  if(xmlXPathNodeSetGetLength(pobj->nodesetval) > 0) {
    xmlNodePtr toremove;
    toremove = xmlXPathNodeSetItem(pobj->nodesetval, 0);
    CONF_REMOVE(mtev_conf_section_from_xmlnodeptr(toremove));
    xmlUnlinkNode(toremove);
  }
  /* TODO: if there are no more children of config, remove config? */
  if(value) {
    if(pobj) xmlXPathFreeObject(pobj);
    /* He we create config if needed and place a child node under it */
    snprintf(xpath, sizeof(xpath), "/noit/%s/config", path);
    pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
    if(!pobj || pobj->type != XPATH_NODESET) goto out;
    if(xmlXPathNodeSetGetLength(pobj->nodesetval) == 0) {
      if(pobj) xmlXPathFreeObject(pobj);
      snprintf(xpath, sizeof(xpath), "/noit/%s", path);
      pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
      if(!pobj || pobj->type != XPATH_NODESET) goto out;
      if(xmlXPathNodeSetGetLength(pobj->nodesetval) != 1) {
        nc_printf(ncct, "Node disappeared from under you!\n");
        goto out;
      }
      confignode = xmlNewChild(xmlXPathNodeSetItem(pobj->nodesetval, 0),
                               NULL, (xmlChar *)"config", NULL);
      if(confignode == NULL) {
        nc_printf(ncct, "Error creating config child node.\n");
        goto out;
      }
    }
    else confignode = xmlXPathNodeSetItem(pobj->nodesetval, 0);

    mtevAssert(confignode);
    /* Now we create a child */
    xmlNewChild(confignode, NULL, (xmlChar *)name, (xmlChar *)value);
    CONF_DIRTY(mtev_conf_section_from_xmlnodeptr(confignode));
  }
  mtev_conf_mark_changed();
  rv = 0;
 out:
  if(pobj) xmlXPathFreeObject(pobj);
  NCUNLOCK;
  return rv;
}
static int
replace_attr(mtev_console_closure_t ncct,
             mtev_conf_t_userdata_t *info, struct _valid_attr_t *attrinfo,
             const char *value) {
  int i, cnt, rv = -1, active = 0;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL;
  xmlNodePtr node;
  mtev_conf_section_t section;
  char xpath[1024], *path;

  path = info->path;
  if(!strcmp(path, "/")) path = "";

  NCLOCK;

  mtev_conf_xml_xpath(NULL, &xpath_ctxt);
  if(attrinfo->checks_fixate) {
    /* Only if checks will fixate this attribute shall we check for
     * child <check> nodes.
     * NOTE: this return nothing and "seems" okay if we are _in_
     *       a <check> node.  That case is handled below.
     */
    snprintf(xpath, sizeof(xpath), "/noit/%s//check[@uuid]", path);
    pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
    if(!pobj || pobj->type != XPATH_NODESET) goto out;
    cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
    for(i=0; i<cnt; i++) {
      uuid_t checkid;
      node = xmlXPathNodeSetItem(pobj->nodesetval, i);
      if(mtev_conf_get_uuid(mtev_conf_section_from_xmlnodeptr(node), "@uuid", checkid)) {
        noit_check_t *check = noit_poller_lookup(checkid);
        if (check) {
          if (NOIT_CHECK_LIVE(check)) {
            active++;
          }
          noit_check_deref(check);
        }
      }
    }
    if(pobj) xmlXPathFreeObject(pobj);
  }
  snprintf(xpath, sizeof(xpath), "/noit/%s", path);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET) goto out;
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  if(cnt != 1) {
    nc_printf(ncct, "Internal error: context node disappeared\n");
    goto out;
  }
  node = xmlXPathNodeSetItem(pobj->nodesetval, 0);
  section = mtev_conf_section_from_xmlnodeptr(node);
  if(attrinfo->checks_fixate &&
     !strcmp((const char *)node->name, "check")) {
    uuid_t checkid;
    /* Detect if  we are actually a <check> node and attempting to
     * change something we shouldn't.
     * This is the counterpart noted above.
     */
    if(mtev_conf_get_uuid(section, "@uuid", checkid)) {
      noit_check_t *check = noit_poller_lookup(checkid);
      if (check) {
        if (NOIT_CHECK_LIVE(check)) {
          active++;
        }
        noit_check_deref(check);
      }
    }
  }
  if(active) {
    nc_printf(ncct, "Cannot set '%s', it would effect %d live check(s)\n",
              attrinfo->name, active);
    goto out;
  }
  xmlUnsetProp(node, (xmlChar *)attrinfo->name);
  if(value)
    xmlSetProp(node, (xmlChar *)attrinfo->name, (xmlChar *)value);
  if(!strcmp((const char *)node->name, "check"))
    noit_conf_check_bump_seq(node);
  CONF_DIRTY(section);
  mtev_conf_mark_changed();
  rv = 0;
 out:
  if(pobj) xmlXPathFreeObject(pobj);
  NCUNLOCK;
  return rv;
}
int
noit_conf_check_set_attr(mtev_console_closure_t ncct,
                         int argc, char **argv,
                         mtev_console_state_t *state, void *closure) {
  struct _valid_attr_t *attrinfo = closure;
  mtev_conf_t_userdata_t *info;

  info = mtev_console_userdata_get(ncct, MTEV_CONF_T_USERDATA);
  if(!info || validate_attr_set_scope(info, attrinfo)) {
    nc_printf(ncct, "'%s' attribute only valid in %s scope\n",
              attrinfo->name, attrinfo->scope);
    return -1;
  }

  if(argc != 1) {
    nc_printf(ncct, "set requires exactly one value\n");
    return -1;
  }
  /* Okay, we have an attribute and it should be set/replaced on the
   * current path.
   */
  if(replace_attr(ncct, info, attrinfo, argv[0])) {
    return -1;
  }

  /* So, we updated an attribute, so we need to reload all checks
   * that are descendent-or-self of this node.
   */
  if(!strncmp(info->path, "/checks", strlen("/checks")))
    refresh_subchecks(ncct, info);
  if(!strncmp(info->path, "/filtersets", strlen("/filtersets")))
    noit_refresh_filtersets(ncct, info);
  return 0;
}

int
noit_conf_check_unset_attr(mtev_console_closure_t ncct,
                           int argc, char **argv,
                           mtev_console_state_t *state, void *closure) {
  struct _valid_attr_t *attrinfo = closure;
  mtev_conf_t_userdata_t *info;

  info = mtev_console_userdata_get(ncct, MTEV_CONF_T_USERDATA);
  if(!info || validate_attr_set_scope(info, attrinfo)) {
    nc_printf(ncct, "'%s' attribute only valid in %s scope\n",
              attrinfo->name, attrinfo->scope);
    return -1;
  }

  if(argc != 0) {
    nc_printf(ncct, "no arguments allowed to this command.\n");
    return -1;
  }
  /* Okay, we have an attribute and it should be set/replaced on the
   * current path.
   */
  if(replace_attr(ncct, info, attrinfo, NULL)) {
    return -1;
  }

  /* So, we updated an attribute, so we need to reload all checks
   * that are descendent-or-self of this node.
   */
  if(!strncmp(info->path, "/checks", strlen("/checks")))
    refresh_subchecks(ncct, info);
  if(!strncmp(info->path, "/filterset", strlen("/filterest")))
    noit_refresh_filtersets(ncct, info);
  return 0;
}

int
noit_console_config_setconfig(mtev_console_closure_t ncct,
                                int argc, char **argv,
                                mtev_console_state_t *state, void *closure) {
  mtev_conf_t_userdata_t *info;

  info = mtev_console_userdata_get(ncct, MTEV_CONF_T_USERDATA);

  if(argc != 2) {
    nc_printf(ncct, "two arguments required.\n");
    return -1;
  }
  /* Okay, we have an child name and it should be culled from
   * current path/config.
   */
  if(replace_config(ncct, info, argv[0], argv[1])) {
    return -1;
  }

  /* So, we updated an attribute, so we need to reload all checks
   * that are descendent-or-self of this node.
   */
  refresh_subchecks(ncct, info);
  return 0;
}

int
noit_console_config_unsetconfig(mtev_console_closure_t ncct,
                                int argc, char **argv,
                                mtev_console_state_t *state, void *closure) {
  mtev_conf_t_userdata_t *info;

  info = mtev_console_userdata_get(ncct, MTEV_CONF_T_USERDATA);

  if(argc != 1) {
    nc_printf(ncct, "one argument required.\n");
    return -1;
  }
  /* Okay, we have an child name and it should be culled from
   * current path/config.
   */
  if(replace_config(ncct, info, argv[0], NULL)) {
    return -1;
  }

  /* So, we updated an attribute, so we need to reload all checks
   * that are descendent-or-self of this node.
   */
  refresh_subchecks(ncct, info);
  return 0;
}

static mtev_hook_return_t
noit_delete_section_impl(void *closure, const char *root, const char *path,
                         const char *name, const char **err) {
  mtev_hook_return_t rv = MTEV_HOOK_CONTINUE;
  char xpath[1024];
  mtev_conf_section_t exists;

  snprintf(xpath, sizeof(xpath), "/%s%s/%s//check", root, path, name);
  exists = mtev_conf_get_section_read(MTEV_CONF_ROOT, xpath);
  if(!mtev_conf_section_is_empty(exists)) {
    if(err) *err = "cannot delete section, has checks";
    rv = MTEV_HOOK_ABORT;
  }
  mtev_conf_release_section_read(exists);
  return rv;
}

#define NEW_STATE(a) (a) = mtev_console_state_alloc()
#define ADD_CMD(a,cmd,func,ac,ss,c) \
  mtev_console_state_add_cmd((a), \
    NCSCMD(cmd, func, ac, ss, c))
#define DELEGATE_CMD(a,cmd,ac,ss) \
  mtev_console_state_add_cmd((a), \
    NCSCMD(cmd, mtev_console_state_delegate, ac, ss, NULL))

static
void register_console_config_check_commands() {
  cmd_info_t *showcmd, *nocmd, *confcmd, *conftcmd, *conftnocmd, *lscmd;
  mtev_console_state_t *tl, *_conf_t_check_state, *_unset_state,
                       *_attr_state, *_uattr_state;

  mtev_conf_delete_section_hook_register("checks_protection",
                                         noit_delete_section_impl, NULL);

  tl = mtev_console_state_initial();
  showcmd = mtev_console_state_get_cmd(tl, "show");
  nocmd = mtev_console_state_get_cmd(tl, "no");
  confcmd = mtev_console_state_get_cmd(tl, "configure");
  conftcmd = mtev_console_state_get_cmd(confcmd->dstate, "terminal");
  conftnocmd = mtev_console_state_get_cmd(conftcmd->dstate, "no");
  lscmd = mtev_console_state_get_cmd(conftcmd->dstate, "ls");
  lscmd->func = noit_console_config_show;
  /* attribute <attrname> <value> */
  NEW_STATE(_attr_state);
  mtev_console_state_add_check_attrs(_attr_state, noit_conf_check_set_attr,
                                     "/checks");
 
  /* no attribute <attrname> <value> */
  NEW_STATE(_uattr_state);
  mtev_console_state_add_check_attrs(_uattr_state, noit_conf_check_unset_attr,
                                     "/checks");
  NEW_STATE(_unset_state);
  DELEGATE_CMD(_unset_state, "attribute",
               mtev_console_opt_delegate, _uattr_state);
  ADD_CMD(_unset_state, "config",
          noit_console_config_unsetconfig, NULL, NULL, NULL);

  DELEGATE_CMD(conftnocmd->dstate, "attribute",
               mtev_console_opt_delegate, _uattr_state);
  ADD_CMD(conftnocmd->dstate, "config",
          noit_console_config_unsetconfig, NULL, NULL, NULL);
  ADD_CMD(conftnocmd->dstate, "check",
          noit_console_config_nocheck, NULL, NULL, NULL);
 
  NEW_STATE(_conf_t_check_state);
  _conf_t_check_state->console_prompt_function = conf_t_check_prompt;
  DELEGATE_CMD(_conf_t_check_state, "attribute",
               mtev_console_opt_delegate, _attr_state);
  DELEGATE_CMD(_conf_t_check_state, "no",
               mtev_console_opt_delegate, _unset_state);
  ADD_CMD(_conf_t_check_state, "config",
          noit_console_config_setconfig, NULL, NULL, NULL);
  ADD_CMD(_conf_t_check_state, "status",
          noit_console_show_check, NULL, NULL, NULL);
  ADD_CMD(_conf_t_check_state, "exit",
          mtev_console_config_cd, NULL, NULL, "..");
  ADD_CMD(_conf_t_check_state, "check",
          noit_console_check, noit_console_conf_check_opts,
          _conf_t_check_state, "..");

  ADD_CMD(conftcmd->dstate, "config",
          noit_console_config_setconfig, NULL, NULL, NULL);
  ADD_CMD(conftcmd->dstate, "check",
          noit_console_check, noit_console_conf_check_opts,
          _conf_t_check_state, NULL);

  ADD_CMD(showcmd->dstate, "check",
          noit_console_show_check, noit_console_check_opts, NULL, NULL);

  ADD_CMD(tl, "watch",
          noit_console_watch_check, noit_console_check_opts, NULL, (void *)1);

  ADD_CMD(nocmd->dstate, "watch",
          noit_console_watch_check, noit_console_check_opts, NULL, (void *)0);

  DELEGATE_CMD(conftcmd->dstate, "attribute",
               mtev_console_opt_delegate, _attr_state);

  ADD_CMD(tl, "reload", noit_conf_checks_reload, NULL, NULL, NULL);
}

void
noit_conf_checks_init_globals(void) {
  mtev_hash_init(&check_attrs);
}

