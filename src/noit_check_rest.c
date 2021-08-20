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
#include <mtev_json.h>
#include <mtev_uuid.h>
#include <mtev_memory.h>

#include "noit_mtev_bridge.h"
#include "noit_filters.h"
#include "noit_check.h"
#include "noit_check_rest.h"
#include "noit_check_lmdb.h"
#include "noit_conf_checks.h"
#include "noit_check_resolver.h"
#include "noit_check_tools.h"

#define FAIL(a) do { error = (a); goto error; } while(0)
#define FAILC(c, a) do { error_code = (c); error = (a); goto error; } while(0)

#define NCINIT_RD \
  const mtev_boolean nc_lock_ro = mtev_true; \
  mtev_boolean nc__locked = mtev_false; \
  mtev_conf_section_t nc__lock
#define NCINIT_WR \
  const mtev_boolean nc_lock_ro = mtev_false; \
  mtev_boolean nc__locked = mtev_false; \
  mtev_conf_section_t nc__lock
#define NCLOCK \
  nc__locked = mtev_true; \
  nc__lock = (nc_lock_ro) ? mtev_conf_get_section_read(MTEV_CONF_ROOT, "/noit") :mtev_conf_get_section_write(MTEV_CONF_ROOT, "/noit")
#define NCUNLOCK if(nc__locked) nc_lock_ro ? mtev_conf_release_section_read(nc__lock) : mtev_conf_release_section_write(nc__lock)

#define NS_NODE_CONTENT(parent, ns, k, v, followup) do { \
  xmlNodePtr tmp; \
  if(v) { \
    if(xmlValidateNameValue((xmlChar *)(k))) { \
      tmp = xmlNewNode(ns, (xmlChar *)(k)); \
    } else { \
      tmp = xmlNewNode(ns, (xmlChar *)"value"); \
      xmlSetProp(tmp, (xmlChar *)"name", (xmlChar *)(k)); \
    } \
    xmlNodeAddContent(tmp, (xmlChar *)(v)); \
    followup \
    xmlAddChild(parent, tmp); \
  } \
} while(0)
#define NODE_CONTENT(parent, k, v) NS_NODE_CONTENT(parent, NULL, k, v, )

static eventer_jobq_t *set_check_jobq = NULL;

static void
add_metrics_to_node(noit_check_t *check, stats_t *c, xmlNodePtr metrics, const char *type,
                    int include_time, mtev_hash_table *supp) {
  mtev_hash_table *mets;
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  const char *k;
  int klen;
  void *data;
  xmlNodePtr tmp;

  mtev_memory_begin();
  mets = noit_check_stats_metrics(c);
  while(mtev_hash_next(mets, &iter, &k, &klen, &data)) {
    char buff[256];
    metric_t *m = (metric_t *)data;
    if(supp) {
      void *unused;
      if(mtev_hash_retrieve(supp, k, klen, &unused)) continue;
      mtev_hash_store(supp, k, klen, NULL);
    }
    xmlAddChild(metrics, (tmp = xmlNewNode(NULL, (xmlChar *)"metric")));
    xmlSetProp(tmp, (xmlChar *)"name", (xmlChar *)m->metric_name);
    buff[0] = m->metric_type; buff[1] = '\0';
    xmlSetProp(tmp, (xmlChar *)"type", (xmlChar *)buff);
    if(!noit_apply_filterset(check->filterset, check, m)) {
      xmlSetProp(tmp, (xmlChar *)"filtered", (xmlChar *)"true");
    }
    if(m->metric_value.s) {
      int rv;
      rv = noit_stats_snprint_metric_value(buff, sizeof(buff), m);
      if(rv < 0)
        xmlSetProp(tmp, (xmlChar *)"error", (xmlChar *)"unknown type");
      else
        xmlNodeAddContent(tmp, (xmlChar *)buff);
    }
  }
  xmlSetProp(metrics, (xmlChar *)"type", (const xmlChar *) type);
  if(include_time) {
    struct timeval *f = noit_check_stats_whence(c, NULL);
    char timestr[20];
    snprintf(timestr, sizeof(timestr), "%0.3f",
             f->tv_sec + (f->tv_usec / 1000000.0));
    xmlSetProp(metrics, (xmlChar *)"timestamp", (xmlChar *)timestr);
  }
  mtev_memory_end();
}
xmlNodePtr
noit_check_state_as_xml(noit_check_t *check, int full) {
  xmlNodePtr state, tmp, metrics;
  struct timeval now, *whence;
  stats_t *c = noit_check_get_stats_current(check);

  mtev_gettimeofday(&now, NULL);
  state = xmlNewNode(NULL, (xmlChar *)"state");
  NODE_CONTENT(state, "running", NOIT_CHECK_RUNNING(check)?"true":"false");
  NODE_CONTENT(state, "killed", NOIT_CHECK_KILLED(check)?"true":"false");
  NODE_CONTENT(state, "configured",
               NOIT_CHECK_CONFIGURED(check)?"true":"false");
  NODE_CONTENT(state, "disabled", NOIT_CHECK_DISABLED(check)?"true":"false");
  NODE_CONTENT(state, "target_ip", check->target_ip);
  xmlAddChild(state, (tmp = xmlNewNode(NULL, (xmlChar *)"last_run")));
  whence = noit_check_stats_whence(c, NULL);
  if(whence->tv_sec) {
    char timestr[20];
    snprintf(timestr, sizeof(timestr), "%0.3f",
             now.tv_sec + (now.tv_usec / 1000000.0));
    xmlSetProp(tmp, (xmlChar *)"now", (xmlChar *)timestr);
    snprintf(timestr, sizeof(timestr), "%0.3f",
             whence->tv_sec + (whence->tv_usec / 1000000.0));
    xmlNodeAddContent(tmp, (xmlChar *)timestr);
  }
  if(full) {
    mtev_hash_table suppression, *supp = NULL;
    stats_t *previous;
    struct timeval *whence;
    uint8_t available = noit_check_stats_available(c, NULL);
    if(available) { /* truth here means the check has been run */
      char buff[20], *compiler_warning;
      snprintf(buff, sizeof(buff), "%0.3f",
               (float)noit_check_stats_duration(c, NULL)/1000.0);
      compiler_warning = buff;
      NODE_CONTENT(state, "runtime", compiler_warning);
    }
    NODE_CONTENT(state, "availability",
                 noit_check_available_string(available));
    NODE_CONTENT(state, "state", noit_check_state_string(noit_check_stats_state(c, NULL)));
    NODE_CONTENT(state, "status", noit_check_stats_status(c, NULL));

    if(full > 0) {
      if(full == 1) {
        mtev_hash_init(&suppression);
        supp = &suppression;
      }
      xmlAddChild(state, (metrics = xmlNewNode(NULL, (xmlChar *)"metrics")));
  
      noit_check_stats_populate_xml_hook_invoke(metrics, check, noit_check_get_stats_inprogress(check), "inprogress");
      add_metrics_to_node(check, noit_check_get_stats_inprogress(check), metrics, "inprogress", 0, supp);
      whence = noit_check_stats_whence(c, NULL);
      if(whence->tv_sec) {
        xmlAddChild(state, (metrics = xmlNewNode(NULL, (xmlChar *)"metrics")));
        noit_check_stats_populate_xml_hook_invoke(metrics, check, c, "current");
        add_metrics_to_node(check, c, metrics, "current", 1, supp);
      }
      previous = noit_check_get_stats_previous(check);
      whence = noit_check_stats_whence(previous, NULL);
      if(whence->tv_sec) {
        xmlAddChild(state, (metrics = xmlNewNode(NULL, (xmlChar *)"metrics")));
        noit_check_stats_populate_xml_hook_invoke(metrics, check, previous, "previous");
        add_metrics_to_node(check, previous, metrics, "previous", 1, supp);
      }
      if(supp) mtev_hash_destroy(supp, NULL, NULL);
    }
  }
  return state;
}

