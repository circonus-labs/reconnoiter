/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2015-2017, Circonus, Inc. All rights reserved.
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
#include <mtev_uuid.h>

#include "noit_mtev_bridge.h"
#include "noit_filters.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_clustering.h"

#define FAIL(a) do { error = (a); goto error; } while(0)

static int
rest_show_filter(mtev_http_rest_closure_t *restc,
                 int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  xmlDocPtr doc = NULL;
  mtev_conf_section_t section = MTEV_CONF_EMPTY;
  xmlNodePtr root;
  char xpath[1024];

  if(npats != 2) goto not_found;

  snprintf(xpath, sizeof(xpath), "//filtersets%sfilterset[@name=\"%s\"]",
           pats[0], pats[1]);

  section = mtev_conf_get_section_read(MTEV_CONF_ROOT, xpath);
  if(mtev_conf_section_is_empty(section)) goto not_found;

  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlCopyNode(mtev_conf_section_to_xmlnodeptr(section), 1);
  xmlDocSetRootElement(doc, root);
  mtev_http_response_ok(ctx, "text/xml");
  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);
  goto cleanup;

 not_found:
  mtev_http_response_not_found(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if(doc) xmlFreeDoc(doc);
  mtev_conf_release_section_read(section);
  return 0;
}

static xmlNodePtr
make_conf_path(char *path) {
  mtev_conf_section_t section;
  xmlNodePtr start, tmp;
  char fullpath[1024], *tok, *brk;
  if(!path || strlen(path) < 1) return NULL;
  snprintf(fullpath, sizeof(fullpath), "%s", path+1);
  fullpath[strlen(fullpath)-1] = '\0';
  section = mtev_conf_get_section(MTEV_CONF_ROOT, "/noit/filtersets");
  start = mtev_conf_section_to_xmlnodeptr(section);
  mtev_conf_release_section(section);
  if(mtev_conf_section_is_empty(section)) return NULL;
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
      CONF_DIRTY(mtev_conf_section_from_xmlnodeptr(tmp));
    }
    start = tmp;
  }
  return start;
}
static xmlNodePtr
validate_filter_post(xmlDocPtr doc, char *name, int64_t *seq, const char **err) {
  xmlNodePtr root, r, previous_child;
  char *old_name;
  *err = "data validation error";

  if(seq) *seq = 0;
  root = xmlDocGetRootElement(doc);
  if(!root) return NULL;
  if(strcmp((char *)root->name, "filterset")) {
    *err = "bad root node";
    return NULL;
  }

  old_name = (char *)xmlGetProp(root, (xmlChar *)"name");
  if(old_name == NULL) {
    xmlSetProp(root, (xmlChar *)"name", (xmlChar *)name);
  } else if(name == NULL || strcmp(old_name, name)) {
    xmlFree(old_name);
    *err = "name mismatch";
    return NULL;
  }
  if(old_name) xmlFree(old_name);

  if(!root->children) {
    *err = "no rules";
    return NULL;
  }

  xmlChar *seqstr = xmlGetProp(root, (xmlChar *)"seq");
  if(seqstr && seq) {
    *seq = strtoll((const char *)seqstr, NULL, 10);
  }
  if(seqstr) xmlFree(seqstr);

  previous_child = root;
  int rulecnt = 0;
  for(r = root->children; r; r = r->next) {
#define CHECK_N_SET(a) if(!strcmp((char *)r->name, #a))
    char *type;
    CHECK_N_SET(rule) {
      type = (char *)xmlGetProp(r, (xmlChar *)"type");
      if(!type || (strcmp(type, "deny") && strcmp(type, "accept") && strcmp(type, "allow") &&
                   strncmp(type, "skipto:", strlen("skipto:")))) {
        if(type) xmlFree(type);
        *err = "unknown type";
        return NULL;
      }
      rulecnt++;
      if(type) xmlFree(type);
    }
    else CHECK_N_SET(seq) {
      xmlChar *v = xmlNodeGetContent(r);
      if(v) xmlSetProp(root, r->name, v);
      else xmlUnsetProp(root, r->name);
      xmlUnlinkNode(r);
      xmlFreeNode(r);
      r = previous_child;

      if (seq && v) *seq = strtoll((const char *)v, NULL, 10);

      xmlFree(v);
    }
    else if(strcmp((char *)r->name, "text") != 0) {
      /* ignore text nodes */
      *err = "unknown attribute";
      return NULL;
    }
    previous_child = r;
  }

  if(rulecnt == 0) {
    *err = "no rules";
    return NULL;
  }
  return root;
}
static int
rest_delete_filter(mtev_http_rest_closure_t *restc,
                   int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  mtev_conf_section_t section = MTEV_CONF_EMPTY;
  char xpath[1024];

  if(npats != 2) goto not_found;

  snprintf(xpath, sizeof(xpath), "//filtersets%sfilterset[@name=\"%s\"]",
           pats[0], pats[1]);
  section = mtev_conf_get_section(MTEV_CONF_ROOT, xpath);
  if(mtev_conf_section_is_empty(section)) goto not_found;
  if(noit_filter_remove(section) == 0) goto not_found;
  CONF_REMOVE(section);
  xmlUnlinkNode(mtev_conf_section_to_xmlnodeptr(section));
  xmlFreeNode(mtev_conf_section_to_xmlnodeptr(section));

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

 cleanup:
  mtev_conf_release_section(section);
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
  mtev_conf_section_t section = MTEV_CONF_EMPTY;
  xmlNodePtr parent, root, newfilter;
  char xpath[1024];
  int error_code = 500, complete = 0, mask = 0;
  mtev_boolean exists;
  int64_t seq = 0;
  int64_t old_seq = 0;
  const char *error = "internal error";

  if(npats != 2) {
    error = "invalid URI";
    error_code = 404;
    goto error;
  }

  indoc = rest_get_xml_upload(restc, &mask, &complete);
  if(!complete) return mask;
  if(indoc == NULL) {
    error_code = 406;
    FAIL("xml parse error");
  }

  snprintf(xpath, sizeof(xpath), "//filtersets%sfilterset[@name=\"%s\"]",
           pats[0], pats[1]);
  section = mtev_conf_get_section(MTEV_CONF_ROOT, xpath);
  exists = noit_filter_get_seq(pats[1], &old_seq);
  if(mtev_conf_section_is_empty(section) && exists == mtev_true) {
    /* It's someone else's */
    error = "invalid path";
    error_code = 403;
    goto error;
  }

  if((newfilter = validate_filter_post(indoc, pats[1], &seq, &error)) == NULL) {
    goto error;
  }
  if(exists && (old_seq >= seq && seq != 0)) {
    error_code = 409;
    error = "sequencing error";
    goto error;
  }

  parent = make_conf_path(pats[0]);
  if(!parent) {
    error_code = 404;
    FAIL("invalid path");
  }
  if(!mtev_conf_section_is_empty(section)) {
    CONF_REMOVE(section);
    xmlUnlinkNode(mtev_conf_section_to_xmlnodeptr(section));
    xmlFreeNode(mtev_conf_section_to_xmlnodeptr(section));
  }
  xmlUnlinkNode(newfilter);
  xmlAddChild(parent, newfilter);
  CONF_DIRTY(mtev_conf_section_from_xmlnodeptr(newfilter));

  mtev_conf_mark_changed();
  if(mtev_conf_write_file(NULL) != 0)
    mtevL(noit_error, "local config write failed\n");
  noit_filter_compile_add(mtev_conf_section_from_xmlnodeptr(newfilter));
  if(restc->call_closure_free) restc->call_closure_free(restc->call_closure);
  restc->call_closure_free = NULL;
  restc->call_closure = NULL;
  restc->fastpath = rest_show_filter;
  mtev_conf_release_section(section);
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
  mtev_conf_release_section(section);
  return 0;
}

