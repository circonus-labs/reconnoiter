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
#include <assert.h>
#include <errno.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include "noit_listener.h"
#include "noit_http.h"
#include "noit_rest.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_conf.h"
#include "noit_conf_private.h"

#define UUID_REGEX "[0-9a-fA-F]{4}(?:[0-9a-fA-F]{4}-){4}[0-9a-fA-F]{12}"

struct rest_xml_payload {
  char *buffer;
  int len;
  int allocd;
  int complete;
};

static int
rest_show_check(noit_http_rest_closure_t *restc,
                int npats, char **pats) {
  noit_http_session_ctx *ctx = restc->http_ctx;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL;
  xmlDocPtr doc = NULL;
  xmlNodePtr node, root, attr, config, state, tmp, anode, metrics;
  uuid_t checkid;
  noit_check_t *check;
  char xpath[1024], *uuid_conf, *module, *value;
  int rv, cnt;
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *k;
  int klen;
  void *data;
  noit_hash_table *configh;

  if(npats != 2) goto error;

  rv = noit_check_xpath(xpath, sizeof(xpath), pats[0], pats[1]);
  if(rv == 0) goto not_found;
  if(rv < 0) goto error;

  noit_conf_xml_xpath(NULL, &xpath_ctxt);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET ||
     xmlXPathNodeSetIsEmpty(pobj->nodesetval)) goto not_found;
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  if(cnt != 1) goto error;

  node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, 0);
  uuid_conf = (char *)xmlGetProp(node, (xmlChar *)"uuid");
  if(!uuid_conf || uuid_parse(uuid_conf, checkid)) goto error;

  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlNewDocNode(doc, NULL, (xmlChar *)"check", NULL);
  xmlDocSetRootElement(doc, root);

