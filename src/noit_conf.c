/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#include "noit_conf.h"
#include "noit_check.h"
#include "noit_console.h"
#include "utils/noit_hash.h"
#include "utils/noit_log.h"

/* tmp hash impl, replace this with something nice */
static noit_hash_table _tmp_config = NOIT_HASH_EMPTY;
static xmlDocPtr master_config = NULL;
static char master_config_file[PATH_MAX] = "";
static xmlXPathContextPtr xpath_ctxt = NULL;

static noit_hash_table _compiled_fallback = NOIT_HASH_EMPTY;
static struct {
  const char *key;
  const char *val;
} config_info[] = {
  /*
   * These are compile-time fallbacks to be used in the event
   * that the current running config does not have values for
   * these config paths.
   *
   * PLEASE: keep them alphabetically sorted.
   */
  { "/noit/modules/directory", MODULES_DIR },

  { NULL, NULL }
};

static void register_console_config_commands();

void noit_conf_init() {
  int i;
  for(i = 0; config_info[i].key != NULL; i++) {
    noit_hash_store(&_compiled_fallback,
                    strdup(config_info[i].key), strlen(config_info[i].key),
                    (void *)strdup(config_info[i].val));
  }
  xmlInitParser();
  xmlXPathInit();
  register_console_config_commands();
}

int noit_conf_load(const char *path) {
  xmlDocPtr new_config;
  new_config = xmlParseFile(path);
  if(new_config) {
    if(master_config) xmlFreeDoc(master_config);
    if(xpath_ctxt) xmlXPathFreeContext(xpath_ctxt);

    master_config = new_config;
    xpath_ctxt = xmlXPathNewContext(master_config);
    if(path != master_config_file) realpath(path, master_config_file);
    return 0;
  }
  return -1;
}
int noit_conf_save(const char *path) {
  return -1;
}