static struct json_object *
stats_to_json(noit_check_t *check, stats_t *c, const char *name, mtev_hash_table *supp) {
  struct json_object *doc;
  doc = json_object_new_object();
  mtev_hash_table *metrics;
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  const char *k;
  int klen;
  void *data;

  noit_check_stats_populate_json_hook_invoke(doc, check, c, name);

  mtev_memory_begin();
  metrics = noit_check_stats_metrics(c);
  while(mtev_hash_next(metrics, &iter, &k, &klen, &data)) {
    char buff[256];
    metric_t *m = (metric_t *)data;
    if(supp) {
      void *unused;
      if(mtev_hash_retrieve(supp, k, klen, &unused)) continue;
      mtev_hash_store(supp, k, klen, NULL);
    }
    struct json_object *metric = json_object_new_object();
    buff[0] = m->metric_type; buff[1] = '\0';
    json_object_object_add(metric, "_type", json_object_new_string(buff));
    if(m->metric_value.s) {
      int rv;
      rv = noit_stats_snprint_metric_value(buff, sizeof(buff), m);
      if(rv >= 0)
        json_object_object_add(metric, "_value", json_object_new_string(buff));
    }
    if(!noit_apply_filterset(check->filterset, check, m)) {
      json_object_object_add(metric, "_filtered", json_object_new_boolean(1));
    }
    json_object_object_add(doc, m->metric_name, metric);
  }
  mtev_memory_end();
  return doc;
}

struct json_object *
noit_check_state_as_json(noit_check_t *check, int full) {
  stats_t *c;
  char seq_str[64];
  char id_str[UUID_STR_LEN+1];
  struct json_object *j_last_run, *j_next_run;
  struct timeval *t, check_whence;
  uint64_t ms = 0;
  struct json_object *doc;
  mtev_uuid_unparse_lower(check->checkid, id_str);
  snprintf(seq_str, sizeof(seq_str), "%lld", (long long)check->config_seq);

  doc = json_object_new_object();
  json_object_object_add(doc, "id", json_object_new_string(id_str));
  json_object_object_add(doc, "seq", json_object_new_string(seq_str));
  json_object_object_add(doc, "flags", json_object_new_int(check->flags));
  if(NOIT_CHECK_DELETED(check)) return doc;

  json_object_object_add(doc, "name", json_object_new_string(check->name));
  json_object_object_add(doc, "module", json_object_new_string(check->module));
  json_object_object_add(doc, "target", json_object_new_string(check->target));
  json_object_object_add(doc, "target_ip", json_object_new_string(check->target_ip));
  json_object_object_add(doc, "filterset", json_object_new_string(check->filterset));
  json_object_object_add(doc, "period", json_object_new_int(check->period));
  json_object_object_add(doc, "timeout", json_object_new_int(check->timeout));
  json_object_object_add(doc, "active_on_cluster_node", json_object_new_boolean(noit_should_run_check(check, NULL)));

  c = noit_check_get_stats_current(check);
  t = noit_check_stats_whence(c, NULL);
  j_last_run = json_object_new_int(ms);
  json_object_set_int_overflow(j_last_run, json_overflow_uint64);
  ms = t->tv_sec;
  ms *= 1000ULL;
  ms += t->tv_usec/1000;
  json_object_set_uint64(j_last_run, ms);
  json_object_object_add(doc, "last_run", j_last_run);

  if(check->fire_event) {
    check_whence = eventer_get_whence(check->fire_event);
    t = &check_whence;
  }
  else t = NULL;
  if(t) {
    j_next_run = json_object_new_int(ms);
    json_object_set_int_overflow(j_next_run, json_overflow_uint64);
    ms = t->tv_sec;
    ms *= 1000ULL;
    ms += t->tv_usec/1000;
    json_object_set_uint64(j_next_run, ms);
    json_object_object_add(doc, "next_run", j_next_run);
  }

  if(full) {
    mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
    const char *k;
    int klen;
    void *data;
    mtev_hash_table *configh;
    char timestr[20];
    struct json_object *status, *metrics, *config;

    /* config */
    config = json_object_new_object();
    configh = check->config;
    while(mtev_hash_next(configh, &iter, &k, &klen, &data))
      json_object_object_add(config, k, json_object_new_string(data));
    json_object_object_add(doc, "config", config);

    /* status */
    status = json_object_new_object();
    switch(noit_check_stats_available(c, NULL)) {
      case NP_UNKNOWN: break;
      case NP_AVAILABLE:
        json_object_object_add(status, "available", json_object_new_boolean(1));
        break;
      case NP_UNAVAILABLE:
        json_object_object_add(status, "available", json_object_new_boolean(0));
        break;
    }
    switch(noit_check_stats_state(c, NULL)) {
      case NP_UNKNOWN: break;
      case NP_GOOD:
        json_object_object_add(status, "good", json_object_new_boolean(1));
        break;
      case NP_BAD:
        json_object_object_add(status, "good", json_object_new_boolean(0));
        break;
    }
    json_object_object_add(doc, "status", status);
    if(full > 0) {
      mtev_hash_table suppression, *supp = NULL;
      if(full == 1) {
        mtev_hash_init(&suppression);
        supp = &suppression;
      }
      metrics = json_object_new_object();
  
      t = noit_check_stats_whence(c, NULL);
      if(t->tv_sec) {
        json_object_object_add(metrics, "current", stats_to_json(check, c, "current", supp));
        snprintf(timestr, sizeof(timestr), "%llu%03d",
                 (unsigned long long int)t->tv_sec, (int)(t->tv_usec / 1000));
        json_object_object_add(metrics, "current_timestamp", json_object_new_string(timestr));
      }
  
      c = noit_check_get_stats_inprogress(check);
      t = noit_check_stats_whence(c, NULL);
      if(t->tv_sec) {
        json_object_object_add(metrics, "inprogress", stats_to_json(check, c, "inprogress", supp));
        snprintf(timestr, sizeof(timestr), "%llu%03d",
                 (unsigned long long int)t->tv_sec, (int)(t->tv_usec / 1000));
        json_object_object_add(metrics, "inprogress_timestamp", json_object_new_string(timestr));
      }
  
      c = noit_check_get_stats_previous(check);
      t = noit_check_stats_whence(c, NULL);
      if(t->tv_sec) {
        json_object_object_add(metrics, "previous", stats_to_json(check, c, "previous", supp));
        snprintf(timestr, sizeof(timestr), "%llu%03d",
                 (unsigned long long int)t->tv_sec, (int)(t->tv_usec / 1000));
        json_object_object_add(metrics, "previous_timestamp", json_object_new_string(timestr));
      }
  
      json_object_object_add(doc, "metrics", metrics);
      if(supp) mtev_hash_destroy(supp, NULL, NULL);
    }
  }
  return doc;
}

