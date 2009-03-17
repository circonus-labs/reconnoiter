/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <math.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"

#include <apr_uri.h>
#include <apr_atomic.h>
#include <apr_strings.h>
#include "serf.h"

#define NOIT_HTTP_VERSION_STRING "0.1"

typedef struct {
  noit_hash_table *options;
  void (*results)(noit_module_t *, noit_check_t *);
} serf_module_conf_t;

typedef struct {
  int using_ssl;
  const char *ca_chain_file;
  const char *certificate_file;
  serf_ssl_context_t *ssl_ctx;
  serf_bucket_alloc_t *bkt_alloc;
} app_baton_t;

typedef struct {
  serf_response_acceptor_t acceptor;
  app_baton_t *acceptor_baton;

  serf_response_handler_t handler;
  const char *host;
  const char *method;
  const char *path;
  const char *authn;

  noit_module_t *self;
  noit_check_t *check;
} handler_baton_t;

typedef struct buf_t {
  char *b;
  int32_t l;
} buf_t;

typedef struct {
  apr_pool_t *pool;
  apr_sockaddr_t *address;
  serf_context_t *context;
  serf_connection_t *connection;
  serf_request_t *request;
  app_baton_t app_ctx;
  handler_baton_t handler_ctx;
  apr_uri_t url;
  int timed_out;

  serf_status_line status;
  buf_t headers;
  buf_t body;

  struct timeval finish_time;
  eventer_t fd_event;
  eventer_t timeout_event;
} serf_check_info_t;

typedef struct {
  serf_check_info_t serf;
  struct timeval xml_doc_time;
  char *xpathexpr;
  xmlDocPtr xml_doc;
  char *resmod;
  char *resserv;
} resmon_check_info_t;

typedef struct {
  noit_module_t *self;
  noit_check_t *check;
  void *serf_baton;
  apr_socket_t *skt;
} serf_closure_t;

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;
static int serf_handler(eventer_t e, int mask, void *closure,
                        struct timeval *now);
static void serf_log_results(noit_module_t *self, noit_check_t *check);
static void resmon_log_results(noit_module_t *self, noit_check_t *check);
static void resmon_part_log_results(noit_module_t *self, noit_check_t *check,
                                    noit_check_t *parent);

