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
#include "eventer/eventer.h"
#include "noit_listener.h"
#include "utils/noit_hash.h"
#include "utils/noit_log.h"
#include "jlog/jlog.h"
#include "noit_jlog_listener.h"

#include <unistd.h>
#define MAX_ROWS_AT_ONCE 1000
#define DEFAULT_SECONDS_BETWEEN_BATCHES 10

static noit_atomic32_t tmpfeedcounter = 0;

void
noit_jlog_listener_init() {
  eventer_name_callback("log_transit/1.0", noit_jlog_handler);
  noit_control_dispatch_delegate(noit_control_dispatch,
                                 NOIT_JLOG_DATA_FEED,
                                 noit_jlog_handler);
  noit_control_dispatch_delegate(noit_control_dispatch,
                                 NOIT_JLOG_DATA_TEMP_FEED,
                                 noit_jlog_handler);
}

typedef struct {
  jlog_ctx *jlog;
  char *subscriber;
  jlog_id chkpt;
  jlog_id start;
  jlog_id finish;
  int count;
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
  if(jcl->jlog) {
    if(jcl->subscriber) {
      if(jcl->subscriber[0] == '~')
        jlog_ctx_remove_subscriber(jcl->jlog, jcl->subscriber);
      free(jcl->subscriber);
    }
    jlog_ctx_close(jcl->jlog);
  }
  free(jcl);
}

static int
__safe_Ewrite(eventer_t e, void *b, int l, int *mask) {
  int w, sofar = 0;
  while(l > sofar) {
    w = e->opset->write(e->fd, (char *)b + sofar, l - sofar, mask, e);
    if(w <= 0) return w;
    sofar += w;
  }
  return sofar;
}
#define Ewrite(a,b) __safe_Ewrite(e,a,b,&mask)

static int
noit_jlog_push(eventer_t e, noit_jlog_closure_t *jcl) {
  jlog_message msg;
  int mask;
  u_int32_t n_count;
  n_count = htonl(jcl->count);
  if(Ewrite(&n_count, sizeof(n_count)) != sizeof(n_count))
    return -1;
  while(jcl->count > 0) {
    int rv;
    struct { jlog_id chkpt; u_int32_t n_sec, n_usec, n_len; } payload;
    if(jlog_ctx_read_message(jcl->jlog, &jcl->start, &msg) == -1)
      return -1;

    /* Here we actually push the message */
    payload.chkpt.log = htonl(jcl->start.log);
    payload.chkpt.marker = htonl(jcl->start.marker);
    payload.n_sec  = htonl(msg.header->tv_sec);
    payload.n_usec = htonl(msg.header->tv_usec);
    payload.n_len  = htonl(msg.mess_len);
    if((rv = Ewrite(&payload, sizeof(payload))) != sizeof(payload)) {
      noitL(noit_error, "Error writing jlog header over SSL %d != %d\n",
            rv, (int)sizeof(payload));
      return -1;
    }
    if((rv = Ewrite(msg.mess, msg.mess_len)) != msg.mess_len) {
      noitL(noit_error, "Error writing jlog message over SSL %d != %d\n",
            rv, msg.mess_len);
      return -1;
    }
    /* Note what the client must checkpoint */
    jcl->chkpt = jcl->start;

    JLOG_ID_ADVANCE(&jcl->start);
    jcl->count--;
  }
  return 0;
}

