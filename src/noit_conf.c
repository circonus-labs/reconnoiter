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
#include <dirent.h>
#include <errno.h>
#include <assert.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <zlib.h>

#include "noit_conf.h"
#include "noit_check.h"
#include "noit_console.h"
#include "noit_version.h"
#include "noit_xml.h"
#include "utils/noit_hash.h"
#include "utils/noit_log.h"
#include "utils/noit_b64.h"
#include "utils/noit_watchdog.h"

const char *_noit_branch = NOIT_BRANCH;
const char *_noit_version = NOIT_VERSION;

/* tmp hash impl, replace this with something nice */
static noit_log_stream_t xml_debug = NULL;
#define XML2LOG(log) do { \
  xmlSetGenericErrorFunc(log, noit_conf_xml_error_func); \
  xmlSetStructuredErrorFunc(log, noit_conf_xml_error_ext_func); \
} while(0)
#define XML2CONSOLE(ncct) do { \
  xmlSetGenericErrorFunc(ncct, noit_conf_xml_console_error_func); \
  xmlSetStructuredErrorFunc(ncct, noit_conf_xml_console_error_ext_func); \
} while(0)
static xmlDocPtr master_config = NULL;
static int config_include_cnt = -1;
static int backingstore_include_cnt = -1;

struct include_node_t{
  xmlNodePtr insertion_point;
  xmlNodePtr old_children;
  xmlDocPtr doc;
  xmlNodePtr root;
  int snippet;
  int ro;
  char path[255];
  int child_count;
  struct include_node_t *children;
};

typedef struct include_node_t include_node_t;

static include_node_t *config_include_nodes = NULL,
                      *backingstore_include_nodes = NULL;

typedef struct noit_xml_userdata {
  char       *name;
  char       *path;
  u_int64_t   dirty_time;
  struct noit_xml_userdata *freelist;
} noit_xml_userdata_t;

static noit_xml_userdata_t *backingstore_freelist = NULL;
static u_int64_t last_config_flush = 0;

#define is_stopnode_name(n) ((n) && \
    (!strcmp((char *)(n), "check") || \
     !strcmp((char *)(n), "config") || \
     !strcmp((char *)(n), "filterset")))
#define is_stopnode(node) ((node) && is_stopnode_name((node)->name))

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
static u_int64_t max_gen_count = 0;
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

void
write_out_include_files(include_node_t *include_nodes, int include_node_cnt) {
  int i;
  for(i=0; i<include_node_cnt; i++) {
    xmlOutputBufferPtr out;
    xmlCharEncodingHandlerPtr enc;
    mode_t mode = 0640;
    char filename[500];
    int len, fd;
    struct stat st;
    uid_t uid = 0;
    gid_t gid = 0;

    if(include_nodes[i].ro) {
      write_out_include_files(include_nodes[i].children, include_nodes[i].child_count);
      continue;
    }
    if(stat(include_nodes[i].path, &st) == 0) {
      mode = st.st_mode;
      uid = st.st_uid;
      gid = st.st_gid;
    }

    sprintf(filename, "%s.tmp", include_nodes[i].path);
    fd = open(filename, O_CREAT|O_TRUNC|O_WRONLY, mode);
    fchown(fd, uid, gid);

    enc = xmlGetCharEncodingHandler(XML_CHAR_ENCODING_UTF8);
    out = xmlOutputBufferCreateFd(fd, enc);
    len = xmlSaveFormatFileTo(out, include_nodes[i].doc, "utf8", 1);
    if (len < 0) {
      noitL(noit_error, "couldn't write out %s\n", include_nodes[i].path);
      close(fd);
      continue;
    }
    close(fd);
    write_out_include_files(include_nodes[i].children, include_nodes[i].child_count);
    if(rename(filename, include_nodes[i].path) != 0) {
      noitL(noit_error, "Failed to replace file %s: %s\n", include_nodes[i].path, strerror(errno));
    }
  }
}

static void
noit_xml_userdata_free(noit_xml_userdata_t *n) {
  if(n->name) free(n->name);
  if(n->path) free(n->path);
}

void
noit_conf_set_namespace(const char *ns) {
  xmlNsPtr nsptr;
  xmlNodePtr root;
  root = xmlDocGetRootElement(master_config);
  nsptr = xmlSearchNs(master_config, root, (xmlChar *)ns);
  if(!nsptr) {
    char url[128];
    snprintf(url, sizeof(url), "noit://module/%s", ns);
    xmlNewNs(root, (xmlChar *)url, (xmlChar *)ns);
  }
}

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

void noit_conf_xml_console_error_func(void *ctx, const char *format, ...) {
  noit_console_closure_t ncct = ctx;
  va_list arg;
  if(!ncct) return;
  va_start(arg, format);
  nc_vprintf(ncct, format, arg);
  va_end(arg);
}

void noit_conf_xml_console_error_ext_func(void *ctx, xmlErrorPtr err) {
  noit_console_closure_t ncct = ctx;
  if(!ctx) return;
  if(err->file)
    nc_printf(ncct, "XML error [%d/%d] in %s on line %d %s\n",
              err->domain, err->code, err->file, err->line, err->message);
  else
    nc_printf(ncct, "XML error [%d/%d] %s\n",
              err->domain, err->code, err->message);
}

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


void
noit_conf_poke(const char *toplevel, const char *key, const char *val) {
  char keystr[256];
  snprintf(keystr, sizeof(keystr), key, toplevel);
  noit_hash_store(&_compiled_fallback,
                  strdup(keystr), strlen(keystr),
                  (void *)strdup(val));
}

