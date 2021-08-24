/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
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
#include <eventer/eventer.h>
#include <mtev_listener.h>
#include <mtev_memory.h>
#include <mtev_sem.h>

#include "noit_mtev_bridge.h"
#include "noit_livestream_listener.h"
#include "noit_check.h"

#include <unistd.h>
#include <errno.h>

static uint32_t ls_counter = 0;

struct log_entry {
  int len;
  char *buff;
  struct log_entry *next;
};

typedef struct {
  uint32_t period;
  mtev_boolean period_read;
  struct log_entry *lqueue;
  struct log_entry *lqueue_end;
  sem_t lqueue_sem;
  pthread_mutex_t lqueue_lock;
  int uuid_read;
  char uuid_str[37];
  char *feed;
  uuid_t uuid;
  noit_check_t *check;
  int wants_shutdown;
  mtev_log_stream_t log_stream;
} noit_livestream_closure_t;

noit_livestream_closure_t *
noit_livestream_closure_alloc(void) {
  noit_livestream_closure_t *jcl;
  jcl = calloc(1, sizeof(*jcl));
  pthread_mutex_init(&jcl->lqueue_lock, NULL);
  sem_init(&jcl->lqueue_sem, 0, 0);
  return jcl;
}

void
noit_livestream_closure_free(noit_livestream_closure_t *jcl) {
  struct log_entry *tofree;
  while(jcl->lqueue) {
    tofree = jcl->lqueue;
    jcl->lqueue = jcl->lqueue->next;
    free(tofree->buff);
    free(tofree);
  }
  noit_check_deref(jcl->check);
  free(jcl);
}

static int
noit_livestream_logio_open(mtev_log_stream_t ls) {
  return 0;
}
static int
noit_livestream_logio_reopen(mtev_log_stream_t ls) {
  /* no op */
  return 0;
}
static int
noit_livestream_logio_write(mtev_log_stream_t ls, const struct timeval *whence,
                            const void *buf, size_t len) {
  noit_livestream_closure_t *jcl;
  struct log_entry *le;
  (void)whence;

  jcl = mtev_log_stream_get_ctx(ls);
  if(!jcl) return 0;

  if(jcl->wants_shutdown) {
    /* This has been terminated by the client, _fail here_ */
    return 0;
  }

  le = calloc(1, sizeof(*le));
  le->len = len;
  le->buff = malloc(len);
  memcpy(le->buff, buf, len);
  le->next = NULL;
  pthread_mutex_lock(&jcl->lqueue_lock);
  if(!jcl->lqueue_end) jcl->lqueue = le;
  else jcl->lqueue_end->next = le;
  jcl->lqueue_end = le;
  pthread_mutex_unlock(&jcl->lqueue_lock);
  sem_post(&jcl->lqueue_sem);
  return len;
}
static int
noit_livestream_logio_close(mtev_log_stream_t ls) {
  noit_livestream_closure_t *jcl;
  jcl = mtev_log_stream_get_ctx(ls);
  if(jcl) noit_livestream_closure_free(jcl);
  mtev_log_stream_set_ctx(ls, NULL);
  return 0;
}
static logops_t noit_livestream_logio_ops = {
  mtev_false,
  noit_livestream_logio_open,
  noit_livestream_logio_reopen,
  noit_livestream_logio_write,
  NULL,
  noit_livestream_logio_close,
  NULL,
  NULL
};

void
noit_livestream_listener_init() {
  mtev_register_logops("noit_livestream", &noit_livestream_logio_ops);
  eventer_name_callback("livestream_transit/1.0", noit_livestream_handler);
  mtev_control_dispatch_delegate(mtev_control_dispatch,
                                 NOIT_LIVESTREAM_DATA_FEED,
                                 noit_livestream_handler);
}

static int
__safe_Ewrite(eventer_t e, void *b, int l, int *mask) {
  int w, sofar = 0;
  while(l > sofar) {
    w = eventer_write(e, (char *)b + sofar, l - sofar, mask);
    if(w <= 0) return w;
    sofar += w;
  }
  return sofar;
}
#define Ewrite(a,b) __safe_Ewrite(e,a,b,&mask)

void *
noit_livestream_thread_main(void *e_vptr) {
  int mask;
  eventer_t e = e_vptr;
  mtev_acceptor_closure_t *ac = eventer_get_closure(e);
  noit_livestream_closure_t *jcl = mtev_acceptor_closure_ctx(ac);
  struct log_entry *le = NULL;

  mtev_memory_init_thread();
  /* Go into blocking mode */
  if(eventer_set_fd_blocking(eventer_get_fd(e)) == -1) {
    mtevL(noit_error, "failed setting livestream to blocking: [%d] [%s]\n",
          errno, strerror(errno));
    goto alldone;
  }

  while(1) {
    uint32_t netlen;
    int rv;
    le = NULL;

    sem_wait(&jcl->lqueue_sem);
    pthread_mutex_lock(&jcl->lqueue_lock);
    if(jcl->lqueue) {
      /* If there are items, pop and advance the header pointer */
      le = jcl->lqueue;
      jcl->lqueue = jcl->lqueue->next;
      if(!jcl->lqueue) jcl->lqueue_end = NULL;
    }
    pthread_mutex_unlock(&jcl->lqueue_lock);

    if(!le) continue;

    /* Here we actually push the message */
    netlen = htonl(le->len);
    if((rv = Ewrite(&netlen, sizeof(netlen))) != sizeof(netlen)) {
      mtevL(noit_error, "Error writing le header over SSL %d != %d\n",
            rv, (int)sizeof(netlen));
      goto alldone;
    }
    if((rv = Ewrite(le->buff, le->len)) != le->len) {
      mtevL(noit_error, "Error writing livestream message over SSL %d != %d\n",
            rv, le->len);
      goto alldone;
    }
    if (le->buff) free(le->buff);
    free(le);
  }

 alldone:
  if (le) {
    if (le->buff) free(le->buff);
    free(le);
  }
  eventer_close(e, &mask);
  jcl->wants_shutdown = 1;
  mtev_acceptor_closure_free(ac);
  mtev_memory_maintenance();
  /* Our semaphores are counting semaphores, not locks. */
  /* coverity[missing_unlock] */
  return NULL;
}

