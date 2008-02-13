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
    realpath(path, master_config_file);
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
int _noit_conf_get_string(noit_conf_section_t section,
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
int noit_conf_get_string(noit_conf_section_t section,
                         const char *path, char **value) {
  char *str;
  if(_noit_conf_get_string(section,path,&str)) {
    *value = strdup(str);
    return 1;
  }
  return 0;
}
int noit_conf_get_stringbuf(noit_conf_section_t section,
                            const char *path, char *buf, int len) {
  char *str;
  if(_noit_conf_get_string(section,path,&str)) {
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
  if(noit_conf_get_string(section,path,&str)) {
    int base = 10;
    if(str[0] == '0') {
      if(str[1] == 'x') base = 16;
      else base = 8;
    }
    longval = strtol(str, NULL, base);
    free(str);
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
  if(noit_conf_get_string(section,path,&str)) {
    *value = atof(str);
    free(str);
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
  if(noit_conf_get_string(section,path,&str)) {
    if(!strcasecmp(str, "true")) *value = noit_true;
    else *value = noit_false;
    free(str);
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
noit_console_config_section(noit_console_closure_t ncct,
                            int argc, char **argv,
                            noit_console_state_t *state, void *closure) {
  const char *err = "internal error";
  char *path, xpath[1024];
  noit_conf_t_userdata_t *info;
  xmlXPathObjectPtr pobj = NULL;
  xmlNodePtr node = NULL, newnode;

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
    nc_printf(ncct, "creation of new toplevel section disallowed\n");
    return -1;
  }

  snprintf(xpath, sizeof(xpath), "/noit%s/%s", info->path, argv[0]);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET ||
     !xmlXPathNodeSetIsEmpty(pobj->nodesetval)) {
    err = "cannot create section";
    goto bad;
  }
  if(pobj) xmlXPathFreeObject(pobj);

  path = strcmp(path, "/") ? info->path : "";
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

  if(argc != 1) {
    nc_printf(ncct, "requires one argument\n");
    return -1;
  }
  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
  if(argv[0][0] == '/')
    snprintf(xpath, sizeof(xpath), "/noit%s", argv[0]);
  else {
    snprintf(xpath, sizeof(xpath), "/noit%s/%s", info->path, argv[0]);
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
  const char *path, *basepath = NULL;
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
nc_printf(ncct, "Looking up path '%s'\n", xpath);
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
             pfmt, "...", info->path + max_len - 3 /* ... */);
  else
    snprintf(info->prompt, sizeof(info->prompt), pfmt, "", info->path);
  return info->prompt;
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
  int cnt, rv = -1;
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
    if(pobj && pobj->type == XPATH_NODESET &&
       !xmlXPathNodeSetIsEmpty(pobj->nodesetval)) {
      nc_printf(ncct, "Cannot set '%s', it would effect live checks\n",
                attrinfo->name);
      goto out;
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
    /* Detect if  we are actually a <check> node and attempting to
     * change something we shouldn't.
     * This is the counterpart noted above.
     */
    nc_printf(ncct, "Cannot set '%s', it would effect live checks\n");
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
static void 
refresh_subchecks(noit_console_closure_t ncct,
                  noit_conf_t_userdata_t *info) {
  char *path;
  char xpath[1024];
 
  path = info->path;
  if(!strcmp(path, "/")) path = "";

  snprintf(xpath, sizeof(xpath), "/noit/%s[@uuid]", path);
  noit_poller_process_checks(xpath);
  snprintf(xpath, sizeof(xpath), "/noit/%s//check[@uuid]", path);
  noit_poller_process_checks(xpath);
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
  noit_console_state_t *tl, *_conf_state, *_conf_t_state,
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
 
  NEW_STATE(_conf_t_state); 
  _conf_t_state->console_prompt_function = conf_t_prompt;
  noit_console_state_add_cmd(_conf_t_state, &console_command_exit);
  ADD_CMD(_conf_t_state, "ls", noit_console_config_show, NULL, NULL);
  ADD_CMD(_conf_t_state, "cd", noit_console_config_cd, NULL, NULL);
  ADD_CMD(_conf_t_state, "section", noit_console_config_section, NULL, NULL);
  DELEGATE_CMD(_conf_t_state, "write", _write_state);
  DELEGATE_CMD(_conf_t_state, "attribute", _attr_state);
  DELEGATE_CMD(_conf_t_state, "no", _unset_state);

  NEW_STATE(_conf_state);
  ADD_CMD(_conf_state, "terminal", noit_console_state_conf_terminal, _conf_t_state, NULL);

  ADD_CMD(tl, "configure", noit_console_state_delegate, _conf_state, NULL);
  ADD_CMD(tl, "write", noit_console_state_delegate, _write_state, NULL);
}