static int serf_config(noit_module_t *self, noit_hash_table *options) {
  serf_module_conf_t *conf;
  conf = noit_module_get_userdata(self);
  if(conf) {
    if(conf->options) {
      noit_hash_destroy(conf->options, free, free);
      free(conf->options);
    }
  }
  else
    conf = calloc(1, sizeof(*conf));
  conf->options = options;
  conf->results = serf_log_results;
  noit_module_set_userdata(self, conf);
  return 1;
}
static int resmon_config(noit_module_t *self, noit_hash_table *options) {
  serf_module_conf_t *conf;
  conf = noit_module_get_userdata(self);
  if(conf) {
    if(conf->options) {
      noit_hash_destroy(conf->options, free, free);
      free(conf->options);
    }
  }
  else
    conf = calloc(1, sizeof(*conf));
  conf->options = options;
  if(!conf->options) conf->options = calloc(1, sizeof(*conf->options));
  noit_hash_store(conf->options, strdup("url"), strlen("url"),
                  strdup("http://localhost:81/"));
  conf->results = resmon_log_results;
  noit_module_set_userdata(self, conf);
  return 1;
}
static void generic_log_results(noit_module_t *self, noit_check_t *check) {
  serf_module_conf_t *module_conf;
  module_conf = noit_module_get_userdata(self);
  module_conf->results(self, check);
}
static void serf_log_results(noit_module_t *self, noit_check_t *check) {
  serf_check_info_t *ci = check->closure;
  struct timeval duration;
  stats_t current;
  int expect_code = 200;
  u_int32_t duration_ms;
  void *code_str; /* void * for use with hash */
  char human_buffer[256], code[4], rt[14];

  noit_check_stats_clear(&current);

  if(noit_hash_retrieve(check->config, "code", strlen("code"), &code_str))
    expect_code = atoi((const char *)code_str);

  sub_timeval(ci->finish_time, check->last_fire_time, &duration);

  snprintf(code, sizeof(code), "%3d", ci->status.code);
  snprintf(rt, sizeof(rt), "%.3fs",
           (float)duration.tv_sec + (float)duration.tv_usec / 1000000.0);
  snprintf(human_buffer, sizeof(human_buffer),
           "code=%s,rt=%s,bytes=%d",
           ci->status.code ? code : "undefined",
           ci->timed_out ? "timeout" : rt,
           ci->body.l);
  noitL(nldeb, "http(%s) [%s]\n", check->target, human_buffer);

  memcpy(&current.whence, &ci->finish_time, sizeof(current.whence));
  current.duration = duration.tv_sec * 1000 + duration.tv_usec / 1000;
  duration_ms = current.duration;
  current.available = (ci->timed_out || !ci->status.code) ? NP_UNAVAILABLE : NP_AVAILABLE;
  current.state = (ci->status.code != 200) ? NP_BAD : NP_GOOD;
  current.status = human_buffer;
  if(current.available == NP_AVAILABLE) {
    noit_stats_set_metric(&current, "code",
                          METRIC_STRING, ci->status.code?code:NULL);
    noit_stats_set_metric(&current, "bytes",
                          METRIC_INT32, &ci->body.l);
    noit_stats_set_metric(&current, "duration",
                          METRIC_UINT32, &duration_ms);
  }
  else {
    noit_stats_set_metric(&current, "code", METRIC_STRING, NULL);
    noit_stats_set_metric(&current, "bytes", METRIC_INT32, NULL);
    noit_stats_set_metric(&current, "duration", METRIC_UINT32, NULL);
  }
  noit_check_set_stats(self, check, &current);
}
static void resmon_part_log_results_xml(noit_module_t *self,
                                        noit_check_t *check,
                                        xmlDocPtr xml) {
  serf_check_info_t *ci = check->closure;
  resmon_check_info_t *rci = check->closure;
  xmlXPathContextPtr xpath_ctxt = NULL;
  stats_t current;

  noit_check_stats_clear(&current);
  memcpy(&current.whence, &ci->finish_time, sizeof(current.whence));
  current.available = NP_UNAVAILABLE;
  current.state = NP_BAD;

  if(xml && rci->xpathexpr) {
    current.available = NP_AVAILABLE;
    xpath_ctxt = xmlXPathNewContext(xml);
    if(xpath_ctxt) {
      xmlXPathObjectPtr pobj;
      pobj = xmlXPathEval((xmlChar *)rci->xpathexpr, xpath_ctxt);
      if(pobj) {
        int i, cnt;
        cnt = xmlXPathNodeSetGetLength(pobj->nodesetval);
        for(i=0; i<cnt; i++) {
          xmlNodePtr node;
          char *value;
          node = xmlXPathNodeSetItem(pobj->nodesetval, i);
          value = (char *)xmlXPathCastNodeToString(node);
          if(!strcmp((char *)node->name,"last_runtime_seconds")) {
            float duration = atof(value) * 1000;
            current.duration = (int) duration;
          }
          else if(!strcmp((char *)node->name, "message")) {
            current.status = strdup(value);
          }
          else if(!strcmp((char *)node->name, "state")) {
            current.state = strcmp(value,"OK") ? NP_BAD : NP_GOOD;
          }
          xmlFree(value);
        }
        xmlXPathFreeObject(pobj);
      }
      xmlXPathFreeContext(xpath_ctxt);
    }
  }
  memcpy(&current.whence, &rci->serf.finish_time, sizeof(current.whence));
  current.status = current.status ? current.status : strdup("unknown");
  noitL(nldeb, "resmon_part(%s/%s/%s) [%s]\n", check->target,
        rci->resmod, rci->resserv, current.status);
  noit_check_set_stats(self, check, &current);
  free(current.status);
}
static void resmon_part_log_results(noit_module_t *self, noit_check_t *check,
                                    noit_check_t *parent) {
  resmon_check_info_t *rci = parent->closure;
  resmon_part_log_results_xml(self, check, rci->xml_doc);
}
static void resmon_log_results(noit_module_t *self, noit_check_t *check) {
  serf_check_info_t *ci = check->closure;
  resmon_check_info_t *rci = check->closure;
  struct timeval duration;
  stats_t current;
  int32_t services = 0;
  char human_buffer[256], rt[14];
  xmlDocPtr resmon_results = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL;
  xmlXPathObjectPtr pobj = NULL;

  noit_check_stats_clear(&current);

  if(ci->body.b) resmon_results = xmlParseMemory(ci->body.b, ci->body.l);
  if(resmon_results) {
    xpath_ctxt = xmlXPathNewContext(resmon_results);
    pobj = xmlXPathEval((xmlChar *)"/ResmonResults/ResmonResult", xpath_ctxt);
    if(pobj)
      if(pobj->type == XPATH_NODESET)
        services = xmlXPathNodeSetGetLength(pobj->nodesetval);
  } else {
    if(ci->body.l)
      noitL(nlerr, "Error in resmon doc: %s\n", ci->body.b);
  }

  /* Save our results for future dependent checks */ 
  memcpy(&current.whence, &ci->finish_time, sizeof(current.whence));
  memcpy(&rci->xml_doc_time, &ci->finish_time, sizeof(ci->finish_time));
  if(rci->xml_doc) xmlFreeDoc(rci->xml_doc);
  rci->xml_doc = resmon_results;

  if(rci->xpathexpr) {
    /* This is actually a part check... we had to do all the work as
     * it isn't being used as a causal firing from a generic resmon check
     */
    resmon_part_log_results_xml(self, check, rci->xml_doc);
    goto out;
  }

  sub_timeval(ci->finish_time, check->last_fire_time, &duration);
  snprintf(rt, sizeof(rt), "%.3fs",
           (float)duration.tv_sec + (float)duration.tv_usec / 1000000.0);
  snprintf(human_buffer, sizeof(human_buffer),
           "services=%d,rt=%s",
           services,
           ci->timed_out ? "timeout" : rt);
  noitL(nldeb, "resmon(%s) [%s]\n", check->target, human_buffer);

  current.duration = duration.tv_sec * 1000 + duration.tv_usec / 1000;
  current.available = (ci->timed_out || ci->status.code != 200) ?
                          NP_UNAVAILABLE : NP_AVAILABLE;
  current.state = services ? NP_GOOD : NP_BAD;
  current.status = human_buffer;

  noit_stats_set_metric(&current, "services", METRIC_INT32, &services);
  if(services) {
    int i;
    for(i=0; i<services; i++) {
      xmlNodePtr node, attrnode;
      node = xmlXPathNodeSetItem(pobj->nodesetval, i);
      if(node) {
        int a;
        char *attrs[3] = { "last_runtime_seconds", "state", "message" };
        char *resmod = NULL, *resserv = NULL, *value = NULL;
        char attr[1024];
        xmlXPathObjectPtr sobj;

        xpath_ctxt->node = node;
        sobj = xmlXPathEval((xmlChar *)"@module", xpath_ctxt);
        if(sobj) {
          resmod = (char *)xmlXPathCastNodeSetToString(sobj->nodesetval);
          xmlXPathFreeObject(sobj);
        }
        sobj = xmlXPathEval((xmlChar *)"@service", xpath_ctxt);
        if(sobj) {
          resserv = (char *)xmlXPathCastNodeSetToString(sobj->nodesetval);
          xmlXPathFreeObject(sobj);
        }
        if(!resmod && !resserv) continue;

        for(a=0; a<3; a++) {
          int32_t intval;
          sobj = xmlXPathEval((xmlChar *)attrs[a], xpath_ctxt);
          attrnode = xmlXPathNodeSetItem(sobj->nodesetval, 0);
          value = (char *)xmlXPathCastNodeToString(attrnode);
          xmlXPathFreeObject(sobj);
          snprintf(attr, sizeof(attr), "%s`%s`%s",
                   resmod, resserv, (char *)attrnode->name);
          switch(a) {
            case 0:
              /* The first is integer */
              intval = (int)(atof(value) * 1000.0);
              noit_stats_set_metric(&current, attr, METRIC_INT32, &intval);
              break;
            case 1:
              noit_stats_set_metric(&current, attr, METRIC_STRING, value);
              break;
            case 2:
              noit_stats_set_metric(&current, attr, METRIC_GUESS, value);
              break;
          }
          xmlFree(value);
        }
        if(resmod) xmlFree(resmod);
        if(resserv) xmlFree(resserv);
      }
    }
  }

  noit_check_set_stats(self, check, &current);

 out:
  if(pobj) xmlXPathFreeObject(pobj);
  if(xpath_ctxt) xmlXPathFreeContext(xpath_ctxt);
}
static void serf_cleanup(noit_module_t *self, noit_check_t *check) {
  serf_check_info_t *ci;
  ci = check->closure;
  if(ci->connection) {
    serf_connection_close(ci->connection);
    ci->connection = NULL;
  }
  if(ci->fd_event) {
    eventer_remove_fd(ci->fd_event->fd);
    eventer_free(ci->fd_event);
    ci->fd_event = NULL;
  }
  ci->timeout_event = NULL;
  if(ci->pool) apr_pool_destroy(ci->pool);
  memset(ci, 0, sizeof(*ci));
}
static int serf_complete(eventer_t e, int mask,
                         void *closure, struct timeval *now) {
  serf_closure_t *ccl = (serf_closure_t *)closure;

  noitLT(nldeb, now, "serf_complete(%s)\n", ccl->check->target);
  if(!NOIT_CHECK_DISABLED(ccl->check) && !NOIT_CHECK_KILLED(ccl->check)) {
    serf_check_info_t *ci = ccl->check->closure;
    if(ci->finish_time.tv_sec == 0 && ci->finish_time.tv_usec == 0)
      memcpy(&ci->finish_time, now, sizeof(*now));
    generic_log_results(ccl->self, ccl->check);
  }
  serf_cleanup(ccl->self, ccl->check);
  ccl->check->flags &= ~NP_RUNNING;
  free(ccl);
  return 0;
}