static int
json_check_accum(noit_check_t *check, void *closure) {
  struct json_object *cobj, *doc = closure;
  char id_str[UUID_STR_LEN+1];
  mtev_uuid_unparse_lower(check->checkid, id_str);
  cobj = noit_check_state_as_json(check, 0);
  json_object_object_del(cobj, "id");
  json_object_object_add(doc, id_str, cobj);
  return 1;
}
static int
rest_show_checks_json(mtev_http_rest_closure_t *restc,
                      int npats, char **pats) {
  const char *jsonstr;
  struct json_object *doc;
  doc = json_object_new_object();
  noit_poller_do(json_check_accum, doc);

  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_ok(restc->http_ctx, "application/json");
  jsonstr = json_object_to_json_string(doc);
  mtev_http_response_append(restc->http_ctx, jsonstr, strlen(jsonstr));
  mtev_http_response_append(restc->http_ctx, "\n", 1);
  json_object_put(doc);
  mtev_http_response_end(restc->http_ctx);
  return 0;
}
static int
rest_show_checks(mtev_http_rest_closure_t *restc,
                 int npats, char **pats) {
  if(npats == 1 && !strcmp(pats[0], ".json")) {
    return rest_show_checks_json(restc, npats, pats);
  }
  else if (noit_check_get_lmdb_instance()) {
    return noit_check_lmdb_show_checks(restc, npats, pats);
  }
  else {
    char *cpath = "/checks";
    return noit_rest_show_config(restc, 1, &cpath);
  }
  mtevFatal(mtev_error, "unreachable\n");
  return 0;
}

static int
rest_show_check_owner(mtev_http_rest_closure_t *restc,
                      int npats, char **pats) {
  uuid_t checkid;
  if(npats != 1 || mtev_uuid_parse(pats[0], checkid) != 0) {
    noit_check_set_db_source_header(restc->http_ctx);
    mtev_http_response_not_found(restc->http_ctx, "application/json");
    mtev_http_response_end(restc->http_ctx);
  }
  noit_check_t *check = noit_poller_lookup(checkid);
  if(!check) {
    noit_check_set_db_source_header(restc->http_ctx);
    mtev_http_response_not_found(restc->http_ctx, "application/json");
    mtev_http_response_end(restc->http_ctx);
    return 0;
  }

  mtev_cluster_node_t *owner = NULL;
  if(!noit_should_run_check(check, &owner) && owner) {
    const char *cn = mtev_cluster_node_get_cn(owner);
    char url[1024];
    struct sockaddr *addr;
    socklen_t addrlen;
    unsigned short port;
    switch(mtev_cluster_node_get_addr(owner, &addr, &addrlen)) {
      case AF_INET:
        port = ntohs(((struct sockaddr_in *)addr)->sin_port);
        break;
      case AF_INET6:
        port = ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
        break;
      default:
        port = 43191;
    }
    char uuid_str[UUID_STR_LEN+1];
    mtev_uuid_unparse_lower(checkid, uuid_str);
    snprintf(url, sizeof(url), "https://%s:%u/checks/owner/%s",
             cn, port, uuid_str);
    mtev_http_response_header_set(restc->http_ctx, "Location", url);
    noit_check_set_db_source_header(restc->http_ctx);
    mtev_http_response_standard(restc->http_ctx, 302, "NOT IT", "application/json");
    mtev_http_response_end(restc->http_ctx);
    return 0;
  }
  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_standard(restc->http_ctx, 204, "OK", "application/json");
  mtev_http_response_end(restc->http_ctx);
  noit_check_deref(check);
  return 0;
}