#define MYATTR(node,a,n,b) _noit_conf_get_string(node, &(n), "@" #a, &(b))
#define INHERIT(node,a,n,b) \
  _noit_conf_get_string(node, &(n), "ancestor-or-self::node()/@" #a, &(b))
#define SHOW_ATTR(parent, node, a) do { \
  xmlNodePtr anode = NULL; \
  char *value = NULL; \
  INHERIT(node, a, anode, value); \
  if(value != NULL) { \
    int clen, plen;\
    const char *cpath, *apath; \
    xmlNodePtr child; \
    cpath = node ? (char *)xmlGetNodePath(node) : ""; \
    apath = anode ? (char *)xmlGetNodePath(anode) : ""; \
    clen = strlen(cpath); \
    plen = strlen("/noit/checks"); \
    child = xmlNewNode(NULL, (xmlChar *)#a); \
    xmlNodeAddContent(child, (xmlChar *)value); \
    if(!strncmp(cpath, apath, clen) && apath[clen] == '/') { \
    } \
    else { \
      xmlSetProp(child, (xmlChar *)"inherited", (xmlChar *)apath+plen); \
    } \
    xmlAddChild(parent, child); \
  } \
} while(0)
#define NODE_CONTENT(parent, k, v) do { \
  xmlNodePtr tmp; \
  if(v) { \
    tmp = xmlNewNode(NULL, (xmlChar *)(k)); \
    xmlNodeAddContent(tmp, (xmlChar *)(v)); \
    xmlAddChild(parent, tmp); \
  } \
} while(0)

  attr = xmlNewNode(NULL, (xmlChar *)"attributes");
  xmlAddChild(root, attr);

  /* Name is odd, it falls back transparently to module */
  if(!INHERIT(node, module, tmp, module)) module = NULL;
  xmlAddChild(attr, (tmp = xmlNewNode(NULL, (xmlChar *)"name")));
  if(MYATTR(node, name, anode, value))
    xmlNodeAddContent(tmp, (xmlChar *)value);
  else if(module)
    xmlNodeAddContent(tmp, (xmlChar *)module);

  SHOW_ATTR(attr,node,module);
  SHOW_ATTR(attr,node,target);
  SHOW_ATTR(attr,node,period);
  SHOW_ATTR(attr,node,timeout);
  SHOW_ATTR(attr,node,oncheck);
  SHOW_ATTR(attr,node,filterset);
  SHOW_ATTR(attr,node,disable);

  /* Add the config */
  config = xmlNewNode(NULL, (xmlChar *)"config");
  configh = noit_conf_get_hash(node, "config");
  while(noit_hash_next(configh, &iter, &k, &klen, &data))
    NODE_CONTENT(config, k, data);
  noit_hash_destroy(configh, free, free);
  free(configh);
  xmlAddChild(root, config);

  /* Add the state */
  xmlAddChild(root, (state = xmlNewNode(NULL, (xmlChar *)"state")));
  check = noit_poller_lookup(checkid);
  if(!check)
    xmlSetProp(state, (xmlChar *)"error", (xmlChar *)"true");
  else {
    stats_t *c = &check->stats.current;
    NODE_CONTENT(state, "running", NOIT_CHECK_RUNNING(check)?"true":"false");
    NODE_CONTENT(state, "killed", NOIT_CHECK_KILLED(check)?"true":"false");
    NODE_CONTENT(state, "configured",
                 NOIT_CHECK_CONFIGURED(check)?"true":"false");
    NODE_CONTENT(state, "disabled", NOIT_CHECK_DISABLED(check)?"true":"false");
    xmlAddChild(state, (tmp = xmlNewNode(NULL, (xmlChar *)"last_run")));
    if(check->stats.current.whence.tv_sec) {
      struct timeval f = check->stats.current.whence;
      struct timeval n;
      char timestr[20];
      gettimeofday(&n, NULL);
      snprintf(timestr, sizeof(timestr), "%0.3f",
               n.tv_sec + (n.tv_usec / 1000000.0));
      xmlSetProp(tmp, (xmlChar *)"now", (xmlChar *)timestr);
      snprintf(timestr, sizeof(timestr), "%0.3f",
               f.tv_sec + (f.tv_usec / 1000000.0));
      xmlNodeAddContent(tmp, (xmlChar *)timestr);
    }
    if(c->available) { /* truth here means the check has been run */
      char buff[20];
      snprintf(buff, sizeof(buff), "%0.3f", (float)c->duration/1000.0);
      NODE_CONTENT(state, "runtime", buff);
    }
    NODE_CONTENT(state, "availability",
                 noit_check_available_string(c->available));
    NODE_CONTENT(state, "state", noit_check_state_string(c->state));
    NODE_CONTENT(state, "status", c->status ? c->status : "");
    memset(&iter, 0, sizeof(iter));
    xmlAddChild(state, (metrics = xmlNewNode(NULL, (xmlChar *)"metrics")));
    while(noit_hash_next(&c->metrics, &iter, &k, &klen, &data)) {
      char buff[256];
      metric_t *m = (metric_t *)data;
      xmlAddChild(metrics, (tmp = xmlNewNode(NULL, (xmlChar *)m->metric_name)));
      buff[0] = m->metric_type; buff[1] = '\0';
      xmlSetProp(tmp, (xmlChar *)"type", (xmlChar *)buff);
      if(m->metric_value.s) {
        int rv;
        rv = noit_stats_snprint_metric_value(buff, sizeof(buff), m);
        if(rv < 0)
          xmlSetProp(tmp, (xmlChar *)"error", (xmlChar *)"unknown type");
        else
          xmlNodeAddContent(tmp, (xmlChar *)buff);
      }
    }

  }
  noit_http_response_ok(ctx, "text/xml");
  noit_http_response_xml(ctx, doc);
  noit_http_response_end(ctx);
  goto cleanup;

 not_found:
  noit_http_response_not_found(ctx, "text/html");
  noit_http_response_end(ctx);
  goto cleanup;

 error:
  noit_http_response_server_error(ctx, "text/html");
  noit_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if(pobj) xmlXPathFreeObject(pobj);
  if(doc) xmlFreeDoc(doc);
  return 0;
}

static void
rest_xml_payload_free(void *f) {
  struct rest_xml_payload *xmlin = f;
  if(xmlin->buffer) free(xmlin->buffer);
}