static int serf_handler(eventer_t e, int mask,
                        void *closure, struct timeval *now) {
  apr_pollfd_t desc = { 0 };
  serf_closure_t *sct = closure;
  serf_check_info_t *ci = sct->check->closure;

  desc.desc_type = APR_POLL_SOCKET;
  desc.desc.s = sct->skt;

  desc.rtnevents = 0;
  if(mask & EVENTER_READ) desc.rtnevents |= APR_POLLIN;
  if(mask & EVENTER_WRITE) desc.rtnevents |= APR_POLLOUT;
  if(mask & EVENTER_EXCEPTION) desc.rtnevents |= APR_POLLERR;
  serf_event_trigger(ci->context, sct->serf_baton, &desc);
  serf_context_prerun(ci->context);

  /* We're about to deschedule and free the event, drop our reference */
  if(!e->mask)
    ci->fd_event = NULL;

  return e->mask;
}

static int serf_init(noit_module_t *self) {
  return 0;
}
static void closed_connection(serf_connection_t *conn,
                              void *closed_baton,
                              apr_status_t why,
                              apr_pool_t *pool) {
}
static apr_status_t need_client_cert(void *data,
                                     const char **path) {
  app_baton_t *ctx = data;
  *path = ctx->certificate_file;
  return APR_SUCCESS;
}
static apr_status_t need_server_cert(void *data,
                                     int failures,
                                     const serf_ssl_certificate_t *cert) {
  return APR_SUCCESS;
}
static serf_bucket_t* conn_setup(apr_socket_t *skt,
                                void *setup_baton,
                                apr_pool_t *pool) {
  serf_bucket_t *c;
  app_baton_t *ctx = setup_baton;

  c = serf_bucket_socket_create(skt, ctx->bkt_alloc);
  if (ctx->using_ssl) {
    c = serf_bucket_ssl_decrypt_create(c, ctx->ssl_ctx, ctx->bkt_alloc);
    if (!ctx->ssl_ctx) {
      serf_ssl_certificate_t *cert;
      ctx->ssl_ctx = serf_bucket_ssl_decrypt_context_get(c);

      /* Setup CA chain */
      if(ctx->ca_chain_file &&
         serf_ssl_load_cert_file(&cert, ctx->ca_chain_file,
                                 pool) != APR_SUCCESS)
        serf_ssl_trust_cert(ctx->ssl_ctx, cert);
      else
        serf_ssl_use_default_certificates(ctx->ssl_ctx);
      serf_ssl_server_cert_callback_set(ctx->ssl_ctx, need_server_cert,
                                        ctx);

      /* Setup client cert */
      serf_ssl_client_cert_provider_set(ctx->ssl_ctx, need_client_cert,
                                        ctx, pool);
    }
  }

  return c;
}

