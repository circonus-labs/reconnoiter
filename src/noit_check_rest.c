/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
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
#include "noit_filters.h"

#define FAIL(a) do { error = (a); goto error; } while(0)

#define NODE_CONTENT(parent, k, v) do { \
  xmlNodePtr tmp; \
  if(v) { \
    tmp = xmlNewNode(NULL, (xmlChar *)(k)); \
    xmlNodeAddContent(tmp, (xmlChar *)(v)); \
    xmlAddChild(parent, tmp); \
  } \
} while(0)

xmlNodePtr
noit_check_state_as_xml(noit_check_t *check) {
  xmlNodePtr state, tmp, metrics;
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *k;
  int klen;
  void *data;
  stats_t *c = &check->stats.current;

  state = xmlNewNode(NULL, (xmlChar *)"state");
  NODE_CONTENT(state, "running", NOIT_CHECK_RUNNING(check)?"true":"false");
  NODE_CONTENT(state, "killed", NOIT_CHECK_KILLED(check)?"true":"false");
  NODE_CONTENT(state, "configured",
               NOIT_CHECK_CONFIGURED(check)?"true":"false");
  NODE_CONTENT(state, "disabled", NOIT_CHECK_DISABLED(check)?"true":"false");
  NODE_CONTENT(state, "target_ip", check->target_ip);
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
    char buff[20], *compiler_warning;
    snprintf(buff, sizeof(buff), "%0.3f", (float)c->duration/1000.0);
    compiler_warning = buff;
    NODE_CONTENT(state, "runtime", compiler_warning);
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
    xmlAddChild(metrics, (tmp = xmlNewNode(NULL, (xmlChar *)"metric")));
    xmlSetProp(tmp, (xmlChar *)"name", (xmlChar *)m->metric_name);
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
  return state;
}

static int
rest_show_check(noit_http_rest_closure_t *restc,
                int npats, char **pats) {
  noit_http_session_ctx *ctx = restc->http_ctx;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL;
  xmlDocPtr doc = NULL;
  xmlNodePtr node, root, attr, config, state, tmp, anode;
  uuid_t checkid;
  noit_check_t *check;
  char xpath[1024], *uuid_conf, *module, *value;
  int rv, cnt, error_code = 500;
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

  attr = xmlNewNode(NULL, (xmlChar *)"attributes");
  xmlAddChild(root, attr);

  SHOW_ATTR(attr,node,uuid);

  /* Name is odd, it falls back transparently to module */
  if(!INHERIT(node, module, tmp, module)) module = NULL;
  xmlAddChild(attr, (tmp = xmlNewNode(NULL, (xmlChar *)"name")));
  if(MYATTR(node, name, anode, value))
    xmlNodeAddContent(tmp, (xmlChar *)value);
  else if(module)
    xmlNodeAddContent(tmp, (xmlChar *)module);

  SHOW_ATTR(attr,node,module);
  SHOW_ATTR(attr,node,target);
  SHOW_ATTR(attr,node,resolve_rtype);
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
  check = noit_poller_lookup(checkid);
  if(!check) {
    state = xmlNewNode(NULL, (xmlChar *)"state");
    xmlSetProp(state, (xmlChar *)"error", (xmlChar *)"true");
  }
  else
    state = noit_check_state_as_xml(check);
  xmlAddChild(root, state);
  noit_http_response_ok(ctx, "text/xml");
  noit_http_response_xml(ctx, doc);
  noit_http_response_end(ctx);
  goto cleanup;

 not_found:
  noit_http_response_not_found(ctx, "text/html");
  noit_http_response_end(ctx);
  goto cleanup;

 error:
  noit_http_response_standard(ctx, error_code, "ERROR", "text/html");
  noit_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if(pobj) xmlXPathFreeObject(pobj);
  if(doc) xmlFreeDoc(doc);
  return 0;
}