DECLARE_CHECKER(name)
void noit_conf_init(const char *toplevel) {
  int i;

  xml_debug = noit_log_stream_find("debug/xml");

  COMPILE_CHECKER(name, "^[-_\\.:/a-zA-Z0-9]+$");
  XML2LOG(noit_error);
  for(i = 0; config_info[i].key != NULL; i++)
    noit_conf_poke(toplevel, config_info[i].key, config_info[i].val);
  xmlKeepBlanksDefault(0);
  xmlInitParser();
  xmlXPathInit();
}

void
noit_conf_magic_separate_includes(include_node_t **root_include_nodes, int *cnt) {
  include_node_t *include_nodes = *root_include_nodes;
  assert(*cnt != -1);
  if(include_nodes) {
    int i;
    for(i=0; i<*cnt; i++) {
      noit_conf_magic_separate_includes(&(include_nodes[i].children), &(include_nodes[i].child_count));
      if(include_nodes[i].doc) {
        xmlNodePtr n;
        for(n=include_nodes[i].insertion_point->children;
            n; n = n->next)
          n->parent = include_nodes[i].root;
        include_nodes[i].insertion_point->children =
          include_nodes[i].old_children;
        xmlFreeDoc(include_nodes[i].doc);
      }
    }
    free(include_nodes);
  }
  *root_include_nodes = NULL;
  *cnt = -1;
}

void
noit_conf_magic_separate() {
  noit_conf_magic_separate_includes(&config_include_nodes, &config_include_cnt);
  assert(config_include_nodes == NULL);
  if(backingstore_include_nodes) {
    int i;
    for(i=0; i<backingstore_include_cnt; i++) {
      if(backingstore_include_nodes[i].doc) {
        xmlNodePtr n;
        for(n=backingstore_include_nodes[i].insertion_point->children;
            n; n = n->next) {
          n->parent = backingstore_include_nodes[i].root;
          n->parent->last = n;
        }
        backingstore_include_nodes[i].insertion_point->children =
          backingstore_include_nodes[i].old_children;
        for(n=backingstore_include_nodes[i].insertion_point->children;
            n; n = n->next) {
          n->parent->last = n; /* sets it to the last child */
        }
        xmlFreeDoc(backingstore_include_nodes[i].doc);
      }
    }
    free(backingstore_include_nodes);
  }
  backingstore_include_nodes = NULL;
  backingstore_include_cnt = -1;
}
void
noit_conf_kansas_city_shuffle_redo(include_node_t *include_nodes, int include_node_cnt) {
  if(include_nodes) {
    int i;
    for(i=0; i<include_node_cnt; i++) {
      noit_conf_kansas_city_shuffle_redo(include_nodes[i].children, include_nodes[i].child_count);
      if(include_nodes[i].doc) {
        xmlNodePtr n;

        if (!include_nodes[i].snippet)
          include_nodes[i].insertion_point->children =
            include_nodes[i].root->children;
        else
          include_nodes[i].insertion_point->children =
            include_nodes[i].root;

        for(n=include_nodes[i].insertion_point->children;
            n; n = n->next)
          n->parent = include_nodes[i].insertion_point;
      }
    }
  }
}
void
noit_conf_kansas_city_shuffle_undo(include_node_t *include_nodes, int include_node_cnt) {
  if(include_nodes) {
    int i;
    for(i=0; i<include_node_cnt; i++) {
      noit_conf_kansas_city_shuffle_undo(include_nodes[i].children, include_nodes[i].child_count);
      if(include_nodes[i].doc) {
        xmlNodePtr n;
        for(n=include_nodes[i].insertion_point->children;
            n; n = n->next)
          n->parent = include_nodes[i].root;
        include_nodes[i].insertion_point->children =
          include_nodes[i].old_children;
      }
    }
  }
}
static u_int64_t
usec_now() {
  u_int64_t usec;
  struct timeval tv;
  gettimeofday(&tv, NULL);
  usec = tv.tv_sec * 1000000UL;
  usec += tv.tv_usec;
  return usec;
}

