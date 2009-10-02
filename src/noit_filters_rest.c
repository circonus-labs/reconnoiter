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
#include "noit_filters.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_conf.h"
#include "noit_conf_private.h"

#define FAIL(a) do { error = (a); goto error; } while(0)

static int
rest_show_filter(noit_http_rest_closure_t *restc,
                 int npats, char **pats) {
  noit_http_session_ctx *ctx = restc->http_ctx;
  xmlDocPtr doc = NULL;
  xmlNodePtr node, root;
  char xpath[1024];
  int error_code = 500;

  if(npats != 2) goto error;

  snprintf(xpath, sizeof(xpath), "//filtersets%sfilterset[@name=\"%s\"]",
           pats[0], pats[1]);

  node = noit_conf_get_section(NULL, xpath);
  if(!node) goto not_found;

  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlCopyNode(node, 1);
  xmlDocSetRootElement(doc, root);
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
  start = noit_conf_get_section(NULL, "/noit/filtersets");
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
    }
    start = tmp;
  }
  return start;
}
static xmlNodePtr
validate_filter_post(xmlDocPtr doc) {
  xmlNodePtr root, r;
  root = xmlDocGetRootElement(doc);
  if(!root) return NULL;
  if(strcmp((char *)root->name, "filterset")) return NULL;
  if(xmlHasProp(root, (xmlChar *)"name")) return NULL;
  if(!root->children) return NULL;
  for(r = root->children; r; r = r->next) {
    char *type;
    if(strcmp((char *)r->name, "rule")) return NULL;
    type = (char *)xmlGetProp(r, (xmlChar *)"type");
    if(!type || (strcmp(type, "deny") && strcmp(type, "allow"))) {
      if(type) xmlFree(type);
      return NULL;
    }
    if(type) xmlFree(type);
  }
  return root;
}
static int
rest_delete_filter(noit_http_rest_closure_t *restc,
                   int npats, char **pats) {
  noit_http_session_ctx *ctx = restc->http_ctx;
  xmlNodePtr node;
  char xpath[1024];
  int error_code = 500;

  if(npats != 2) goto error;

  snprintf(xpath, sizeof(xpath), "//filtersets%sfilterset[@name=\"%s\"]",
           pats[0], pats[1]);
  node = noit_conf_get_section(NULL, xpath);
  if(!node) goto not_found;
  noit_filter_remove(node);

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
  return 0;
}

static int
rest_set_filter(noit_http_rest_closure_t *restc,
                int npats, char **pats) {
  noit_http_session_ctx *ctx = restc->http_ctx;
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
  node = noit_conf_get_section(NULL, xpath);
  if(!node && noit_filter_exists(pats[1])) {
    /* It's someone else's */
    error_code = 403;
    goto error;
  }

  if((newfilter = validate_filter_post(indoc)) == NULL) goto error;
  xmlSetProp(newfilter, (xmlChar *)"name", (xmlChar *)pats[1]);

  parent = make_conf_path(pats[0]);
  if(!parent) FAIL("invalid path");
  if(node) {
    xmlUnlinkNode(node);
    xmlFreeNode(node);
  }
  xmlUnlinkNode(newfilter);
  xmlAddChild(parent, newfilter);

  if(noit_conf_write_file(NULL) != 0)
    noitL(noit_error, "local config write failed\n");
  noit_conf_mark_changed();
  noit_filter_compile_add(newfilter);
  if(restc->call_closure_free) restc->call_closure_free(restc->call_closure);
  restc->call_closure_free = NULL;
  restc->call_closure = NULL;
  restc->fastpath = rest_show_filter;
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
  if(doc) xmlFreeDoc(doc);
  return 0;
}

void
noit_filters_rest_init() {
  assert(noit_http_rest_register_auth(
    "GET", "/filters/", "^show(/.*)(?<=/)([^/]+)$",
    rest_show_filter, noit_http_rest_client_cert_auth
  ) == 0);
  assert(noit_http_rest_register_auth(
    "PUT", "/filters/", "^set(/.*)(?<=/)([^/]+)$",
    rest_set_filter, noit_http_rest_client_cert_auth
  ) == 0);
  assert(noit_http_rest_register_auth(
    "DELETE", "/filters/", "^delete(/.*)(?<=/)([^/]+)$",
    rest_delete_filter, noit_http_rest_client_cert_auth
  ) == 0);
}

