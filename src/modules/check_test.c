/*
 * Copyright (c) 2011-2015, Circonus, Inc. All rights reserved.
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
 *     * Neither the name Circonus, Inc. nor the names
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

#include <mtev_rest.h>
#include <mtev_log.h>
#include <mtev_memory.h>

#include "noit_mtev_bridge.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_rest.h"

#include "check_test.xmlh"

static void check_test_schedule_sweeper();
static mtev_log_stream_t nlerr = NULL;
static mtev_log_stream_t nldeb = NULL;

struct check_test_closure {
  noit_check_t *check;
  mtev_http_rest_closure_t *restc;
  enum { WANTS_XML = 0, WANTS_JSON } output;
};

static mtev_skiplist in_progress;
static eventer_t sweeper_event = NULL;
static int default_sweep_interval = 10; /* 10ms seems good */

static int
check_complete_heap_key(const void *av, const void *bv) {
  const mtev_http_rest_closure_t *restc = av;
  const struct check_test_closure *b = bv;
  if(restc < b->restc) return -1;
  if(restc == b->restc) return 0;
  return 1;
}
static int
check_complete_heap(const void *av, const void *bv) {
  const struct check_test_closure *a = av;
  const struct check_test_closure *b = bv;
  if(a->restc < b->restc) return -1;
  if(a->restc == b->restc) return 0;
  return 1;
}

static int
check_test_onload(mtev_image_t *self) {
  nlerr = mtev_log_stream_find("error/checktest");
  nldeb = mtev_log_stream_find("debug/checktest");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;
  mtev_skiplist_init(&in_progress);
  mtev_skiplist_set_compare(&in_progress, check_complete_heap,
                            check_complete_heap_key);
  return 0;
}

static int
check_test_config(mtev_dso_generic_t *self, mtev_hash_table *o) {
  const char *str;
  int new_interval = 0;
  if(mtev_hash_retr_str(o, "sweep_interval", strlen("sweep_interval"),
                        &str))
    new_interval = atoi(str);
  if(new_interval > 0) default_sweep_interval = new_interval;
  return 0;
}

noit_check_t *
noit_fire_check(xmlNodePtr attr, xmlNodePtr config, const char **error) {
  char *target = NULL, *name = NULL, *module = NULL, *filterset = NULL;
  char *resolve_rtype = NULL;
  int timeout = 0, flags = NP_TRANSIENT, i, mod_cnt;
  noit_module_t *m = NULL;
  noit_check_t *c = NULL;
  xmlNodePtr a, co;
  mtev_hash_table *conf_hash = NULL;
  mtev_hash_table **moptions = NULL;

  for(a = attr->children; a; a = a->next) {
    if(!strcmp((char *)a->name, "target"))
      target = (char *)xmlNodeGetContent(a);
    else if(!strcmp((char *)a->name, "name"))
      name = (char *)xmlNodeGetContent(a);
    else if(!strcmp((char *)a->name, "module"))
      module = (char *)xmlNodeGetContent(a);
    else if(!strcmp((char *)a->name, "filterset"))
      filterset = (char *)xmlNodeGetContent(a);
    else if(!strcmp((char *)a->name, "timeout")) {
      char *timeout_str = (char *)xmlNodeGetContent(a);
      timeout = atoi(timeout_str);
      free(timeout_str);
    } else if(!strcmp((char *)a->name, "resolve_rtype")) 
      resolve_rtype = (char *)xmlNodeGetContent(a);
  }
  if(!noit_check_validate_target(target)) {
    *error = "malformed target";
    goto error;
  }
  if(!noit_check_validate_name(name)) {
    *error = "malformed name";
    goto error;
  }
  if(module) m = noit_module_lookup(module);
  if(!m) {
    *error = "cannot find requested module";
    goto error;
  }
  conf_hash = calloc(1, sizeof(*conf_hash));
  if(config) {
    for(co = config->children; co; co = co->next) {
      char *name, *val;
      xmlChar *tmp_val;
      name = strdup((char *)co->name);
      tmp_val = xmlNodeGetContent(co);
      val = strdup(tmp_val ? (char *)tmp_val : "");
      mtev_hash_replace(conf_hash, name, strlen(name), val, free, free);
      xmlFree(tmp_val);
    }
  }
  mod_cnt = noit_check_registered_module_cnt();
  if(mod_cnt > 0) {
    moptions = alloca(mod_cnt * sizeof(*moptions));
    memset(moptions, 0, mod_cnt * sizeof(*moptions));
    for(i=0; i<mod_cnt; i++) {
      const char *name;
      mtev_conf_section_t checks;
      name = noit_check_registered_module(i);
      checks = mtev_conf_get_section(NULL, "/noit/checks");
      if(name) moptions[i] = mtev_conf_get_namespaced_hash(checks, "config", name);
    }
  }
  if(!m->initiate_check) {
    *error = "that module cannot run checks";
    goto error;
  }
  flags |= noit_calc_rtype_flag(resolve_rtype);
  c = mtev_memory_safe_calloc(1, sizeof(*c));
  c->module = strdup(module);
  c->flags = NP_TRANSIENT;
  noit_check_update(c, target, name, filterset,
                    conf_hash, moptions, 0, timeout, NULL, 0, flags);
  uuid_generate(c->checkid);
  c->flags |= NP_DISABLED; /* this is hack to know we haven't run it yet */
  if(NOIT_CHECK_SHOULD_RESOLVE(c))
    noit_check_resolve(c);

 error:
  if(conf_hash) {
    mtev_hash_destroy(conf_hash, free, free);
    free(conf_hash);
  }
  if(moptions) {
    for(i=0; i<mod_cnt; i++) {
      if(moptions[i]) {
        mtev_hash_destroy(moptions[i], free, free);
        free(moptions[i]);
      }
    }
  }
  if(target) xmlFree(target);
  if(name) xmlFree(name);
  if(module) xmlFree(module);
  if(filterset) xmlFree(filterset);
  if(resolve_rtype) xmlFree(resolve_rtype);
  return c;
}

