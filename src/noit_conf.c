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
  struct recurrent_journaler *rj;
  struct timeval __now;
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
void noit_conf_init(const char *toplevel) {
  int i;
  char keystr[256];
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

int noit_conf_load(const char *path) {
  xmlDocPtr new_config;
  new_config = xmlParseFile(path);
  if(new_config) {
    if(master_config) xmlFreeDoc(master_config);
    if(xpath_ctxt) xmlXPathFreeContext(xpath_ctxt);

    master_config = new_config;
    xpath_ctxt = xmlXPathNewContext(master_config);
    if(path != master_config_file) realpath(path, master_config_file);
    noit_conf_mark_changed();
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
  char *str;
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
  if(noit_hash_retrieve(&_compiled_fallback,
                        path, strlen(path), (void **)&str)) {
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
    xmlFree(str);
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
int noit_conf_get_boolean(noit_conf_section_t section,
                          const char *path, noit_conf_boolean *value) {
  char *str;
  if(_noit_conf_get_string(section,NULL,path,&str)) {
    if(!strcasecmp(str, "true") ||
       !strcasecmp(str, "on")) *value = noit_true;
    else *value = noit_false;
    xmlFree(str);
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
  xmlSaveFormatFileTo(out, master_config, "utf8", 1);
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
  len = xmlSaveFormatFileTo(out, master_config, "utf8", 1);
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
  xmlSaveFormatFileTo(out, master_config, "utf8", 1);
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
  xmlSaveFormatFileTo(out, master_config, "utf8", 1);
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
