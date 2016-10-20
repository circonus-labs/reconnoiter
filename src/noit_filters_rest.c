/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2015, Circonus, Inc. All rights reserved.
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

#include <errno.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#include <mtev_listener.h>
#include <mtev_http.h>
#include <mtev_rest.h>
#include <mtev_conf.h>
#include <mtev_conf_private.h>

#include "noit_mtev_bridge.h"
#include "noit_filters.h"
#include "noit_check.h"
#include "noit_check_tools.h"

#define FAIL(a) do { error = (a); goto error; } while(0)

static int
rest_show_filter(mtev_http_rest_closure_t *restc,
                 int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  xmlDocPtr doc = NULL;
  xmlNodePtr node, root;
  char xpath[1024];
  int error_code = 500;

  if(npats != 2) goto error;

  snprintf(xpath, sizeof(xpath), "//filtersets%sfilterset[@name=\"%s\"]",
           pats[0], pats[1]);

  node = mtev_conf_get_section(NULL, xpath);
  if(!node) goto not_found;

  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlCopyNode(node, 1);
  xmlDocSetRootElement(doc, root);
  mtev_http_response_ok(ctx, "text/xml");
  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);
  goto cleanup;

 not_found:
  mtev_http_response_not_found(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 error:
  mtev_http_response_standard(ctx, error_code, "ERROR", "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if(doc) xmlFreeDoc(doc);
  return 0;
}

static xmlNodePtr
make_conf_path(char *path) {
  xmlNodePtr start, tmp;
  char fullpath[1024], *tok, *brk;
  if(!path || strlen(path) < 1) return NULL;
  snprintf(fullpath, sizeof(fullpath), "%s", path+1);
  fullpath[strlen(fullpath)-1] = '\0';
  start = mtev_conf_get_section(NULL, "/noit/filtersets");
  if(!start) return NULL;
  for (tok = strtok_r(fullpath, "/", &brk);
       tok;
       tok = strtok_r(NULL, "/", &brk)) {
    if(!xmlValidateNameValue((xmlChar *)tok)) return NULL;
    if(!strcmp(tok, "filterset")) return NULL;
    for (tmp = start->children; tmp; tmp = tmp->next) {
      if(!strcmp((char *)tmp->name, tok)) break;
    }
    if(!tmp) {
      tmp = xmlNewNode(NULL, (xmlChar *)tok);
      xmlAddChild(start, tmp);
      CONF_DIRTY(tmp);
    }
    start = tmp;
  }
  return start;
}
static xmlNodePtr
validate_filter_post(xmlDocPtr doc, char *name) {
  xmlNodePtr root, r;
  char *old_name;
  root = xmlDocGetRootElement(doc);
  if(!root) return NULL;
  if(strcmp((char *)root->name, "filterset")) return NULL;

  old_name = (char *)xmlGetProp(root, (xmlChar *)"name");
  if(old_name == NULL) {
    xmlSetProp(root, (xmlChar *)"name", (xmlChar *)name);
  } else if(name == NULL || strcmp(old_name, name)) {
    xmlFree(old_name);
    return NULL;
  }
  if(old_name) xmlFree(old_name);

  if(!root->children) return NULL;
  for(r = root->children; r; r = r->next) {
    char *type;
    if(strcmp((char *)r->name, "rule")) return NULL;
    type = (char *)xmlGetProp(r, (xmlChar *)"type");
    if(!type || (strcmp(type, "deny") && strcmp(type, "accept") && strcmp(type, "allow"))) {
      if(type) xmlFree(type);
      return NULL;
    }
    if(type) xmlFree(type);
  }
  return root;
}
static int
rest_delete_filter(mtev_http_rest_closure_t *restc,
                   int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  xmlNodePtr node;
  char xpath[1024];
  int error_code = 500;

  if(npats != 2) goto error;

  snprintf(xpath, sizeof(xpath), "//filtersets%sfilterset[@name=\"%s\"]",
           pats[0], pats[1]);
  node = mtev_conf_get_section(NULL, xpath);
  if(!node) goto not_found;
  if(noit_filter_remove(node) == 0) goto not_found;
  CONF_REMOVE(node);
  xmlUnlinkNode(node);
  xmlFreeNode(node);

  if(mtev_conf_write_file(NULL) != 0)
    mtevL(noit_error, "local config write failed\n");
  mtev_conf_mark_changed();
  mtev_http_response_ok(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 not_found:
  mtev_http_response_not_found(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 error:
  mtev_http_response_standard(ctx, error_code, "ERROR", "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  return 0;
}

static int
rest_cull_filter(mtev_http_rest_closure_t *restc,
                 int npats, char **pats) {
  int rv;
  char cnt_str[32];
  mtev_http_session_ctx *ctx = restc->http_ctx;

  rv = noit_filtersets_cull_unused();
  if(rv > 0) mtev_conf_mark_changed();
  snprintf(cnt_str, sizeof(cnt_str), "%d", rv);
  mtev_http_response_ok(ctx, "text/html");
  mtev_http_response_header_set(ctx, "X-Filters-Removed", cnt_str);
  mtev_http_response_end(ctx);
  return 0;
}

static int
rest_set_filter(mtev_http_rest_closure_t *restc,
                int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  xmlDocPtr doc = NULL, indoc = NULL;
  xmlNodePtr node, parent, root, newfilter;
  char xpath[1024];
  int error_code = 500, complete = 0, mask = 0;
  const char *error = "internal error";

  if(npats != 2) goto error;

  indoc = rest_get_xml_upload(restc, &mask, &complete);
  if(!complete) return mask;
  if(indoc == NULL) FAIL("xml parse error");

  snprintf(xpath, sizeof(xpath), "//filtersets%sfilterset[@name=\"%s\"]",
           pats[0], pats[1]);
  node = mtev_conf_get_section(NULL, xpath);
  if(!node && noit_filter_exists(pats[1])) {
    /* It's someone else's */
    error_code = 403;
    goto error;
  }

  if((newfilter = validate_filter_post(indoc, pats[1])) == NULL) goto error;

  parent = make_conf_path(pats[0]);
  if(!parent) FAIL("invalid path");
  if(node) {
    CONF_REMOVE(node);
    xmlUnlinkNode(node);
    xmlFreeNode(node);
  }
  xmlUnlinkNode(newfilter);
  xmlAddChild(parent, newfilter);
  CONF_DIRTY(newfilter);

  mtev_conf_mark_changed();
  if(mtev_conf_write_file(NULL) != 0)
    mtevL(noit_error, "local config write failed\n");
  noit_filter_compile_add(newfilter);
  if(restc->call_closure_free) restc->call_closure_free(restc->call_closure);
  restc->call_closure_free = NULL;
  restc->call_closure = NULL;
  restc->fastpath = rest_show_filter;
  return restc->fastpath(restc, restc->nparams, restc->params);

 error:
  mtev_http_response_standard(ctx, error_code, "ERROR", "text/html");
  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlNewDocNode(doc, NULL, (xmlChar *)"error", NULL);
  xmlDocSetRootElement(doc, root);
  xmlNodeAddContent(root, (xmlChar *)error);
  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if(doc) xmlFreeDoc(doc);
  return 0;
}

void
noit_filters_rest_init() {
  mtevAssert(mtev_http_rest_register_auth(
    "GET", "/filters/", "^show(/.*)(?<=/)([^/]+)$",
    rest_show_filter, mtev_http_rest_client_cert_auth
  ) == 0);
  mtevAssert(mtev_http_rest_register_auth(
    "PUT", "/filters/", "^set(/.*)(?<=/)([^/]+)$",
    rest_set_filter, mtev_http_rest_client_cert_auth
  ) == 0);
  mtevAssert(mtev_http_rest_register_auth(
    "DELETE", "/filters/", "^delete(/.*)(?<=/)([^/]+)$",
    rest_delete_filter, mtev_http_rest_client_cert_auth
  ) == 0);
  mtevAssert(mtev_http_rest_register_auth(
    "POST", "/filters/", "^cull$",
    rest_cull_filter, mtev_http_rest_client_cert_auth
  ) == 0);
}

