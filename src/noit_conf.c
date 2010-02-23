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

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <zlib.h>

#include "noit_conf.h"
#include "noit_check.h"
#include "noit_console.h"
#include "utils/noit_hash.h"
#include "utils/noit_log.h"
#include "utils/noit_b64.h"

/* tmp hash impl, replace this with something nice */
static noit_hash_table _tmp_config = NOIT_HASH_EMPTY;
static xmlDocPtr master_config = NULL;
static int config_include_cnt = -1;
static struct { 
  xmlNodePtr insertion_point;
  xmlNodePtr old_children;
  xmlDocPtr doc;
  xmlNodePtr root;
} *config_include_nodes = NULL;

static char *root_node_name = NULL;
static char master_config_file[PATH_MAX] = "";
static xmlXPathContextPtr xpath_ctxt = NULL;

/* This is used to notice config changes and journal the config out
 * using a user-specified function.  It supports allowing multiple config
 * changed to coalesce so you don't write out 1000 changes in a few seconds.
 */
static u_int32_t __config_gen = 0;
static u_int32_t __config_coalesce = 0;
static u_int32_t __config_coalesce_time = 0;
void noit_conf_coalesce_changes(u_int32_t seconds) {
  __config_coalesce_time = seconds;
}
void noit_conf_mark_changed() {
  /* increment the change counter -- in case anyone cares */
  __config_gen++;
  /* reset the coalesce counter.  It is decremented each second and
   * the journal function fires on a transition from 1 => 0
   */
  __config_coalesce = __config_coalesce_time;
}
struct recurrent_journaler {
  int (*journal_config)(void *);
  void *jc_closure;
};
static int
noit_conf_watch_config_and_journal(eventer_t e, int mask, void *closure,
                                   struct timeval *now) {
  struct recurrent_journaler *rj = closure;
  eventer_t newe;

  if(__config_coalesce == 1)
    rj->journal_config(rj->jc_closure);
  if(__config_coalesce > 0)
    __config_coalesce--;

  /* Schedule the same event to fire a second form now */
  newe = eventer_alloc();
  gettimeofday(&newe->whence, NULL);
  newe->whence.tv_sec += 1;
  newe->mask = EVENTER_TIMER;
  newe->callback = noit_conf_watch_config_and_journal;
  newe->closure = closure;
  eventer_add(newe);
  return 0;
}
void
noit_conf_watch_and_journal_watchdog(int (*f)(void *), void *c) {
  static int callbacknamed = 0;
  struct recurrent_journaler *rj;
  struct timeval __now;

  if(!callbacknamed) {
    callbacknamed = 1;
    eventer_name_callback("noit_conf_watch_config_and_journal",
                          noit_conf_watch_config_and_journal);
  }
  rj = calloc(1, sizeof(*rj));
  rj->journal_config = f;
  rj->jc_closure = c;
  gettimeofday(&__now, NULL);
  noit_conf_watch_config_and_journal(NULL, EVENTER_TIMER, rj, &__now);
}

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
  { "/%s/eventer/@implementation", DEFAULT_EVENTER },
  { "/%s/modules/@directory", MODULES_DIR },

  { NULL, NULL }
};

void noit_conf_xml_error_func(void *ctx, const char *format, ...) {
  struct timeval __now;
  noit_log_stream_t ls = ctx;
  va_list arg;
  if(!ls) return;
  va_start(arg, format);
  gettimeofday(&__now,  NULL);
  noit_vlog(ls, &__now, __FILE__, __LINE__, format, arg);
  va_end(arg);
}
void noit_conf_xml_error_ext_func(void *ctx, xmlErrorPtr err) {
  struct timeval __now;
  noit_log_stream_t ls = ctx;
  if(!ls) return;
  gettimeofday(&__now,  NULL);
  if(err->file)
    noit_log(ls, &__now, err->file, err->line,
             "XML error [%d/%d] in %s on line %d %s\n",
             err->domain, err->code, err->file, err->line, err->message);
  else
    noit_log(ls, &__now, err->file, err->line,
             "XML error [%d/%d] %s\n",
             err->domain, err->code, err->message);
}