static serf_bucket_t* accept_response(serf_request_t *request,
                                      serf_bucket_t *stream,
                                      void *acceptor_baton,
                                      apr_pool_t *pool) {
  serf_bucket_t *c;
  serf_bucket_alloc_t *bkt_alloc;

  /* get the per-request bucket allocator */
  bkt_alloc = serf_request_get_alloc(request);

  /* Create a barrier so the response doesn't eat us! */
  c = serf_bucket_barrier_create(stream, bkt_alloc);

  return serf_bucket_response_create(c, bkt_alloc);
}

static void append_buf(apr_pool_t *p, buf_t *b,
                       const char *data, int len) {
  char *n;
  n = apr_palloc(p, b->l + len + 1);
  if(b->l == 0)
    b->b = n;
  else {
    memcpy(n, b->b, b->l);
    b->b = n;
  }
  memcpy(b->b + b->l, data, len);
  b->l += len;
  b->b[b->l] = '\0';
}

static apr_status_t handle_response(serf_request_t *request,
                                    serf_bucket_t *response,
                                    void *handler_baton,
                                    apr_pool_t *pool) {
  const char *data;
  apr_size_t len;
  apr_status_t status;
  handler_baton_t *ctx = handler_baton;
  serf_check_info_t *ci = ctx->check->closure;

  if(response == NULL) {
    /* We were cancelled. */
    goto finish;
  }
  status = serf_bucket_response_status(response, &ci->status);
  if (status) {
    if (APR_STATUS_IS_EAGAIN(status)) {
      return status;
    }
    goto finish;
  }

  while (1) {
    status = serf_bucket_read(response, 1024*32, &data, &len);
    if (SERF_BUCKET_READ_ERROR(status))
      return status;

    append_buf(ci->pool, &ci->body, data, len);

    /* are we done yet? */
    if (APR_STATUS_IS_EOF(status)) {
      serf_bucket_t *hdrs;
      hdrs = serf_bucket_response_get_headers(response);
      while (1) {
        status = serf_bucket_read(hdrs, 2048, &data, &len);
        if (SERF_BUCKET_READ_ERROR(status))
          return status;

        append_buf(ci->pool, &ci->headers, data, len);
        if (APR_STATUS_IS_EOF(status)) {
          break;
        }
      }

      goto finish;
    }

    /* have we drained the response so far? */
    if (APR_STATUS_IS_EAGAIN(status))
      return status;

    /* loop to read some more. */
  }
 finish:
  gettimeofday(&ci->finish_time, NULL);
  noitL(nldeb, "serf finished request (%s) [%ld.%06d]\n", ctx->check->target,
        (long int)ci->finish_time.tv_sec, (int)ci->finish_time.tv_usec);
  if(ci->timeout_event) {
    eventer_remove(ci->timeout_event);
    ci->timed_out = 0;
    memcpy(&ci->timeout_event->whence, &ci->finish_time,
           sizeof(ci->finish_time));
    eventer_add(ci->timeout_event);
  }
  return APR_EOF;
}