int
rest_show_check_json(mtev_http_rest_closure_t *restc,
                     uuid_t checkid) {
  noit_check_t *check;
  struct json_object *doc;
  const char *jsonstr;
  check = noit_poller_lookup(checkid);
  if(!check) {
    noit_check_set_db_source_header(restc->http_ctx);
    mtev_http_response_not_found(restc->http_ctx, "application/json");
    mtev_http_response_end(restc->http_ctx);
    return 0;
  }

  mtev_http_session_ctx *ctx = restc->http_ctx;
  mtev_http_request *req = mtev_http_session_request(ctx);
  const char *redirect_s = mtev_http_request_querystring(req, "redirect");
  mtev_boolean redirect = !redirect_s || strcmp(redirect_s, "0");
  const char *metrics = mtev_http_request_querystring(req, "metrics");
  int full = 1;
  if(metrics && strtoll(metrics, NULL, 10) == 0) full = -1;
  doc = noit_check_state_as_json(check, full);

  mtev_cluster_node_t *owner = NULL;
  if(!noit_should_run_check(check, &owner) && owner) {
    const char *cn = mtev_cluster_node_get_cn(owner);
    char url[1024];
    struct sockaddr *addr;
    socklen_t addrlen;
    unsigned short port;
    switch(mtev_cluster_node_get_addr(owner, &addr, &addrlen)) {
      case AF_INET:
        port = ntohs(((struct sockaddr_in *)addr)->sin_port);
        break;
      case AF_INET6:
        port = ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
        break;
      default:
        port = 43191;
    }
    char uuid_str[UUID_STR_LEN+1];
    mtev_uuid_unparse_lower(checkid, uuid_str);
    snprintf(url, sizeof(url), "https://%s:%u/checks/show/%s.json",
             cn, port, uuid_str);
    mtev_http_response_header_set(restc->http_ctx, "Location", url);
    noit_check_set_db_source_header(restc->http_ctx);
    mtev_http_response_standard(ctx, redirect ? 302 : 200, "NOT IT", "application/json");
  }
  else {
    noit_check_set_db_source_header(restc->http_ctx);
    mtev_http_response_ok(restc->http_ctx, "application/json");
  }
  jsonstr = json_object_to_json_string(doc);
  mtev_http_response_append(restc->http_ctx, jsonstr, strlen(jsonstr));
  mtev_http_response_append(restc->http_ctx, "\n", 1);
  json_object_put(doc);
  mtev_http_response_end(restc->http_ctx);
  noit_check_deref(check);
  return 0;
}
static int
rest_show_check(mtev_http_rest_closure_t *restc,
                int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL;
  xmlDocPtr doc = NULL;
  xmlNodePtr node, root, attr, config, state, tmp, anode;
  mtev_conf_section_t section;
  uuid_t checkid;
  noit_check_t *check = NULL;
  char xpath[1024], *uuid_conf = NULL, *module = NULL, *value = NULL;
  int rv, mod, mod_cnt, cnt, error_code = 500;
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  const char *k;
  int klen;
  void *data;
  mtev_hash_table *configh;

  if(noit_check_get_lmdb_instance()) {
    return noit_check_lmdb_show_check(restc, npats, pats);
  }
  NCINIT_RD;

  if(npats != 2 && npats != 3) goto error;

  rv = noit_check_xpath(xpath, sizeof(xpath), pats[0], pats[1]);
  if(rv == 0) goto not_found;
  if(rv < 0) goto error;

  NCLOCK;
  mtev_conf_xml_xpath(NULL, &xpath_ctxt);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET ||
     xmlXPathNodeSetIsEmpty(pobj->nodesetval)) goto not_found;
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  if(cnt != 1) goto error;

  node = xmlXPathNodeSetItem(pobj->nodesetval, 0);
  section = mtev_conf_section_from_xmlnodeptr(node);
  uuid_conf = (char *)xmlGetProp(node, (xmlChar *)"uuid");
  if(!uuid_conf || mtev_uuid_parse(uuid_conf, checkid)) goto error;

  if(npats == 3 && !strcmp(pats[2], ".json")) {
    if(uuid_conf) xmlFree(uuid_conf);
    if(pobj) xmlXPathFreeObject(pobj);
    NCUNLOCK;
    return rest_show_check_json(restc, checkid);
  }

  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlNewDocNode(doc, NULL, (xmlChar *)"check", NULL);
  xmlDocSetRootElement(doc, root);

  mtev_http_request *req = mtev_http_session_request(ctx);
  const char *redirect_s = mtev_http_request_querystring(req, "redirect");
  mtev_boolean redirect = !redirect_s || strcmp(redirect_s, "0");
  const char *metrics = mtev_http_request_querystring(req, "metrics");