DECLARE_CHECKER(name)
void noit_conf_init(const char *toplevel) {
  int i;
  char keystr[256];
  COMPILE_CHECKER(name, "^[-_\\.:/a-zA-Z0-9]+$");
  xmlSetGenericErrorFunc(noit_error, noit_conf_xml_error_func);
  xmlSetStructuredErrorFunc(noit_error, noit_conf_xml_error_ext_func);
  for(i = 0; config_info[i].key != NULL; i++) {
    snprintf(keystr, sizeof(keystr), config_info[i].key, toplevel);
    noit_hash_store(&_compiled_fallback,
                    strdup(keystr), strlen(keystr),
                    (void *)strdup(config_info[i].val));
  }
  xmlKeepBlanksDefault(0);
  xmlInitParser();
  xmlXPathInit();
}

void
noit_conf_magic_separate(xmlDocPtr doc) {
  assert(config_include_cnt != -1);
  if(config_include_nodes) {
    int i;
    for(i=0; i<config_include_cnt; i++) {
      if(config_include_nodes[i].doc) {
        xmlNodePtr n;
        for(n=config_include_nodes[i].insertion_point->children;
            n; n = n->next)
          n->parent = config_include_nodes[i].root;
        config_include_nodes[i].insertion_point->children =
          config_include_nodes[i].old_children;
        xmlFreeDoc(config_include_nodes[i].doc);
      }
    }
    free(config_include_nodes);
  }
  config_include_nodes = NULL;
  config_include_cnt = -1;
}
void
noit_conf_kansas_city_shuffle_redo(xmlDocPtr doc) {
  if(config_include_nodes) {
    int i;
    for(i=0; i<config_include_cnt; i++) {
      if(config_include_nodes[i].doc) {
        xmlNodePtr n;
        config_include_nodes[i].insertion_point->children =
          config_include_nodes[i].root->children;
        for(n=config_include_nodes[i].insertion_point->children;
            n; n = n->next)
          n->parent = config_include_nodes[i].insertion_point;
      }
    }
  }
}
void
noit_conf_kansas_city_shuffle_undo(xmlDocPtr doc) {
  if(config_include_nodes) {
    int i;
    for(i=0; i<config_include_cnt; i++) {
      if(config_include_nodes[i].doc) {
        xmlNodePtr n;
        for(n=config_include_nodes[i].insertion_point->children;
            n; n = n->next)
          n->parent = config_include_nodes[i].root;
        config_include_nodes[i].insertion_point->children =
          config_include_nodes[i].old_children;
      }
    }
  }
}
int
noit_conf_magic_mix(const char *parentfile, xmlDocPtr doc) {
  xmlXPathContextPtr mix_ctxt = NULL;
  xmlXPathObjectPtr pobj = NULL;
  xmlNodePtr node;
  int i, cnt, rv = 0;

  assert(config_include_cnt == -1);

  config_include_cnt = 0;
  mix_ctxt = xmlXPathNewContext(doc);
  pobj = xmlXPathEval((xmlChar *)"//include[@file]", mix_ctxt);
  if(!pobj) goto out;
  if(pobj->type != XPATH_NODESET) goto out;
  if(xmlXPathNodeSetIsEmpty(pobj->nodesetval)) goto out;
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  if(cnt > 0)
    config_include_nodes = calloc(cnt, sizeof(*config_include_nodes));
  for(i=0; i<cnt; i++) {
    char *path, *infile;
    node = xmlXPathNodeSetItem(pobj->nodesetval, i);
    path = (char *)xmlGetProp(node, (xmlChar *)"file");
    if(!path) continue;
    if(*path == '/') infile = strdup(path);
    else {
      char *cp;
      infile = malloc(PATH_MAX);
      strlcpy(infile, parentfile, PATH_MAX);
      for(cp = infile + strlen(infile) - 1; cp > infile; cp--) {
        if(*cp == '/') { *cp = '\0'; break; }
        else *cp = '\0';
      }
      strlcat(infile, "/", PATH_MAX);
      strlcat(infile, path, PATH_MAX);
    }
    config_include_nodes[i].doc = xmlParseFile(infile);
    if(config_include_nodes[i].doc) {
      xmlNodePtr n;
      config_include_nodes[i].insertion_point = node;
      config_include_nodes[i].root = xmlDocGetRootElement(config_include_nodes[i].doc);
      config_include_nodes[i].old_children = node->children;
      node->children = config_include_nodes[i].root->children;
      for(n=node->children; n; n = n->next)
        n->parent = config_include_nodes[i].insertion_point;
    }
    else {
      noitL(noit_error, "Could not load: '%s'\n", infile);
      rv = -1;
    }
    free(infile);
  }
  config_include_cnt = cnt;
  noitL(noit_debug, "Processed %d includes\n", config_include_cnt);
 out:
  if(pobj) xmlXPathFreeObject(pobj);
  if(mix_ctxt) xmlXPathFreeContext(mix_ctxt);
  return rv;
}