static apr_status_t setup_request(serf_request_t *request,
                                  void *setup_baton,
                                  serf_bucket_t **req_bkt,
                                  serf_response_acceptor_t *acceptor,                                             void **acceptor_baton,
                                  serf_response_handler_t *handler,
                                  void **handler_baton,
                                  apr_pool_t *pool) {
  handler_baton_t *ctx = setup_baton;
  serf_bucket_t *hdrs_bkt;
  serf_bucket_t *body_bkt;

  body_bkt = NULL;

  *req_bkt = serf_bucket_request_create(ctx->method, ctx->path, body_bkt,
                                        serf_request_get_alloc(request));

  hdrs_bkt = serf_bucket_request_get_headers(*req_bkt);

  serf_bucket_headers_setn(hdrs_bkt, "Host", ctx->host);
  serf_bucket_headers_setn(hdrs_bkt, "User-Agent",
                           "Noit/" NOIT_HTTP_VERSION_STRING);
  /* Shouldn't serf do this for us? */
  serf_bucket_headers_setn(hdrs_bkt, "Accept-Encoding", "gzip");

  if (ctx->authn != NULL) {
    serf_bucket_headers_setn(hdrs_bkt, "Authorization", ctx->authn);
  }

  if (ctx->acceptor_baton->using_ssl) {
    serf_bucket_alloc_t *req_alloc;
    app_baton_t *app_ctx = ctx->acceptor_baton;

    req_alloc = serf_request_get_alloc(request);

    if (app_ctx->ssl_ctx == NULL) {
      *req_bkt =
        serf_bucket_ssl_encrypt_create(*req_bkt, NULL,
                                       app_ctx->bkt_alloc);
      app_ctx->ssl_ctx =
        serf_bucket_ssl_encrypt_context_get(*req_bkt);
    }
    else {
      *req_bkt =
        serf_bucket_ssl_encrypt_create(*req_bkt, app_ctx->ssl_ctx,
                                       app_ctx->bkt_alloc);
    }
  }

  *acceptor = ctx->acceptor;
  *acceptor_baton = ctx->acceptor_baton;
  *handler = ctx->handler;
  *handler_baton = ctx;

  return APR_SUCCESS;
}