static int
validate_check_post(xmlDocPtr doc, xmlNodePtr *a, xmlNodePtr *c) {
  xmlNodePtr root, tl, an;
  int name=0, module=0, target=0, period=0, timeout=0, filterset=0, disable=0;
  *a = *c = NULL;
  root = xmlDocGetRootElement(doc);
  if(!root || strcmp((char *)root->name, "check")) return 0;
  for(tl = root->children; tl; tl = tl->next) {
    if(!strcmp((char *)tl->name, "attributes")) {
      *a = tl->children;
      for(an = tl->children; an; an = an->next) {
#define CHECK_N_SET(a) if(!strcmp((char *)an->name, #a)) a = 1
        CHECK_N_SET(name);
        else CHECK_N_SET(module);
        else CHECK_N_SET(target);
        else CHECK_N_SET(period);
        else CHECK_N_SET(timeout);
        else CHECK_N_SET(filterset);
        else CHECK_N_SET(disable);
        else return 0;
      }
    }
    else if(!strcmp((char *)tl->name, "config")) {
      *c = tl->children;
      /* Noop, anything goes */
    }
    else return 0;
  }
  if(name && module && target && period && timeout && filterset) return 1;
  return 0;
}
static void
configure_xml_check(xmlNodePtr check, xmlNodePtr a, xmlNodePtr c) {
  xmlNodePtr n, config, oldconfig;
  for(n = a; n; n = n->next) {
#define ATTR2PROP(attr) do { \
  if(!strcmp((char *)n->name, #attr)) { \
    xmlChar *v = xmlNodeGetContent(n); \
    if(v) xmlSetProp(check, n->name, v); \
    else xmlUnsetProp(check, n->name); \
    if(v) xmlFree(v); \
  } \
} while(0)
    ATTR2PROP(name);
    ATTR2PROP(target);
    ATTR2PROP(module);
    ATTR2PROP(period);
    ATTR2PROP(timeout);
    ATTR2PROP(disable);
    ATTR2PROP(filter);
  }
  for(oldconfig = check->children; oldconfig; oldconfig = oldconfig->next)
    if(!strcmp((char *)oldconfig->name, "config")) break;
  config = xmlNewNode(NULL, (xmlChar *)"config");
  for(n = c; n; n = n->next) {
    xmlNodePtr co = xmlNewNode(NULL, n->name);
    xmlNodeAddContent(co, XML_GET_CONTENT(n));
    xmlAddChild(config, co);
  }
  if(oldconfig) {
    xmlReplaceNode(oldconfig, config);
    xmlFreeNode(oldconfig);
  }
  else xmlAddChild(check, config);
}
static xmlNodePtr
make_conf_path(char *path) {
  xmlNodePtr start, tmp;
  char fullpath[1024], *tok, *brk;
  if(!path || strlen(path) < 1) return NULL;
  snprintf(fullpath, sizeof(fullpath), "%s", path+1);
  fullpath[strlen(fullpath)-1] = '\0';
  start = noit_conf_get_section(NULL, "/noit/checks");
  if(!start) return NULL;
  for (tok = strtok_r(fullpath, "/", &brk);
       tok;
       tok = strtok_r(NULL, "/", &brk)) {
    if(!xmlValidateNameValue((xmlChar *)tok)) return NULL;
    if(!strcmp(tok, "check")) return NULL;  /* These two paths */
    if(!strcmp(tok, "config")) return NULL; /* are off limits. */
    for (tmp = start->children; tmp; tmp = tmp->next) {
      if(!strcmp((char *)tmp->name, tok)) break;
    }
    if(!tmp) {
      tmp = xmlNewNode(NULL, (xmlChar *)tok);
      xmlAddChild(start, tmp);
    }
    start = tmp;
  }
  return start;
}
static int
rest_set_check(noit_http_rest_closure_t *restc,
               int npats, char **pats) {
  noit_http_session_ctx *ctx = restc->http_ctx;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL;
  xmlDocPtr doc = NULL, indoc = NULL;
  xmlNodePtr node, root, attr, config, parent;
  uuid_t checkid;
  noit_check_t *check;
  char xpath[1024], *uuid_conf;
  int rv, cnt;
  const char *error = "internal error";
  noit_boolean exists = noit_false;
  struct rest_xml_payload *rxc;

  if(npats != 2) goto error;

#define FAIL(a) do { error = (a); goto error; } while(0)

  if(restc->call_closure == NULL) {
    rxc = restc->call_closure = calloc(1, sizeof(*rxc));
    restc->call_closure_free = rest_xml_payload_free;
  }
  rxc = restc->call_closure;
  while(!rxc->complete) {
    int len, mask;
    if(rxc->len == rxc->allocd) {
      char *b;
      rxc->allocd += 32768;
      b = rxc->buffer ? realloc(rxc->buffer, rxc->allocd) :
                        malloc(rxc->allocd);
      if(!b) FAIL("alloc failed");
      rxc->buffer = b;
    }
    len = noit_http_session_req_consume(restc->http_ctx,
                                        rxc->buffer + rxc->len,
                                        rxc->allocd - rxc->len,
                                        &mask);
    if(len > 0) rxc->len += len;
    if(len < 0 && errno == EAGAIN) return mask;
    if(rxc->len == restc->http_ctx->req.content_length) rxc->complete = 1;
  }

  indoc = xmlParseMemory(rxc->buffer, rxc->len);
  if(indoc == NULL) FAIL("xml parse error");
  if(!validate_check_post(indoc, &attr, &config)) FAIL("xml validate error");

  if(uuid_parse(pats[1], checkid)) goto error;
  check = noit_poller_lookup(checkid);
  if(check)
    exists = noit_true;

  rv = noit_check_xpath(xpath, sizeof(xpath), pats[0], pats[1]);
  if(rv == 0) FAIL("uuid not valid");
  if(rv < 0) FAIL("Tricky McTrickster... No");

  noit_conf_xml_xpath(NULL, &xpath_ctxt);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET ||
     xmlXPathNodeSetIsEmpty(pobj->nodesetval)) {
    if(exists) FAIL("uuid not yours");
    else {
      char *target = NULL, *name = NULL, *module = NULL;
      noit_module_t *m;
      xmlNodePtr newcheck, a;
      /* make sure this isn't a dup */
      for(a = attr; a; a = a->next) {
        if(!strcmp((char *)a->name, "target"))
          target = (char *)xmlNodeGetContent(a);
        if(!strcmp((char *)a->name, "name"))
          name = (char *)xmlNodeGetContent(a);
        if(!strcmp((char *)a->name, "module"))
          module = (char *)xmlNodeGetContent(a);
      }
      exists = (noit_poller_lookup_by_name(target, name) != NULL);
      m = noit_module_lookup(module);
      xmlFree(target);
      xmlFree(name);
      xmlFree(module);
      if(exists) FAIL("target`name already registered");
      if(!m) FAIL("module does not exist");
      /* create a check here */
      newcheck = xmlNewNode(NULL, (xmlChar *)"check");
      xmlSetProp(newcheck, (xmlChar *)"uuid", (xmlChar *)pats[1]);
      configure_xml_check(newcheck, attr, config);
      parent = make_conf_path(pats[0]);
      if(!parent) FAIL("invalid path");
      xmlAddChild(parent, newcheck);
    }
  }
  if(exists) {
    int module_change;
    char *target, *name, *module;
    xmlNodePtr a;
    noit_check_t *ocheck;
    cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
    if(cnt != 1) FAIL("internal error, |checkid| > 1");
    node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, 0);
    uuid_conf = (char *)xmlGetProp(node, (xmlChar *)"uuid");
    if(!uuid_conf || strcasecmp(uuid_conf, pats[1]))
      FAIL("internal error uuid");
    /* update check here */

    /* make sure this isn't a dup */
    for(a = attr; a; a = a->next) {
      if(!strcmp((char *)a->name, "target"))
        target = (char *)xmlNodeGetContent(a);
      if(!strcmp((char *)a->name, "name"))
        name = (char *)xmlNodeGetContent(a);
      if(!strcmp((char *)a->name, "module"))
        module = (char *)xmlNodeGetContent(a);
    }
    ocheck = noit_poller_lookup_by_name(target, name);
    module_change = strcmp(check->module, module);
    xmlFree(target);
    xmlFree(name);
    xmlFree(module);
    if(ocheck && ocheck != check) FAIL("new target`name would collide");
    if(module_change) FAIL("cannot change module");
    configure_xml_check(node, attr, config);
    parent = make_conf_path(pats[0]);
    if(!parent) FAIL("invalid path");
    xmlUnlinkNode(node);
    xmlAddChild(parent, node);
  }

  noit_conf_mark_changed();
  noit_poller_reload(xpath);
  if(restc->call_closure_free) restc->call_closure_free(restc->call_closure);
  restc->call_closure_free = NULL;
  restc->call_closure = NULL;
  if(pobj) xmlXPathFreeObject(pobj);
  if(doc) xmlFreeDoc(doc);
  if(indoc) xmlFreeDoc(indoc);
  restc->fastpath = rest_show_check;
  return restc->fastpath(restc, restc->nparams, restc->params);

 error:
  noit_http_response_server_error(ctx, "text/xml");
  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlNewDocNode(doc, NULL, (xmlChar *)"error", NULL);
  xmlDocSetRootElement(doc, root);
  xmlNodeAddContent(root, (xmlChar *)error);
  noit_http_response_xml(ctx, doc);
  noit_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if(pobj) xmlXPathFreeObject(pobj);
  if(doc) xmlFreeDoc(doc);
  if(indoc) xmlFreeDoc(indoc);
  return 0;
}

void
noit_check_rest_init() {
  assert(noit_http_rest_register(
    "GET",
    "/checks/",
    "^show(/.*)(?<=/)(" UUID_REGEX ")$",
    rest_show_check
  ) == 0);
  assert(noit_http_rest_register(
    "POST",
    "/checks/",
    "^set(/.*)(?<=/)(" UUID_REGEX ")$",
    rest_set_check
  ) == 0);
}