static int
rest_show_filter_updates(mtev_http_rest_closure_t *restc,
                         int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  mtev_http_request *req = mtev_http_session_request(ctx);
  xmlDocPtr doc = NULL;
  xmlNodePtr root;
  int64_t prev = 0, end = 0;

  const char *prev_str = mtev_http_request_querystring(req, "prev"); 
  if(prev_str) prev = strtoll(prev_str, NULL, 10);
  const char *end_str = mtev_http_request_querystring(req, "end"); 
  if(end_str) end = strtoll(end_str, NULL, 10);
  const char *peer_str = mtev_http_request_querystring(req, "peer");
  uuid_t peerid;
  if(!peer_str || !restc->remote_cn || mtev_uuid_parse(peer_str, peerid) != 0) {
    mtev_http_response_server_error(ctx, "text/xml");
    mtev_http_response_end(ctx);
    return 0;
  }

  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlNewNode(NULL, (xmlChar *)"filtersets");
  xmlDocSetRootElement(doc, root);
  noit_cluster_xml_filter_changes(peerid, restc->remote_cn, prev, end, root);
  mtev_http_response_ok(ctx, "text/xml");
  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);

  if(doc) xmlFreeDoc(doc);

  return 0;
}


void
noit_filters_rest_init() {
  mtevAssert(mtev_http_rest_register_auth(
    "GET", "/filters/", "^updates$",
    rest_show_filter_updates, mtev_http_rest_client_cert_auth
  ) == 0);
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