noit_hash_table *noit_conf_get_hash(noit_conf_section_t section,
                                    const char *path) {
  int i, cnt;
  noit_hash_table *table = NULL;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr current_ctxt;
  xmlNodePtr current_node = (xmlNodePtr)section;
  xmlNodePtr node;

  current_ctxt = xpath_ctxt;
  if(current_node) {
    current_ctxt = xmlXPathNewContext(master_config);
    current_ctxt->node = current_node;
  }
  pobj = xmlXPathEval((xmlChar *)path, current_ctxt);
  if(!pobj) goto out;
  if(pobj->type != XPATH_NODESET) goto out;
  if(xmlXPathNodeSetIsEmpty(pobj->nodesetval)) goto out;
  table = calloc(1, sizeof(*table));
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  for(i=0; i<cnt; i++) {
    char *value;
    node = xmlXPathNodeSetItem(pobj->nodesetval, i);
    value = (char *)xmlXPathCastNodeToString(node);
    noit_hash_replace(table,
                      strdup((char *)node->name), strlen((char *)node->name),
                      strdup(value), free, free);
  }
 out:
  if(pobj) xmlXPathFreeObject(pobj);
  if(current_ctxt && current_ctxt != xpath_ctxt)
    xmlXPathFreeContext(current_ctxt);
  return table;
}
noit_conf_section_t noit_conf_get_section(noit_conf_section_t section,
                                          const char *path) {
  noit_conf_section_t subsection = NULL;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr current_ctxt;
  xmlNodePtr current_node = (xmlNodePtr)section;

  current_ctxt = xpath_ctxt;
  if(current_node) {
    current_ctxt = xmlXPathNewContext(master_config);
    current_ctxt->node = current_node;
  }
  pobj = xmlXPathEval((xmlChar *)path, current_ctxt);
  if(!pobj) goto out;
  if(pobj->type != XPATH_NODESET) goto out;
  if(xmlXPathNodeSetIsEmpty(pobj->nodesetval)) goto out;
  subsection = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, 0);
 out:
  if(pobj) xmlXPathFreeObject(pobj);
  if(current_ctxt && current_ctxt != xpath_ctxt)
    xmlXPathFreeContext(current_ctxt);
  return subsection;
}
noit_conf_section_t *noit_conf_get_sections(noit_conf_section_t section,
                                            const char *path,
                                            int *cnt) {
  int i;
  noit_conf_section_t *sections = NULL;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr current_ctxt;
  xmlNodePtr current_node = (xmlNodePtr)section;

  *cnt = 0;
  current_ctxt = xpath_ctxt;
  if(current_node) {
    current_ctxt = xmlXPathNewContext(master_config);
    current_ctxt->node = current_node;
  }
  pobj = xmlXPathEval((xmlChar *)path, current_ctxt);
  if(!pobj) goto out;
  if(pobj->type != XPATH_NODESET) goto out;
  if(xmlXPathNodeSetIsEmpty(pobj->nodesetval)) goto out;
  *cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  sections = calloc(*cnt, sizeof(*sections));
  for(i=0; i<*cnt; i++)
    sections[i] = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, i);
 out:
  if(pobj) xmlXPathFreeObject(pobj);
  if(current_ctxt && current_ctxt != xpath_ctxt)
    xmlXPathFreeContext(current_ctxt);
  return sections;
}
int _noit_conf_get_string(noit_conf_section_t section, xmlNodePtr *vnode,
                          const char *path, char **value) {
  char *str;
  int i;
  xmlXPathObjectPtr pobj;
  xmlXPathContextPtr current_ctxt;
  xmlNodePtr current_node = (xmlNodePtr)section;

  current_ctxt = xpath_ctxt;
  if(current_node) {
    current_ctxt = xmlXPathNewContext(master_config);
    current_ctxt->node = current_node;
  }
  pobj = xmlXPathEval((xmlChar *)path, current_ctxt);
  if(pobj) {
    xmlNodePtr node;
    switch(pobj->type) {
      case XPATH_NODESET:
        if(xmlXPathNodeSetIsEmpty(pobj->nodesetval)) return 0;
        i = xmlXPathNodeSetGetLength(pobj->nodesetval);
        node = xmlXPathNodeSetItem(pobj->nodesetval, i-1);
        if(vnode) *vnode = node;
        *value = (char *)xmlXPathCastNodeToString(node);
        break;
      default:
        *value = (char *)xmlXPathCastToString(pobj);
    }
    goto found;
  }
  if(noit_hash_retrieve(&_compiled_fallback,
                        path, strlen(path), (void **)&str)) {
    *value = str;
    goto found;
  }
  return 0;
 found:
  if(current_ctxt && current_ctxt != xpath_ctxt)
    xmlXPathFreeContext(current_ctxt);
  return 1;
}
int noit_conf_get_uuid(noit_conf_section_t section,
                       const char *path, uuid_t out) {
  char *str;
  if(_noit_conf_get_string(section,NULL,path,&str)) {
    if(uuid_parse(str, out) == 0) return 1;
    return 0;
  }
  return 0;
}
int noit_conf_get_string(noit_conf_section_t section,
                         const char *path, char **value) {
  char *str;
  if(_noit_conf_get_string(section,NULL,path,&str)) {
    *value = strdup(str);
    return 1;
  }
  return 0;
}
int noit_conf_get_stringbuf(noit_conf_section_t section,
                            const char *path, char *buf, int len) {
  char *str;
  if(_noit_conf_get_string(section,NULL,path,&str)) {
    strlcpy(buf, str, len);
    return 1;
  }
  return 0;
}
int noit_conf_set_string(noit_conf_section_t section,
                         const char *path, const char *value) {
  noit_hash_replace(&_tmp_config,
                    strdup(path), strlen(path), (void *)strdup(value),
                    free, free);
  return 1;
}
int noit_conf_get_int(noit_conf_section_t section,
                      const char *path, int *value) {
  char *str;
  long longval;
  if(_noit_conf_get_string(section,NULL,path,&str)) {
    int base = 10;
    if(str[0] == '0') {
      if(str[1] == 'x') base = 16;
      else base = 8;
    }
    longval = strtol(str, NULL, base);
    *value = (int)longval;
    return 1;
  }
  return 0;
}
int noit_conf_set_int(noit_conf_section_t section,
                      const char *path, int value) {
  char buffer[32];
  snprintf(buffer, 32, "%d", value);
  return noit_conf_set_string(section,path,buffer);
}
int noit_conf_get_float(noit_conf_section_t section,
                        const char *path, float *value) {
  char *str;
  if(_noit_conf_get_string(section,NULL,path,&str)) {
    *value = atof(str);
    return 1;
  }
  return 0;
}
int noit_conf_set_float(noit_conf_section_t section,
                        const char *path, float value) {
  char buffer[32];
  snprintf(buffer, 32, "%f", value);
  return noit_conf_set_string(section,path,buffer);
}
int noit_conf_get_boolean(noit_conf_section_t section,
                          const char *path, noit_conf_boolean *value) {
  char *str;
  if(_noit_conf_get_string(section,NULL,path,&str)) {
    if(!strcasecmp(str, "true")) *value = noit_true;
    else *value = noit_false;
    return 1;
  }
  return 0;
}
int noit_conf_set_boolean(noit_conf_section_t section,
                          const char *path, noit_conf_boolean value) {
  if(value == noit_true)
    return noit_conf_set_string(section,path,"true");
  return noit_conf_set_string(section,path,"false");
}