int noit_conf_load(const char *path) {
  int rv = 0;
  xmlDocPtr new_config;
  xmlNodePtr root;
  new_config = xmlParseFile(path);
  if(new_config) {
    root = xmlDocGetRootElement(new_config);
    if(root_node_name) free(root_node_name);
    root_node_name = strdup((char *)root->name);

    if(master_config) {
      /* separate all includes */
      noit_conf_magic_separate(master_config);
      xmlFreeDoc(master_config);
    }
    if(xpath_ctxt) xmlXPathFreeContext(xpath_ctxt);

    master_config = new_config;
    /* mixin all the includes */
    if(noit_conf_magic_mix(path, master_config)) rv = -1;

    xpath_ctxt = xmlXPathNewContext(master_config);
    if(path != master_config_file)
      if(realpath(path, master_config_file) == NULL)
        noitL(noit_error, "realpath failed: %s\n", strerror(errno));
    noit_conf_mark_changed();
    return rv;
  }
  rv = -1;
  return rv;
}

char *noit_conf_config_filename() {
  return strdup(master_config_file);
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
    xmlFree(value);
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
    if(parent_node != current_node)
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
  const char *str;
  int i, rv = 1;
  xmlXPathObjectPtr pobj = NULL;
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
        if(xmlXPathNodeSetIsEmpty(pobj->nodesetval)) goto fallback;
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
 fallback:
  if(noit_hash_retr_str(&_compiled_fallback,
                        path, strlen(path), &str)) {
    *value = (char *)xmlStrdup((xmlChar *)str);
    goto found;
  }
  rv = 0;
 found:
  if(pobj) xmlXPathFreeObject(pobj);
  if(current_ctxt && current_ctxt != xpath_ctxt)
    xmlXPathFreeContext(current_ctxt);
  return rv;
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
    xmlFree(str);
    return 1;
  }
  return 0;
}
int noit_conf_get_stringbuf(noit_conf_section_t section,
                            const char *path, char *buf, int len) {
  char *str;
  if(_noit_conf_get_string(section,NULL,path,&str)) {
    strlcpy(buf, str, len);
    xmlFree(str);
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
int noit_conf_string_to_int(const char *str) {
  int base = 10;
  if(!str) return 0;
  if(str[0] == '0') {
    if(str[1] == 'x') base = 16;
    else base = 8;
  }
  return strtol(str, NULL, base);
}
int noit_conf_get_int(noit_conf_section_t section,
                      const char *path, int *value) {
  char *str;
  if(_noit_conf_get_string(section,NULL,path,&str)) {
    *value = (int)noit_conf_string_to_int(str);
    xmlFree(str);
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
float noit_conf_string_to_float(const char *str) {
  if(!str) return 0.0;
  return atof(str);
}
int noit_conf_get_float(noit_conf_section_t section,
                        const char *path, float *value) {
  char *str;
  if(_noit_conf_get_string(section,NULL,path,&str)) {
    *value = noit_conf_string_to_float(str);
    xmlFree(str);
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
noit_boolean noit_conf_string_to_boolean(const char *str) {
  if(!str) return noit_false;
  if(!strcasecmp(str, "true") || !strcasecmp(str, "on")) return noit_true;
  return noit_false;
}
int noit_conf_get_boolean(noit_conf_section_t section,
                          const char *path, noit_boolean *value) {
  char *str;
  if(_noit_conf_get_string(section,NULL,path,&str)) {
    *value = noit_conf_string_to_boolean(str);
    xmlFree(str);
    return 1;
  }
  return 0;
}
int noit_conf_set_boolean(noit_conf_section_t section,
                          const char *path, noit_boolean value) {
  if(value == noit_true)
    return noit_conf_set_string(section,path,"true");
  return noit_conf_set_string(section,path,"false");
}

struct config_line_vstr {
  char *buff;
  int raw_len;
  int len;
  int allocd;
  enum { CONFIG_RAW = 0, CONFIG_COMPRESSED, CONFIG_B64 } target, encoded;
};
static int
noit_config_log_write_xml(void *vstr, const char *buffer, int len) {
  struct config_line_vstr *clv = vstr;
  assert(clv->encoded == CONFIG_RAW);
  if(!clv->buff) {
    clv->allocd = 8192;
    clv->buff = malloc(clv->allocd);
  }
  while(len + clv->len > clv->allocd) {
    char *newbuff;
    int newsize = clv->allocd;
    newsize <<= 1;
    newbuff = realloc(clv->buff, newsize);
    if(!newbuff) {
      return -1;
    }
    clv->allocd = newsize;
    clv->buff = newbuff;
  }
  memcpy(clv->buff + clv->len, buffer, len);
  clv->len += len;
  return len;
}
static int
noit_config_log_close_xml(void *vstr) {
  struct config_line_vstr *clv = vstr;
  uLong initial_dlen, dlen;
  char *compbuff, *b64buff;

  if(clv->buff == NULL) {
    clv->encoded = clv->target;
    return 0;
  }
  clv->raw_len = clv->len;
  assert(clv->encoded == CONFIG_RAW);
  if(clv->encoded == clv->target) return 0;

  /* Compress */
  initial_dlen = dlen = compressBound(clv->len);
  compbuff = malloc(initial_dlen);
  if(!compbuff) return -1;
  if(Z_OK != compress2((Bytef *)compbuff, &dlen,
                       (Bytef *)clv->buff, clv->len, 9)) {
    noitL(noit_error, "Error compressing config for transmission.\n");
    free(compbuff);
    return -1;
  }
  free(clv->buff);
  clv->buff = compbuff;
  clv->allocd = initial_dlen;
  clv->len = dlen;
  clv->encoded = CONFIG_COMPRESSED;
  if(clv->encoded == clv->target) return 0;

  /* Encode */
  initial_dlen = ((clv->len + 2) / 3) * 4;
  b64buff = malloc(initial_dlen);
  dlen = noit_b64_encode((unsigned char *)clv->buff, clv->len,
                         b64buff, initial_dlen);
  if(dlen == 0) {
    free(b64buff);
    return -1;
  }
  free(clv->buff);
  clv->buff = b64buff;
  clv->allocd = initial_dlen;
  clv->len = dlen;
  clv->encoded = CONFIG_B64;
  if(clv->encoded == clv->target) return 0;
  return -1;
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
  noit_conf_kansas_city_shuffle_undo(master_config);
  xmlSaveFormatFileTo(out, master_config, "utf8", 1);
  noit_conf_kansas_city_shuffle_redo(master_config);
  return 0;
}
int
noit_conf_write_file_console(noit_console_closure_t ncct,
                             int argc, char **argv,
                             noit_console_state_t *state, void *closure) {
  int rv;
  char *err = NULL;
  rv = noit_conf_write_file(&err);
  nc_printf(ncct, "%s\n", err);
  if(err) free(err);
  return rv;
}
int
noit_conf_write_file(char **err) {
  int fd, len;
  char master_file_tmp[PATH_MAX];
  char errstr[1024];
  xmlOutputBufferPtr out;
  xmlCharEncodingHandlerPtr enc;
  struct stat st;
  mode_t mode = 0640; /* the default */

  if(stat(master_config_file, &st) == 0)
    mode = st.st_mode;
  snprintf(master_file_tmp, sizeof(master_file_tmp),
           "%s.tmp", master_config_file);
  unlink(master_file_tmp);
  fd = open(master_file_tmp, O_CREAT|O_EXCL|O_WRONLY, mode);
  if(fd < 0) {
    snprintf(errstr, sizeof(errstr), "Failed to open tmp file: %s",
             strerror(errno));
    if(err) *err = strdup(errstr);
    return -1;
  }
  enc = xmlGetCharEncodingHandler(XML_CHAR_ENCODING_UTF8);
  out = xmlOutputBufferCreateFd(fd, enc);
  if(!out) {
    close(fd);
    unlink(master_file_tmp);
    if(err) *err = strdup("internal error: OutputBufferCreate failed");
    return -1;
  }
  noit_conf_kansas_city_shuffle_undo(master_config);
  len = xmlSaveFormatFileTo(out, master_config, "utf8", 1);
  noit_conf_kansas_city_shuffle_redo(master_config);
  close(fd);
  if(len <= 0) {
    if(err) *err = strdup("internal error: writing to tmp file failed.");
    return -1;
  }
  if(rename(master_file_tmp, master_config_file) != 0) {
    snprintf(errstr, sizeof(errstr), "Failed to replace file: %s",
             strerror(errno));
    if(err) *err = strdup(errstr);
    return -1;
  }
  snprintf(errstr, sizeof(errstr), "%d bytes written.", len);
  if(err) *err = strdup(errstr);
  return 0;
}
char *
noit_conf_xml_in_mem(size_t *len) {
  struct config_line_vstr *clv;
  xmlOutputBufferPtr out;
  xmlCharEncodingHandlerPtr enc;
  char *rv;

  clv = calloc(1, sizeof(*clv));
  clv->target = CONFIG_RAW;
  enc = xmlGetCharEncodingHandler(XML_CHAR_ENCODING_UTF8);
  out = xmlOutputBufferCreateIO(noit_config_log_write_xml,
                                noit_config_log_close_xml,
                                clv, enc);
  noit_conf_kansas_city_shuffle_undo(master_config);
  xmlSaveFormatFileTo(out, master_config, "utf8", 1);
  noit_conf_kansas_city_shuffle_redo(master_config);
  if(clv->encoded != CONFIG_RAW) {
    noitL(noit_error, "Error logging configuration\n");
    if(clv->buff) free(clv->buff);
    free(clv);
    return NULL;
  }
  rv = clv->buff;
  *len = clv->len;
  free(clv);
  return rv;
}

int
noit_conf_write_log() {
  static u_int32_t last_write_gen = 0;
  static noit_log_stream_t config_log = NULL;
  struct timeval __now;
  xmlOutputBufferPtr out;
  xmlCharEncodingHandlerPtr enc;
  struct config_line_vstr *clv;
  SETUP_LOG(config, return -1);

  /* We know we haven't changed */
  if(last_write_gen == __config_gen) return 0;

  gettimeofday(&__now, NULL);
  clv = calloc(1, sizeof(*clv));
  clv->target = CONFIG_B64;
  enc = xmlGetCharEncodingHandler(XML_CHAR_ENCODING_UTF8);
  out = xmlOutputBufferCreateIO(noit_config_log_write_xml,
                                noit_config_log_close_xml,
                                clv, enc);
  noit_conf_kansas_city_shuffle_undo(master_config);
  xmlSaveFormatFileTo(out, master_config, "utf8", 1);
  noit_conf_kansas_city_shuffle_redo(master_config);
  if(clv->encoded != CONFIG_B64) {
    noitL(noit_error, "Error logging configuration\n");
    if(clv->buff) free(clv->buff);
    free(clv);
    return -1;
  }
  noitL(config_log, "n\t%lu.%03lu\t%d\t%.*s\n",
        __now.tv_sec, __now.tv_usec / 1000UL, clv->raw_len,
        clv->len, clv->buff);
  free(clv->buff);
  free(clv);
  last_write_gen = __config_gen;
  return 0;
}

void
noit_conf_log_init(const char *toplevel) {
  int i, cnt = 0, o, ocnt = 0;
  noit_conf_section_t *log_configs, *outlets;
  char path[256];

  snprintf(path, sizeof(path), "/%s/logs//log", toplevel);
  log_configs = noit_conf_get_sections(NULL, path, &cnt);
  noitL(noit_debug, "Found %d %s stanzas\n", cnt, path);
  for(i=0; i<cnt; i++) {
    noit_log_stream_t ls;
    char name[256], type[256], path[256];
    noit_hash_table *config;
    noit_boolean disabled;
    noit_boolean debug;

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
                             path[0] ? path : NULL, NULL, config);
    if(!ls) {
      fprintf(stderr, "Error configuring log: %s[%s:%s]\n", name, type, path);
      exit(-1);
    }

    if(noit_conf_get_boolean(log_configs[i],
                             "ancestor-or-self::node()/@disabled",
                             &disabled) && disabled)
      ls->enabled = 0;
      
    if(noit_conf_get_boolean(log_configs[i],
                             "ancestor-or-self::node()/@debug",
                             &debug) && debug)
      ls->debug = 1;
      
    outlets = noit_conf_get_sections(log_configs[i],
                                     "ancestor-or-self::node()/outlet", &ocnt);
    noitL(noit_debug, "Found %d outlets for log '%s'\n", ocnt, name);

    for(o=0; o<ocnt; o++) {
      noit_log_stream_t outlet;
      char oname[256];
      noit_conf_get_stringbuf(outlets[o], "@name",
                              oname, sizeof(oname));
      outlet = noit_log_stream_find(oname);
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
  xmlXPathContextPtr xpath_ctxt = NULL;
  xmlNodePtr node = NULL, newnode;
  vpsized_int delete = (vpsized_int)closure;

  noit_conf_xml_xpath(NULL, &xpath_ctxt);
  if(argc != 1) {
    nc_printf(ncct, "requires one argument\n");
    return -1;
  }
  if(strchr(argv[0], '/')) {
    nc_printf(ncct, "invalid section name\n");
    return -1;
  }
  if(!strcmp(argv[0], "check") ||
     !strcmp(argv[0], "noit") ||
     !strcmp(argv[0], "filterset") ||
     !strcmp(argv[0], "config")) {
    nc_printf(ncct, "%s is reserved.\n", argv[0]);
    return -1;
  }
  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
  if(!strcmp(info->path, "/")) {
    nc_printf(ncct, "manipulation of toplevel section disallowed\n");
    return -1;
  }

  if(delete) {
    /* We cannot delete if we have checks */
    snprintf(xpath, sizeof(xpath), "/%s%s/%s//check", root_node_name,
             info->path, argv[0]);
    pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
    if(!pobj || pobj->type != XPATH_NODESET ||
       !xmlXPathNodeSetIsEmpty(pobj->nodesetval)) {
      err = "cannot delete section, has checks";
      goto bad;
    }
    if(pobj) xmlXPathFreeObject(pobj);
  }

  snprintf(xpath, sizeof(xpath), "/%s%s/%s", root_node_name,
           info->path, argv[0]);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET) {
    err = "internal error: cannot detect section";
    goto bad;
  }
  if(!delete && !xmlXPathNodeSetIsEmpty(pobj->nodesetval)) {
    if(xmlXPathNodeSetGetLength(pobj->nodesetval) == 1) {
      node = xmlXPathNodeSetItem(pobj->nodesetval, 0);
      if(info->path) free(info->path);
      info->path = strdup((char *)xmlGetNodePath(node) +
                          1 + strlen(root_node_name));
      goto cdout;
    }
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
    noit_conf_mark_changed();
    return 0;
  }
  if(pobj) xmlXPathFreeObject(pobj);
  pobj = NULL;

  if(!strcmp(argv[0],"include")) {
    err = "include is a reserved section name";
    goto bad;
  }
  path = strcmp(info->path, "/") ? info->path : "";
  snprintf(xpath, sizeof(xpath), "/%s%s", root_node_name, path);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET ||
     xmlXPathNodeSetGetLength(pobj->nodesetval) != 1) {
    err = "path invalid?";
    goto bad;
  }
  node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, 0);
  if((newnode = xmlNewChild(node, NULL, (xmlChar *)argv[0], NULL)) != NULL) {
    noit_conf_mark_changed();
    if(info->path) free(info->path);
    info->path = strdup((char *)xmlGetNodePath(newnode) + 1 +
                        strlen(root_node_name));
  }
  else {
    err = "failed to create section";
    goto bad;
  }
 cdout:
  if(pobj) xmlXPathFreeObject(pobj);
  return 0;
 bad:
  if(pobj) xmlXPathFreeObject(pobj);
  nc_printf(ncct, "%s\n", err);
  return -1;
}

int
noit_console_generic_show(noit_console_closure_t ncct,
                          int argc, char **argv,
                          noit_console_state_t *state, void *closure) {
  int i, cnt, titled = 0, cliplen = 0;
  const char *path = "", *basepath = NULL;
  char xpath[1024];
  noit_conf_t_userdata_t *info = NULL;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL, current_ctxt;
  xmlDocPtr master_config = NULL;
  xmlNodePtr node = NULL;

  noit_conf_xml_xpath(&master_config, &xpath_ctxt);
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
  cliplen = strlen(root_node_name) + 2; /* /name/ */

  /* If we are in configuration mode
   * and we are without an argument or the argument is absolute,
   * clip the current path off */
  if(info && (argc == 0 || path[0] != '/')) cliplen += strlen(basepath);
  if(!path[0] || path[0] == '/') /* base only, or absolute path requested */
    snprintf(xpath, sizeof(xpath), "/%s%s/@*", root_node_name, path);
  else
    snprintf(xpath, sizeof(xpath), "/%s%s/%s/@*", root_node_name,
             basepath, path);

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
    if(node->children && node->children == xmlGetLastChild(node) &&
      xmlNodeIsText(node->children)) {
      if(!titled++) nc_printf(ncct, "== Section Settings ==\n");
      nc_printf(ncct, "%s: %s\n", xmlGetNodePath(node) + cliplen,
                xmlXPathCastNodeToString(node->children));
    }
  }
  xmlXPathFreeObject(pobj);

  /* _shorten string_ turning last { / @ * } to { / * } */
  if(!path[0] || path[0] == '/') /* base only, or absolute path requested */
    snprintf(xpath, sizeof(xpath), "/%s%s/*", root_node_name, path);
  else
    snprintf(xpath, sizeof(xpath), "/%s%s/%s/*",
             root_node_name, basepath, path);
  pobj = xmlXPathEval((xmlChar *)xpath, current_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET) {
    nc_printf(ncct, "no such object\n");
    goto bad;
  }
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  titled = 0;
  for(i=0; i<cnt; i++) {
    node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, i);
    if(!(node->children && node->children == xmlGetLastChild(node) &&
         xmlNodeIsText(node->children))) {
      if(!titled++) nc_printf(ncct, "== Subsections ==\n");
      nc_printf(ncct, "%s\n", xmlGetNodePath(node) + cliplen);
    }
  }
  xmlXPathFreeObject(pobj);
  return 0;
 bad:
  if(pobj) xmlXPathFreeObject(pobj);
  return -1;
}
int
noit_console_config_cd(noit_console_closure_t ncct,
                       int argc, char **argv,
                       noit_console_state_t *state, void *closure) {
  const char *err = "internal error";
  char *path, xpath[1024];
  noit_conf_t_userdata_t *info;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL, current_ctxt;
  xmlNodePtr node = NULL;
  char *dest;

  noit_conf_xml_xpath(NULL, &xpath_ctxt);
  if(argc != 1 && !closure) {
    nc_printf(ncct, "requires one argument\n");
    return -1;
  }
  dest = argc ? argv[0] : (char *)closure;
  info = noit_console_userdata_get(ncct, NOIT_CONF_T_USERDATA);
  if(dest[0] == '/')
    snprintf(xpath, sizeof(xpath), "/%s%s", root_node_name, dest);
  else {
    snprintf(xpath, sizeof(xpath), "/%s%s/%s", root_node_name,
             info->path, dest);
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
  if(!strcmp((char *)node->name, "check") ||
     !strcmp((char *)node->name, "noit") ||
     !strcmp((char *)node->name, "filterset") ||
     !strcmp((char *)node->name, "config")) {
    err = "reserved word";
    goto bad;
  }
  path = (char *)xmlGetNodePath(node);
  if(strlen(path) < strlen(root_node_name) + 1 ||
     strncmp(path + 1, root_node_name, strlen(root_node_name)) ||
     (path[strlen(root_node_name) + 1] != '/' &&
      path[strlen(root_node_name) + 1] != '\0')) {
    err = "new path outside out tree";
    goto bad;
  }
  free(info->path);
  if(!strcmp(path + 1, root_node_name))
    info->path = strdup("/");
  else
    info->path = strdup((char *)xmlGetNodePath(node) + 1 +
                        strlen(root_node_name));
  if(pobj) xmlXPathFreeObject(pobj);
  if(closure) noit_console_state_pop(ncct, argc, argv, NULL, NULL);
  return 0;
 bad:
  if(pobj) xmlXPathFreeObject(pobj);
  nc_printf(ncct, "%s [%s]\n", err, xpath);
  return -1;
}

char *
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

#define NEW_STATE(a) (a) = noit_console_state_alloc()
#define ADD_CMD(a,cmd,func,ac,ss,c) \
  noit_console_state_add_cmd((a), \
    NCSCMD(cmd, func, ac, ss, c))
#define DELEGATE_CMD(a,cmd,ac,ss) \
  noit_console_state_add_cmd((a), \
    NCSCMD(cmd, noit_console_state_delegate, ac, ss, NULL))

void noit_console_conf_init() {
  noit_console_state_t *tl, *_conf_state, *_conf_t_state,
                       *_write_state, *_unset_state;

  tl = noit_console_state_initial();

  /* write <terimal|memory|file> */
  NEW_STATE(_write_state);
  ADD_CMD(_write_state, "terminal", noit_conf_write_terminal, NULL, NULL, NULL);
  ADD_CMD(_write_state, "file", noit_conf_write_file_console, NULL, NULL, NULL);
  /* write memory?  It's to a file, but I like router syntax */
  ADD_CMD(_write_state, "memory", noit_conf_write_file_console, NULL, NULL, NULL);

  NEW_STATE(_unset_state);
  ADD_CMD(_unset_state, "section",
          noit_console_config_section, NULL, NULL, (void *)1);

  NEW_STATE(_conf_t_state); 
  _conf_t_state->console_prompt_function = conf_t_prompt;
  noit_console_state_add_cmd(_conf_t_state, &console_command_exit);

  ADD_CMD(_conf_t_state, "ls", noit_console_generic_show, NULL, NULL, NULL);
  ADD_CMD(_conf_t_state, "cd", noit_console_config_cd, NULL, NULL, NULL);
  ADD_CMD(_conf_t_state, "section",
          noit_console_config_section, NULL, NULL, (void *)0);

  DELEGATE_CMD(_conf_t_state, "write",
               noit_console_opt_delegate, _write_state);
  DELEGATE_CMD(_conf_t_state, "no", noit_console_opt_delegate, _unset_state);

  NEW_STATE(_conf_state);
  ADD_CMD(_conf_state, "terminal",
          noit_console_state_conf_terminal, NULL, _conf_t_state, NULL);

  ADD_CMD(tl, "configure",
          noit_console_state_delegate, noit_console_opt_delegate,
          _conf_state, NULL);
  ADD_CMD(tl, "write",
          noit_console_state_delegate, noit_console_opt_delegate,
          _write_state, NULL);
}

