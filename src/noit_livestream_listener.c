/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "noit_listener.h"
#include "utils/noit_hash.h"
#include "utils/noit_log.h"
#include "utils/noit_sem.h"
#include "noit_livestream_listener.h"
#include "noit_check.h"

#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

static noit_atomic32_t ls_counter = 0;

struct log_entry {
  int len;
  char *buff;
  struct log_entry *next;
};

typedef struct {
  u_int32_t period;
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
  free(jcl);
}

static int
noit_livestream_logio_open(noit_log_stream_t ls) {
  return 0;
}
static int
noit_livestream_logio_reopen(noit_log_stream_t ls) {
  /* no op */
  return 0;
}
static int
noit_livestream_logio_write(noit_log_stream_t ls, const void *buf, size_t len) {
  noit_livestream_closure_t *jcl = ls->op_ctx;
  struct log_entry *le;
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
noit_livestream_logio_close(noit_log_stream_t ls) {
  noit_livestream_closure_t *jcl = ls->op_ctx;
  if(jcl) noit_livestream_closure_free(jcl);
  ls->op_ctx = NULL;
  return 0;
}
static logops_t noit_livestream_logio_ops = {
  noit_livestream_logio_open,
  noit_livestream_logio_reopen,
  noit_livestream_logio_write,
  noit_livestream_logio_close,
  NULL
};

void
noit_livestream_listener_init() {
  noit_register_logops("noit_livestream", &noit_livestream_logio_ops);
  eventer_name_callback("livestream_transit", noit_livestream_handler);
  noit_control_dispatch_delegate(noit_control_dispatch,
                                 NOIT_LIVESTREAM_DATA_FEED,
                                 noit_livestream_handler);
}

static int
__safe_Ewrite(eventer_t e, void *b, int l, int *mask) {
  int w, sofar = 0;
  while(l > sofar) {
    w = e->opset->write(e->fd, b + sofar, l - sofar, mask, e);
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
  acceptor_closure_t *ac = e->closure;
  noit_livestream_closure_t *jcl = ac->service_ctx;
  long off = 0;

  /* Go into blocking mode */
  if(ioctl(e->fd, FIONBIO, &off) == -1) {
    noitL(noit_error, "ioctl failed setting livestream to blocking: [%d] [%s]\n",
          errno, strerror(errno));
    goto alldone;
  }

  while(1) {
    u_int32_t netlen;
    struct log_entry *le = NULL;
    int rv;
   
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
      noitL(noit_error, "Error writing le header over SSL %d != %d\n",
            rv, (int)sizeof(netlen));
      goto alldone;
    }
    if((rv = Ewrite(le->buff, le->len)) != le->len) {
      noitL(noit_error, "Error writing livestream message over SSL %d != %d\n",
            rv, le->len);
      goto alldone;
    }
  }

 alldone:
  e->opset->close(e->fd, &mask, e);
  jcl->wants_shutdown = 1;
  if(ac) acceptor_closure_free(ac);
  return NULL;
}

int
noit_livestream_handler(eventer_t e, int mask, void *closure,
                        struct timeval *now) {
  eventer_t newe;
  pthread_t tid;
  int newmask = EVENTER_READ | EVENTER_EXCEPTION;
  acceptor_closure_t *ac = closure;
  noit_livestream_closure_t *jcl = ac->service_ctx;

  if(mask & EVENTER_EXCEPTION || (jcl && jcl->wants_shutdown)) {
socket_error:
    /* Exceptions cause us to simply snip the connection */
    eventer_remove_fd(e->fd);
    e->opset->close(e->fd, &newmask, e);
    if(jcl) noit_livestream_closure_free(jcl);
    if(ac) acceptor_closure_free(ac);
    return 0;
  }

  if(!ac->service_ctx || !jcl->feed) {
    int len;
    jcl = ac->service_ctx = noit_livestream_closure_alloc();
    /* Setup logger to this channel */
    if(!jcl->period) {
      u_int32_t nperiod;
      len = e->opset->read(e->fd, &nperiod, sizeof(nperiod), &mask, e);
      if(len == -1 && errno == EAGAIN) return mask | EVENTER_EXCEPTION;
      if(len != sizeof(nperiod)) goto socket_error;
      jcl->period = ntohl(nperiod);
      if(!jcl->period) {
        noitL(noit_error, "period of 0 specified in livestream.  not allowed.\n");
        goto socket_error;
      }
    }
    while(jcl->uuid_read < 36) {
      len = e->opset->read(e->fd, jcl->uuid_str + jcl->uuid_read, 36 - jcl->uuid_read, &mask, e);
      if(len == -1 && errno == EAGAIN) return mask | EVENTER_EXCEPTION;
      if(len == 0) goto socket_error;
      jcl->uuid_read += len;
    }
    jcl->uuid_str[36] = '\0';
    if(uuid_parse(jcl->uuid_str, jcl->uuid)) {
      noitL(noit_error, "bad uuid received in livestream handler '%s'\n", jcl->uuid_str);
      goto socket_error;
    }

    jcl->feed = malloc(32);
    snprintf(jcl->feed, 32, "livestream/%d", noit_atomic_inc32(&ls_counter));
    noit_log_stream_new(jcl->feed, "noit_livestream", jcl->feed,
                        jcl, NULL);


    jcl->check = noit_check_watch(jcl->uuid, jcl->period);
    /* This check must be watched from the livestream */
    noit_check_transient_add_feed(jcl->check, jcl->feed);
    /* Note the check */
    noit_check_log_check(jcl->check);
    /* kick it off, if it isn't running already */
    if(!NOIT_CHECK_LIVE(jcl->check)) noit_check_activate(jcl->check);
  }

  eventer_remove_fd(e->fd);
  newe = eventer_alloc();
  memcpy(newe, e, sizeof(*e));
  if(pthread_create(&tid, NULL, noit_livestream_thread_main, newe) == 0) {
    return 0;
  }

  noit_check_transient_remove_feed(jcl->check, jcl->feed);
  noit_livestream_closure_free(jcl);
  /* Undo our dup */
  eventer_free(newe);
  /* Creating the thread failed, close it down and deschedule. */
  e->opset->close(e->fd, &newmask, e);
  return 0;
}
