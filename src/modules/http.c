/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
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

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <math.h>
#include <ctype.h>

#include <pcre.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"

#include <curl/curl.h>

#define NOIT_HTTP_VERSION_STRING "0.1"

typedef struct {
  noit_hash_table *options;
  void (*results)(noit_module_t *, noit_check_t *);
} http_module_conf_t;

typedef struct buf_t {
  char *b;
  int32_t l;
} buf_t;

/* pqTODO: directly copied from serf_bucket_types.h */
#define HTTP_VERSION(major, minor)  ((major) * 1000 + (minor))
#define HTTP_11 HTTP_VERSION(1, 1)
#define HTTP_10 HTTP_VERSION(1, 0)

typedef struct {
  int version;
  int code;
  char *reason;
} http_status_line;

typedef struct {
  struct curl_slist *cheaders;
  CURL *curl;
  CURLM *mcurl;
  int timed_out;

  xmlURIPtr url;
  buf_t headers;
  buf_t body;

  struct timeval finish_time;
  eventer_t fd_event;
  eventer_t timeout_event;
  eventer_t process_event;
  eventer_t done_event;

  noit_module_t *self;
  noit_check_t *check;
  
  http_status_line status;
  int inside_handler;
} http_check_info_t;

typedef struct {
  http_check_info_t http;
  struct timeval xml_doc_time;
  char *xpathexpr;
  xmlDocPtr xml_doc;
  char *resmod;
  char *resserv;
} resmon_check_info_t;

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;
static void http_eventer_free(eventer_t e, http_check_info_t *ci);
static int http_consume_messages(http_check_info_t *ci);
static void http_log_results(noit_module_t *self, noit_check_t *check);
static void resmon_log_results(noit_module_t *self, noit_check_t *check);
static void resmon_part_log_results(noit_module_t *self, noit_check_t *check,
                                    noit_check_t *parent);

static int http_config(noit_module_t *self, noit_hash_table *options) {
  http_module_conf_t *conf;
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
  conf->results = http_log_results;
  noit_module_set_userdata(self, conf);
  return 1;
}
static int resmon_config(noit_module_t *self, noit_hash_table *options) {
  http_module_conf_t *conf;
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
  http_module_conf_t *conf;
  conf = noit_module_get_userdata(self);
  conf->results(self, check);
}

