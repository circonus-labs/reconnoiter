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
  { "/%s/modules/@directory", MODULES_DIR },

  { NULL, NULL }
};

void noit_conf_init(const char *toplevel) {
  int i;
  char keystr[256];
  for(i = 0; config_info[i].key != NULL; i++) {
    snprintf(keystr, sizeof(keystr), config_info[i].key, toplevel);
    noit_hash_store(&_compiled_fallback,
                    strdup(keystr), strlen(keystr),
                    (void *)strdup(config_info[i].val));
  }
  xmlInitParser();
  xmlXPathInit();
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
int noit_conf_xml_xpath(xmlDocPtr *mc, xmlXPathContextPtr *xp) {
  if(mc) *mc = master_config;
  if(xp) *xp = xpath_ctxt;
  return 0;
}
int noit_conf_save(const char *path) {
  return -1;
}

void noit_conf_get_elements_into_hash(noit_conf_section_t section,
                                      const char *path,
                                      noit_hash_table *table) {
  int i, cnt;
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
}
void noit_conf_get_into_hash(noit_conf_section_t section,
                             const char *path,
                             noit_hash_table *table) {
  int cnt;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr current_ctxt;
  xmlNodePtr current_node = (xmlNodePtr)section;
  xmlNodePtr node, parent_node;
  char xpath_expr[1024];
  char *inheritid;

  current_ctxt = xpath_ctxt;
  if(current_node) {
    current_ctxt = xmlXPathNewContext(master_config);
    current_ctxt->node = current_node;
  }
  if(path[0] == '/')
    strlcpy(xpath_expr, path, sizeof(xpath_expr));
  else
    snprintf(xpath_expr, sizeof(xpath_expr),
             "ancestor-or-self::node()/%s", path);
  pobj = xmlXPathEval((xmlChar *)xpath_expr, current_ctxt);
  if(!pobj) goto out;
  if(pobj->type != XPATH_NODESET) goto out;
  if(xmlXPathNodeSetIsEmpty(pobj->nodesetval)) goto out;
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  /* These are in the order of root to leaf
   * We want to recurse... apply:
   *   1. our parent's config
   *   2. our "inherit" config if it exists.
   *   3. our config.
   */
  node = xmlXPathNodeSetItem(pobj->nodesetval, cnt-1);
  /* 1. */
  if(cnt > 1) {
    parent_node = xmlXPathNodeSetItem(pobj->nodesetval, cnt-2);
    noit_conf_get_into_hash(parent_node, (const char *)node->name, table);
  }
  /* 2. */
  inheritid = (char *)xmlGetProp(node, (xmlChar *)"inherit");
  if(inheritid) {
    snprintf(xpath_expr, sizeof(xpath_expr), "//*[@id=\"%s\"]", inheritid);
    noit_conf_get_into_hash(NULL, xpath_expr, table);
  }
  /* 3. */
  noit_conf_get_elements_into_hash(node, "*", table);

 out:
  if(pobj) xmlXPathFreeObject(pobj);
  if(current_ctxt && current_ctxt != xpath_ctxt)
    xmlXPathFreeContext(current_ctxt);
}
noit_hash_table *noit_conf_get_hash(noit_conf_section_t section,
                                    const char *path) {
  noit_hash_table *table = NULL;

  table = calloc(1, sizeof(*table));
  noit_conf_get_into_hash(section, path, table);
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

static int
noit_console_write_xml(void *vncct, const char *buffer, int len) {
  noit_console_closure_t ncct = vncct;
  return nc_write(ncct, buffer, len);
}
static int
noit_console_close_xml(void *vncct) {
  return 0;
}

int
noit_conf_reload(noit_console_closure_t ncct,
                 int argc, char **argv,
                 noit_console_state_t *state, void *closure) {
  if(noit_conf_load(master_config_file)) {
    nc_printf(ncct, "error loading config\n");
    return -1;
  }
  return 0;
}
int
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
int
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

void
noit_conf_log_init(const char *toplevel) {
  int i, cnt = 0, o, ocnt = 0;
  noit_conf_section_t *log_configs, *outlets;
  char path[256];

  snprintf(path, sizeof(path), "/%s/logs//log", toplevel);
  log_configs = noit_conf_get_sections(NULL, path, &cnt);
  noitL(noit_stderr, "Found %d %s stanzas\n", cnt, path);
  for(i=0; i<cnt; i++) {
    noit_log_stream_t ls;
    char name[256], type[256], path[256];
    noit_hash_table *config;
    noit_conf_boolean disabled;

    if(!noit_conf_get_stringbuf(log_configs[i],
                                "ancestor-or-self::node()/@name",
                                name, sizeof(name))) {
      noitL(noit_error, "log section %d does not have a name attribute\n", i+1);
      exit(-1);
    }
    if(!noit_conf_get_stringbuf(log_configs[i],
                                "ancestor-or-self::node()/@type",
                                type, sizeof(type))) {
      type[0] = '\0';
    }
    if(!noit_conf_get_stringbuf(log_configs[i],
                                "ancestor-or-self::node()/@path",
                                path, sizeof(path))) {
      path[0] = '\0';
    }
    config = noit_conf_get_hash(log_configs[i],
                                "ancestor-or-self::node()/config/*");
    ls = noit_log_stream_new(name, type[0] ? type : NULL,
                             path[0] ? path : NULL, config);
    if(!ls) {
      fprintf(stderr, "Error configuring log: %s[%s:%s]\n", name, type, path);
      exit(-1);
    }

    if(noit_conf_get_boolean(log_configs[i],
                             "ancestor-or-self::node()/@disabled",
                             &disabled) && disabled)
      ls->enabled = 0;
      
    outlets = noit_conf_get_sections(log_configs[i],
                                     "ancestor-or-self::node()/outlet", &ocnt);
    noitL(noit_debug, "found %d outlets for log '%s'\n", ocnt, name);

    for(o=0; o<ocnt; o++) {
      noit_log_stream_t outlet;
      char oname[256];
      noit_conf_get_stringbuf(outlets[o], "@name",
                              oname, sizeof(oname));
      outlet = noit_log_stream_find(oname);
      noitL(noit_debug, "log(%s) => log(%s)\n", name, oname);
      if(!outlet) {
        fprintf(stderr, "Cannot find outlet '%s' for %s[%s:%s]\n", oname,
              name, type, path);
        exit(-1);
      }
      else
        noit_log_stream_add_stream(ls, outlet);
    }
    if(outlets) free(outlets);
  }
  if(log_configs) free(log_configs);
}