int
noit_livestream_handler(eventer_t e, int mask, void *closure,
                        struct timeval *now) {
  eventer_t newe;
  pthread_t tid;
  pthread_attr_t tattr;
  int newmask = EVENTER_READ | EVENTER_EXCEPTION;
  mtev_acceptor_closure_t *ac = closure;
  noit_livestream_closure_t *jcl = mtev_acceptor_closure_ctx(ac);

  if(mask & EVENTER_EXCEPTION || (jcl && jcl->wants_shutdown)) {
socket_error:
    /* Exceptions cause us to simply snip the connection */
    eventer_remove_fde(e);
    eventer_close(e, &newmask);
    mtev_log_stream_t ls = jcl->log_stream;
    /* will free the noit_livestream_closure_t */
    mtev_log_stream_close(ls);
    mtev_log_stream_free(ls);
    mtev_acceptor_closure_set_ctx(ac, NULL, NULL);
    mtev_acceptor_closure_free(ac);
    return 0;
  }

  if(!jcl || !jcl->feed) {
    int len;
    if(!jcl) {
      jcl = noit_livestream_closure_alloc();
      mtev_acceptor_closure_set_ctx(ac, jcl, NULL);
    }
    /* Setup logger to this channel */
    mtevL(noit_debug, "livestream initializing on fd %d\n", eventer_get_fd(e));
    if(!jcl->period_read) {
      uint32_t nperiod;
      len = eventer_read(e, &nperiod, sizeof(nperiod), &mask);
      if(len == -1 && errno == EAGAIN) return mask | EVENTER_EXCEPTION;
      if(len != sizeof(nperiod)) goto socket_error;
      jcl->period = ntohl(nperiod);
      jcl->period_read = mtev_true;
      mtevL(noit_debug, "livestream initializing on fd %d [period %d]\n",
            eventer_get_fd(e), jcl->period);
    }
    while(jcl->uuid_read < 36) {
      len = eventer_read(e, jcl->uuid_str + jcl->uuid_read, 36 - jcl->uuid_read, &mask);
      if(len == -1 && errno == EAGAIN) return mask | EVENTER_EXCEPTION;
      if(len == 0) goto socket_error;
      jcl->uuid_read += len;
    }
    jcl->uuid_str[36] = '\0';
    if(mtev_uuid_parse(jcl->uuid_str, jcl->uuid)) {
      mtevL(noit_error, "bad uuid received in livestream handler '%s'\n", jcl->uuid_str);
      goto socket_error;
    }
    mtevL(noit_debug, "livestream initializing on fd %d [uuid %s]\n",
          eventer_get_fd(e), jcl->uuid_str);

    jcl->feed = malloc(32);
    uint32_t next = ck_pr_faa_32(&ls_counter, 1) + 1;
    snprintf(jcl->feed, 32, "livestream/%u", next);
    jcl->log_stream = mtev_log_stream_new(jcl->feed, "noit_livestream", jcl->feed,
                                          jcl, NULL);


    jcl->check = noit_check_watch(jcl->uuid, jcl->period);
    if(!jcl->check) {
      eventer_close(e, &newmask);
      return 0;
    }
    /* This check must be watched from the livestream */
    noit_check_transient_add_feed(jcl->check, jcl->feed);
    /* Note the check */
    noit_check_log_check(jcl->check);
    /* kick it off, if it isn't running already */
    if(!NOIT_CHECK_LIVE(jcl->check)) noit_check_activate(jcl->check);
  }

  eventer_remove_fde(e);
  newe = eventer_alloc_copy(e);
  pthread_attr_init(&tattr);
  pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
  if(pthread_create(&tid, &tattr, noit_livestream_thread_main, newe) == 0) {
    return 0;
  }

  noit_check_transient_remove_feed(jcl->check, jcl->feed);
  mtev_log_stream_t ls = jcl->log_stream;
  mtev_log_stream_close(ls);
  mtev_log_stream_free(ls);
  mtev_acceptor_closure_set_ctx(ac, NULL, NULL);
  /* Undo our dup */
  eventer_free(newe);
  /* Creating the thread failed, close it down and deschedule. */
  eventer_close(e, &newmask);
  return 0;
}