#define MYATTR(section,a,n,b) _mtev_conf_get_string(section, &(n), "@" #a, &(b))
#define INHERIT(section,a,n,b) \
  _mtev_conf_get_string(section, &(n), "ancestor-or-self::node()/@" #a, &(b))
#define SHOW_ATTR(parent, node, section, a) do { \
  char *_value = NULL; \
  INHERIT(section, a, anode, _value); \
  if(_value != NULL) { \
    int clen, plen;\
    char *_cpath, *_apath; \
    xmlNodePtr child; \
    _cpath = node ? (char *)xmlGetNodePath(node) : strdup(""); \
    _apath = anode ? (char *)xmlGetNodePath(anode) : strdup(""); \
    clen = strlen(_cpath); \
    plen = strlen("/noit/checks"); \
    child = xmlNewNode(NULL, (xmlChar *)#a); \
    xmlNodeAddContent(child, (xmlChar *)_value); \
    if(!strncmp(_cpath, _apath, clen) && _apath[clen] == '/') { \
    } \
    else { \
      xmlSetProp(child, (xmlChar *)"inherited", (xmlChar *)_apath+plen); \
    } \
    xmlAddChild(parent, child); \
    free(_cpath); \
    free(_apath); \
    free(_value); \
  } \
} while(0)

  attr = xmlNewNode(NULL, (xmlChar *)"attributes");
  xmlAddChild(root, attr);

  SHOW_ATTR(attr,node,section,uuid);
  SHOW_ATTR(attr,node,section,seq);

  /* Name is odd, it falls back transparently to module */
  if(!INHERIT(section, module, tmp, module)) module = NULL;
  xmlAddChild(attr, (tmp = xmlNewNode(NULL, (xmlChar *)"name")));
  if(MYATTR(section, name, anode, value))
    xmlNodeAddContent(tmp, (xmlChar *)value);
  else if(module)
    xmlNodeAddContent(tmp, (xmlChar *)module);
  if(value) free(value);
  if(module) free(module);

  SHOW_ATTR(attr,node,section,module);
  SHOW_ATTR(attr,node,section,target);
  SHOW_ATTR(attr,node,section,resolve_rtype);
  SHOW_ATTR(attr,node,section,period);
  SHOW_ATTR(attr,node,section,timeout);
  SHOW_ATTR(attr,node,section,oncheck);
  SHOW_ATTR(attr,node,section,filterset);
  SHOW_ATTR(attr,node,section,disable);

  /* Add the config */
  config = xmlNewNode(NULL, (xmlChar *)"config");
  configh = mtev_conf_get_hash(section, "config");
  while(mtev_hash_next(configh, &iter, &k, &klen, &data))
    NODE_CONTENT(config, k, data);
  mtev_hash_destroy(configh, free, free);
  free(configh);

  mod_cnt = noit_check_registered_module_cnt();
  for(mod=0; mod<mod_cnt; mod++) {
    xmlNsPtr ns;
    const char *nsname;
    char buff[256];

    nsname = noit_check_registered_module(mod);
 
    snprintf(buff, sizeof(buff), "noit://module/%s", nsname);
    ns = xmlSearchNs(root->doc, root, (xmlChar *)nsname);
    if(!ns) ns = xmlNewNs(root, (xmlChar *)buff, (xmlChar *)nsname);
    if(ns) {
      configh = mtev_conf_get_namespaced_hash(section, "config", nsname);
      if(configh) {
        memset(&iter, 0, sizeof(iter));
        while(mtev_hash_next(configh, &iter, &k, &klen, &data)) {
          NS_NODE_CONTENT(config, ns, "value", data,
            xmlSetProp(tmp, (xmlChar *)"name", (xmlChar *)k);
          );
        }
        mtev_hash_destroy(configh, free, free);
        free(configh);
      }
    }
  }
  xmlAddChild(root, config);

  /* Add the state */
  check = noit_poller_lookup(checkid);
  if(!check) {
    state = xmlNewNode(NULL, (xmlChar *)"state");
    xmlSetProp(state, (xmlChar *)"error", (xmlChar *)"true");
  }
  else {
    int full = 1;
    if(metrics && strtoll(metrics, NULL, 10) == 0) full = -1;
    state = noit_check_state_as_xml(check, full);
  }
  xmlAddChild(root, state);

  mtev_cluster_node_t *owner = NULL;
  if(check && !noit_should_run_check(check, &owner) && owner) {
    const char *cn = mtev_cluster_node_get_cn(owner);
    char url[1024];
    struct sockaddr *addr;
    socklen_t addrlen;
    unsigned short port;
    switch(mtev_cluster_node_get_addr(owner, &addr, &addrlen)) {
      case AF_INET:
        port = ntohs(((struct sockaddr_in *)addr)->sin_port);
        break;
      case AF_INET6:
        port = ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
        break;
      default:
        port = 43191;
    }
    char uuid_str[UUID_STR_LEN+1];
    mtev_uuid_unparse_lower(checkid, uuid_str);
    snprintf(url, sizeof(url), "https://%s:%u/checks/show/%s",
             cn, port, uuid_str);
    mtev_http_response_header_set(restc->http_ctx, "Location", url);
    noit_check_set_db_source_header(restc->http_ctx);
    mtev_http_response_standard(ctx, redirect ? 302 : 200, "NOT IT", "text/xml");
  }
  else {
    noit_check_set_db_source_header(restc->http_ctx);
    mtev_http_response_ok(ctx, "text/xml");
  }
  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);
  goto cleanup;

 not_found:
  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_not_found(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 error:
  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_standard(ctx, error_code, "ERROR", "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if(uuid_conf) xmlFree(uuid_conf);
  if(pobj) xmlXPathFreeObject(pobj);
  if(doc) xmlFreeDoc(doc);
  noit_check_deref(check);
  NCUNLOCK;
  return 0;
}

static void
collect_namespaces(xmlNodePtr node, xmlNsPtr *capture_ns, int cnt, int *idx) {
  int i;
  for(xmlNsPtr ns = node->ns; ns; ns = ns->next) {
    if(*idx < 0 || *idx >= cnt)  {
      *idx = -1;
      return;
    }
    /* This bruteforce is fast as cnt should be very small */
    for(i=0; i<*idx; i++) {
      if(capture_ns[i] == ns) break;
    }
    /* if i == *idx, we didn't find a match, record it */
    if(i == *idx) capture_ns[(*idx)++] = ns;
  }
  if(node->next) collect_namespaces(node->next, capture_ns, cnt, idx);
  if(node->children) collect_namespaces(node->children, capture_ns, cnt, idx);
}

static int
missing_namespaces(xmlNodePtr ctx, xmlNodePtr root) {
  xmlNsPtr namespaces[10];
  int i, nns = 0;
  /* Walk the incoming heirarchy for namespaces */
  collect_namespaces(root, namespaces, 10, &nns);
  /* If there were too many to fit (>10), fail. */
  if(nns < 0) return 1;
  /* If any is missing, say so. */
  for(i=0; i<nns; i++) {
    if(!xmlSearchNs(ctx->doc, ctx, namespaces[i]->prefix)) return 1;
  }
  /* All namespaces in root are in ctx */
  return 0;
}

int
noit_validate_check_rest_post(xmlDocPtr doc, xmlNodePtr *a, xmlNodePtr *c,
                              const char **error) {
  mtev_conf_section_t toplevel;
  xmlNodePtr root, tl, an, master_config_root;
  int name=0, module=0, target=0, period=0, timeout=0, filterset=0;
  *a = *c = NULL;
  root = xmlDocGetRootElement(doc);
  /* Make sure any present namespaces are in the master document already */
  toplevel = mtev_conf_get_section_read(MTEV_CONF_ROOT, "/*");
  master_config_root = mtev_conf_section_to_xmlnodeptr(toplevel);
  if(!master_config_root) {
    *error = "no document";
    goto out;
  }
  if(missing_namespaces(master_config_root, root)) {
    *error = "invalid namespace provided";
    goto out;
  }
      
  if(strcmp((char *)root->name, "check")) return 0;
  for(tl = root->children; tl; tl = tl->next) {
    if(tl->type != XML_ELEMENT_NODE) continue;
    if(!strcmp((char *)tl->name, "attributes")) {
      *a = tl;
      for(an = tl->children; an; an = an->next) {
        if(an->type != XML_ELEMENT_NODE) continue;
#define CHECK_N_SET(a) if(!strcmp((char *)an->name, #a))
        CHECK_N_SET(name) {
          xmlChar *tmp;
          pcre *valid_name = mtev_conf_get_valid_name_checker();
          int ovector[30], valid;
          tmp = xmlNodeGetContent(an);
          valid = (pcre_exec(valid_name, NULL,
                             (char *)tmp, strlen((char *)tmp), 0, 0,
                             ovector, sizeof(ovector)/sizeof(*ovector)) > 0);
          xmlFree(tmp);
          if(!valid) { *error = "invalid name"; goto out; }
          name = 1;
        }
        else CHECK_N_SET(module) module = 1; /* This is validated by called */
        else CHECK_N_SET(target) {
          mtev_boolean should_resolve;
          int valid;
          xmlChar *tmp;
          tmp = xmlNodeGetContent(an);
          valid = noit_check_is_valid_target((char *)tmp);
          xmlFree(tmp);
          if(noit_check_should_resolve_targets(&should_resolve) &&
             should_resolve == mtev_false &&
             !valid) {
            *error = "invalid target";
            goto out;
          }
          target = 1;
        }
        else CHECK_N_SET(resolve_rtype) {
          xmlChar *tmp;
          mtev_boolean invalid;
          tmp = xmlNodeGetContent(an);
          invalid = strcmp((char *)tmp, PREFER_IPV4) &&
                    strcmp((char *)tmp, PREFER_IPV6) &&
                    strcmp((char *)tmp, FORCE_IPV4) &&
                    strcmp((char *)tmp, FORCE_IPV6);
          xmlFree(tmp);
          if(invalid) {
            *error = "invalid reslove_rtype";
            goto out;
          }
        }
        else CHECK_N_SET(period) {
					/* period is enfored in noit_check */
          period = 1;
        }
        else CHECK_N_SET(timeout) {
					/* timeout is enforces in noit_check */
          timeout = 1;
        }
        else CHECK_N_SET(filterset) {
          int valid;
          xmlChar *tmp;
          tmp = xmlNodeGetContent(an);
          valid = noit_filter_exists((char *)tmp);
          xmlFree(tmp);
          if(!valid) {
            *error = "filterset does not exist";
            goto out;
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
          if(!valid) { *error = "bad disable parameter"; goto out; }
          target = 1;
        }
        else CHECK_N_SET(seq) {}
        else {
          mtevL(mtev_debug, "Unknown check set option: %s\n", an->name);
          *error = "unknown option specified";
          goto out;
        }
      }
    }
    else if(!strcmp((char *)tl->name, "config")) {
      *c = tl;
      /* Noop, anything goes */
    }
    else goto out;
  }
  if(name && module && target && period && timeout && filterset) {
    mtev_conf_release_section_read(toplevel);
    return 1;
  }
  *error = "insufficient information";
 out:
  mtev_conf_release_section_read(toplevel);
  return 0;
}
static void
configure_xml_check(xmlNodePtr parent, xmlNodePtr check, xmlNodePtr a, xmlNodePtr c, int64_t *seq) {
  xmlNodePtr n, config, oldconfig;
  if(seq) *seq = 0;
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
    ATTR2PROP(seq);
    ATTR2PROP(transient_min_period);
    ATTR2PROP(transient_period_granularity);
    xmlUnsetProp(check, (xmlChar*)"deleted");
    if(seq && !strcmp((char *)n->name, "seq")) {
      xmlChar *v = xmlNodeGetContent(n);
      *seq = strtoll((const char *)v, NULL, 10);
      xmlFree(v);
    }
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
      xmlNsPtr targetns = NULL;
      xmlChar *v = xmlNodeGetContent(n);
      if(n->ns) {
        targetns = xmlSearchNs(parent->doc, xmlDocGetRootElement(parent->doc),
                               n->ns->prefix);
        if(!targetns) {
          targetns = xmlSearchNs(config->doc, config, n->ns->prefix);
          if(!targetns) targetns = xmlNewNs(config, n->ns->href, n->ns->prefix);
          mtevL(noit_debug, "Setting a config value in a new namespace (%p)\n", targetns);
        }
        else {
          mtevL(noit_debug,"Setting a config value in a namespace (%p)\n", targetns);
        }
      }
      xmlNodePtr co = xmlNewNode(targetns, n->name);
      if(n->ns && !strcmp((char *)n->name, "value")) {
        xmlChar *name = xmlGetProp(n, (xmlChar *)"name");
        if(name) xmlSetProp(co, (xmlChar *)"name", name);
      }
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
  CONF_DIRTY(mtev_conf_section_from_xmlnodeptr(config));
}
static xmlNodePtr
make_conf_path(char *path) {
  mtev_conf_section_t section;
  xmlNodePtr start, tmp;
  char fullpath[1024], *tok, *brk;
  if(!path || strlen(path) < 1) return NULL;
  snprintf(fullpath, sizeof(fullpath), "%s", path+1);
  section = mtev_conf_get_section_write(MTEV_CONF_ROOT, "/noit/checks");
  start = mtev_conf_section_to_xmlnodeptr(section);
  if(mtev_conf_section_is_empty(section)) {
    mtev_conf_release_section_write(section);
    return NULL;
  }
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
      CONF_DIRTY(mtev_conf_section_from_xmlnodeptr(tmp));
    }
    start = tmp;
  }
  mtev_conf_release_section_write(section);
  return start;
}