struct __unix_apr_socket_t {
  apr_pool_t *pool;
  int socketdes;
};

static apr_status_t serf_eventer_add(void *user_baton,
                                     apr_pollfd_t *pfd,
                                     void *serf_baton) {
  eventer_t e, newe = NULL;
  serf_closure_t *sct = user_baton, *newsct;
  assert(pfd->desc_type == APR_POLL_SOCKET);
  struct __unix_apr_socket_t *hack = (struct __unix_apr_socket_t *)pfd->desc.s;

  noitL(nldeb, "serf_eventer_add() => %d\n", hack->socketdes);
  e = eventer_find_fd(hack->socketdes);
  if(!e) {
    newe = e = eventer_alloc();
    e->fd = hack->socketdes;
    e->callback = serf_handler;
  }
  if(!e->closure)
    e->closure = calloc(1, sizeof(serf_closure_t));
  newsct = e->closure;
  newsct->self = sct->self;
  newsct->check = sct->check;
  newsct->serf_baton = serf_baton;
  newsct->skt = pfd->desc.s;
  e->mask = 0;
  if(pfd->reqevents & APR_POLLIN) e->mask |= EVENTER_READ;
  if(pfd->reqevents & APR_POLLOUT) e->mask |= EVENTER_WRITE;
  if(pfd->reqevents & APR_POLLERR) e->mask |= EVENTER_EXCEPTION;
  if(newe) {
    serf_check_info_t *ci = sct->check->closure;
    eventer_add(newe);
    ci->fd_event = newe;
  }
/* ** Unneeded as this is called recursively **
  else
    eventer_update(e);
*/
  return APR_SUCCESS;
}
static apr_status_t serf_eventer_remove(void *user_baton,
                                        apr_pollfd_t *pfd,
                                        void *serf_baton) {
  serf_closure_t *sct = user_baton;
  serf_check_info_t *ci;
  eventer_t e;

  ci = sct->check->closure;
  assert(pfd->desc_type == APR_POLL_SOCKET);
  struct __unix_apr_socket_t *hack = (struct __unix_apr_socket_t *)pfd->desc.s;

  noitL(nldeb, "serf_eventer_remove() => %d\n", hack->socketdes);
  e = eventer_find_fd(hack->socketdes);
  if(e) {
    free(e->closure);
    e->closure = NULL;
    e->mask = 0;
  }
  return 0;
}