static void
conf_t_userdata_free(void *data) {
  noit_conf_t_userdata_t *info = data;
  if(info) {
    if(info->path) free(info->path);
    free(info);
  }
}
static int
noit_console_state_conf_terminal(noit_console_closure_t ncct,
                                 int argc, char **argv,
                                 noit_console_state_t *state, void *closure) {
  noit_conf_t_userdata_t *info;
  if(argc) {
    nc_printf(ncct, "extra arguments not expected.\n");
    return -1;
  }
  info = calloc(1, sizeof(*info));
  info->path = strdup("/");
  noit_console_userdata_set(ncct, NOIT_CONF_T_USERDATA, info,
                            conf_t_userdata_free);
  noit_console_state_push_state(ncct, state);
  noit_console_state_init(ncct);
  return 0;
}
static int
noit_console_mkcheck_xpath(char *xpath, int len,
                           noit_conf_t_userdata_t *info,
                           const char *arg) {
  uuid_t checkid;
  char argcopy[1024], *target, *name;

  argcopy[0] = '\0';
  if(arg) strlcpy(argcopy, arg, sizeof(argcopy));

  if(uuid_parse(argcopy, checkid) == 0) {
    /* If they kill by uuid, we'll seek and destroy -- find it anywhere */
    snprintf(xpath, len, "/noit/checks//check[@uuid=\"%s\"]",
             argcopy);
  }
  else if((name = strchr(argcopy, '`')) != NULL) {
    noit_check_t *check;
    char uuid_str[37];
    target = argcopy;
    *name++ = '\0';
    check = noit_poller_lookup_by_name(target, name);
    if(!check) {
      return -1;
    }
    uuid_unparse_lower(check->checkid, uuid_str);
    snprintf(xpath, len, "/noit/checks//check[@uuid=\"%s\"]",
             uuid_str);
  }
  else {
    char *path = (!info || !strcmp(info->path, "/")) ? "" : info->path;
    snprintf(xpath, len, "/noit%s%s%s[@uuid]",
             path, arg ? "/" : "", arg ? arg : "");
  }
  return 0;
}
static void
nc_attr_show(noit_console_closure_t ncct, const char *name, xmlNodePtr cnode,
             xmlNodePtr anode, const char *value) {
  const char *cpath, *apath;
  cpath = cnode ? (char *)xmlGetNodePath(cnode) : "";
  apath = anode ? (char *)xmlGetNodePath(anode) : "";
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
}
static void 
refresh_subchecks(noit_console_closure_t ncct,
                  noit_conf_t_userdata_t *info) {
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
noit_conf_mkcheck_under(const char *ppath, uuid_t out) {
  int rv = -1;
  const char *path;
  char xpath[1024];
  xmlXPathObjectPtr pobj = NULL;
  xmlNodePtr node = NULL, newnode;

  path = strcmp(ppath, "/") ? ppath : "";
  snprintf(xpath, sizeof(xpath), "/noit%s", path);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET ||
     xmlXPathNodeSetGetLength(pobj->nodesetval) != 1) {
    goto out;
  }
  node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, 0);
  if((newnode = xmlNewChild(node, NULL, (xmlChar *)"check", NULL)) != NULL) {
    char outstr[37];
    uuid_generate(out);
    uuid_unparse_lower(out, outstr);
    xmlSetProp(newnode, (xmlChar *)"uuid", (xmlChar *)outstr);
    rv = 0;
  }
 out:
  if(pobj) xmlXPathFreeObject(pobj);
  return rv;
}
static int
noit_console_check(noit_console_closure_t ncct,
                   int argc, char **argv,
                   noit_console_state_t *state, void *closure) {
  int cnt;
  noit_conf_t_userdata_t *info;
  char xpath[1024], newuuid_str[37];
  char *uuid_conf, *wanted;
  uuid_t checkid;
  xmlXPathObjectPtr pobj = NULL;
  xmlNodePtr node = NULL;
  noit_conf_boolean creating_new = noit_false;

  if(argc > 1) {
    nc_printf(ncct, "requires zero or one arguments\n");
    return -1;
  }

  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
  wanted = argc ? argv[0] : NULL;
  if(!wanted) {
    /* We are creating a new node */
    uuid_t out;
    creating_new = noit_true;
    if(noit_conf_mkcheck_under(info->path, out)) {
      nc_printf(ncct, "Error creating new check\n");
      return -1;
    }
    newuuid_str[0] = '\0';
    uuid_unparse_lower(out, newuuid_str);
    wanted = newuuid_str;
  }
  /* We many not be in conf-t mode -- that's fine */
  if(noit_console_mkcheck_xpath(xpath, sizeof(xpath), info, wanted)) {
    nc_printf(ncct, "could not find check '%s'\n", wanted);
    return -1;
  }

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
  node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, 0);
  uuid_conf = (char *)xmlGetProp(node, (xmlChar *)"uuid");
  if(!uuid_conf || uuid_parse(uuid_conf, checkid)) {
    nc_printf(ncct, "%s has invalid or missing UUID!\n",
              (char *)xmlGetNodePath(node) + strlen("/noit"));
    goto out;
  }
  if(info) {
    if(info->path) free(info->path);
    info->path = strdup((char *)xmlGetNodePath(node) + strlen("/noit"));
    uuid_copy(info->current_check, checkid);
    if(creating_new) refresh_subchecks(ncct, info);
    noit_console_state_push_state(ncct, state);
    noit_console_state_init(ncct);
    goto out;
  }
 out:
  if(pobj) xmlXPathFreeObject(pobj);
  return 0;
}
static int
noit_console_show_check(noit_console_closure_t ncct,
                        int argc, char **argv,
                        noit_console_state_t *state, void *closure) {
  int i, cnt;
  noit_conf_t_userdata_t *info;
  char xpath[1024];
  xmlXPathObjectPtr pobj = NULL;

  if(argc > 1) {
    nc_printf(ncct, "requires zero or one arguments\n");
    return -1;
  }

  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
  /* We many not be in conf-t mode -- that's fine */
  if(noit_console_mkcheck_xpath(xpath, sizeof(xpath), info,
                                argc ? argv[0] : NULL)) {
    nc_printf(ncct, "could not find check '%s'\n", argv[0]);
    return -1;
  }

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
    uuid_t checkid;
    noit_check_t *check;
    xmlNodePtr node, anode, mnode = NULL;
    char *uuid_conf;
    char *module, *value;

    node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, i);
    uuid_conf = (char *)xmlGetProp(node, (xmlChar *)"uuid");
    if(!uuid_conf || uuid_parse(uuid_conf, checkid)) {
      nc_printf(ncct, "%s has invalid or missing UUID!\n",
                (char *)xmlGetNodePath(node) + strlen("/noit"));
      continue;
    }
    nc_printf(ncct, "==== %s ====\n", uuid_conf);