static void http_log_results(noit_module_t *self, noit_check_t *check) {
  http_check_info_t *ci = check->closure;
  struct timeval duration;
  stats_t current;
  pcre *expect_code = NULL, *body_match = NULL;
  u_int32_t duration_ms;
  void *code_str, *body_str; /* void * for use with hash */
  char human_buffer[256], code[4], rt[14], bmatch[30];
  const char *error;
  int body_matched = 1;
  int erroffset;
  int ovector[30];

  noit_check_stats_clear(&current);

  if(!noit_hash_retrieve(check->config, "code", strlen("code"), &code_str)) {
    code_str = "^200$";
  }
  expect_code = pcre_compile((const char *)code_str, 0,
                             &error, &erroffset, NULL);
  if(!expect_code)
    noitL(nlerr, "http code match /%s/ failed @ %d: %s\n", (char *)code_str,
          erroffset, error);

  if(noit_hash_retrieve(check->config, "body", strlen("body"), &body_str)) {
    body_match = pcre_compile((const char *)body_str, 0,
                              &error, &erroffset, NULL);
    if(!body_match)
      noitL(nlerr, "http body match /%s/ failed @ %d: %s\n",
            (char *)body_str, erroffset, error);
  }

  sub_timeval(ci->finish_time, check->last_fire_time, &duration);

  snprintf(code, sizeof(code), "%3d", ci->status.code);
  snprintf(rt, sizeof(rt), "%.3fs",
           (float)duration.tv_sec + (float)duration.tv_usec / 1000000.0);

  bmatch[0] = '\0';
  if(body_match) {
    if(pcre_exec(body_match, NULL, ci->body.b, ci->body.l, 0, 0,
                 ovector, sizeof(ovector)/sizeof(*ovector)) <= 0) {
      body_matched = 0;
    }
    snprintf(bmatch, sizeof(bmatch),
             ",body=%s", body_matched ? "matched" : "failed");
  }

  snprintf(human_buffer, sizeof(human_buffer),
           "code=%s,rt=%s,bytes=%d%s",
           ci->status.code ? code : "undefined",
           ci->timed_out ? "timeout" : rt,
           ci->body.l, bmatch);
  noitL(nldeb, "http(%s) [%s]\n", check->target, human_buffer);

  memcpy(&current.whence, &ci->finish_time, sizeof(current.whence));
  current.duration = duration.tv_sec * 1000 + duration.tv_usec / 1000;
  duration_ms = current.duration;
  current.available = (ci->timed_out || !ci->status.code) ? NP_UNAVAILABLE : NP_AVAILABLE;

  if(body_matched == 0) current.state = NP_BAD;
  else if(expect_code &&
          pcre_exec(expect_code, NULL, code, strlen(code), 0, 0,
                    ovector, sizeof(ovector)/sizeof(*ovector)) > 0)
    current.state = NP_GOOD;
  else
    current.state = NP_BAD;

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
  if(expect_code) pcre_free(expect_code);
  if(body_match) pcre_free(body_match);
}
static void resmon_part_log_results_xml(noit_module_t *self,
                                        noit_check_t *check,
                                        xmlDocPtr xml) {
  resmon_check_info_t *rci = check->closure;
  http_check_info_t *ci = &rci->http;
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
  memcpy(&current.whence, &ci->finish_time, sizeof(current.whence));
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
  resmon_check_info_t *rci = check->closure;
  http_check_info_t *ci = &rci->http;
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

static void http_cleanup_check(noit_module_t *self, noit_check_t *check) {
  http_check_info_t *ci;
  ci = check->closure;
  noitL(nldeb, "http_cleanup_check(%p)\n", ci);
  if (ci->curl) {
    if (ci->mcurl) {
      curl_multi_remove_handle(ci->mcurl, ci->curl);
    }
    curl_easy_cleanup(ci->curl);
    ci->curl = NULL;
  }

  if (ci->mcurl) {
    curl_multi_cleanup(ci->mcurl);
    ci->mcurl = NULL;
  }

  if (ci->cheaders) {
    curl_slist_free_all(ci->cheaders);
    ci->cheaders = NULL;
  }

  if(ci->fd_event) {
    http_eventer_free(eventer_remove_fd(ci->fd_event->fd), ci);
    ci->fd_event = NULL;
  }

  if(ci->process_event) {
    http_eventer_free(eventer_remove_recurrent(ci->process_event), ci);
    ci->process_event = NULL;
  }
  
  if (ci->status.reason) {
    free(ci->status.reason);
    ci->status.reason = NULL;
  }

  ci->check->flags &= ~NP_RUNNING;
  ci->timeout_event = NULL;
  free(ci->body.b);
  free(ci->headers.b);
  memset(ci, 0, sizeof(*ci));
}


static int http_init(noit_module_t *self) {
  return 0;
}

static void append_buf(buf_t *b,
                       const char *data, int len) {
  //noitL(nldeb, "append_buf(%p, %d)\n", b, len);
  b->b = realloc(b->l == 0 ? NULL : b->b, b->l + len + 1);
  memcpy(b->b + b->l, data, len);
  b->l += len;
  b->b[b->l] = '\0';
}

#define BODY_MAX_SIZE 1024 * 512

static size_t http_write_data( void *ptr, size_t size, size_t nmemb, void *baton)
{
  http_check_info_t *ci = baton;
  size_t len =  size * nmemb;
  size_t used = MIN(BODY_MAX_SIZE - ci->body.l, len);
  noitL(nldeb, "http_write_data(%p, %d)\n", ci, (int)used);
  if (used != 0) {
    append_buf(&ci->body, ptr, used);
  }
  return used;
}


static size_t http_write_headers( void *ptr, size_t size, size_t nmemb, void *baton)
{
  http_check_info_t *ci = baton;
  size_t len =  size * nmemb;
  size_t used = MIN(BODY_MAX_SIZE - ci->headers.l, len);
  noitL(nldeb, "http_write_headers(%p, %d)\n", ci, (int)used);
  if (used != 0) {
    append_buf(&ci->headers, ptr, used);
  }

  if (ci->status.code == 0) {
      /* HTTP/1.1 200 OK */
    if (ci->headers.l > strlen("HTTP/#.# ###")) {
      char *p = NULL;
      ci->status.version = HTTP_VERSION(ci->headers.b[5] - '0',  ci->headers.b[7] - '0');
      if (ci->status.version != HTTP_11 && ci->status.version != HTTP_10) {
        /* TODO: log error*/
        noitL(nldeb, "http_write_headers(%p) -- Invalid HTTP Version: %d\n", ci, ci->status.version);
        return 0;
      }
      
      ci->status.code = strtol(ci->headers.b + 8, &p, 10);
      if (ci->status.code != 0 && p) {
        while (*p && isspace(*p)) p++;
        if (*p) {
          ci->status.reason = strdup(p);
        }
      }
    }
  }
  return used;
}

static int http_handler(eventer_t e, int mask,
                        void *closure, struct timeval *now)
{
  int cmask = 0;
  int handles = 0;
  http_check_info_t *ci = closure;

  if (!ci->curl) {
    return 0;
  }

  if(mask & EVENTER_READ) cmask |= CURL_CSELECT_IN;
  if(mask & EVENTER_WRITE) cmask|= CURL_CSELECT_OUT;
  if(mask & EVENTER_EXCEPTION) cmask |= CURL_CSELECT_ERR;
  
  ci->inside_handler = 1;
  //noitL(nldeb, "http_handler(%p, emask=%d, cmask=%d)\n", ci, mask, cmask);
  curl_multi_socket_action(ci->mcurl, e->fd, cmask, &handles);
  ci->inside_handler = 0;

  return e->mask;
}

static int http_socket_cb(CURL *_curl, curl_socket_t fd, int action,
                          void *userp,
                          void *socketp)
{
  eventer_t e;
  http_check_info_t *ci =  (http_check_info_t *)userp;

  e = eventer_find_fd(fd);
  switch (action) {
    case CURL_POLL_INOUT:
    case CURL_POLL_OUT:
    case CURL_POLL_IN:
      if(!e) {
        ci->fd_event = e = eventer_alloc();
        e->fd = fd;
        e->callback = http_handler;
        e->closure = ci;
      }

      /* curl API don't have a command to look for err, but it actually
       * does want to know if there is one!
       */
      e->mask |= EVENTER_EXCEPTION;
      if (action == CURL_POLL_INOUT) e->mask |= EVENTER_READ|EVENTER_WRITE;
      if (action == CURL_POLL_OUT) e->mask |= EVENTER_WRITE;
      if (action == CURL_POLL_IN) e->mask |= EVENTER_READ;
      noitL(nldeb, "http_socket_cb(add) => %d, %x [%c%c%c]\n",
            fd, action,
            (e->mask & EVENTER_READ) ? 'I' : '-',
            (e->mask & EVENTER_WRITE) ? 'O' : '-',
            (e->mask & EVENTER_EXCEPTION) ? 'E' : '-');
      eventer_add(e);
      break;
    case CURL_POLL_REMOVE:
      noitL(nldeb, "http_socket_cb(remove) => %d\n", fd);
      if (e) {
        e->closure = NULL;
        e->mask = 0;
        if (ci->fd_event) {
          eventer_t te = eventer_remove_fd(ci->fd_event->fd);
          if (te && ci->inside_handler == 0) {
            http_eventer_free(te, ci);
          }
          else {
            te->mask = 0;
          }
          ci->fd_event = NULL;
        }
      }
      break;
    default:
      abort();
  }

  return 0;
}


static int http_all_done(eventer_t e, int mask,
                          void *closure, struct timeval *now)
{
  http_check_info_t *ci = closure;
  noitL(nldeb, "http_all_done(%p)\n", ci);
  generic_log_results(ci->self, ci->check);
  http_cleanup_check(ci->self, ci->check);
  return 0;
}

static int http_consume_messages(http_check_info_t *ci)
{
  CURLMsg *msg;
  int count;

  while ((msg = curl_multi_info_read(ci->mcurl, &count)) != NULL) {
    if (msg->msg == CURLMSG_DONE) {
      struct timeval when;
      long ret;

      noitL(nldeb, "http_consume_messages(%p) => DONE\n", ci);
      gettimeofday(&when, NULL);
      memcpy(&ci->finish_time, &when, sizeof(when));
      curl_easy_getinfo(ci->curl, CURLINFO_RESPONSE_CODE, &ret);
      if (ret == 0) {
        /* no HTTP transfer took place! */
        ci->timed_out = 1;
      }
      else {
        ci->timed_out = 0;
      }

      ci->done_event = eventer_alloc();;
      ci->done_event->mask = EVENTER_TIMER;
      memcpy(&ci->done_event->whence, &when, sizeof(when));
      ci->done_event->closure = ci;
      ci->done_event->callback = http_all_done;
      eventer_add(ci->done_event);
    }
  }

  return 0;
}


static int http_recurrent(eventer_t e, int mask,
                          void *closure, struct timeval *now)
{
  http_check_info_t *ci = closure;
  http_consume_messages(ci);
  return e->mask;
}

static int http_timeout(eventer_t e, int mask,
                         void *closure, struct timeval *now)
{
  int cc;
  int handles = 0;
  http_check_info_t *ci = closure;

  if (ci->mcurl == NULL) {
    ci->timeout_event = NULL;
    return 0;
  }
  
  noitL(nldeb, "http_timeout(%p)\n", ci);

  do {
    cc = curl_multi_socket(ci->mcurl, CURL_SOCKET_TIMEOUT, &handles);
  } while(cc == CURLM_CALL_MULTI_PERFORM && handles != 0);
  
  ci->timeout_event = NULL;
  return 0;
}  

static int http_set_timeout_cb(CURLM *_curlm, long timeoutms, void *closure)
{
  struct timeval when, p_int;
  http_check_info_t *ci = closure;

  noitL(nldeb, "http_set_timeout_cb(%p, %d)\n", ci, (int)timeoutms);
  
  if (ci->timeout_event != NULL) {
    http_eventer_free(eventer_remove(ci->timeout_event), ci);
    ci->timeout_event = NULL;
  }

  ci->timeout_event = eventer_alloc();
  ci->timeout_event->mask = EVENTER_TIMER;
  gettimeofday(&when, NULL);
  p_int.tv_sec = timeoutms / 1000;
  p_int.tv_usec = (timeoutms % 1000) * 1000;
  add_timeval(when, p_int, &ci->timeout_event->whence);
  ci->timeout_event->closure = ci;
  ci->timeout_event->callback = http_timeout;
  eventer_add(ci->timeout_event);
  
  return 0;
}

static int http_initiate(noit_module_t *self, noit_check_t *check) {
  char buf[1024];
  struct timeval when;
  http_check_info_t *ci;
  http_module_conf_t *mod_config;
  void *config_url;
  char *urlstr;

  mod_config = noit_module_get_userdata(self);
  ci = (http_check_info_t *)check->closure;
  /* We cannot be running */
  if (check->flags & NP_RUNNING) {
    generic_log_results(ci->self, ci->check);
    http_cleanup_check(ci->self, ci->check);
    assert(!(check->flags & NP_RUNNING));
  }
  check->flags |= NP_RUNNING;

  /* remove a timeout if we still have one -- we should unless someone
   * has set a lower timeout than the period.
   */
  ci->timed_out = 1;
  if(ci->timeout_event) {
    http_eventer_free(eventer_remove(ci->timeout_event), ci);
    ci->timeout_event = NULL;
  }

  gettimeofday(&when, NULL);
  memcpy(&check->last_fire_time, &when, sizeof(when));
  ci->finish_time.tv_sec = ci->finish_time.tv_usec = 0L;

  ci->self = self;
  ci->check = check;

  if(!noit_hash_retrieve(check->config, "url", strlen("url"), &config_url)) {
    if(!mod_config->options ||
       !noit_hash_retrieve(mod_config->options, "url", strlen("url"),
                           &config_url)) {
      config_url = "http://localhost/";
    }
  }

  ci->url = xmlParseURI(config_url);

  if (!ci->url->scheme) {
    ci->url->scheme = strdup("http");
  }

  if (!ci->url->port) {
    if (strcmp("http", ci->url->scheme) == 0) {
      ci->url->port = 80;
    }
    else if (strcmp("https", ci->url->scheme) == 0) {
      ci->url->port = 443;
    }
    else {
      ci->url->port = 80;
    }
  }

  if (!ci->url->path) {
    ci->url->path = strdup("/");
  }

  if (strcasecmp(ci->url->scheme, "https") == 0) {
#if 0
    /* TODO: Custom CA validation */
    void *vstr;
    http_module_conf_t *conf;
    conf = noit_module_get_userdata(self);
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
#endif
  }

  snprintf(buf, sizeof(buf), "Host: %s", ci->url->server);
  ci->cheaders = curl_slist_append(ci->cheaders, buf);
  ci->cheaders = curl_slist_append(ci->cheaders,   "Accept-Encoding: deflate,gzip");

  ci->curl = curl_easy_init();

  curl_easy_setopt(ci->curl, CURLOPT_NOSIGNAL, 0);
  curl_easy_setopt(ci->curl, CURLOPT_WRITEFUNCTION, http_write_data);
  curl_easy_setopt(ci->curl, CURLOPT_WRITEDATA, ci);
  curl_easy_setopt(ci->curl, CURLOPT_HEADERFUNCTION, http_write_headers);
  curl_easy_setopt(ci->curl, CURLOPT_HEADERDATA, ci);

  free(ci->url->server);
  ci->url->server = strdup(check->target);
  urlstr = (char *)xmlSaveUri(ci->url);
  curl_easy_setopt(ci->curl, CURLOPT_URL, urlstr);
  noitL(nldeb, "http_initiate(%p,%s,url=%s)\n",
        ci, check->target, urlstr);
  xmlFreeURI(ci->url);
  free(urlstr);
  ci->url = NULL;


  curl_easy_setopt(ci->curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP|CURLPROTO_HTTPS);
  curl_easy_setopt(ci->curl, CURLOPT_FOLLOWLOCATION, 0);
  curl_easy_setopt(ci->curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP);

  curl_easy_setopt(ci->curl, CURLOPT_USERAGENT, "Noit/" NOIT_HTTP_VERSION_STRING);
  curl_easy_setopt(ci->curl, CURLOPT_HTTPHEADER, ci->cheaders);

  curl_easy_setopt(ci->curl, CURLOPT_FRESH_CONNECT, 1);
  curl_easy_setopt(ci->curl, CURLOPT_FORBID_REUSE, 1);

  /* TODO: more SSL options */
  curl_easy_setopt(ci->curl, CURLOPT_SSL_VERIFYPEER, 0);
  curl_easy_setopt(ci->curl, CURLOPT_SSL_VERIFYHOST, 0);
  curl_easy_setopt(ci->curl, CURLOPT_TIMEOUT_MS, check->timeout);
  curl_easy_setopt(ci->curl, CURLOPT_CONNECTTIMEOUT_MS, check->timeout);

  /* TODO: Consider re-using the multi-init */
  ci->mcurl = curl_multi_init();
  curl_multi_setopt(ci->mcurl, CURLMOPT_SOCKETFUNCTION, http_socket_cb);
  curl_multi_setopt(ci->mcurl, CURLMOPT_SOCKETDATA, ci);
  curl_multi_setopt(ci->mcurl, CURLMOPT_PIPELINING, 0);
  
  curl_multi_setopt(ci->mcurl, CURLMOPT_TIMERFUNCTION, http_set_timeout_cb);
  curl_multi_setopt(ci->mcurl, CURLMOPT_TIMERDATA, ci);

  curl_multi_add_handle(ci->mcurl, ci->curl);

  {
    int running_handles = 0;
    while (curl_multi_perform(ci->mcurl, &running_handles) == CURLM_CALL_MULTI_PERFORM);
  }

  ci->process_event = eventer_alloc();
  ci->process_event->closure = ci;
  ci->process_event->mask = EVENTER_RECURRENT;
  ci->process_event->callback = http_recurrent;

  eventer_add_recurrent(ci->process_event);

  return 0;
}
static int http_initiate_check(noit_module_t *self, noit_check_t *check,
                               int once, noit_check_t *cause) {
  if(!check->closure) check->closure = calloc(1, sizeof(http_check_info_t));
  INITIATE_CHECK(http_initiate, self, check);
  return 0;
}
static int resmon_initiate_check(noit_module_t *self, noit_check_t *check,
                                 int once, noit_check_t *parent) {
  /* resmon_check_info_t gives us a bit more space */
  if(!check->closure) check->closure = calloc(1, sizeof(resmon_check_info_t));
  INITIATE_CHECK(http_initiate, self, check);
  return 0;
}

static void resmon_cleanup_check(noit_module_t *self, noit_check_t *check) {
  resmon_check_info_t *rci;
  rci = check->closure;
  if(rci) {
    if(rci->xpathexpr) free(rci->xpathexpr);
    if(rci->resmod) free(rci->resmod);
    if(rci->resserv) free(rci->resserv);
    if(rci->xml_doc) xmlFreeDoc(rci->xml_doc);
    http_cleanup_check(self, check);
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
    http_check_info_t *ci = &rci->http;
    gettimeofday(&ci->finish_time, NULL);
    resmon_part_log_results(self, check, parent);
    return 0;
  }
  INITIATE_CHECK(http_initiate, self, check);
  return 0;
}

static void http_eventer_free(eventer_t e, http_check_info_t *ci)
{
  noitL(nldeb, "http_eventer_free(%p)\n", e);
  if (!e)
    return;
  eventer_free(e);
  return;
}


static int http_onload(noit_image_t *self) {
  curl_global_init(CURL_GLOBAL_ALL);
  atexit(curl_global_cleanup);
  
  nlerr = noit_log_stream_find("error/http");
  nldeb = noit_log_stream_find("debug/http");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;

  eventer_name_callback("http/http_handler", http_handler);
  eventer_name_callback("http/http_timeout", http_timeout);
  eventer_name_callback("http/http_all_done", http_all_done);
  eventer_name_callback("http/http_recurrent", http_recurrent);
  return 0;
}

#include "http.xmlh"
noit_module_t http = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "http",
    "libcurl-based HTTP and HTTPS resource checker",
    http_xml_description,
    http_onload
  },
  http_config,
  http_init,
  http_initiate_check,
  http_cleanup_check
};

#include "resmon.xmlh"
noit_module_t resmon = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "resmon",
    "libserf-based resmon resource checker",
    resmon_xml_description,
    http_onload
  },
  resmon_config,
  http_init,
  resmon_initiate_check,
  resmon_cleanup_check
};

#include "resmon_part.xmlh"
noit_module_t resmon_part = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "resmon_part",
    "resmon part resource checker",
    resmon_part_xml_description,
    http_onload
  },
  resmon_config,
  http_init,
  resmon_part_initiate_check,
  resmon_cleanup_check
};