static void
rest_test_check_result(struct check_test_closure *cl) {
  mtev_http_session_ctx *ctx = cl->restc->http_ctx;

  mtevL(nlerr, "Flushing check test result\n");

  if(cl->restc->call_closure_free)
    cl->restc->call_closure_free(cl->restc->call_closure);
  cl->restc->call_closure_free = NULL;
  cl->restc->call_closure = NULL;
  cl->restc->fastpath = NULL;

  if(ctx) {
    eventer_t conne;

    if(cl->output == WANTS_JSON) {
      struct json_object *doc;
      const char *jsonstr;

      doc = noit_check_state_as_json(cl->check, 1);
      mtev_http_response_ok(ctx, "application/json");
      jsonstr = json_object_to_json_string(doc);
      mtev_http_response_append(ctx, jsonstr, strlen(jsonstr));
      mtev_http_response_append(ctx, "\n", 1);
      json_object_put(doc);
    } else {
      xmlDocPtr doc = NULL;
      xmlNodePtr root, state;
  
      doc = xmlNewDoc((xmlChar *)"1.0");
      root = xmlNewDocNode(doc, NULL, (xmlChar *)"check", NULL);
      xmlDocSetRootElement(doc, root);
      state = noit_check_state_as_xml(cl->check, 1);
      xmlAddChild(root, state);
      mtev_http_response_ok(ctx, "text/xml");
      mtev_http_response_xml(ctx, doc);
      xmlFreeDoc(doc);
    }
    mtev_http_response_end(ctx);
  
    conne = mtev_http_connection_event(mtev_http_session_connection(ctx));
    if(conne) {
      // The event already exists, why re-add it?  Did we want to update it?
      //eventer_add(conne);
      eventer_trigger(conne, EVENTER_READ | EVENTER_WRITE);
    }
  }

  noit_poller_free_check(cl->check);
  free(cl);
}

