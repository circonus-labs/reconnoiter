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
#include "noit_filters_lmdb.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_clustering.h"

#define FAIL(a) do { error = (a); goto error; } while(0)

static eventer_jobq_t *filter_updates_jobq = NULL;
static eventer_jobq_t *filterset_cull_jobq = NULL;
static eventer_jobq_t *filterset_delete_jobq = NULL;

static int
rest_show_filters(mtev_http_rest_closure_t *restc,
                  int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  xmlDocPtr doc = NULL;
  mtev_conf_section_t section = MTEV_CONF_EMPTY;
  xmlNodePtr root;
  char xpath[1024];

  if (noit_filters_get_lmdb_instance()) {
    return noit_filters_lmdb_rest_show_filters(restc, npats, pats);
  }

  snprintf(xpath, sizeof(xpath), "//filtersets");

  section = mtev_conf_get_section_read(MTEV_CONF_ROOT, xpath);
  if(mtev_conf_section_is_empty(section)) goto not_found;

  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlCopyNode(mtev_conf_section_to_xmlnodeptr(section), 1);
  xmlDocSetRootElement(doc, root);
  noit_filter_set_db_source_header(restc->http_ctx);
  mtev_http_response_ok(ctx, "text/xml");
  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);
  goto cleanup;

 not_found:
  noit_filter_set_db_source_header(restc->http_ctx);
  mtev_http_response_not_found(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if(doc) {
    xmlFreeDoc(doc);
  }
  mtev_conf_release_section_read(section);
  return 0;
}