int
noit_validate_check_rest_post(xmlDocPtr doc, xmlNodePtr *a, xmlNodePtr *c,
                              const char **error) {
  xmlNodePtr root, tl, an;
  int name=0, module=0, target=0, period=0, timeout=0, filterset=0;
  *a = *c = NULL;
  root = xmlDocGetRootElement(doc);
  if(!root || strcmp((char *)root->name, "check")) return 0;
  for(tl = root->children; tl; tl = tl->next) {
    if(!strcmp((char *)tl->name, "attributes")) {
      *a = tl;
      for(an = tl->children; an; an = an->next) {
#define CHECK_N_SET(a) if(!strcmp((char *)an->name, #a))
        CHECK_N_SET(name) {
          xmlChar *tmp;
          pcre *valid_name = noit_conf_get_valid_name_checker();
          int ovector[30], valid;
          tmp = xmlNodeGetContent(an);
          valid = (pcre_exec(valid_name, NULL,
                             (char *)tmp, strlen((char *)tmp), 0, 0,
                             ovector, sizeof(ovector)/sizeof(*ovector)) > 0);
          xmlFree(tmp);
          if(!valid) { *error = "invalid name"; return 0; }
          name = 1;
        }
        else CHECK_N_SET(module) module = 1; /* This is validated by called */
        else CHECK_N_SET(target) {
          noit_boolean should_resolve;
          int valid;
          xmlChar *tmp;
          tmp = xmlNodeGetContent(an);
          valid = noit_check_is_valid_target((char *)tmp);
          xmlFree(tmp);
          if(noit_conf_get_boolean(NULL, "//checks/@resolve_targets",
                                   &should_resolve) &&
             should_resolve == noit_false &&
             !valid) {
            *error = "invalid target";
            return 0;
          }
          target = 1;
        }
        else CHECK_N_SET(resolve_rtype) {
          xmlChar *tmp;
          noit_boolean invalid;
          tmp = xmlNodeGetContent(an);
          invalid = strcmp((char *)tmp, PREFER_IPV4) &&
                    strcmp((char *)tmp, PREFER_IPV6) &&
                    strcmp((char *)tmp, FORCE_IPV4) &&
                    strcmp((char *)tmp, FORCE_IPV6);
          xmlFree(tmp);
          if(invalid) {
            *error = "invalid reslove_rtype";
            return 0;
          }
        }
        else CHECK_N_SET(period) {
          int pint;
          xmlChar *tmp;
          tmp = xmlNodeGetContent(an);
          pint = noit_conf_string_to_int((char *)tmp);
          xmlFree(tmp);
          if(pint < 1000 || pint > 300000) {
            *error = "invalid period";
            return 0;
          }
          period = 1;
        }
        else CHECK_N_SET(timeout) {
          int pint;
          xmlChar *tmp;
          tmp = xmlNodeGetContent(an);
          pint = noit_conf_string_to_int((char *)tmp);
          xmlFree(tmp);
          if(pint < 0 || pint > 300000) {
            *error = "invalid timeout";
            return 0;
          }
          timeout = 1;
        }
        else CHECK_N_SET(filterset) {
          xmlChar *tmp;
          tmp = xmlNodeGetContent(an);
          if(!noit_filter_exists((char *)tmp)) {
            *error = "filterset does not exist";
            return 0;
          }
          filterset = 1;
        }
        else CHECK_N_SET(disable) { /* not required */
          int valid;
          xmlChar *tmp;
          tmp = xmlNodeGetContent(an);
          valid = (!strcasecmp((char *)tmp, "true") ||
                   !strcasecmp((char *)tmp, "on") ||
                   !strcasecmp((char *)tmp, "false") ||
                   !strcasecmp((char *)tmp, "off"));
          xmlFree(tmp);
          if(!valid) { *error = "bad disable parameter"; return 0; }
          target = 1;
        }
        else return 0;
      }
    }
    else if(!strcmp((char *)tl->name, "config")) {
      *c = tl;
      /* Noop, anything goes */
    }
    else return 0;
  }
  if(name && module && target && period && timeout && filterset) return 1;
  *error = "insufficient information";
  return 0;
}
static void
configure_xml_check(xmlNodePtr check, xmlNodePtr a, xmlNodePtr c) {
  xmlNodePtr n, config, oldconfig;
  for(n = a->children; n; n = n->next) {
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
    ATTR2PROP(resolve_rtype);
    ATTR2PROP(module);
    ATTR2PROP(period);
    ATTR2PROP(timeout);
    ATTR2PROP(disable);
    ATTR2PROP(filterset);
  }
  for(oldconfig = check->children; oldconfig; oldconfig = oldconfig->next)
    if(!strcmp((char *)oldconfig->name, "config")) break;
  config = xmlNewNode(NULL, (xmlChar *)"config");
  if(c) {
    xmlAttrPtr inherit;
    if((inherit = xmlHasProp(c, (xmlChar *)"inherit")) != NULL &&
        inherit->children && inherit->children->content)
      xmlSetProp(config, (xmlChar *)"inherit", inherit->children->content);
    for(n = c->children; n; n = n->next) {
      xmlChar *v = xmlNodeGetContent(n);
      xmlNodePtr co = xmlNewNode(NULL, n->name);
      xmlNodeAddContent(co, v);
      xmlFree(v);
      xmlAddChild(config, co);
    }
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
rest_delete_check(noit_http_rest_closure_t *restc,
                  int npats, char **pats) {
  noit_http_session_ctx *ctx = restc->http_ctx;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL;
  xmlNodePtr node;
  uuid_t checkid;
  noit_check_t *check;
  const char *error;
  char xpath[1024], *uuid_conf;
  int rv, cnt, error_code = 500;
  noit_boolean exists = noit_false;

  if(npats != 2) goto error;

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
    if(exists) { error_code = 403; FAIL("uuid not yours"); }
    goto not_found;
  }
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  if(cnt != 1) FAIL("internal error, |checkid| > 1");
  node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, 0);
  uuid_conf = (char *)xmlGetProp(node, (xmlChar *)"uuid");
  if(!uuid_conf || strcasecmp(uuid_conf, pats[1]))
    FAIL("internal error uuid");

  /* delete this here */
  noit_poller_deschedule(check->checkid);
  xmlUnlinkNode(node);
  xmlFreeNode(node);
  if(noit_conf_write_file(NULL) != 0)
    noitL(noit_error, "local config write failed\n");
  noit_conf_mark_changed();
  noit_http_response_ok(ctx, "text/html");
  noit_http_response_end(ctx);
  goto cleanup;

 not_found:
  noit_http_response_not_found(ctx, "text/html");
  noit_http_response_end(ctx);
  goto cleanup;

 error:
  noit_http_response_standard(ctx, error_code, "ERROR", "text/html");
  noit_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if(pobj) xmlXPathFreeObject(pobj);
  return 0;
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
  int rv, cnt, error_code = 500, complete = 0, mask = 0;
  const char *error = "internal error";
  noit_boolean exists = noit_false;

  if(npats != 2) goto error;

  indoc = rest_get_xml_upload(restc, &mask, &complete);
  if(!complete) return mask;
  if(indoc == NULL) FAIL("xml parse error");
  if(!noit_validate_check_rest_post(indoc, &attr, &config, &error)) goto error;

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
    if(exists) { error_code = 403; FAIL("uuid not yours"); }
    else {
      char *target = NULL, *name = NULL, *module = NULL;
      noit_module_t *m;
      xmlNodePtr newcheck, a;
      /* make sure this isn't a dup */
      for(a = attr->children; a; a = a->next) {
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
    char *target = NULL, *name = NULL, *module = NULL;
    xmlNodePtr a;
    noit_check_t *ocheck;

    cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
    if(cnt != 1) FAIL("internal error, |checkid| > 1");
    node = (noit_conf_section_t)xmlXPathNodeSetItem(pobj->nodesetval, 0);
    uuid_conf = (char *)xmlGetProp(node, (xmlChar *)"uuid");
    if(!uuid_conf || strcasecmp(uuid_conf, pats[1]))
      FAIL("internal error uuid");

    /* make sure this isn't a dup */
    for(a = attr->children; a; a = a->next) {
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

  if(noit_conf_write_file(NULL) != 0)
    noitL(noit_error, "local config write failed\n");
  noit_conf_mark_changed();
  noit_poller_reload(xpath);
  if(restc->call_closure_free) restc->call_closure_free(restc->call_closure);
  restc->call_closure_free = NULL;
  restc->call_closure = NULL;
  if(pobj) xmlXPathFreeObject(pobj);
  if(doc) xmlFreeDoc(doc);
  restc->fastpath = rest_show_check;
  return restc->fastpath(restc, restc->nparams, restc->params);

 error:
  noit_http_response_standard(ctx, error_code, "ERROR", "text/html");
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
  return 0;
}

static int
rest_show_config(noit_http_rest_closure_t *restc,
                 int npats, char **pats) {
  noit_http_session_ctx *ctx = restc->http_ctx;
  xmlDocPtr doc = NULL;
  xmlNodePtr node, root;
  char xpath[1024];

  snprintf(xpath, sizeof(xpath), "/noit%s", pats ? pats[0] : "");
  node = noit_conf_get_section(NULL, xpath);

  if(!node) {
    noit_http_response_not_found(ctx, "text/xml");
    noit_http_response_end(ctx);
  }
  else {
    doc = xmlNewDoc((xmlChar *)"1.0");
    root = xmlCopyNode(node, 1);
    xmlDocSetRootElement(doc, root);
    noit_http_response_ok(ctx, "text/xml");
    noit_http_response_xml(ctx, doc);
    noit_http_response_end(ctx);
  }

  if(doc) xmlFreeDoc(doc);

  return 0;
}

void
noit_check_rest_init() {
  assert(noit_http_rest_register_auth(
    "GET", "/", "^config(/.*)?$",
    rest_show_config, noit_http_rest_client_cert_auth
  ) == 0);
  assert(noit_http_rest_register_auth(
    "GET", "/checks/", "^show(/.*)(?<=/)(" UUID_REGEX ")$",
    rest_show_check, noit_http_rest_client_cert_auth
  ) == 0);
  assert(noit_http_rest_register_auth(
    "PUT", "/checks/", "^set(/.*)(?<=/)(" UUID_REGEX ")$",
    rest_set_check, noit_http_rest_client_cert_auth
  ) == 0);
  assert(noit_http_rest_register_auth(
    "DELETE", "/checks/", "^delete(/.*)(?<=/)(" UUID_REGEX ")$",
    rest_delete_check, noit_http_rest_client_cert_auth
  ) == 0);
}