static void
remove_emancipated_child_node(xmlNodePtr oldp, xmlNodePtr node) {
  /* node was once a child of oldp... it's still in it's children list
   * but node's parent isn't this child.
   */
  assert(node->parent != oldp);
  if(oldp->children == NULL) return;
  if(oldp->children == node) {
    oldp->children = node->next;
    node->next->prev = node->prev;
  }
  else {
    xmlNodePtr prev = oldp->children;
    for(prev = oldp->children; prev->next && prev->next != node; prev = prev->next);
    if(prev) prev->next = node->next;
    if(node->next) node->next->prev = prev;
  }
}
void
noit_conf_include_remove(noit_conf_section_t vnode) {
  int i;
  xmlNodePtr node = vnode;
  for(i=0;i<config_include_cnt;i++) {
    if(node->parent == config_include_nodes[i].insertion_point) {
      remove_emancipated_child_node(config_include_nodes[i].root, node);
    }
  }
}
void
noit_conf_backingstore_remove(noit_conf_section_t vnode) {
  int i;
  xmlNodePtr node = vnode;
  noit_xml_userdata_t *subctx = node->_private;
  for(i=0;i<backingstore_include_cnt;i++) {
    if(node->parent == backingstore_include_nodes[i].insertion_point) {
      remove_emancipated_child_node(backingstore_include_nodes[i].root, node);
    }
  }
  if(subctx) {
    noitL(noit_debug, "marking %s for removal\n", subctx->path);
    if(!backingstore_freelist) backingstore_freelist = subctx;
    else {
      noit_xml_userdata_t *fl = backingstore_freelist;
      while(fl->freelist) fl = fl->freelist;
      fl->freelist = subctx;
    }
    node->_private = NULL;
  }
  /* If we're deleted, we'll mark the parent as dirty */
  if(node->parent) noit_conf_backingstore_dirty(node->parent);
}
void
noit_conf_backingstore_dirty(noit_conf_section_t vnode) {
  xmlNodePtr node = vnode;
  noit_xml_userdata_t *subctx = node->_private;
  if(subctx) {
    noitL(noit_debug, "backingstore[%s] marked dirty\n", subctx->path);
    subctx->dirty_time = usec_now();
    return;
  }
  if(node->parent) noit_conf_backingstore_dirty(node->parent);
}
int
noit_conf_backingstore_write(noit_xml_userdata_t *ctx, noit_boolean skip,
                             xmlAttrPtr attrs, xmlNodePtr node) {
  int failure = 0;
  char newpath[PATH_MAX];
  xmlNodePtr n;
  snprintf(newpath, sizeof(newpath), "%s/.attrs", ctx->path);
  if(attrs) {
    xmlDocPtr tmpdoc;
    xmlNodePtr tmpnode;
    noitL(noit_debug, " **> %s\n", newpath);
    tmpdoc = xmlNewDoc((xmlChar *)"1.0");
    tmpnode = xmlNewNode(NULL, ctx->name ? (xmlChar *)ctx->name : (xmlChar *)"stub");
    xmlDocSetRootElement(tmpdoc, tmpnode);
    tmpnode->properties = attrs;
    failure = noit_xmlSaveToFile(tmpdoc, newpath);
    tmpnode->properties = NULL;
    xmlFreeDoc(tmpdoc);
    if(failure) return -1;
  }
  else if(!skip) {
    unlink(newpath);
  }
  for(n = node; n; n = n->next) {
    int leaf;
    noit_xml_userdata_t *subctx;
    subctx = n->_private;
    leaf = is_stopnode(n);
    if(!subctx) { /* This has never been written out */
      subctx = calloc(1, sizeof(*subctx));
      subctx->name = strdup((char *)n->name);
      snprintf(newpath, sizeof(newpath), "%s/%s#%llu", ctx->path, n->name,
               (unsigned long long)++max_gen_count);
      if(leaf) strlcat(newpath, ".xml", sizeof(newpath));
      subctx->path = strdup(newpath);
      subctx->dirty_time = usec_now();
      n->_private = subctx;
      noitL(noit_debug, " !!> %s\n", subctx->path);
    }
    if(leaf) {
      xmlDocPtr tmpdoc;
      xmlNodePtr tmpnode;
      if(subctx->dirty_time > last_config_flush) {
        xmlNsPtr *parent_nslist, iter_ns;
        xmlNodePtr root;
        root = xmlDocGetRootElement(master_config);
        parent_nslist = xmlGetNsList(master_config, root);

        tmpdoc = xmlNewDoc((xmlChar *)"1.0");
        tmpnode = xmlNewNode(NULL, n->name);
        xmlDocSetRootElement(tmpdoc, tmpnode);
        if(parent_nslist) {
          for(iter_ns = *parent_nslist; iter_ns; iter_ns = iter_ns->next)
            xmlNewNs(tmpnode, iter_ns->href, iter_ns->prefix);
          xmlFree(parent_nslist);
        }
        tmpnode->properties = n->properties;
        tmpnode->children = n->children;
        failure = noit_xmlSaveToFile(tmpdoc, subctx->path);
        tmpnode->properties = NULL;
        tmpnode->children = NULL;
        xmlFreeDoc(tmpdoc);
        noitL(noit_debug, " ==> %s\n", subctx->path);
        if(failure) return -1;
      }
    }
    else {
      noit_boolean skip_attrs;
      skip_attrs = leaf || (subctx->dirty_time <= last_config_flush);
      noitL(noit_debug, " --> %s\n", subctx->path);
      if(noit_conf_backingstore_write(subctx, skip_attrs, skip_attrs ? NULL : n->properties, n->children))
        return -1;
    }
  }
  return 0;
}
void
noit_conf_shatter_write(xmlDocPtr doc) {
  if(backingstore_freelist) {
    noit_xml_userdata_t *fl, *last;
    for(fl = backingstore_freelist; fl; ) {
      last = fl;
      fl = fl->freelist;
      /* If it is a file, we'll unlink it, otherwise,
       * we need to delete the attributes and the directory.
       */
      if(unlink(last->path)) {
        char attrpath[PATH_MAX];
        snprintf(attrpath, sizeof(attrpath), "%s/.attrs", last->path);
        unlink(attrpath);
        if(rmdir(last->path) && errno != ENOENT) {
          /* This shouldn't happen, but if it does we risk
           * leaving a mess. Don't do that.
           */
          noitL(noit_error, "backingstore mess %s: %s\n",
                last->path, strerror(errno));
        }
      }
      noit_xml_userdata_free(last);
    }
    backingstore_freelist = NULL;
  }
  if(backingstore_include_nodes) {
    int i;
    for(i=0; i<backingstore_include_cnt; i++) {
      if(backingstore_include_nodes[i].doc) {
        xmlNodePtr n;
        noit_xml_userdata_t *what = backingstore_include_nodes[i].doc->_private;

        for(n=backingstore_include_nodes[i].insertion_point->children;
            n; n = n->next) {
          n->parent = backingstore_include_nodes[i].root;
          n->parent->last = n;
        }
        backingstore_include_nodes[i].root->children =
          backingstore_include_nodes[i].insertion_point->children;
        backingstore_include_nodes[i].insertion_point->children =
          backingstore_include_nodes[i].old_children;
        for(n=backingstore_include_nodes[i].insertion_point->children;
            n; n = n->next) {
          n->parent->last = n; /* sets it to the last child */
        }
        noit_conf_backingstore_write(what, noit_false, NULL, backingstore_include_nodes[i].root->children);
      }
    }
    last_config_flush = usec_now();
  }
}
void
noit_conf_shatter_postwrite(xmlDocPtr doc) {
  if(backingstore_include_nodes) {
    int i;
    for(i=0; i<backingstore_include_cnt; i++) {
      if(backingstore_include_nodes[i].doc) {
        xmlNodePtr n;
        backingstore_include_nodes[i].insertion_point->children =
          backingstore_include_nodes[i].root->children;
        for(n=backingstore_include_nodes[i].insertion_point->children;
            n; n = n->next) {
          n->parent = backingstore_include_nodes[i].insertion_point;
          n->parent->last = n;
        }
      }
    }
  }
}

