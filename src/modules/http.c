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
#include "noit_poller.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"

#include <apr_uri.h>
#include <apr_atomic.h>
#include <apr_strings.h>
#include "serf.h"

#define NOIT_HTTP_VERSION_STRING "0.1"

typedef struct {
  noit_hash_table *options;
  void (*results)(noit_module_t *, noit_check_t);
} serf_module_conf_t;

typedef struct {
  int using_ssl;
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
  noit_check_t check;
} handler_baton_t;

typedef struct buf_t {
  char *b;
  int l;
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
  xmlDocPtr xml_doc;
} resmon_check_info_t;

typedef struct {
  noit_module_t *self;
  noit_check_t check;
  void *serf_baton;
  apr_socket_t *skt;
} serf_closure_t;

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;
static int serf_handler(eventer_t e, int mask, void *closure,
                        struct timeval *now);
static int serf_recur_handler(eventer_t e, int mask, void *closure,
                              struct timeval *now);
static void serf_log_results(noit_module_t *self, noit_check_t check);
static void resmon_log_results(noit_module_t *self, noit_check_t check);

static int serf_config(noit_module_t *self, noit_hash_table *options) {
  serf_module_conf_t *conf;
  conf = calloc(1, sizeof(*conf));
  conf->options = options;
  conf->results = serf_log_results;
  noit_module_set_userdata(self, conf);
  return 0;
}
static int resmon_config(noit_module_t *self, noit_hash_table *options) {
  serf_module_conf_t *conf;
  conf = calloc(1, sizeof(*conf));
  conf->options = options;
  if(!conf->options) conf->options = calloc(1, sizeof(*conf->options));
  noit_hash_store(conf->options, strdup("url"), strlen("url"),
                  strdup("http://localhost:81/"));
  conf->results = resmon_log_results;
  noit_module_set_userdata(self, conf);
  return 0;
}
static void generic_log_results(noit_module_t *self, noit_check_t check) {
  serf_module_conf_t *module_conf;
  module_conf = noit_module_get_userdata(self);
  module_conf->results(self, check);
}
static void serf_log_results(noit_module_t *self, noit_check_t check) {
  serf_check_info_t *ci = check->closure;
  struct timeval duration;
  stats_t current;
  int expect_code = 200;
  char *code_str;
  char human_buffer[256], code[4], rt[14];

  if(noit_hash_retrieve(check->config, "code", strlen("code"),
                        (void **)&code_str))
    expect_code = atoi(code_str);

  sub_timeval(ci->finish_time, check->last_fire_time, &duration);

  snprintf(code, sizeof(code), "%3d", ci->status.code);
  snprintf(rt, sizeof(rt), "%.3fms",
           (float)duration.tv_sec + (float)duration.tv_usec / 1000000.0);
  snprintf(human_buffer, sizeof(human_buffer),
           "code=%s,rt=%s,bytes=%d",
           ci->status.code ? code : "undefined",
           ci->timed_out ? "timeout" : rt,
           ci->body.l);
  noitL(nldeb, "http(%s) [%s]\n", check->target, human_buffer);

  current.duration = duration.tv_sec * 1000 + duration.tv_usec / 1000;
  current.available = (ci->timed_out || !ci->status.code) ? NP_UNAVAILABLE : NP_AVAILABLE;
  current.state = (ci->status.code != 200) ? NP_BAD : NP_GOOD;
  current.status = human_buffer;
  noit_poller_set_state(check, &current);
}
static void resmon_log_results(noit_module_t *self, noit_check_t check) {
  serf_check_info_t *ci = check->closure;
  resmon_check_info_t *rci = check->closure;
  struct timeval duration;
  stats_t current;
  int services = 0;
  char human_buffer[256], rt[14];
  xmlDocPtr resmon_results = NULL;
  xmlXPathContextPtr xpath_ctxt = NULL;

  if(ci->body.b) resmon_results = xmlParseMemory(ci->body.b, ci->body.l);
  if(resmon_results) {
    xmlXPathObjectPtr pobj;
    xpath_ctxt = xmlXPathNewContext(resmon_results);
    pobj = xmlXPathEval((xmlChar *)"/ResmonResults/ResmonResult", xpath_ctxt);
    if(pobj)
      if(pobj->type == XPATH_NODESET)
        services = xmlXPathNodeSetGetLength(pobj->nodesetval);
    xmlXPathFreeObject(pobj);
  } else {
    noitL(nlerr, "Error in resmon doc: %s\n", ci->body.b);
  }
  if(xpath_ctxt) xmlXPathFreeContext(xpath_ctxt);

  /* Save out results for future dependent checks */ 
  memcpy(&rci->xml_doc_time, &ci->finish_time, sizeof(ci->finish_time));
  if(rci->xml_doc) xmlFreeDoc(rci->xml_doc);
  rci->xml_doc = resmon_results;

  sub_timeval(ci->finish_time, check->last_fire_time, &duration);

  snprintf(rt, sizeof(rt), "%.3fms",
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
  noit_poller_set_state(check, &current);
}
static int serf_complete(eventer_t e, int mask,
                         void *closure, struct timeval *now) {
  serf_closure_t *ccl = (serf_closure_t *)closure;
  serf_check_info_t *ci = (serf_check_info_t *)ccl->check->closure;

  noitLT(nldeb, now, "serf_complete(%s)\n", ccl->check->target);
  generic_log_results(ccl->self, ccl->check);
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
  apr_pool_destroy(ci->pool);
  memset(ci, 0, sizeof(*ci));
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

static serf_bucket_t* conn_setup(apr_socket_t *skt,
                                void *setup_baton,
                                apr_pool_t *pool) {
  serf_bucket_t *c;
  app_baton_t *ctx = setup_baton;

  c = serf_bucket_socket_create(skt, ctx->bkt_alloc);
  if (ctx->using_ssl) {
      c = serf_bucket_ssl_decrypt_create(c, ctx->ssl_ctx, ctx->bkt_alloc);
      if (!ctx->ssl_ctx) {
          ctx->ssl_ctx = serf_bucket_ssl_decrypt_context_get(c);
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
  if(ci->timeout_event) {
    eventer_remove(ci->timeout_event);
    ci->timed_out = 0;
    memcpy(&ci->timeout_event->whence, &ci->finish_time,
           sizeof(&ci->finish_time));
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
    e->closure = calloc(1, sizeof(serf_closure_t));
  }
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
  if(e) e->mask = 0;
  return 0;
}

static int serf_initiate(noit_module_t *self, noit_check_t check) {
  serf_closure_t *ccl;
  serf_check_info_t *ci;
  struct timeval when, p_int;
  apr_status_t status;
  eventer_t newe;
  serf_module_conf_t *mod_config;
  char *config_url;

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

  ccl = apr_pcalloc(ci->pool, sizeof(*ccl));
  ccl->self = self;
  ccl->check = check;

  if(!noit_hash_retrieve(check->config, "url", strlen("url"),
                        (void **)&config_url))
    if(!mod_config->options ||
       !noit_hash_retrieve(mod_config->options, "url", strlen("url"),
                           (void **)&config_url))
      config_url = "http://localhost/";
  apr_uri_parse(ci->pool, config_url, &ci->url);

  if (!ci->url.port) {
    ci->url.port = apr_uri_port_of_scheme(ci->url.scheme);
  }
  if (!ci->url.path) {
    ci->url.path = "/";
  }

  if (strcasecmp(ci->url.scheme, "https") == 0) {
    ci->app_ctx.using_ssl = 1;
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
  ci->handler_ctx.host = apr_pstrdup(ci->pool, check->target);
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
static int serf_schedule_next(noit_module_t *self,
                              eventer_t e, noit_check_t check,
                              struct timeval *now) {
  eventer_t newe;
  struct timeval last_check = { 0L, 0L };
  struct timeval period, earliest;
  serf_closure_t *ccl;

  if(check->period == 0) return 0;

  /* If we have an event, we know when we intended it to fire.  This means
   * we should schedule that point + period.
   */
  if(now)
    memcpy(&earliest, now, sizeof(earliest));
  else
    gettimeofday(&earliest, NULL);
  if(e) memcpy(&last_check, &e->whence, sizeof(last_check));
  period.tv_sec = check->period / 1000;
  period.tv_usec = (check->period % 1000) * 1000;

  newe = eventer_alloc();
  memcpy(&newe->whence, &last_check, sizeof(last_check));
  add_timeval(newe->whence, period, &newe->whence);
  if(compare_timeval(newe->whence, earliest) < 0)
    memcpy(&newe->whence, &earliest, sizeof(earliest));
  newe->mask = EVENTER_TIMER;
  newe->callback = serf_recur_handler;
  ccl = calloc(1, sizeof(*ccl));
  ccl->self = self;
  ccl->check = check;
  newe->closure = ccl;

  eventer_add(newe);
  check->fire_event = newe;
  return 0;
}
static int serf_recur_handler(eventer_t e, int mask, void *closure,
                              struct timeval *now) {
  serf_closure_t *cl = (serf_closure_t *)closure;
  serf_schedule_next(cl->self, e, cl->check, now);
  serf_initiate(cl->self, cl->check);
  free(cl);
  return 0;
}
static int serf_initiate_check(noit_module_t *self, noit_check_t check,
                               int once) {
  if(!check->closure) check->closure = calloc(1, sizeof(serf_check_info_t));
  if(once) {
    serf_initiate(self, check);
    return 0;
  }
  /* If check->fire_event, we're already scheduled... */
  if(!check->fire_event)
    serf_schedule_next(self, NULL, check, NULL);
  return 0;
}
static int resmon_initiate_check(noit_module_t *self, noit_check_t check,
                                 int once) {
  /* resmon_check_info_t gives us a bit more space */
  if(!check->closure) check->closure = calloc(1, sizeof(resmon_check_info_t));
  if(once) {
    serf_initiate(self, check);
    return 0;
  }
  if(!check->fire_event)
    serf_schedule_next(self, NULL, check, NULL);
  return 0;
}


static int serf_onload(noit_module_t *self) {
  apr_initialize();
  atexit(apr_terminate);

  nlerr = noit_log_stream_find("error/serf");
  nldeb = noit_log_stream_find("debug/serf");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;

  eventer_name_callback("http/serf_handler", serf_handler);
  eventer_name_callback("http/serf_complete", serf_complete);
  eventer_name_callback("http/serf_recur_handler", serf_recur_handler);
  return 0;
}
noit_module_t http = {
  NOIT_MODULE_MAGIC,
  NOIT_MODULE_ABI_VERSION,
  "http",
  "libserf-based HTTP and HTTPS resource checker",
  serf_onload,
  serf_config,
  serf_init,
  serf_initiate_check
};

noit_module_t resmon = {
  NOIT_MODULE_MAGIC,
  NOIT_MODULE_ABI_VERSION,
  "resmon",
  "libserf-based resmon resource checker",
  serf_onload,
  resmon_config,
  serf_init,
  resmon_initiate_check
};