static int
rest_delete_check(mtev_http_rest_closure_t *restc,
                  int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL;
  xmlNodePtr node;
  uuid_t checkid;
  noit_check_t *check;
  const char *error;
  char xpath[1024], *uuid_conf = NULL;
  int rv, cnt, error_code = 500;
  mtev_boolean exists = mtev_false;

  if(noit_check_get_lmdb_instance()) {
    return noit_check_lmdb_delete_check(restc, npats, pats);
  }

  NCINIT_WR;

  if(npats != 2) goto error;

  if(mtev_uuid_parse(pats[1], checkid)) goto error;
  check = noit_poller_lookup(checkid);
  if(check)
    exists = mtev_true;

  rv = noit_check_xpath(xpath, sizeof(xpath), pats[0], pats[1]);
  if(rv == 0) FAILC(400, "uuid not valid");
  if(rv < 0) FAILC(403, "Tricky McTrickster... No");

  NCLOCK;
  mtev_conf_xml_xpath(NULL, &xpath_ctxt);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET ||
     xmlXPathNodeSetIsEmpty(pobj->nodesetval)) {
    if(exists) FAILC(403, "uuid not yours");
    goto not_found;
  }
  cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
  if(cnt != 1) FAIL("internal error, |checkid| > 1");
  node = xmlXPathNodeSetItem(pobj->nodesetval, 0);
  uuid_conf = (char *)xmlGetProp(node, (xmlChar *)"uuid");
  if(!uuid_conf || strcasecmp(uuid_conf, pats[1]))
    FAIL("internal error uuid");

  /* delete this here */
  mtev_boolean just_mark = mtev_false;
  if(check)
    if(!noit_poller_deschedule(check->checkid, mtev_true, mtev_false))
      just_mark = mtev_true;
  if(just_mark) {
    int64_t newseq = noit_conf_check_bump_seq(node);
    xmlSetProp(node, (xmlChar *)"deleted", (xmlChar *)"deleted");
    if(check) {
      check->config_seq = newseq;
      noit_cluster_mark_check_changed(check, NULL);
    }
    CONF_DIRTY(mtev_conf_section_from_xmlnodeptr(node));
  } else {
    CONF_REMOVE(mtev_conf_section_from_xmlnodeptr(node));
    xmlUnlinkNode(node);
    xmlFreeNode(node);
  }
  mtev_conf_mark_changed();
  if(mtev_conf_write_file(NULL) != 0)
    mtevL(noit_error, "local config write failed\n");
  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_ok(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 not_found:
  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_not_found(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 error:
  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_standard(ctx, error_code, "ERROR", "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if(uuid_conf) xmlFree(uuid_conf);
  if(pobj) xmlXPathFreeObject(pobj);
  (void)error;
  NCUNLOCK;
  return 0;
}

static int
rest_set_check(mtev_http_rest_closure_t *restc,
               int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  xmlXPathObjectPtr pobj = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL;
  xmlDocPtr doc = NULL, indoc = NULL;
  xmlNodePtr node, root, attr, config, parent;
  uuid_t checkid;
  noit_check_t *check;
  char xpath[1024], *uuid_conf = NULL;
  int rv, cnt, error_code = 500, complete = 0, mask = 0;
  const char *error = "internal error";
  mtev_boolean exists = mtev_false;

  if(noit_check_get_lmdb_instance()) {
    return noit_check_lmdb_set_check(restc, npats, pats, set_check_jobq);
  }
  NCINIT_WR;

  if(npats != 2) goto error;

  indoc = rest_get_xml_upload(restc, &mask, &complete);
  if(!complete) return mask;
  if(indoc == NULL) FAILC(400, "xml parse error");
  if(!noit_validate_check_rest_post(indoc, &attr, &config, &error)) goto error;

  if(mtev_uuid_parse(pats[1], checkid)) goto error;

  check = noit_poller_lookup(checkid);
  if(check)
    exists = mtev_true;

  rv = noit_check_xpath(xpath, sizeof(xpath), pats[0], pats[1]);
  if(rv == 0) FAILC(403, "uuid not valid");
  if(rv < 0) FAILC(403, "Tricky McTrickster... No");

  NCLOCK;
  mtev_conf_xml_xpath(NULL, &xpath_ctxt);
  pobj = xmlXPathEval((xmlChar *)xpath, xpath_ctxt);
  if(!pobj || pobj->type != XPATH_NODESET ||
     xmlXPathNodeSetIsEmpty(pobj->nodesetval)) {
    if(exists) FAILC(403, "uuid not yours");
    else {
      int64_t seq;
      uint64_t old_seq = 0;
      char *target = NULL, *name = NULL, *module = NULL;
      noit_module_t *m = NULL;
      noit_check_t *check = NULL;
      xmlNodePtr newcheck = NULL;
      /* make sure this isn't a dup */
      rest_check_get_attrs(attr, &target, &name, &module);
      exists = (!target || (check = noit_poller_lookup_by_name(target, name)) != NULL);
      if(check) {
        old_seq = check->config_seq;
        noit_check_deref(check);
      }
      if(module) m = noit_module_lookup(module);
      rest_check_free_attrs(target, name, module);
      if(exists) FAILC(409, "target`name already registered");
      if(!m) FAILC(412, "module does not exist");
      /* create a check here */
      newcheck = xmlNewNode(NULL, (xmlChar *)"check");
      xmlSetProp(newcheck, (xmlChar *)"uuid", (xmlChar *)pats[1]);
      parent = make_conf_path(pats[0]);
      if(!parent) FAIL("invalid path");
      configure_xml_check(parent, newcheck, attr, config, &seq);
      if(old_seq >= seq && seq != 0) FAILC(409, "sequencing error");
      xmlAddChild(parent, newcheck);
      CONF_DIRTY(mtev_conf_section_from_xmlnodeptr(newcheck));
    }
  }
  if(exists) {
    int64_t seq;
    uint64_t old_seq = 0;
    int module_change;
    char *target = NULL, *name = NULL, *module = NULL;
    noit_check_t *ocheck;
    if(!check)
      FAIL("internal check error");

    cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
    if(cnt != 1) FAIL("internal error, |checkid| > 1");
    node = xmlXPathNodeSetItem(pobj->nodesetval, 0);
    uuid_conf = (char *)xmlGetProp(node, (xmlChar *)"uuid");
    if(!uuid_conf || strcasecmp(uuid_conf, pats[1]))
      FAIL("internal error uuid");
    xmlFree(uuid_conf);
    uuid_conf = NULL;

    /* make sure this isn't a dup */
    rest_check_get_attrs(attr, &target, &name, &module);

    ocheck = noit_poller_lookup_by_name(target, name);
    noit_check_deref(ocheck);
    module_change = strcmp(check->module, module);
    rest_check_free_attrs(target, name, module);
    if(ocheck && ocheck != check) FAILC(409, "new target`name would collide");
    if(module_change) FAILC(400, "cannot change module");
    parent = make_conf_path(pats[0]);
    if(!parent) FAIL("invalid path");
    configure_xml_check(parent, node, attr, config, &seq);
    if(check) old_seq = check->config_seq;
    if(old_seq >= seq && seq != 0) FAILC(409, "sequencing error");
    xmlUnlinkNode(node);
    xmlAddChild(parent, node);
    CONF_DIRTY(mtev_conf_section_from_xmlnodeptr(node));
  }

  mtev_conf_mark_changed();
  if(mtev_conf_write_file(NULL) != 0)
    mtevL(noit_error, "local config write failed\n");
  noit_poller_reload(xpath);
  if(restc->call_closure_free) restc->call_closure_free(restc->call_closure);
  restc->call_closure_free = NULL;
  restc->call_closure = NULL;
  if(pobj) xmlXPathFreeObject(pobj);
  restc->fastpath = rest_show_check;
  NCUNLOCK;
  return restc->fastpath(restc, restc->nparams, restc->params);

 error:
  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_standard(ctx, error_code, "ERROR", "text/xml");
  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlNewDocNode(doc, NULL, (xmlChar *)"error", NULL);
  xmlDocSetRootElement(doc, root);
  xmlNodeAddContent(root, (xmlChar *)error);
  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if(uuid_conf) xmlFree(uuid_conf);
  if(pobj) xmlXPathFreeObject(pobj);
  if(doc) xmlFreeDoc(doc);
  NCUNLOCK;
  noit_check_deref(check);
  return 0;
}

static int
rest_show_check_updates(mtev_http_rest_closure_t *restc,
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
    noit_check_set_db_source_header(restc->http_ctx);
    mtev_http_response_server_error(ctx, "text/xml");
    mtev_http_response_end(ctx);
    return 0;
  }

  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlNewNode(NULL, (xmlChar *)"checks");
  xmlDocSetRootElement(doc, root);
  noit_cluster_xml_check_changes(peerid, restc->remote_cn, prev, end, root);
  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_ok(ctx, "text/xml");
  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);

  if(doc) xmlFreeDoc(doc);

  return 0;
}

