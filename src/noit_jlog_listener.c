/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "noit_listener.h"
#include "utils/noit_hash.h"
#include "utils/noit_log.h"
#include "jlog/jlog.h"
#include "noit_jlog_listener.h"

void
noit_jlog_listener_init() {
  eventer_name_callback("log_transit", noit_jlog_handler);
}

typedef struct {
  jlog_ctx *jlog;
  int wants_shutdown;
} noit_jlog_closure_t;

noit_jlog_closure_t *
noit_jlog_closure_alloc(void) {
  noit_jlog_closure_t *jcl;
  jcl = calloc(1, sizeof(*jcl));
  return jcl;
}

void
noit_jlog_closure_free(noit_jlog_closure_t *jcl) {
  if(jcl->jlog) jlog_ctx_close(jcl->jlog);
  free(jcl);
}

int
noit_jlog_handler(eventer_t e, int mask, void *closure,
                     struct timeval *now) {
  int newmask = EVENTER_READ | EVENTER_EXCEPTION;
  acceptor_closure_t *ac = closure;
  noit_jlog_closure_t *jcl = ac->service_ctx;

  if(mask & EVENTER_EXCEPTION || (jcl && jcl->wants_shutdown)) {
socket_error:
    /* Exceptions cause us to simply snip the connection */
    eventer_remove_fd(e->fd);
    e->opset->close(e->fd, &newmask, e);
    if(jcl) noit_jlog_closure_free(jcl);
    if(ac) acceptor_closure_free(ac);
    return 0;
  }

  if(!ac->service_ctx) {
    noit_log_stream_t ls;
    const char *logname;
    jcl = ac->service_ctx = noit_jlog_closure_alloc();
    if(!noit_hash_retrieve(ac->config, "log", strlen("log"),
                           (void **)&logname)) {
      noitL(noit_error, "No 'log' specified in log_transit.\n");
      goto socket_error;
    }
    ls = noit_log_stream_find(logname);
    if(!ls) {
      noitL(noit_error, "Could not find log '%s' for log_transit.\n",
            logname);
      goto socket_error;
    }
    if(!ls->type || strcmp(ls->type, "jlog")) {
      noitL(noit_error, "Log '%s' for log_transit is not a jlog.\n",
            logname);
      goto socket_error;
    }
  }

  e->opset->close(e->fd, &newmask, e);
  eventer_remove_fd(e->fd);
  return 0;
}