#define MYATTR(a,n,b) _noit_conf_get_string(node, &(n), "@" #a, &(b))
#define INHERIT(a,n,b) \
  _noit_conf_get_string(node, &(n), "ancestor-or-self::node()/@" #a, &(b))
#define SHOW_ATTR(a) do { \
  anode = NULL; \
  value = NULL; \
  INHERIT(a, anode, value); \
  nc_attr_show(ncct, #a, node, anode, value); \
} while(0)

    if(!INHERIT(module, mnode, module)) module = NULL;
    if(MYATTR(name, anode, value))
      nc_printf(ncct, " name: %s\n", value);
    else
      nc_printf(ncct, " name: %s [from module]\n", module ? module : "[undef]");
    nc_attr_show(ncct, "module", node, mnode, module);
    SHOW_ATTR(target);
    SHOW_ATTR(period);
    SHOW_ATTR(timeout);
    SHOW_ATTR(oncheck);
    SHOW_ATTR(disable);
    check = noit_poller_lookup(checkid);
    if(!check) {
      nc_printf(ncct, " ERROR: not in running system\n");
    }
    else {
      int idx = 0;
      nc_printf(ncct, " currently: ");
      if(NOIT_CHECK_RUNNING(check)) nc_printf(ncct, "%srunning", idx++?",":"");
      if(NOIT_CHECK_KILLED(check)) nc_printf(ncct, "%skilled", idx++?",":"");
      if(!NOIT_CHECK_CONFIGURED(check)) nc_printf(ncct, "%sunconfig", idx++?",":"");
      if(NOIT_CHECK_DISABLED(check)) nc_printf(ncct, "%sdisabled", idx++?",":"");
      nc_write(ncct, "\n", 1);
      if(check->stats.current.status)
        nc_printf(ncct, " recently: %s\n", check->stats.current.status);
    }
  }
 out:
  if(pobj) xmlXPathFreeObject(pobj);
  return 0;
}
static int
noit_console_config_nocheck(noit_console_closure_t ncct,
                            int argc, char **argv,
                            noit_console_state_t *state, void *closure) {
  int i, cnt;
  const char *err = "internal error";
  noit_conf_t_userdata_t *info;
  xmlXPathObjectPtr pobj = NULL;
  char xpath[1024];
  uuid_t checkid;

  if(argc != 1) {
    nc_printf(ncct, "requires one argument\n");
    return -1;
  }

  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
  if(noit_console_mkcheck_xpath(xpath, sizeof(xpath), info, argv[0])) {
    nc_printf(ncct, "could not find check '%s'\n", argv[0]);
    return -1;
  }
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET ||
     xmlXPathNodeSetIsEmpty(pobj->nodesetval)) {
    err = "no checks found";
    goto bad;
  }
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  for(i=0; i<cnt; i++) {
    xmlNodePtr node;
    char *uuid_conf;
    node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, i);
    uuid_conf = (char *)xmlGetProp(node, (xmlChar *)"uuid");
    if(!uuid_conf || uuid_parse(uuid_conf, checkid)) {
      nc_printf(ncct, "%s has invalid or missing UUID!\n",
                (char *)xmlGetNodePath(node) + strlen("/noit"));
    }
    else {
      nc_printf(ncct, "descheduling %s\n", uuid_conf);
      noit_poller_deschedule(checkid);
      xmlUnlinkNode(node);
    }
  }
  nc_printf(ncct, "rebuilding causal map...\n");
  noit_poller_make_causal_map();
  if(pobj) xmlXPathFreeObject(pobj);
  return 0;
 bad:
  if(pobj) xmlXPathFreeObject(pobj);
  nc_printf(ncct, "%s\n", err);
  return -1;
}
static int
noit_console_config_section(noit_console_closure_t ncct,
                            int argc, char **argv,
                            noit_console_state_t *state, void *closure) {
  const char *err = "internal error";
  char *path, xpath[1024];
  noit_conf_t_userdata_t *info;
  xmlXPathObjectPtr pobj = NULL;
  xmlNodePtr node = NULL, newnode;
  vpsized_int delete = (vpsized_int)closure;

  if(argc != 1) {
    nc_printf(ncct, "requires one argument\n");
    return -1;
  }
  if(strchr(argv[0], '/')) {
    nc_printf(ncct, "invalid section name\n");
    return -1;
  }
  if(!strcmp(argv[0], "check")) {
    nc_printf(ncct, "use 'check' to create checks\n");
    return -1;
  }
  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
  if(!strcmp(info->path, "/")) {
    nc_printf(ncct, "manipulation of toplevel section disallowed\n");
    return -1;
  }

  if(delete) {
    /* We cannot delete if we have checks */
    snprintf(xpath, sizeof(xpath), "/noit%s/%s//check", info->path, argv[0]);
    pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
    if(!pobj || pobj->type != XPATH_NODESET ||
       !xmlXPathNodeSetIsEmpty(pobj->nodesetval)) {
      err = "cannot delete section, has checks";
      goto bad;
    }
    if(pobj) xmlXPathFreeObject(pobj);
  }

  snprintf(xpath, sizeof(xpath), "/noit%s/%s", info->path, argv[0]);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET) {
    err = "internal error: cannot detect section";
    goto bad;
  }
  if(!delete && !xmlXPathNodeSetIsEmpty(pobj->nodesetval)) {
    err = "cannot create section";
    goto bad;
  }
  if(delete && xmlXPathNodeSetIsEmpty(pobj->nodesetval)) {
    err = "no such section";
    goto bad;
  }
  if(delete) {
    node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, 0);
    xmlUnlinkNode(node);
    return 0;
  }
  if(pobj) xmlXPathFreeObject(pobj);

  path = strcmp(info->path, "/") ? info->path : "";
  snprintf(xpath, sizeof(xpath), "/noit%s", path);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET ||
     xmlXPathNodeSetGetLength(pobj->nodesetval) != 1) {
    err = "path invalid?";
    goto bad;
  }
  node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, 0);
  if((newnode = xmlNewChild(node, NULL, (xmlChar *)argv[0], NULL)) != NULL)
    info->path = strdup((char *)xmlGetNodePath(newnode) + strlen("/noit"));
  else {
    err = "failed to create section";
    goto bad;
  }
  if(pobj) xmlXPathFreeObject(pobj);
  return 0;
 bad:
  if(pobj) xmlXPathFreeObject(pobj);
  nc_printf(ncct, "%s\n", err);
  return -1;
}