int
noit_conf_read_into_node(xmlNodePtr node, const char *path) {
  DIR *dirroot;
  struct dirent *de, *entry;
  char filepath[PATH_MAX];
  xmlDocPtr doc;
  xmlNodePtr root = NULL;
  struct stat sb;
  int size, rv;

  noitL(noit_debug, "read backing store: %s\n", path);
  snprintf(filepath, sizeof(filepath), "%s/.attrs", path);
  while((rv = stat(filepath, &sb)) < 0 && errno == EINTR);
  if(rv == 0) {
    doc = xmlReadFile(filepath, "utf8", XML_PARSE_NOENT);
    if(doc) root = xmlDocGetRootElement(doc);
    if(doc && root) {
      node->properties = xmlCopyPropList(node, root->properties);
      xmlFreeDoc(doc);
      doc = NULL;
    }
  }
#ifdef _PC_NAME_MAX
  size = pathconf(path, _PC_NAME_MAX);
#endif
  size = MAX(size, PATH_MAX + 128);
  de = alloca(size);
  dirroot = opendir(path);
  if(!dirroot) return -1;
  while(portable_readdir_r(dirroot, de, &entry) == 0 && entry != NULL) {
    noit_xml_userdata_t *udata;
    char name[PATH_MAX];
    char *sep;
    xmlNodePtr child;
    u_int64_t gen;

    noit_watchdog_child_heartbeat();

    sep = strchr(entry->d_name, '#');
    if(!sep) continue;
    snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);
    while((rv = stat(filepath, &sb)) < 0 && errno == EINTR);
    if(rv == 0) {
      strlcpy(name, entry->d_name, sizeof(name));
      name[sep - entry->d_name] = '\0';
      gen = strtoull(sep+1, NULL, 10);
      if(gen > max_gen_count) max_gen_count = gen;

      if(S_ISDIR(sb.st_mode)) {
        noitL(noit_debug, "<DIR< %s\n", entry->d_name);
        child = xmlNewNode(NULL, (xmlChar *)name);
        noit_conf_read_into_node(child, filepath);
        udata = calloc(1, sizeof(*udata));
        udata->name = strdup(name);
        udata->path = strdup(filepath);
        child->_private = udata;
        xmlAddChild(node, child);
      }
      else if(S_ISREG(sb.st_mode)) {
        xmlDocPtr cdoc;
        xmlNodePtr cnode = NULL;
        noitL(noit_debug, "<FILE< %s\n", entry->d_name);
        cdoc = xmlParseFile(filepath);
        if(cdoc) {
          cnode = xmlDocGetRootElement(cdoc);
          xmlDocSetRootElement(cdoc, xmlNewNode(NULL, (xmlChar *)"dummy"));
          if(cnode) {
            udata = calloc(1, sizeof(*udata));
            udata->name = strdup(name);
            udata->path = strdup(filepath);
            cnode->_private = udata;
            xmlAddChild(node, cnode);
          }
          xmlFreeDoc(cdoc);
        }
      }
    }
  }
  closedir(dirroot);
  return 0;
}

