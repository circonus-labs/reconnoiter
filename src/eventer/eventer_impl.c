/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include <pthread.h>
#include <assert.h>

static struct timeval *eventer_impl_epoch = NULL;

#ifdef HAVE_KQUEUE
extern struct _eventer_impl eventer_kqueue_impl;
#endif
#ifdef HAVE_EPOLL
extern struct _eventer_impl eventer_epoll_impl;
#endif
#ifdef HAVE_PORTS
extern struct _eventer_impl eventer_ports_impl;
#endif

eventer_impl_t registered_eventers[] = {
#ifdef HAVE_KQUEUE
  &eventer_kqueue_impl,
#endif
#ifdef HAVE_EPOLL
  &eventer_epoll_impl,
#endif
#ifdef HAVE_PORTS
  &eventer_ports_impl,
#endif
  NULL
};

eventer_impl_t __eventer = NULL;
noit_log_stream_t eventer_err = NULL;
noit_log_stream_t eventer_deb = NULL;

static int __default_queue_threads = 5;
static eventer_jobq_t __global_backq, __default_jobq;
static pthread_mutex_t recurrent_lock = PTHREAD_MUTEX_INITIALIZER;
struct recurrent_events {
  eventer_t e;
  struct recurrent_events *next;
} *recurrent_events = NULL;


int eventer_impl_propset(const char *key, const char *value) {
  if(!strcasecmp(key, "default_queue_threads")) {
    __default_queue_threads = atoi(value);
    if(__default_queue_threads < 1) {
      noitL(noit_error, "default_queue_threads must be >= 1\n");
      return -1;
    }
    return 0;
  }
  return -1;
}

eventer_jobq_t *eventer_default_backq() {
  return &__global_backq;
}

int eventer_get_epoch(struct timeval *epoch) {
  if(!eventer_impl_epoch) return -1;
  memcpy(epoch, eventer_impl_epoch, sizeof(*epoch));
  return 0;
}

int eventer_impl_init() {
  int i;
  eventer_t e;

  eventer_impl_epoch = malloc(sizeof(struct timeval));
  gettimeofday(eventer_impl_epoch, NULL);

  eventer_err = noit_log_stream_find("error/eventer");
  eventer_deb = noit_log_stream_find("debug/eventer");
  if(!eventer_err) eventer_err = noit_stderr;
  if(!eventer_deb) eventer_deb = noit_debug;

  eventer_ssl_init();
  eventer_jobq_init(&__global_backq, "default_back_queue");
  e = eventer_alloc();
  e->mask = EVENTER_RECURRENT;
  e->closure = &__global_backq;
  e->callback = eventer_jobq_consume_available;

  /* We call directly here as we may not be completely initialized */
  eventer_add_recurrent(e);

  eventer_jobq_init(&__default_jobq, "default_queue");
  __default_jobq.backq = &__global_backq;
  for(i=0; i<__default_queue_threads; i++)
    eventer_jobq_increase_concurrency(&__default_jobq);
  return 0;
}

void eventer_add_asynch(eventer_jobq_t *q, eventer_t e) {
  eventer_job_t *job;
  job = calloc(1, sizeof(*job));
  job->fd_event = e;
  gettimeofday(&job->create_time, NULL);
  if(e->whence.tv_sec) {
    job->timeout_event = eventer_alloc();
    memcpy(&job->timeout_event->whence, &e->whence, sizeof(e->whence));
    job->timeout_event->mask = EVENTER_TIMER;
    job->timeout_event->closure = job;
    job->timeout_event->callback = eventer_jobq_execute_timeout;
    eventer_add(job->timeout_event);
  }
  eventer_jobq_enqueue(q ? q : &__default_jobq, job);
}

void eventer_dispatch_recurrent(struct timeval *now) {
  struct recurrent_events *node;
  struct timeval __now;
  if(!now) {
    gettimeofday(&__now, NULL);
    now = &__now;
  }
  pthread_mutex_lock(&recurrent_lock);
  for(node = recurrent_events; node; node = node->next) {
    node->e->callback(node->e, EVENTER_RECURRENT, node->e->closure, now);
  }
  pthread_mutex_unlock(&recurrent_lock);
}
eventer_t eventer_remove_recurrent(eventer_t e) {
  struct recurrent_events *node, *prev = NULL;
  pthread_mutex_lock(&recurrent_lock);
  for(node = recurrent_events; node; node = node->next) {
    if(node->e == e) {
      if(prev) prev->next = node->next;
      else recurrent_events = node->next;
      free(node);
      pthread_mutex_unlock(&recurrent_lock);
      return e;
    }
    prev = node;
  }
  pthread_mutex_unlock(&recurrent_lock);
  return NULL;
}
void eventer_add_recurrent(eventer_t e) {
  struct recurrent_events *node;
  assert(e->mask & EVENTER_RECURRENT);
  pthread_mutex_lock(&recurrent_lock);
  for(node = recurrent_events; node; node = node->next)
    if(node->e == e) {
      pthread_mutex_unlock(&recurrent_lock);
      return;
    }
  node = calloc(1, sizeof(*node));
  node->e = e;
  node->next = recurrent_events;
  recurrent_events = node;
  pthread_mutex_unlock(&recurrent_lock);
}