static int
check_test_sweeper(eventer_t e, int mask, void *closure,
                   struct timeval *now) {
  int left = 0;
  mtev_skiplist_node *iter = NULL;
  sweeper_event = NULL;
  iter = mtev_skiplist_getlist(&in_progress);
  while(iter) {
    struct check_test_closure *cl = iter->data;
    /* advance here, we might delete */
    mtev_skiplist_next(&in_progress,&iter);
    if(NOIT_CHECK_DISABLED(cl->check)) {
      if(NOIT_CHECK_SHOULD_RESOLVE(cl->check))
        noit_check_resolve(cl->check);
      if(NOIT_CHECK_RESOLVED(cl->check)) {
        noit_module_t *m = noit_module_lookup(cl->check->module);
        cl->check->flags &= ~NP_DISABLED;
        if(NOIT_CHECK_SHOULD_RESOLVE(cl->check))
          mtevL(nldeb, "translated to %s\n", cl->check->target_ip);
        if(m) m->initiate_check(m, cl->check, 1, NULL);
      }
      left++;
    }
    else if(NOIT_CHECK_RUNNING(cl->check)) left++;
    else
      mtev_skiplist_remove(&in_progress, cl->restc,
                           (mtev_freefunc_t)rest_test_check_result);
  }

  if(left) check_test_schedule_sweeper();
  return 0;
}

static void
check_test_schedule_sweeper() {
  struct timeval now, diff;
  if(sweeper_event) return;
  sweeper_event = eventer_alloc();
  sweeper_event->mask = EVENTER_TIMER;
  sweeper_event->callback = check_test_sweeper;
  diff.tv_sec = 0L;
  diff.tv_usec = default_sweep_interval * 1000L;
  gettimeofday(&now, NULL);
  add_timeval(now, diff, &sweeper_event->whence);
  eventer_add(sweeper_event);
}

static int
rest_test_check_err(mtev_http_rest_closure_t *restc,
                    int npats, char **pats) {
  mtev_http_response *res = mtev_http_session_response(restc->http_ctx);
  mtev_skiplist_remove(&in_progress, restc,
                       (mtev_freefunc_t)rest_test_check_result);
  if(mtev_http_response_complete(res) != mtev_true) {
    mtev_http_response_standard(restc->http_ctx, 500, "ERROR", "text/html");
    mtev_http_response_end(restc->http_ctx);
  }
  return 0;
}
static int
rest_test_check(mtev_http_rest_closure_t *restc,
                int npats, char **pats) {
  noit_check_t *tcheck;
  mtev_http_session_ctx *ctx = restc->http_ctx;
  int mask, complete = 0;
  int error_code = 500;
  const char *error = "internal error";
  xmlDocPtr indoc, doc = NULL;
  xmlNodePtr attr, config, root;

  indoc = rest_get_xml_upload(restc, &mask, &complete);
  if(!complete) return mask;
  if(indoc == NULL) {
    error = "xml parse error";
    goto error;
  }
  if(!noit_validate_check_rest_post(indoc, &attr, &config, &error))
    goto error;

  tcheck = noit_fire_check(attr, config, &error);
  if(tcheck) {
    /* push the check and the context into a queue to complete on */
    struct check_test_closure *cl;
    cl = calloc(1, sizeof(*cl));
    if(npats == 1 && !strcmp(pats[0], ".json"))
      cl->output = WANTS_JSON;
    cl->check = tcheck;
    cl->restc = restc;
    mtev_skiplist_insert(&in_progress, cl);
    check_test_schedule_sweeper();
    if(restc->call_closure_free)
      restc->call_closure_free(restc->call_closure);
    restc->call_closure_free = NULL;
    restc->call_closure = NULL;
    restc->fastpath = rest_test_check_err;
    eventer_t conne = mtev_http_connection_event_float(mtev_http_session_connection(ctx));
    if(conne) {
      eventer_remove_fd(conne->fd);
    }
    goto cleanup;
  }

 error:
  mtev_http_response_standard(ctx, error_code, "ERROR", "text/xml");
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

static int
check_test_init(mtev_dso_generic_t *self) {
  mtevAssert(mtev_http_rest_register(
    "POST", "/checks/", "^test(\\.xml|\\.json)?$",
    rest_test_check
  ) == 0);
  return 0;
}

mtev_dso_generic_t check_test = {
  {
    .magic = MTEV_GENERIC_MAGIC,
    .version = MTEV_GENERIC_ABI_VERSION,
    .name = "check_test",
    .description = "Check Tester",
    .xml_description = check_test_xml_description,
    .onload = check_test_onload
  },
  check_test_config,
  check_test_init
};