void *
noit_jlog_thread_main(void *e_vptr) {
  int mask, bytes_read;
  eventer_t e = e_vptr;
  acceptor_closure_t *ac = e->closure;
  noit_jlog_closure_t *jcl = ac->service_ctx;
  char inbuff[sizeof(jlog_id)];

  eventer_set_fd_blocking(e->fd);

  while(1) {
    jlog_id client_chkpt;
    int sleeptime = (ac->cmd == NOIT_JLOG_DATA_TEMP_FEED) ?
                      1 : DEFAULT_SECONDS_BETWEEN_BATCHES;
    jlog_get_checkpoint(jcl->jlog, ac->remote_cn, &jcl->chkpt);
    jcl->count = jlog_ctx_read_interval(jcl->jlog, &jcl->start, &jcl->finish);
    if(jcl->count > MAX_ROWS_AT_ONCE) {
      /* Artificially set down the range to make the batches a bit easier
       * to handle on the stratcond/postgres end.
       * However, we must have more data, so drop the sleeptime to 0
       */
      jcl->count = MAX_ROWS_AT_ONCE;
      jcl->finish.marker = jcl->start.marker + jcl->count;
      sleeptime = 0;
    }
    if(jcl->count > 0) {
      if(noit_jlog_push(e, jcl)) {
        goto alldone;
      }
      /* Read our jlog_id accounting for possibly short reads */
      bytes_read = 0;
      while(bytes_read < sizeof(jlog_id)) {
        int len;
        if((len = e->opset->read(e->fd, inbuff + bytes_read,
                                 sizeof(jlog_id) - bytes_read,
                                 &mask, e)) <= 0)
          goto alldone;
        bytes_read += len;
      }
      memcpy(&client_chkpt, inbuff, sizeof(jlog_id));
      /* Fix the endian */
      client_chkpt.log = ntohl(client_chkpt.log);
      client_chkpt.marker = ntohl(client_chkpt.marker);
  
      if(memcmp(&jcl->chkpt, &client_chkpt, sizeof(jlog_id))) {
        noitL(noit_error,
              "client %s submitted invalid checkpoint %u:%u expected %u:%u\n",
              ac->remote_cn, client_chkpt.log, client_chkpt.marker,
              jcl->chkpt.log, jcl->chkpt.marker);
        goto alldone;
      }
      jlog_ctx_read_checkpoint(jcl->jlog, &jcl->chkpt);
    }
    if(sleeptime) sleep(sleeptime);
  }

 alldone:
  e->opset->close(e->fd, &mask, e);
  if(jcl) noit_jlog_closure_free(jcl);
  if(ac) acceptor_closure_free(ac);
  return NULL;
}

int
noit_jlog_handler(eventer_t e, int mask, void *closure,
                     struct timeval *now) {
  eventer_t newe;
  pthread_t tid;
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
    char path[PATH_MAX], subscriber[256], *sub;
    jcl = ac->service_ctx = noit_jlog_closure_alloc();
    if(!noit_hash_retr_str(ac->config,
                           "log_transit_feed_name",
                           strlen("log_transit_feed_name"),
                           &logname)) {
      noitL(noit_error, "No 'log_transit_feed_name' specified in log_transit.\n");
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
    if(ac->cmd == NOIT_JLOG_DATA_FEED) {
      if(!ac->remote_cn) {
        noitL(noit_error, "jlog transit started to unidentified party.\n");
        goto socket_error;
      }
      strlcpy(subscriber, ac->remote_cn, sizeof(subscriber));
    }
    else {
      snprintf(subscriber, sizeof(subscriber),
               "~%07d", noit_atomic_inc32(&tmpfeedcounter));
    }
    jcl->subscriber = strdup(subscriber);

    strlcpy(path, ls->path, sizeof(path));
    sub = strchr(path, '(');
    if(sub) {
      char *esub = strchr(sub, ')');
      if(esub) {
        *esub = '\0';
        *sub = '\0';
        sub += 1;
      }
    }

    jcl->jlog = jlog_new(path);
    if(ac->cmd == NOIT_JLOG_DATA_TEMP_FEED)
      if(jlog_ctx_add_subscriber(jcl->jlog, jcl->subscriber, JLOG_END) == -1)
        noitL(noit_error, "jlog reader[%s] error: %s\n", jcl->subscriber,
              jlog_ctx_err_string(jcl->jlog));
    if(jlog_ctx_open_reader(jcl->jlog, jcl->subscriber) == -1) {
      noitL(noit_error, "jlog reader[%s] error: %s\n", jcl->subscriber,
            jlog_ctx_err_string(jcl->jlog));
      goto socket_error;
    }
  }

  /* The jlog stuff is disk I/O and can block us.
   * We'll create a new thread to just handle this connection.
   */
  eventer_remove_fd(e->fd);
  newe = eventer_alloc();
  memcpy(newe, e, sizeof(*e));
  if(pthread_create(&tid, NULL, noit_jlog_thread_main, newe) == 0) {
    return 0;
  }

  /* Undo our dup */
  eventer_free(newe);
  /* Creating the thread failed, close it down and deschedule. */
  e->opset->close(e->fd, &newmask, e);
  return 0;
}