xmlDocPtr
noit_conf_read_backing_store(const char *path) {
  xmlDocPtr doc;
  xmlNodePtr root;
  noit_xml_userdata_t *what;

  doc = xmlNewDoc((xmlChar *)"1.0");
  what = calloc(1, sizeof(*what));
  what->path = strdup(path);
  doc->_private = what;
  root = xmlNewNode(NULL, (xmlChar *)"stub");
  xmlDocSetRootElement(doc, root);
  noit_conf_read_into_node(root, path);
  return doc;
}
int
noit_conf_magic_mix(const char *parentfile, xmlDocPtr doc, include_node_t* inc_node) {
  xmlXPathContextPtr mix_ctxt = NULL;
  xmlXPathObjectPtr pobj = NULL;
  xmlNodePtr node;
  int i, cnt, rv = 0, master = 0;
  int *include_cnt;
  include_node_t* include_nodes;

  if (inc_node) {
    include_cnt = &(inc_node->child_count);
    include_nodes = inc_node->children;
  }
  else {
    include_cnt = &(config_include_cnt);
    include_nodes = config_include_nodes;
    master = 1;
  }

  assert(*include_cnt == -1);

  if (master) {
    assert(backingstore_include_cnt == -1);
    backingstore_include_cnt = 0;
  }
  mix_ctxt = xmlXPathNewContext(doc);
  if (master)
    pobj = xmlXPathEval((xmlChar *)"//*[@backingstore]", mix_ctxt);
  else
    pobj = NULL;

  if(!pobj) goto includes;
  if(pobj->type != XPATH_NODESET) goto includes;
  if(xmlXPathNodeSetIsEmpty(pobj->nodesetval)) goto includes;

  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  if(cnt > 0)
    backingstore_include_nodes = calloc(cnt, sizeof(*backingstore_include_nodes));
  for(i=0; i<cnt; i++) {
    char *path, *infile;
    node = xmlXPathNodeSetItem(pobj->nodesetval, i);
    path = (char *)xmlGetProp(node, (xmlChar *)"backingstore");
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
    xmlFree(path);
    backingstore_include_nodes[i].doc = noit_conf_read_backing_store(infile);
    if(backingstore_include_nodes[i].doc) {
      xmlNodePtr n, lchild;
      backingstore_include_nodes[i].insertion_point = node;
      backingstore_include_nodes[i].root = xmlDocGetRootElement(backingstore_include_nodes[i].doc);
      /* for backing store, they are permanently reattached under the backing store.
       * so for any children, we need to glue them into the new parent document.
       */
      lchild = backingstore_include_nodes[i].root->children;
      while(lchild && lchild->next) lchild = lchild->next;
      if(lchild) {
        lchild->next = node->children;
        if(node->children) node->children->prev = lchild;
      }
      else
        backingstore_include_nodes[i].root->children = node->children;
      for(n=node->children; n; n = n->next) {
        n->parent = backingstore_include_nodes[i].root; /* this gets mapped right back, just for clarity */
        n->doc = backingstore_include_nodes[i].doc;
      }
      backingstore_include_nodes[i].old_children = NULL;
      node->children = backingstore_include_nodes[i].root->children;
      for(n=node->children; n; n = n->next) {
        n->parent = backingstore_include_nodes[i].insertion_point;
        n->parent->last = n;
      }
    }
    else {
      noitL(noit_error, "Could not load: '%s'\n", infile);
      rv = -1;
    }
    free(infile);
  }
  if(mix_ctxt) xmlXPathFreeContext(mix_ctxt);
  mix_ctxt = xmlXPathNewContext(doc);
  backingstore_include_cnt = cnt;
  noitL(noit_debug, "Processed %d backing stores.\n", backingstore_include_cnt);

 includes:
  if(pobj) xmlXPathFreeObject(pobj);
  *include_cnt = 0;
  pobj = xmlXPathEval((xmlChar *)"//include[@file]", mix_ctxt);
  if(!pobj) goto out;
  if(pobj->type != XPATH_NODESET) goto out;
  if(xmlXPathNodeSetIsEmpty(pobj->nodesetval)) goto out;
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  if(cnt > 0) {
    include_nodes = calloc(cnt, sizeof(*include_nodes));
    if (master) {
      config_include_nodes = include_nodes;
    }
    else {
      inc_node->children = include_nodes;
    }
    for (i=0; i < cnt; i++) {
      include_nodes[i].child_count = -1;
    }
  }
  for(i=0; i<cnt; i++) {
    char *path, *infile, *snippet, *ro;
    node = xmlXPathNodeSetItem(pobj->nodesetval, i);
    path = (char *)xmlGetProp(node, (xmlChar *)"file");
    if(!path) continue;
    snippet = (char *)xmlGetProp(node, (xmlChar *)"snippet");
    include_nodes[i].snippet = (snippet && strcmp(snippet, "false"));
    if(snippet) xmlFree(snippet);
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
    xmlFree(path);
    ro = (char *)xmlGetProp(node, (xmlChar *)"readonly");
    if (ro && !strcmp(ro, "true")) include_nodes[i].ro = 1;
    if (ro) xmlFree(ro);
    if (include_nodes[i].snippet)
      include_nodes[i].doc = xmlParseEntity(infile);
    else
      include_nodes[i].doc = xmlReadFile(infile, "utf8", XML_PARSE_NOENT);
    if((include_nodes[i].doc) || (include_nodes[i].snippet)) {
      xmlNodePtr n;
      noit_conf_magic_mix(infile, include_nodes[i].doc, &(include_nodes[i]));
      strncpy(include_nodes[i].path, infile, sizeof(include_nodes[i].path));
      include_nodes[i].insertion_point = node;
      include_nodes[i].root = xmlDocGetRootElement(include_nodes[i].doc);
      include_nodes[i].old_children = node->children;
      if (!include_nodes[i].snippet)
        node->children = include_nodes[i].root->children;
      else
        node->children = include_nodes[i].root;
      for(n=node->children; n; n = n->next)
        n->parent = include_nodes[i].insertion_point;
    }
    else {
      noitL(noit_error, "Could not load: '%s'\n", infile);
      rv = -1;
    }
    free(infile);
  }
  *include_cnt = cnt;
  noitL(noit_debug, "Processed %d includes\n", *include_cnt);
 out:
  if(pobj) xmlXPathFreeObject(pobj);
  if(mix_ctxt) xmlXPathFreeContext(mix_ctxt);
  return rv;
}