static int serf_initiate(noit_module_t *self, noit_check_t *check) {
  serf_closure_t *ccl;
  serf_check_info_t *ci;
  struct timeval when, p_int;
  apr_status_t status;
  eventer_t newe;
  serf_module_conf_t *mod_config;
  void *config_url;

  mod_config = noit_module_get_userdata(self);
  ci = (serf_check_info_t *)check->closure;
  /* We cannot be running */
  assert(!(check->flags & NP_RUNNING));
  check->flags |= NP_RUNNING;
  noitL(nldeb, "serf_initiate(%p,%s)\n",
        self, check->target);

  /* remove a timeout if we still have one -- we should unless someone
   * has set a lower timeout than the period.
   */
  ci->timed_out = 1;
  if(ci->timeout_event) {
    eventer_remove(ci->timeout_event);
    free(ci->timeout_event->closure);
    eventer_free(ci->timeout_event);
    ci->timeout_event = NULL;
  }
  assert(!ci->pool);
  apr_pool_create(&ci->pool, NULL);
  apr_atomic_init(ci->pool);

  gettimeofday(&when, NULL);
  memcpy(&check->last_fire_time, &when, sizeof(when));
  ci->finish_time.tv_sec = ci->finish_time.tv_usec = 0L;

  ccl = apr_pcalloc(ci->pool, sizeof(*ccl));
  ccl->self = self;
  ccl->check = check;

  if(!noit_hash_retrieve(check->config, "url", strlen("url"), &config_url))
    if(!mod_config->options ||
       !noit_hash_retrieve(mod_config->options, "url", strlen("url"),
                           &config_url))
      config_url = "http://localhost/";
  apr_uri_parse(ci->pool, config_url, &ci->url);

  if (!ci->url.port) {
    ci->url.port = apr_uri_port_of_scheme(ci->url.scheme);
  }
  if (!ci->url.path) {
    ci->url.path = "/";
  }

  if (strcasecmp(ci->url.scheme, "https") == 0) {
    void *vstr;
    serf_module_conf_t *conf;
    conf = noit_module_get_userdata(self);

    ci->app_ctx.using_ssl = 1;

    if(noit_hash_retrieve(check->config, "ca_chain",
                          strlen("ca_chain"), &vstr))
      ci->app_ctx.ca_chain_file = apr_pstrdup(ci->pool, vstr);
    else if(noit_hash_retrieve(conf->options, "ca_chain",
                               strlen("ca_chain"), &vstr))
      ci->app_ctx.ca_chain_file = apr_pstrdup(ci->pool, vstr);

    if(noit_hash_retrieve(check->config, "certificate_file",
                          strlen("certificate_file"), &vstr))
      ci->app_ctx.certificate_file = apr_pstrdup(ci->pool, vstr);
    else if(noit_hash_retrieve(conf->options, "certificate_file",
                               strlen("certificate_file"), &vstr))
      ci->app_ctx.certificate_file = apr_pstrdup(ci->pool, vstr);
  }
  else {
    ci->app_ctx.using_ssl = 0;
  }

  status = apr_sockaddr_info_get(&ci->address,
                                 check->target, APR_UNSPEC, ci->url.port, 0,
                                 ci->pool);
  if (status) {
    /* Handle error -- log failure */
    apr_pool_destroy(ci->pool);
    memset(ci, 0, sizeof(*ci));
    check->flags &= ~NP_RUNNING;
    return 0;
  }

  ci->context = serf_context_create_ex(ccl, serf_eventer_add,
                                       serf_eventer_remove, ci->pool);

  ci->app_ctx.bkt_alloc = serf_bucket_allocator_create(ci->pool, NULL, NULL);
  ci->app_ctx.ssl_ctx = NULL;

  ci->connection = serf_connection_create(ci->context, ci->address,
                                          conn_setup, &ci->app_ctx,
                                          closed_connection, &ci->app_ctx,
                                          ci->pool);

  ci->handler_ctx.method = apr_pstrdup(ci->pool, "GET");
  ci->handler_ctx.host = apr_pstrdup(ci->pool, ci->url.hostname);
  ci->handler_ctx.path = ci->url.path;
  ci->handler_ctx.authn = NULL;

  ci->handler_ctx.acceptor = accept_response;
  ci->handler_ctx.acceptor_baton = &ci->app_ctx;
  ci->handler_ctx.handler = handle_response;
  ci->handler_ctx.self = self;
  ci->handler_ctx.check = check;

  ci->request = serf_connection_request_create(ci->connection, setup_request,
                                               &ci->handler_ctx);
  serf_context_prerun(ci->context);

  newe = eventer_alloc();
  newe->mask = EVENTER_TIMER;
  gettimeofday(&when, NULL);
  p_int.tv_sec = check->timeout / 1000;
  p_int.tv_usec = (check->timeout % 1000) * 1000;
  add_timeval(when, p_int, &newe->whence);
  ccl = calloc(1, sizeof(*ccl));
  ccl->self = self;
  ccl->check = check;
  newe->closure = ccl;
  newe->callback = serf_complete;
  eventer_add(newe);
  ci->timeout_event = newe;
  return 0;
}
static int serf_initiate_check(noit_module_t *self, noit_check_t *check,
                               int once, noit_check_t *cause) {
  if(!check->closure) check->closure = calloc(1, sizeof(serf_check_info_t));
  INITIATE_CHECK(serf_initiate, self, check);
  return 0;
}
static int resmon_initiate_check(noit_module_t *self, noit_check_t *check,
                                 int once, noit_check_t *parent) {
  /* resmon_check_info_t gives us a bit more space */
  if(!check->closure) check->closure = calloc(1, sizeof(resmon_check_info_t));
  INITIATE_CHECK(serf_initiate, self, check);
  return 0;
}

