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
#include "utils/noit_log.h"
#include "utils/noit_skiplist.h"
#include <pthread.h>
#include <assert.h>

static struct timeval *eventer_impl_epoch = NULL;
static int EVENTER_DEBUGGING = 0;
static pthread_mutex_t te_lock;
static noit_skiplist *timed_events = NULL;

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
  else if(!strcasecmp(key, "debugging")) {
    if(strcmp(value, "0")) {
      EVENTER_DEBUGGING = 1;
      noitL(noit_error, "Enabling debugging from property\n");
    }
    return 0;
  }
  else if(!strcasecmp(key, "default_ca_chain")) {
    /* used by eventer consumers */
    return 0;
  }
  noitL(noit_error, "Warning: unknown eventer config '%s'\n", key);
  return 0;
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
  char *evdeb;

  evdeb = getenv("EVENTER_DEBUGGING");
  if(evdeb) {
    if(strcmp(evdeb, "0")) {
      /* Set to anything but "0" turns debugging on */
      EVENTER_DEBUGGING = 1;
      noitL(noit_error, "Disabling eventer debugging from environment\n");
    }
    else {
      EVENTER_DEBUGGING = 1;
      noitL(noit_error, "Enabling eventer debugging from environment\n");
    }
  }
  eventer_name_callback("eventer_jobq_execute_timeout",
                        eventer_jobq_execute_timeout);
  eventer_name_callback("eventer_jobq_consume_available",
                        eventer_jobq_consume_available);

  eventer_impl_epoch = malloc(sizeof(struct timeval));
  gettimeofday(eventer_impl_epoch, NULL);
  pthread_mutex_init(&te_lock, NULL);
    timed_events = calloc(1, sizeof(*timed_events));
  noit_skiplist_init(timed_events);
  noit_skiplist_set_compare(timed_events,
                            eventer_timecompare, eventer_timecompare);
  noit_skiplist_add_index(timed_events,
                          noit_compare_voidptr, noit_compare_voidptr);

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
  /* If we're debugging the eventer, these cross thread timeouts will
   * make it impossible for us to slowly trace an asynch job. */
  if(!EVENTER_DEBUGGING && e->whence.tv_sec) {
    job->timeout_event = eventer_alloc();
    memcpy(&job->timeout_event->whence, &e->whence, sizeof(e->whence));
    job->timeout_event->mask = EVENTER_TIMER;
    job->timeout_event->closure = job;
    job->timeout_event->callback = eventer_jobq_execute_timeout;
    eventer_add(job->timeout_event);
  }
  eventer_jobq_enqueue(q ? q : &__default_jobq, job);
}

void eventer_add_timed(eventer_t e) {
  assert(e->mask & EVENTER_TIMER);
  if(EVENTER_DEBUGGING) {
    const char *cbname;
    cbname = eventer_name_for_callback(e->callback);
    noitL(eventer_deb, "debug: eventer_add timed (%s)\n",
          cbname ? cbname : "???");
  }
  pthread_mutex_lock(&te_lock);
  noit_skiplist_insert(timed_events, e);
  pthread_mutex_unlock(&te_lock);
}
eventer_t eventer_remove_timed(eventer_t e) {
  eventer_t removed = NULL;
  assert(e->mask & EVENTER_TIMER);
  pthread_mutex_lock(&te_lock);
  if(noit_skiplist_remove_compare(timed_events, e, NULL,
                                  noit_compare_voidptr))
    removed = e;
  pthread_mutex_unlock(&te_lock);
  return removed;
}
void eventer_update_timed(eventer_t e, int mask) {
  assert(mask & EVENTER_TIMER);
  pthread_mutex_lock(&te_lock);
  noit_skiplist_remove_compare(timed_events, e, NULL, noit_compare_voidptr);
  noit_skiplist_insert(timed_events, e);
  pthread_mutex_unlock(&te_lock);
}
void eventer_dispatch_timed(struct timeval *now, struct timeval *next) {
  int max_timed_events_to_process;
    /* Handle timed events...
     * we could be multithreaded, so if we pop forever we could starve
     * ourselves. */
  max_timed_events_to_process = timed_events->size;
  while(max_timed_events_to_process-- > 0) {
    int newmask;
    eventer_t timed_event;

    gettimeofday(now, NULL);

    pthread_mutex_lock(&te_lock);
    /* Peek at our next timed event, if should fire, pop it.
     * otherwise we noop and NULL it out to break the loop. */
    timed_event = noit_skiplist_peek(timed_events);
    if(timed_event) {
      if(compare_timeval(timed_event->whence, *now) < 0) {
        timed_event = noit_skiplist_pop(timed_events, NULL);
      }
      else {
        sub_timeval(timed_event->whence, *now, next);
        timed_event = NULL;
      }
    }
    pthread_mutex_unlock(&te_lock);
    if(timed_event == NULL) break;
    if(EVENTER_DEBUGGING) {
      const char *cbname;
      cbname = eventer_name_for_callback(timed_event->callback);
      noitLT(eventer_deb, now, "debug: timed dispatch(%s)\n",
             cbname ? cbname : "???");
    }
    /* Make our call */
    newmask = timed_event->callback(timed_event, EVENTER_TIMER,
                                    timed_event->closure, now);
    if(newmask)
      eventer_add_timed(timed_event);
    else
      eventer_free(timed_event);
  }

  if(compare_timeval(eventer_max_sleeptime, *next) < 0) {
    /* we exceed our configured maximum, set it down */
    memcpy(next, &eventer_max_sleeptime, sizeof(*next));
  }
}
void
eventer_foreach_timedevent (void (*f)(eventer_t e, void *), void *closure) {
  noit_skiplist_node *iter = NULL;
  pthread_mutex_lock(&te_lock);
  for(iter = noit_skiplist_getlist(timed_events); iter;
      noit_skiplist_next(timed_events,&iter)) {
    if(iter->data) f(iter->data, closure);
  }
  pthread_mutex_unlock(&te_lock);
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