static int
noit_console_config_cd(noit_console_closure_t ncct,
                       int argc, char **argv,
                       noit_console_state_t *state, void *closure) {
  const char *err = "internal error";
  char *path, xpath[1024];
  noit_conf_t_userdata_t *info;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr current_ctxt;
  xmlNodePtr node = NULL;
  char *dest;

  if(argc != 1 && !closure) {
    nc_printf(ncct, "requires one argument\n");
    return -1;
  }
  dest = argc ? argv[0] : (char *)closure;
  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
  if(dest[0] == '/')
    snprintf(xpath, sizeof(xpath), "/noit%s", dest);
  else {
    snprintf(xpath, sizeof(xpath), "/noit%s/%s", info->path, dest);
  }
  if(xpath[strlen(xpath)-1] == '/') xpath[strlen(xpath)-1] = '\0';

  current_ctxt = xpath_ctxt;
  pobj = xmlXPathEval((xmlChar *)xpath, current_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET ||
     xmlXPathNodeSetIsEmpty(pobj->nodesetval)) {
    err = "no such section";
    goto bad;
  }
  if(xmlXPathNodeSetGetLength(pobj->nodesetval) > 1) {
    err = "ambiguous section";
    goto bad;
  }

  node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, 0);
  if(!strcmp((char *)node->name, "check")) {
    err = "can't cd into a check, use 'check' instead";
    goto bad;
  }
  path = (char *)xmlGetNodePath(node);
  if(strncmp(path, "/noit/", strlen("/noit/")) && strcmp(path, "/noit")) {
    err = "new path outside out tree";
    goto bad;
  }
  free(info->path);
  if(!strcmp(path, "/noit"))
    info->path = strdup("/");
  else
    info->path = strdup((char *)xmlGetNodePath(node) + strlen("/noit"));
  if(pobj) xmlXPathFreeObject(pobj);
  if(closure) noit_console_state_pop(ncct, argc, argv, NULL, NULL);
  return 0;
 bad:
  if(pobj) xmlXPathFreeObject(pobj);
  nc_printf(ncct, "%s\n", err);
  return -1;
}
static int
noit_console_config_show(noit_console_closure_t ncct,
                         int argc, char **argv,
                         noit_console_state_t *state, void *closure) {
  int i, cnt, titled = 0, cliplen = 0;
  const char *path = "", *basepath = NULL;
  char xpath[1024];
  noit_conf_t_userdata_t *info = NULL;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr current_ctxt;
  xmlNodePtr node;

  if(argc > 1) {
    nc_printf(ncct, "too many arguments\n");
    return -1;
  }

  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
  if(info) path = basepath = info->path;
  if(!info && argc == 0) {
    nc_printf(ncct, "argument required when not in configuration mode\n");
    return -1;
  }

  if(argc == 1) path = argv[0];
  if(!basepath) basepath = path;

  /* { / } is a special case */
  if(!strcmp(basepath, "/")) basepath = "";
  if(!strcmp(path, "/")) path = "";

  if(!master_config) {
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
    node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, i);
    if(!strcmp((char *)node->name, "check")) continue;
    if(node->children && node->children == xmlGetLastChild(node) &&
      xmlNodeIsText(node->children)) {
      if(!titled++) nc_printf(ncct, "== Section Settings ==\n");
      nc_printf(ncct, "%s: %s\n", xmlGetNodePath(node) + cliplen,
                xmlXPathCastNodeToString(node->children));
    }
  }
  xmlXPathFreeObject(pobj);

  /* _shorten string_ turning last { / @ * } to { / * } */
  strlcpy(xpath + strlen(xpath) - 2, "*", 2);
  pobj = xmlXPathEval((xmlChar *)xpath, current_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET) {
    nc_printf(ncct, "no such object\n");
    goto bad;
  }
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  titled = 0;
  for(i=0; i<cnt; i++) {
    node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, i);
    if(!strcmp((char *)node->name, "check")) continue;
    if(!(node->children && node->children == xmlGetLastChild(node) &&
         xmlNodeIsText(node->children))) {
      if(!titled++) nc_printf(ncct, "== Subsections ==\n");
      nc_printf(ncct, "%s\n", xmlGetNodePath(node) + cliplen);
    }
  }

  titled = 0;
  for(i=0; i<cnt; i++) {
    node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, i);
    if(!strcmp((char *)node->name, "check")) {
      int busted = 1;
      xmlAttr *attr;
      char *uuid_str = "undefined";;

      if(!titled++) nc_printf(ncct, "== Checks ==\n");

      for(attr=node->properties; attr; attr = attr->next) {
        if(!strcmp((char *)attr->name, "uuid"))
          uuid_str = (char *)xmlXPathCastNodeToString(attr->children);
      }
      if(uuid_str) {
        uuid_t checkid;
        nc_printf(ncct, "check[@uuid=\"%s\"] ", uuid_str);
        if(uuid_parse(uuid_str, checkid) == 0) {
          noit_check_t *check;
          check = noit_poller_lookup(checkid);
          if(check) {
            busted = 0;
            nc_printf(ncct, "%s`%s", check->target, check->name);
          }
        }
      }
      else
        nc_printf(ncct, "%s ", xmlGetNodePath(node) + cliplen);
      if(busted) nc_printf(ncct, "[check not in running system]");
      nc_write(ncct, "\n", 1);
    }
  }
  xmlXPathFreeObject(pobj);
  return 0;
 bad:
  if(pobj) xmlXPathFreeObject(pobj);
  return -1;
}

