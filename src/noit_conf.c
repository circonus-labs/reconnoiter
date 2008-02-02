/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include <stdio.h>
#include <assert.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#include "noit_conf.h"
#include "utils/noit_hash.h"
#include "utils/noit_log.h"

/* tmp hash impl, replace this with something nice */
static noit_hash_table _tmp_config = NOIT_HASH_EMPTY;
static xmlDocPtr master_config = NULL;
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


void noit_conf_init() {
  int i;
  for(i = 0; config_info[i].key != NULL; i++) {
    noit_hash_store(&_compiled_fallback,
                    strdup(config_info[i].key), strlen(config_info[i].key),
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
  xmlXPathObjectPtr pobj;
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
  if(current_ctxt && current_ctxt != xpath_ctxt)
    xmlXPathFreeContext(current_ctxt);
  return table;
}
noit_conf_section_t noit_conf_get_section(noit_conf_section_t section,
                                          const char *path) {
  noit_conf_section_t subsection = NULL;
  xmlXPathObjectPtr pobj;
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
  if(current_ctxt && current_ctxt != xpath_ctxt)
    xmlXPathFreeContext(current_ctxt);
  return subsection;
}
noit_conf_section_t *noit_conf_get_sections(noit_conf_section_t section,
                                            const char *path,
                                            int *cnt) {
  int i;
  noit_conf_section_t *sections = NULL;
  xmlXPathObjectPtr pobj;
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