static int
rest_show_filter(mtev_http_rest_closure_t *restc,
                 int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  xmlDocPtr doc = NULL;
  mtev_conf_section_t section = MTEV_CONF_EMPTY;
  xmlNodePtr root;
  char xpath[1024];

  if (noit_filters_get_lmdb_instance()) {
    return noit_filters_lmdb_rest_show_filter(restc, npats, pats);
  }

  if(npats != 2) goto not_found;

  snprintf(xpath, sizeof(xpath), "//filtersets%sfilterset[@name=\"%s\"]",
           pats[0], pats[1]);

  section = mtev_conf_get_section_read(MTEV_CONF_ROOT, xpath);
  if(mtev_conf_section_is_empty(section)) goto not_found;

  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlCopyNode(mtev_conf_section_to_xmlnodeptr(section), 1);
  xmlDocSetRootElement(doc, root);
  noit_filter_set_db_source_header(restc->http_ctx);
  mtev_http_response_ok(ctx, "text/xml");
  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);
  goto cleanup;

 not_found:
  noit_filter_set_db_source_header(restc->http_ctx);
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
  section = mtev_conf_get_section_write(MTEV_CONF_ROOT, "/noit/filtersets");
  start = mtev_conf_section_to_xmlnodeptr(section);
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
  mtev_conf_release_section_write(section);
  return start;
}
static int
rest_delete_filter(mtev_http_rest_closure_t *restc,
                   int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  mtev_conf_section_t section = MTEV_CONF_EMPTY;

  char xpath[1024];
  if (noit_filters_get_lmdb_instance()) {
    return noit_filters_lmdb_rest_delete_filter(restc, npats, pats, filterset_delete_jobq);
  }

  if(npats != 2) goto not_found;

  snprintf(xpath, sizeof(xpath), "//filtersets%sfilterset[@name=\"%s\"]",
           pats[0], pats[1]);
  section = mtev_conf_get_section_write(MTEV_CONF_ROOT, xpath);
  if(mtev_conf_section_is_empty(section)) goto not_found;
  if(noit_filter_remove(section) == 0) goto not_found;
  CONF_REMOVE(section);
  xmlUnlinkNode(mtev_conf_section_to_xmlnodeptr(section));
  xmlFreeNode(mtev_conf_section_to_xmlnodeptr(section));

  if(mtev_conf_write_file(NULL) != 0)
    mtevL(noit_error, "local config write failed\n");
  mtev_conf_mark_changed();
  noit_filter_set_db_source_header(restc->http_ctx);
  mtev_http_response_ok(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 not_found:
  noit_filter_set_db_source_header(restc->http_ctx);
  mtev_http_response_not_found(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  mtev_conf_release_section_write(section);
  return 0;
}

typedef struct cull_filter_closure {
  mtev_http_rest_closure_t *restc;
  int count;
} cull_filter_closure_t;

static void
rest_cull_filter_free_closure(void *closure) {
  cull_filter_closure_t *cfc = (cull_filter_closure_t *)closure;
  if (cfc) {
    free(cfc);
  }
}

static int
rest_cull_filter_complete(mtev_http_rest_closure_t *restc, int npats, char **pats) {
  cull_filter_closure_t *cfc = (cull_filter_closure_t *)restc->call_closure;
  mtev_http_session_ctx *ctx = restc->http_ctx;

  char cnt_str[32];
  snprintf(cnt_str, sizeof(cnt_str), "%d", cfc->count);
  noit_filter_set_db_source_header(restc->http_ctx);
  mtev_http_response_ok(ctx, "text/html");
  mtev_http_response_header_set(ctx, "X-Filters-Removed", cnt_str);
  mtev_http_response_end(ctx);
  return 0;
}

static int
rest_cull_filter_asynch(eventer_t e, int mask, void *closure,
                        struct timeval *now) {
  cull_filter_closure_t *cfc = (cull_filter_closure_t *)closure;
  if(mask == EVENTER_ASYNCH_WORK) {
    if (noit_filters_get_lmdb_instance()) {
      cfc->count = noit_filters_lmdb_cull_unused();
    }
    else {
      cfc->count = noit_filtersets_cull_unused();
      if(cfc->count > 0) {
        mtev_conf_mark_changed();
      }
    }
  }
  if(mask == EVENTER_ASYNCH) {
    if (cfc) {
      mtev_http_session_resume_after_float(cfc->restc->http_ctx);
    }
  }
  return 0;
}

static int
rest_cull_filter(mtev_http_rest_closure_t *restc,
                 int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;

  cull_filter_closure_t *cfc = (cull_filter_closure_t *)calloc(1, sizeof(*cfc));
  cfc->restc = restc;
  cfc->count = 0;

  mtev_http_connection *connection = mtev_http_session_connection(ctx);
  mtevAssert(connection);

  eventer_t conne = mtev_http_connection_event_float(connection);
  if(conne) eventer_remove_fde(conne);

  restc->call_closure = cfc;
  restc->call_closure_free = rest_cull_filter_free_closure;
  restc->fastpath = rest_cull_filter_complete;

  eventer_t e = eventer_alloc_asynch(rest_cull_filter_asynch, cfc);
  if(conne) eventer_set_owner(e, eventer_get_owner(conne));

  eventer_add_asynch(filterset_cull_jobq, e);

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

  if (noit_filters_get_lmdb_instance()) {
    return noit_filters_lmdb_rest_set_filter(restc, npats, pats);
  }

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
  section = mtev_conf_get_section_write(MTEV_CONF_ROOT, xpath);
  exists = noit_filter_get_seq(pats[1], &old_seq);
  if(mtev_conf_section_is_empty(section) && exists == mtev_true) {
    /* It's someone else's */
    error = "invalid path";
    error_code = 403;
    goto error;
  }

  if((newfilter = noit_filter_validate_filter(indoc, pats[1], &seq, &error)) == NULL) {
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
  mtev_conf_release_section_write(section);
  return restc->fastpath(restc, restc->nparams, restc->params);

 error:
  noit_filter_set_db_source_header(restc->http_ctx);
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
  mtev_conf_release_section_write(section);
  return 0;
}

typedef struct rest_filter_updates_closure {
  mtev_http_rest_closure_t *restc;
  int64_t prev;
  int64_t end;
  uuid_t peerid;
  xmlDocPtr doc;
} rest_filter_updates_closure_t;

static void rest_show_filter_updates_free_closure(void *v_rfu) {
  rest_filter_updates_closure_t *rfu = (rest_filter_updates_closure_t *)v_rfu;
  if(rfu->doc) xmlFreeDoc(rfu->doc);
  free(rfu);
}

int rest_show_filter_updates_complete(mtev_http_rest_closure_t *restc, int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  rest_filter_updates_closure_t *rfu = (rest_filter_updates_closure_t *) restc->call_closure;
  if (ctx) {
    mtev_http_response_xml(ctx, rfu->doc);
  }
  return 0;
}

static int
rest_show_filter_updates_asynch(eventer_t e, int mask, void *closure, struct timeval *now) {
  rest_filter_updates_closure_t *rfu = (rest_filter_updates_closure_t *)closure;
  if(mask == EVENTER_ASYNCH_WORK) {
    xmlNodePtr root = xmlNewNode(NULL, (xmlChar *)"filtersets");
    rfu->doc = xmlNewDoc((xmlChar *)"1.0");
    xmlDocSetRootElement(rfu->doc, root);
    if (noit_filters_get_lmdb_instance()) {
      noit_cluster_lmdb_filter_changes(rfu->peerid, rfu->restc->remote_cn, rfu->prev, rfu->end, root);
    }
    else {
      noit_cluster_xml_filter_changes(rfu->peerid, rfu->restc->remote_cn, rfu->prev, rfu->end, root);
    }
    noit_filter_set_db_source_header(rfu->restc->http_ctx);
    mtev_http_response_ok(rfu->restc->http_ctx, "text/xml");
  }
  if(mask == EVENTER_ASYNCH) {
    if (rfu) {
      mtev_http_session_resume_after_float(rfu->restc->http_ctx);
    }
  }
  return 0;
}

static int
rest_show_filter_updates(mtev_http_rest_closure_t *restc,
                         int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  mtev_http_request *req = mtev_http_session_request(ctx);
  int64_t prev = 0, end = 0;

  const char *prev_str = mtev_http_request_querystring(req, "prev"); 
  if(prev_str) prev = strtoll(prev_str, NULL, 10);
  const char *end_str = mtev_http_request_querystring(req, "end"); 
  if(end_str) end = strtoll(end_str, NULL, 10);
  const char *peer_str = mtev_http_request_querystring(req, "peer");
  uuid_t peerid;
  if(!peer_str || !restc->remote_cn || mtev_uuid_parse(peer_str, peerid) != 0) {
    noit_filter_set_db_source_header(restc->http_ctx);
    mtev_http_response_server_error(ctx, "text/xml");
    mtev_http_response_end(ctx);
    return 0;
  }

  rest_filter_updates_closure_t *closure = (rest_filter_updates_closure_t*)calloc(1, sizeof(*closure));
  mtevAssert(closure);

  closure->restc = restc;
  closure->prev = prev;
  closure->end = end;
  mtev_uuid_copy(closure->peerid, peerid);
  closure->doc = NULL;

  restc->call_closure = (void *)closure;
  restc->call_closure_free = rest_show_filter_updates_free_closure;
  restc->fastpath = rest_show_filter_updates_complete;

  mtev_http_connection *connection = mtev_http_session_connection(ctx);
  mtevAssert(connection);

  eventer_t conne = mtev_http_connection_event_float(connection);
  if(conne) eventer_remove_fde(conne);

  eventer_t e = eventer_alloc_asynch(rest_show_filter_updates_asynch, closure);
  if(conne) eventer_set_owner(e, eventer_get_owner(conne));

  eventer_add_asynch(filter_updates_jobq, e);

  return 0;
}


void
noit_filters_rest_init() {
  filter_updates_jobq = eventer_jobq_retrieve("filter_updates");
  mtevAssert(filter_updates_jobq);
  filterset_cull_jobq = eventer_jobq_retrieve("filterset_cull");
  mtevAssert(filterset_cull_jobq);
  filterset_delete_jobq = eventer_jobq_retrieve("delete_filter");
  mtevAssert(filterset_delete_jobq);

  mtevAssert(mtev_http_rest_register_auth(
    "GET", "/filters/", "^updates$",
    rest_show_filter_updates, mtev_http_rest_client_cert_auth
  ) == 0);
  mtevAssert(mtev_http_rest_register_auth(
    "GET", "/filters/", "^show$",
    rest_show_filters, mtev_http_rest_client_cert_auth
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

