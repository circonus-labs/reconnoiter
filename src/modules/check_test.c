/*
 * Copyright (c) 2011, Circonus, Inc.
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

#include "noit_defines.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_rest.h"
#include "noit_check_rest.h"
#include "utils/noit_log.h"
#include "check_test.xmlh"

#include <assert.h>

static void check_test_schedule_sweeper();
static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;

struct check_test_closure {
  noit_check_t *check;
  noit_http_rest_closure_t *restc;
};

static noit_skiplist in_progress;
static eventer_t sweeper_event = NULL;
static int default_sweep_interval = 10; /* 10ms seems good */

static int
check_complete_heap_key(const void *av, const void *bv) {
  const noit_http_rest_closure_t *restc = av;
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
check_test_onload(noit_image_t *self) {
  nlerr = noit_log_stream_find("error/checktest");
  nldeb = noit_log_stream_find("debug/checktest");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;
  noit_skiplist_init(&in_progress);
  noit_skiplist_set_compare(&in_progress, check_complete_heap,
                            check_complete_heap_key);
  return 0;
}

static int
check_test_config(noit_module_generic_t *self, noit_hash_table *o) {
  const char *str;
  int new_interval = 0;
  if(noit_hash_retr_str(o, "sweep_interval", strlen("sweep_interval"),
                        &str))
    new_interval = atoi(str);
  if(new_interval > 0) default_sweep_interval = new_interval;
  return 0;
}

noit_check_t *
noit_fire_check(xmlNodePtr attr, xmlNodePtr config, const char **error) {
  char *target = NULL, *name = NULL, *module = NULL, *filterset = NULL;
  char *resolve_rtype = NULL;
  int timeout = 0, flags = NP_TRANSIENT;
  noit_module_t *m;
  noit_check_t *c = NULL;
  xmlNodePtr a, co;
  noit_hash_table *conf_hash = NULL;

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
  m = noit_module_lookup(module);
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
      noit_hash_replace(conf_hash, name, strlen(name), val, free, free);
      xmlFree(tmp_val);
    }
  }
  if(!m->initiate_check) {
    *error = "that module cannot run checks";
    goto error;
  }
  flags |= noit_calc_rtype_flag(resolve_rtype);
  c = calloc(1, sizeof(*c));
  noit_check_update(c, target, name, filterset,
                    conf_hash, 0, timeout, NULL, flags);
  c->module = strdup(module);
  uuid_generate(c->checkid);
  c->flags |= NP_DISABLED; /* this is hack to know we haven't run it yet */
  if(NOIT_CHECK_SHOULD_RESOLVE(c))
    noit_check_resolve(c);

 error:
  if(conf_hash) {
    noit_hash_destroy(conf_hash, free, free);
    free(conf_hash);
  }
  if(target) xmlFree(target);
  if(name) xmlFree(name);
  if(module) xmlFree(module);
  if(filterset) xmlFree(filterset);
  if (resolve_rtype) xmlFree(resolve_rtype);
  return c;
}

static void
rest_test_check_result(struct check_test_closure *cl) {
  xmlDocPtr doc = NULL;
  xmlNodePtr root, state;
  noit_http_session_ctx *ctx = cl->restc->http_ctx;

  noitL(nlerr, "Flushing check test result\n");

  if(cl->restc->call_closure_free)
    cl->restc->call_closure_free(cl->restc->call_closure);
  cl->restc->call_closure_free = NULL;
  cl->restc->call_closure = NULL;
  cl->restc->fastpath = NULL;

  if(ctx) {
    eventer_t conne = noit_http_connection_event(noit_http_session_connection(ctx));

    doc = xmlNewDoc((xmlChar *)"1.0");
    root = xmlNewDocNode(doc, NULL, (xmlChar *)"check", NULL);
    xmlDocSetRootElement(doc, root);
    state = noit_check_state_as_xml(cl->check);
    xmlAddChild(root, state);
    noit_http_response_ok(ctx, "text/xml");
    noit_http_response_xml(ctx, doc);
    noit_http_response_end(ctx);
  
    if(conne) {
      eventer_add(conne);
      eventer_trigger(conne, EVENTER_READ | EVENTER_WRITE);
    }
  }

  noit_poller_free_check(cl->check);
  xmlFreeDoc(doc);
  free(cl);
}

static int
check_test_sweeper(eventer_t e, int mask, void *closure,
                   struct timeval *now) {
  int left = 0;
  noit_skiplist_node *iter = NULL;
  sweeper_event = NULL;
  iter = noit_skiplist_getlist(&in_progress);
  while(iter) {
    struct check_test_closure *cl = iter->data;
    /* advance here, we might delete */
    noit_skiplist_next(&in_progress,&iter);
    if(NOIT_CHECK_DISABLED(cl->check)) {
      if(NOIT_CHECK_SHOULD_RESOLVE(cl->check))
        noit_check_resolve(cl->check);
      if(NOIT_CHECK_RESOLVED(cl->check)) {
        noit_module_t *m = noit_module_lookup(cl->check->module);
        cl->check->flags &= ~NP_DISABLED;
        if(NOIT_CHECK_SHOULD_RESOLVE(cl->check))
          noitL(nldeb, "translated to %s\n", cl->check->target_ip);
        if(m) m->initiate_check(m, cl->check, 1, NULL);
      }
      left++;
    }
    else if(NOIT_CHECK_RUNNING(cl->check)) left++;
    else
      noit_skiplist_remove(&in_progress, cl->restc,
                           (noit_freefunc_t)rest_test_check_result);
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
rest_test_check_err(noit_http_rest_closure_t *restc,
                    int npats, char **pats) {
  noit_http_response *res = noit_http_session_response(restc->http_ctx);
  noit_skiplist_remove(&in_progress, restc,
                       (noit_freefunc_t)rest_test_check_result);
  if(noit_http_response_complete(res) != noit_true) {
    noit_http_response_standard(restc->http_ctx, 500, "ERROR", "text/html");
    noit_http_response_end(restc->http_ctx);
  }
  return 0;
}
static int
rest_test_check(noit_http_rest_closure_t *restc,
                int npats, char **pats) {
  noit_check_t *tcheck;
  noit_http_session_ctx *ctx = restc->http_ctx;
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
    cl->check = tcheck;
    cl->restc = restc;
    noit_skiplist_insert(&in_progress, cl);
    check_test_schedule_sweeper();
    if(restc->call_closure_free)
      restc->call_closure_free(restc->call_closure);
    restc->call_closure_free = NULL;
    restc->call_closure = NULL;
    restc->fastpath = rest_test_check_err;
    goto cleanup;
  }

 error:
  noit_http_response_standard(ctx, error_code, "ERROR", "text/xml");
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

static int
check_test_init(noit_module_generic_t *self) {
  assert(noit_http_rest_register(
    "POST", "/checks/", "^test$",
    rest_test_check
  ) == 0);
  return 0;
}

noit_module_generic_t check_test = {
  {
    NOIT_GENERIC_MAGIC,
    NOIT_GENERIC_ABI_VERSION,
    "check_test",
    "Check Tester",
    "",
    check_test_onload
  },
  check_test_config,
  check_test_init
};