void
rest_check_get_attrs(xmlNodePtr attr, char **target, char **name, char **module) {
  xmlNodePtr a = NULL;

  *target = NULL;
  *name = NULL;
  *module = NULL;

  for(a = attr->children; a; a = a->next) {
    if(!strcmp((char *)a->name, "target")) {
      *target = (char *)xmlNodeGetContent(a);
    }
    else if(!strcmp((char *)a->name, "name")) {
      *name = (char *)xmlNodeGetContent(a);
    }
    else if(!strcmp((char *)a->name, "module")) {
      *module = (char *)xmlNodeGetContent(a);
    }
  }
}

void
rest_check_free_attrs(char *target, char *name, char *module) {
  if (target) {
    xmlFree(target);
  }
  if (name) {
    xmlFree(name);
  }
  if (module) {
    xmlFree(module);
  }
}

void
noit_check_rest_init() {
  set_check_jobq = eventer_jobq_retrieve("set_check");
  mtevAssert(set_check_jobq);
  mtevAssert(mtev_http_rest_register_auth(
    "GET", "/", "^config(/.*)?$",
    noit_rest_show_config, mtev_http_rest_client_cert_auth
  ) == 0);
  mtevAssert(mtev_http_rest_register_auth(
    "GET", "/checks/", "^updates$",
    rest_show_check_updates, mtev_http_rest_client_cert_auth
  ) == 0);
  mtevAssert(mtev_http_rest_register_auth(
    "GET", "/checks/", "^owner/(" UUID_REGEX ")$",
    rest_show_check_owner, mtev_http_rest_client_cert_auth
  ) == 0);
  mtevAssert(mtev_http_rest_register_auth(
    "GET", "/checks/", "^show(\\.json)?$",
    rest_show_checks, mtev_http_rest_client_cert_auth
  ) == 0);
  mtevAssert(mtev_http_rest_register_auth(
    "GET", "/checks/", "^show(/.*)(?<=/)(" UUID_REGEX ")(\\.json)?$",
    rest_show_check, mtev_http_rest_client_cert_auth
  ) == 0);
  mtevAssert(mtev_http_rest_register_auth(
    "PUT", "/checks/", "^set(/.*)(?<=/)(" UUID_REGEX ")$",
    rest_set_check, mtev_http_rest_client_cert_auth
  ) == 0);
  mtevAssert(mtev_http_rest_register_auth(
    "DELETE", "/checks/", "^delete(/.*)(?<=/)(" UUID_REGEX ")$",
    rest_delete_check, mtev_http_rest_client_cert_auth
  ) == 0);
}