static int noit_conf_load_internal(const char *path) {
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
      noit_conf_magic_separate();
      xmlFreeDoc(master_config);
    }
    if(xpath_ctxt) xmlXPathFreeContext(xpath_ctxt);

    master_config = new_config;
    /* mixin all the includes */
    if(noit_conf_magic_mix(path, master_config, NULL)) rv = -1;

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
int noit_conf_load(const char *path) {
  int rv;
  XML2LOG(noit_error);
  rv = noit_conf_load_internal(path);
  XML2LOG(xml_debug);
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
                                      noit_hash_table *table,
                                      const char *namespace) {
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
    const xmlChar *name;
    int freename = 0;
    char *value;
    node = xmlXPathNodeSetItem(pobj->nodesetval, i);
    if(namespace && node->ns && !strcmp((char *)node->ns->prefix, namespace)) {
      name = node->name;
      if(!strcmp((char *)name, "value")) {
        name = xmlGetProp(node, (xmlChar *)"name");
        if(!name) name = node->name;
        else freename = 1;
      }
      value = (char *)xmlXPathCastNodeToString(node);
      noit_hash_replace(table,
                        strdup((char *)name), strlen((char *)name),
                        strdup(value), free, free);
      xmlFree(value);
    }
    else if(!namespace && !node->ns) {
      name = node->name;
      if(!strcmp((char *)name, "value")) {
        name = xmlGetProp(node, (xmlChar *)"name");
        if(!name) name = node->name;
        else freename = 1;
      }
      value = (char *)xmlXPathCastNodeToString(node);
      noit_hash_replace(table,
                        strdup((char *)name), strlen((char *)name),
                        strdup(value), free, free);
      xmlFree(value);
    }
    if(freename) xmlFree((void *)name);
  }
 out:
  if(pobj) xmlXPathFreeObject(pobj);
  if(current_ctxt && current_ctxt != xpath_ctxt)
    xmlXPathFreeContext(current_ctxt);
}
void noit_conf_get_into_hash(noit_conf_section_t section,
                             const char *path,
                             noit_hash_table *table,
                             const char *namespace) {
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
  if(cnt > 1 && node) {
    parent_node = xmlXPathNodeSetItem(pobj->nodesetval, cnt-2);
    if(parent_node != current_node)
      noit_conf_get_into_hash(parent_node, (const char *)node->name, table, namespace);
  }
  /* 2. */
  inheritid = (char *)xmlGetProp(node, (xmlChar *)"inherit");
  if(inheritid) {
    snprintf(xpath_expr, sizeof(xpath_expr), "//*[@id=\"%s\"]", inheritid);
    noit_conf_get_into_hash(NULL, xpath_expr, table, namespace);
    xmlFree(inheritid);
  }
  /* 3. */
  noit_conf_get_elements_into_hash(node, "*", table, namespace);

 out:
  if(pobj) xmlXPathFreeObject(pobj);
  if(current_ctxt && current_ctxt != xpath_ctxt)
    xmlXPathFreeContext(current_ctxt);
}
noit_hash_table *noit_conf_get_namespaced_hash(noit_conf_section_t section,
                                               const char *path, const char *ns) {
  noit_hash_table *table = NULL;

  table = calloc(1, sizeof(*table));
  noit_conf_get_into_hash(section, path, table, ns);
  if(table->size == 0) {
    noit_hash_destroy(table, free, free);
    free(table);
    table = NULL;
  }
  return table;
}
noit_hash_table *noit_conf_get_hash(noit_conf_section_t section,
                                    const char *path) {
  noit_hash_table *table = NULL;

  table = calloc(1, sizeof(*table));
  noit_conf_get_into_hash(section, path, table, NULL);
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
int noit_conf_remove_section(noit_conf_section_t section) {
  if (!section) return -1;
  xmlNodePtr node = (xmlNodePtr) section;
  xmlUnlinkNode(node);
  xmlFreeNode(node);
  noit_conf_mark_changed();
  return 0;
}
int _noit_conf_get_string(noit_conf_section_t section, xmlNodePtr *vnode,
                          const char *path, char **value) {
  const char *str, *interest;
  char fullpath[1024];
  int rv = 1, i;
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
  interest = path;
  if(*interest != '/' && current_node) {
    xmlChar *basepath = xmlGetNodePath(current_node);
    snprintf(fullpath, sizeof(fullpath), "%s/%s", (char *)basepath, path);
    free(basepath);
    interest = fullpath;
  }
  if(noit_hash_retr_str(&_compiled_fallback,
                        interest, strlen(interest), &str)) {
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
  xmlNodePtr current_node = (xmlNodePtr)section;
  if(strchr(path, '/')) return 0;
  if(path[0] == '@') {
    xmlSetProp(current_node, (xmlChar *)path+1, (xmlChar *)value);
    CONF_DIRTY(current_node);
  }
  else {
    xmlNodePtr child_node;
    if(value) {
      child_node = xmlNewTextChild(current_node, NULL, (xmlChar *)path, (xmlChar *)value);
    }
    else {
      child_node = xmlNewChild(current_node, NULL, (xmlChar *)path, NULL);
    }
    CONF_DIRTY(child_node);
  }
  noit_conf_mark_changed();
  if(noit_conf_write_file(NULL) != 0)
    noitL(noit_error, "local config write failed\n");
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

int noit_conf_should_resolve_targets(noit_boolean *should_resolve) {
  static int inited = 0, cached_rv;;
  static noit_boolean cached_should_resolve;
  if(!inited) {
    cached_rv = noit_conf_get_boolean(NULL, "//checks/@resolve_targets",
                                      &cached_should_resolve);
    inited = 1;
  }
  *should_resolve = cached_should_resolve;
  return cached_rv;
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
  XML2CONSOLE(ncct);
  if(noit_conf_load_internal(master_config_file)) {
    XML2LOG(xml_debug);
    nc_printf(ncct, "error loading config\n");
    return -1;
  }
  XML2LOG(xml_debug);
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
  noit_conf_kansas_city_shuffle_undo(config_include_nodes, config_include_cnt);
  xmlSaveFormatFileTo(out, master_config, "utf8", 1);
  write_out_include_files(config_include_nodes, config_include_cnt);
  noit_conf_kansas_city_shuffle_redo(config_include_nodes, config_include_cnt);
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
  uid_t uid = 0;
  gid_t gid = 0;

  if(stat(master_config_file, &st) == 0) {
    mode = st.st_mode;
    uid = st.st_uid;
    gid = st.st_gid;
  }
  snprintf(master_file_tmp, sizeof(master_file_tmp),
           "%s.tmp", master_config_file);
  unlink(master_file_tmp);
  fd = open(master_file_tmp, O_CREAT|O_EXCL|O_WRONLY|NE_O_CLOEXEC, mode);
  if(fd < 0) {
    snprintf(errstr, sizeof(errstr), "Failed to open tmp file: %s",
             strerror(errno));
    if(err) *err = strdup(errstr);
    return -1;
  }
  fchown(fd, uid, gid);

  enc = xmlGetCharEncodingHandler(XML_CHAR_ENCODING_UTF8);
  out = xmlOutputBufferCreateFd(fd, enc);
  if(!out) {
    close(fd);
    unlink(master_file_tmp);
    if(err) *err = strdup("internal error: OutputBufferCreate failed");
    return -1;
  }
  noit_conf_kansas_city_shuffle_undo(config_include_nodes, config_include_cnt);
  noit_conf_shatter_write(master_config);
  len = xmlSaveFormatFileTo(out, master_config, "utf8", 1);
  noit_conf_shatter_postwrite(master_config);
  write_out_include_files(config_include_nodes, config_include_cnt);
  noit_conf_kansas_city_shuffle_redo(config_include_nodes, config_include_cnt);
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
  noit_conf_kansas_city_shuffle_undo(config_include_nodes, config_include_cnt);
  xmlSaveFormatFileTo(out, master_config, "utf8", 1);
  write_out_include_files(config_include_nodes, config_include_cnt);
  noit_conf_kansas_city_shuffle_redo(config_include_nodes, config_include_cnt);
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
  noit_boolean notify_only = noit_false;
  const char *v;
  SETUP_LOG(config, return -1);
  if(!N_L_S_ON(config_log)) return 0;

  v = noit_log_stream_get_property(config_log, "notify_only");
  if(v && (!strcmp(v, "on") || !strcmp(v, "true"))) notify_only = noit_true;

  /* We know we haven't changed */
  if(last_write_gen == __config_gen) return 0;
  gettimeofday(&__now, NULL);

  if(notify_only) {
    noitL(config_log, "n\t%lu.%03lu\t%d\t\n",
          (unsigned long int)__now.tv_sec,
          (unsigned long int)__now.tv_usec / 1000UL, 0);
    last_write_gen = __config_gen;
    return 0;
  }

  clv = calloc(1, sizeof(*clv));
  clv->target = CONFIG_B64;
  enc = xmlGetCharEncodingHandler(XML_CHAR_ENCODING_UTF8);
  out = xmlOutputBufferCreateIO(noit_config_log_write_xml,
                                noit_config_log_close_xml,
                                clv, enc);
  noit_conf_kansas_city_shuffle_undo(config_include_nodes, config_include_cnt);
  xmlSaveFormatFileTo(out, master_config, "utf8", 1);
  write_out_include_files(config_include_nodes, config_include_cnt);
  noit_conf_kansas_city_shuffle_redo(config_include_nodes, config_include_cnt);
  if(clv->encoded != CONFIG_B64) {
    noitL(noit_error, "Error logging configuration\n");
    if(clv->buff) free(clv->buff);
    free(clv);
    return -1;
  }
  noitL(config_log, "n\t%lu.%03lu\t%d\t%.*s\n",
        (unsigned long int)__now.tv_sec,
        (unsigned long int)__now.tv_usec / 1000UL, clv->raw_len,
        clv->len, clv->buff);
  free(clv->buff);
  free(clv);
  last_write_gen = __config_gen;
  return 0;
}

struct log_rotate_crutch {
  noit_log_stream_t ls;
  int seconds;
  size_t max_size;
};

static int
noit_conf_log_rotate_size(eventer_t e, int mask, void *closure,
                          struct timeval *now) {
  struct log_rotate_crutch *lrc = closure;
  if(noit_log_stream_written(lrc->ls) > lrc->max_size) {
    noit_log_stream_rename(lrc->ls, NOIT_LOG_RENAME_AUTOTIME);
    noit_log_stream_reopen(lrc->ls);
  }
  /* Yes the 5 is arbitrary, but this is cheap */
  eventer_add_in_s_us(noit_conf_log_rotate_size, closure, 5, 0);
  return 0;
}
static int
noit_conf_log_rotate_time(eventer_t e, int mask, void *closure,
                          struct timeval *now) {
  struct timeval lnow;
  eventer_t newe;
  struct log_rotate_crutch *lrc = closure;

  if(now) {
    noit_log_stream_rename(lrc->ls, NOIT_LOG_RENAME_AUTOTIME);
    noit_log_stream_reopen(lrc->ls);
  }
  
  newe = eventer_alloc();
  newe->closure = closure;
  if(!now) { gettimeofday(&lnow, NULL); now = &lnow; }
  if(e)
    memcpy(&newe->whence, &e->whence, sizeof(newe->whence));
  else if(now) {
    memcpy(&newe->whence, now, sizeof(newe->whence));
    newe->whence.tv_sec = (newe->whence.tv_sec / lrc->seconds) * lrc->seconds;
  }
  newe->whence.tv_sec += lrc->seconds;
  newe->mask = EVENTER_TIMER;
  newe->callback = noit_conf_log_rotate_time;
  eventer_add(newe);
  return 0;
}
int
noit_conf_log_init_rotate(const char *toplevel, noit_boolean validate) {
  int i, cnt = 0, max_time, max_size, rv = 0;
  noit_conf_section_t *log_configs;
  char path[256];

  snprintf(path, sizeof(path), "/%s/logs//log", toplevel);
  log_configs = noit_conf_get_sections(NULL, path, &cnt);
  noitL(noit_debug, "Found %d %s stanzas\n", cnt, path);
  for(i=0; i<cnt; i++) {
    noit_log_stream_t ls;
    char name[256];

    if(!noit_conf_get_stringbuf(log_configs[i],
                                "ancestor-or-self::node()/@name",
                                name, sizeof(name))) {
      noitL(noit_error, "log section %d does not have a name attribute\n", i+1);
      if(validate) { rv = -1; break; }
      else exit(-2);
    }
    ls = noit_log_stream_find(name);
    if(!ls) continue;

    if(noit_conf_get_int(log_configs[i],    
                         "ancestor-or-self::node()/@rotate_seconds",
                         &max_time) && max_time) {
      struct log_rotate_crutch *lrc;
      if(max_time < 600) {
        fprintf(stderr, "rotate_seconds must be >= 600s (10 minutes)\n");
        if(validate) { rv = -1; break; }
        else exit(-2);
      }
      if(!validate) {
        lrc = calloc(1, sizeof(*lrc));
        lrc->ls = ls;
        lrc->seconds = max_time;
        noit_conf_log_rotate_time(NULL, EVENTER_TIMER, lrc, NULL);
      }
    }

    if(noit_conf_get_int(log_configs[i],    
                         "ancestor-or-self::node()/@rotate_bytes",
                         &max_size) && max_size) {
      struct log_rotate_crutch *lrc;
      if(max_size < 102400) {
        fprintf(stderr, "rotate_bytes must be >= 102400 (100k)\n");
        if(validate) { rv = -1; break; }
        else exit(-2);
      }
      if(!validate) {
        lrc = calloc(1, sizeof(*lrc));
        lrc->ls = ls;
        lrc->max_size = max_size;
        noit_conf_log_rotate_size(NULL, EVENTER_TIMER, lrc, NULL);
      }
    }
  }
  free(log_configs);
  return rv;
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
    int flags;
    noit_log_stream_t ls;
    char name[256], type[256], path[256];
    noit_hash_table *config;
    noit_boolean disabled, debug, timestamps, facility;

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
                                "ancestor-or-self::node()/config");
    ls = noit_log_stream_new(name, type[0] ? type : NULL,
                             path[0] ? path : NULL, NULL, config);
    if(!ls) {
      fprintf(stderr, "Error configuring log: %s[%s:%s]\n", name, type, path);
      exit(-1);
    }

    flags = noit_log_stream_get_flags(ls);
    if(noit_conf_get_boolean(log_configs[i],
                             "ancestor-or-self::node()/@disabled",
                             &disabled)) {
      if(disabled) flags &= ~NOIT_LOG_STREAM_ENABLED;
      else         flags |= NOIT_LOG_STREAM_ENABLED;
    }
    if(noit_conf_get_boolean(log_configs[i],
                             "ancestor-or-self::node()/@debug",
                             &debug)) {
      if(debug) flags |= NOIT_LOG_STREAM_DEBUG;
      else      flags &= ~NOIT_LOG_STREAM_DEBUG;
    }
    if(noit_conf_get_boolean(log_configs[i],
                             "ancestor-or-self::node()/@timestamps",
                             &timestamps)) {
      if(timestamps) flags |= NOIT_LOG_STREAM_TIMESTAMPS;
      else           flags &= ~NOIT_LOG_STREAM_TIMESTAMPS;
    }
    if(noit_conf_get_boolean(log_configs[i],
                             "ancestor-or-self::node()/@facility",
                             &facility)) {
      if(facility) flags |= NOIT_LOG_STREAM_FACILITY;
      else         flags &= ~NOIT_LOG_STREAM_FACILITY;
    }
    noit_log_stream_set_flags(ls, flags);

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
  if(noit_conf_log_init_rotate(toplevel, noit_true)) exit(-1);
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
      free(info->path);
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
    if(node) {
      CONF_REMOVE(node);
      xmlUnlinkNode(node);
      noit_conf_mark_changed();
    }
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
    free(info->path);
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
  char *path = NULL, xpath[1024];
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
  if(!node) {
    err = "internal XML error";
    goto bad;
  }
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
  else {
    char *xmlpath = (char *)xmlGetNodePath(node);
    info->path = strdup(xmlpath + 1 +
                        strlen(root_node_name));
    free(xmlpath);
  }

  free(path);
  if(pobj) xmlXPathFreeObject(pobj);
  if(closure) noit_console_state_pop(ncct, argc, argv, NULL, NULL);
  return 0;
 bad:
  if(path) free(path);
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