static void resmon_cleanup(noit_module_t *self, noit_check_t *check) {
  resmon_check_info_t *rci;
  rci = check->closure;
  if(rci) {
    if(rci->xpathexpr) free(rci->xpathexpr);
    if(rci->resmod) free(rci->resmod);
    if(rci->resserv) free(rci->resserv);
    if(rci->xml_doc) xmlFreeDoc(rci->xml_doc);
    serf_cleanup(self, check);
    memset(rci, 0, sizeof(*rci));
  }
}
static int resmon_part_initiate_check(noit_module_t *self, noit_check_t *check,
                                      int once, noit_check_t *parent) {
  char xpathexpr[1024];
  void *resmod, *resserv;
  resmon_check_info_t *rci;

  if(NOIT_CHECK_DISABLED(check) || NOIT_CHECK_KILLED(check)) return 0;

  if(!check->closure) check->closure = calloc(1, sizeof(resmon_check_info_t));
  rci = check->closure;
  if(!rci->xpathexpr) {
    if(!noit_hash_retrieve(check->config,
                           "resmon_module", strlen("resmon_module"),
                           &resmod)) {
      resmod = "DUMMY_MODULE";
    }
    if(!noit_hash_retrieve(check->config,
                           "resmon_service", strlen("resmon_service"),
                           &resserv)) {
      resserv = "DUMMY_SERVICE";
    }
    snprintf(xpathexpr, sizeof(xpathexpr),
             "//ResmonResult[@module=\"%s\" and @service=\"%s\"]/*",
             (const char *)resmod, (const char *)resserv);
    rci->xpathexpr = strdup(xpathexpr);
    rci->resmod = strdup(resmod);
    rci->resserv = strdup(resserv);
  }

  if(parent && !strcmp(parent->module, "resmon")) {
    /* Content is cached in the parent */
    serf_check_info_t *ci = (serf_check_info_t *)rci;
    gettimeofday(&ci->finish_time, NULL);
    resmon_part_log_results(self, check, parent);
    return 0;
  }
  INITIATE_CHECK(serf_initiate, self, check);
  return 0;
}

static int serf_onload(noit_image_t *self) {
  apr_initialize();
  atexit(apr_terminate);

  nlerr = noit_log_stream_find("error/serf");
  nldeb = noit_log_stream_find("debug/serf");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;

  eventer_name_callback("http/serf_handler", serf_handler);
  eventer_name_callback("http/serf_complete", serf_complete);
  return 0;
}

#include "http.xmlh"
noit_module_t http = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "http",
    "libserf-based HTTP and HTTPS resource checker",
    http_xml_description,
    serf_onload
  },
  serf_config,
  serf_init,
  serf_initiate_check,
  serf_cleanup
};

#include "resmon.xmlh"
noit_module_t resmon = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "resmon",
    "libserf-based resmon resource checker",
    resmon_xml_description,
    serf_onload
  },
  resmon_config,
  serf_init,
  resmon_initiate_check,
  resmon_cleanup
};

#include "resmon_part.xmlh"
noit_module_t resmon_part = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "resmon_part",
    "resmon part resource checker",
    resmon_part_xml_description,
    serf_onload
  },
  resmon_config,
  serf_init,
  resmon_part_initiate_check,
  resmon_cleanup
};

