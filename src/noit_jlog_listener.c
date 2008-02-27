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

#include <unistd.h>
#include <sys/ioctl.h>

void
noit_jlog_listener_init() {
  eventer_name_callback("log_transit", noit_jlog_handler);
}

typedef struct {
  jlog_ctx *jlog;
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
  if(jcl->jlog) jlog_ctx_close(jcl->jlog);
  free(jcl);
}

#define Ewrite(a,b) e->opset->write(e->fd, a, b, &mask, e)
static int
noit_jlog_push(eventer_t e, noit_jlog_closure_t *jcl) {
  jlog_message msg;
  int mask;
  u_int32_t n_count;
  n_count = htonl(jcl->count);
  if(Ewrite(&n_count, sizeof(n_count)) != sizeof(n_count))
    return -1;
  while(jcl->count > 0) {
    struct { u_int32_t n_sec, n_usec, n_len; } payload;
    if(jlog_ctx_read_message(jcl->jlog, &jcl->start, &msg) == -1)
      return -1;

    /* Here we actually push the message */
    payload.n_sec  = htonl(msg.header->tv_sec);
    payload.n_usec = htonl(msg.header->tv_usec);
    payload.n_len  = htonl(msg.mess_len);
    if(Ewrite(&payload, sizeof(payload)) != sizeof(payload))
      return -1;
    if(Ewrite(msg.mess, msg.mess_len) != msg.mess_len)
      return -1;
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
  long off = 0;
  char inbuff[sizeof(jlog_id)];

  /* Go into blocking mode */
  ioctl(e->fd, FIONBIO, &off);

  while(1) {
    jlog_id client_chkpt;
    jlog_get_checkpoint(jcl->jlog, ac->remote_cn, &jcl->chkpt);
    jcl->count = jlog_ctx_read_interval(jcl->jlog, &jcl->start, &jcl->finish);
    if(jcl->count > 0) {
      if(noit_jlog_push(e, jcl)) {
        goto alldone;
      }
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
    sleep(5);
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
    if(!ac->remote_cn) {
      noitL(noit_error, "jlog transit started to unidentified party.\n");
      goto socket_error;
    }

    jcl->jlog = jlog_new(ls->path);
    if(jlog_ctx_open_reader(jcl->jlog, ac->remote_cn) == -1) {
      noitL(noit_error, "jlog reader[%s] error: %s\n", ac->remote_cn,
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