static char *
conf_t_check_prompt(EditLine *el) {
  noit_console_closure_t ncct;
  noit_conf_t_userdata_t *info;
  noit_check_t *check;
  static char *tl = "noit(conf)# ";
  static char *pfmt = "noit(conf:%s%s%s)# ";

  el_get(el, EL_USERDATA, (void *)&ncct);
  if(!ncct) return tl;
  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
  if(!info) return tl;

  check = noit_poller_lookup(info->current_check);
  if(check &&
     check->target && check->target[0] &&
     check->name && check->name[0])
    snprintf(info->prompt, sizeof(info->prompt),
             pfmt, check->target, "`", check->name);
  else {
    char uuid_str[37];
    uuid_unparse_lower(info->current_check, uuid_str);
    snprintf(info->prompt, sizeof(info->prompt), pfmt, "[", uuid_str, "]");
  }
  return info->prompt;
}
static char *
conf_t_prompt(EditLine *el) {
  noit_console_closure_t ncct;
  noit_conf_t_userdata_t *info;
  static char *tl = "noit(conf)# ";
  static char *pfmt = "noit(conf:%s%s)# ";
  int path_len, max_len;

  el_get(el, EL_USERDATA, (void *)&ncct);
  if(!ncct) return tl;
  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
  if(!info) return tl;

  path_len = strlen(info->path);
  max_len = sizeof(info->prompt) - (strlen(pfmt) - 4 /* %s%s */) - 1 /* \0 */;
  if(path_len > max_len)
    snprintf(info->prompt, sizeof(info->prompt),
             pfmt, "...", info->path + path_len - max_len + 3 /* ... */);
  else
    snprintf(info->prompt, sizeof(info->prompt), pfmt, "", info->path);
  return info->prompt;
}
static int
noit_conf_reload(noit_console_closure_t ncct,
                 int argc, char **argv,
                 noit_console_state_t *state, void *closure) {
  if(noit_conf_load(master_config_file)) {
    nc_printf(ncct, "error loading config\n");
    return -1;
  }
  noit_poller_reload(NULL);
  return 0;
}
static int
noit_console_write_xml(void *vncct, const char *buffer, int len) {
  noit_console_closure_t ncct = vncct;
  return nc_write(ncct, buffer, len);
}
static int
noit_console_close_xml(void *vncct) {
  return 0;
}
static int
noit_conf_write_terminal(noit_console_closure_t ncct,
                         int argc, char **argv,
                         noit_console_state_t *state, void *closure) {
  xmlOutputBufferPtr out;
  xmlCharEncodingHandlerPtr enc;
  enc = xmlGetCharEncodingHandler(XML_CHAR_ENCODING_UTF8);
  out = xmlOutputBufferCreateIO(noit_console_write_xml,
                                noit_console_close_xml,
                                ncct, enc);
  xmlSaveFileTo(out, master_config, "utf8");
  return 0;
}
static int
noit_conf_write_file(noit_console_closure_t ncct,
                     int argc, char **argv,
                     noit_console_state_t *state, void *closure) {
  int fd, len;
  char master_file_tmp[PATH_MAX];
  xmlOutputBufferPtr out;
  xmlCharEncodingHandlerPtr enc;

  snprintf(master_file_tmp, sizeof(master_file_tmp),
           "%s.tmp", master_config_file);
  unlink(master_file_tmp);
  fd = open(master_file_tmp, O_CREAT|O_EXCL|O_WRONLY, 0640);
  if(fd < 0) {
    nc_printf(ncct, "Failed to open tmp file: %s\n", strerror(errno));
    return -1;
  }
  enc = xmlGetCharEncodingHandler(XML_CHAR_ENCODING_UTF8);
  out = xmlOutputBufferCreateFd(fd, enc);
  if(!out) {
    close(fd);
    unlink(master_file_tmp);
    nc_printf(ncct, "internal error: OutputBufferCreate failed\n");
    return -1;
  }
  len = xmlSaveFileTo(out, master_config, "utf8");
  close(fd);
  if(len <= 0) {
    nc_printf(ncct, "internal error: writing to tmp file failed.\n");
    return -1;
  }
  if(rename(master_file_tmp, master_config_file) != 0) {
    nc_printf(ncct, "Failed to replace file: %s\n", strerror(errno));
    return -1;
  }
  nc_printf(ncct, "%d bytes written.\n", len);
  return 0;
}

static struct _valid_attr_t {
  const char *scope;
  const char *name;
  const char *xpath;
  int checks_fixate;
} valid_attrs[] = {
  { "/checks", "name", "@name", 0 },
  { "/checks", "target", "@target", 0 },
  { "/checks", "period", "@period", 0 },
  { "/checks", "timeout", "@timeout", 0 },
  { "/checks", "oncheck", "@oncheck", 0 },
  { "/checks", "disable", "@disable", 0 },
  { "/checks", "module", "@module", 1 },
};

void
noit_console_state_add_check_attrs(noit_console_state_t *state,
                                   console_cmd_func_t f) {
  int i;
  for(i = 0;
      i < sizeof(valid_attrs)/sizeof(valid_attrs[0]);
      i++) {
    noit_console_state_add_cmd(state,
      NCSCMD(valid_attrs[i].name, f,
             NULL, &valid_attrs[i]));
  }
}

static int
validate_attr_set_scope(noit_conf_t_userdata_t *info,
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
replace_attr(noit_console_closure_t ncct,
             noit_conf_t_userdata_t *info, struct _valid_attr_t *attrinfo,
             const char *value) {
  int i, cnt, rv = -1, active = 0;
  xmlXPathObjectPtr pobj = NULL;
  xmlNodePtr node;
  char xpath[1024], *path;

  path = info->path;
  if(!strcmp(path, "/")) path = "";

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
      node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, i);
      if(noit_conf_get_uuid(node, "@uuid", checkid)) {
        noit_check_t *check;
        check = noit_poller_lookup(checkid);
        if(NOIT_CHECK_LIVE(check)) active++;
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
  node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, 0);
  if(attrinfo->checks_fixate &&
     !strcmp((const char *)node->name, "check")) {
    uuid_t checkid;
    /* Detect if  we are actually a <check> node and attempting to
     * change something we shouldn't.
     * This is the counterpart noted above.
     */
    if(noit_conf_get_uuid(node, "@uuid", checkid)) {
      noit_check_t *check;
      check = noit_poller_lookup(checkid);
      if(NOIT_CHECK_LIVE(check)) active++;
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
  rv = 0;
 out:
  if(pobj) xmlXPathFreeObject(pobj);
  return rv;
}
int
noit_conf_check_set_attr(noit_console_closure_t ncct,
                         int argc, char **argv,
                         noit_console_state_t *state, void *closure) {
  struct _valid_attr_t *attrinfo = closure;
  noit_conf_t_userdata_t *info;

  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
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
  refresh_subchecks(ncct, info);
  return 0;
}

int
noit_conf_check_unset_attr(noit_console_closure_t ncct,
                           int argc, char **argv,
                           noit_console_state_t *state, void *closure) {
  struct _valid_attr_t *attrinfo = closure;
  noit_conf_t_userdata_t *info;

  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
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
  refresh_subchecks(ncct, info);
  return 0;
}

#define NEW_STATE(a) (a) = calloc(1, sizeof(*(a)))
#define ADD_CMD(a,cmd,func,ss,c) \
  noit_console_state_add_cmd((a), \
    NCSCMD(cmd, func, ss, c))
#define DELEGATE_CMD(a,cmd,ss) \
  noit_console_state_add_cmd((a), \
    NCSCMD(cmd, noit_console_state_delegate, ss, NULL))

static
void register_console_config_commands() {
  cmd_info_t *showcmd;
  noit_console_state_t *tl, *_conf_state, *_conf_t_state,
                       *_conf_t_check_state,
                       *_write_state, *_attr_state,
                       *_unset_state, *_uattr_state;

  tl = noit_console_state_initial();

  /* write <terimal|memory|file> */
  NEW_STATE(_write_state);
  ADD_CMD(_write_state, "terminal", noit_conf_write_terminal, NULL, NULL);
  ADD_CMD(_write_state, "file", noit_conf_write_file, NULL, NULL);
  /* write memory?  It's to a file, but I like router syntax */
  ADD_CMD(_write_state, "memory", noit_conf_write_file, NULL, NULL);

  /* attribute <attrname> <value> */
  NEW_STATE(_attr_state);
  noit_console_state_add_check_attrs(_attr_state, noit_conf_check_set_attr);
 
  /* no attribute <attrname> <value> */
  NEW_STATE(_uattr_state);
  noit_console_state_add_check_attrs(_uattr_state, noit_conf_check_unset_attr);
  NEW_STATE(_unset_state);
  DELEGATE_CMD(_unset_state, "attribute", _uattr_state);
  ADD_CMD(_unset_state, "section", noit_console_config_section, NULL, (void *)1);
  ADD_CMD(_unset_state, "check", noit_console_config_nocheck, NULL, NULL);
 
  NEW_STATE(_conf_t_check_state);
  _conf_t_check_state->console_prompt_function = conf_t_check_prompt;
  DELEGATE_CMD(_conf_t_check_state, "attribute", _attr_state);
  DELEGATE_CMD(_conf_t_check_state, "no", _unset_state);
  ADD_CMD(_conf_t_check_state, "status", noit_console_show_check, NULL, NULL);
  ADD_CMD(_conf_t_check_state, "exit", noit_console_config_cd, NULL, "..");

  NEW_STATE(_conf_t_state); 
  _conf_t_state->console_prompt_function = conf_t_prompt;
  noit_console_state_add_cmd(_conf_t_state, &console_command_exit);
  ADD_CMD(_conf_t_state, "ls", noit_console_config_show, NULL, NULL);
  ADD_CMD(_conf_t_state, "cd", noit_console_config_cd, NULL, NULL);
  ADD_CMD(_conf_t_state, "section", noit_console_config_section, NULL, (void *)0);
  ADD_CMD(_conf_t_state, "check", noit_console_check, _conf_t_check_state, NULL);

  showcmd = noit_console_state_get_cmd(tl, "show");
  ADD_CMD(showcmd->dstate, "check", noit_console_show_check, NULL, NULL);

  DELEGATE_CMD(_conf_t_state, "write", _write_state);
  DELEGATE_CMD(_conf_t_state, "attribute", _attr_state);
  DELEGATE_CMD(_conf_t_state, "no", _unset_state);

  NEW_STATE(_conf_state);
  ADD_CMD(_conf_state, "terminal", noit_console_state_conf_terminal, _conf_t_state, NULL);

  ADD_CMD(tl, "configure", noit_console_state_delegate, _conf_state, NULL);
  ADD_CMD(tl, "write", noit_console_state_delegate, _write_state, NULL);
  ADD_CMD(tl, "reload", noit_conf_reload, NULL, NULL);
}

void
noit_conf_log_init() {
  int i, cnt = 0, o, ocnt = 0;
  noit_conf_section_t *log_configs, *outlets;

  log_configs = noit_conf_get_sections(NULL, "/noit/logs/log", &cnt);
  noitL(noit_stderr, "Found %d /noit/logs/log stanzas\n", cnt);
  for(i=0; i<cnt; i++) {
    noit_log_stream_t ls;
    char name[256], type[256], path[256];
    noit_hash_table *config;
    noit_conf_boolean disabled;

    if(!noit_conf_get_stringbuf(log_configs[i], "@name", name, sizeof(name))) {
      noitL(noit_error, "log section %d does not have a name attribute\n", i+1);
      exit(-1);
    }
    if(!noit_conf_get_stringbuf(log_configs[i], "@type", type, sizeof(type))) {
      type[0] = '\0';
    }
    if(!noit_conf_get_stringbuf(log_configs[i], "@path", path, sizeof(path))) {
      path[0] = '\0';
    }
    config = noit_conf_get_hash(log_configs[i], "config/*");
    ls = noit_log_stream_new(name, type[0] ? type : NULL,
                             path[0] ? path : NULL, config);
    if(!ls) {
      fprintf(stderr, "Error configuring log: %s[%s:%s]\n", name, type, path);
      exit(-1);
    }
    if(noit_conf_get_boolean(log_configs[i], "@disabled", &disabled))
      if(disabled)
        ls->enabled = 0;
      
    outlets = noit_conf_get_sections(log_configs[i], "outlet", &ocnt);
    for(o=0; o<ocnt; o++) {
      noit_log_stream_t outlet;
      char oname[256];
      noit_conf_get_stringbuf(outlets[i], "@name",
                              oname, sizeof(oname));
      outlet = noit_log_stream_find(oname);
      if(!outlet) {
        fprintf(stderr, "Cannot find outlet '%s' for %s[%s:%s]\n", oname,
              name, type, path);
        exit(-1);
      }
      noit_log_stream_add_stream(ls, outlet);
    }
    if(outlets) free(outlets);
  }
  if(log_configs) free(log_configs);
}
