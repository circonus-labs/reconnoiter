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
#include "utils/noit_memory.h"
#include "utils/noit_log.h"
#include "utils/noit_skiplist.h"
#include "utils/noit_watchdog.h"
#include "dtrace_probes.h"
#include <pthread.h>
#include <errno.h>
#include <assert.h>
#include <netinet/in.h>
#include <hwloc.h>

static struct timeval *eventer_impl_epoch = NULL;
static int PARALLELISM_MULTIPLIER = 4;
static int EVENTER_DEBUGGING = 0;
static int desired_nofiles = 1024*1024;

struct cross_thread_trigger {
  struct cross_thread_trigger *next;
  eventer_t e;
  int mask;
};
struct eventer_impl_data {
  int id;
  pthread_t tid;
  pthread_mutex_t te_lock;
  noit_skiplist *timed_events;
  eventer_jobq_t __global_backq;
  pthread_mutex_t recurrent_lock;
  struct recurrent_events {
    eventer_t e;
    struct recurrent_events *next;
  } *recurrent_events;
  pthread_mutex_t cross_lock;
  struct cross_thread_trigger *cross;
  void *spec;
};

static __thread struct eventer_impl_data *my_impl_data;
static struct eventer_impl_data *eventer_impl_tls_data = NULL;

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
static int __loop_concurrency = 0;
static noit_atomic32_t __loops_started = 0;
static eventer_jobq_t __default_jobq;

/* Multi-threaded event loops...

   We will instantiate __loop_concurrency separate threads each running their
   own event loop.  This event loops can concurrently fire callbacks, so it is
   important that they be written in a thread-safe manner.

   Sadly, some libraries that are leveraged simply aren't up to the challenge.

   We reserve the first event loop to run all stuff that isn't multi-thread safe.
   If you don't specify an thr_owner for an event, it will be assigned idx=0.
   This can cause a lot of (unavoidable) contention on that event thread.  In
   order to alleviate (or at least avoid) that contention, we will assist thread-
   safe events by only choosing thr_owners other than idx=0.

   This has the effect of using 1 thread for some checks and __loop_concurrency-1
   for all the others.

*/

pthread_t eventer_choose_owner(int i) {
  int idx;
  if(__loop_concurrency == 1) return eventer_impl_tls_data[0].tid;
  idx = ((unsigned int)i)%(__loop_concurrency-1) + 1; /* see comment above */
  noitL(eventer_deb, "eventer_choose -> %u %% %d = %d t@%u\n",
        (unsigned int)i, __loop_concurrency, idx,
        (unsigned int)eventer_impl_tls_data[idx].tid);
  return eventer_impl_tls_data[idx].tid;
}
static struct eventer_impl_data *get_my_impl_data() {
  return my_impl_data;
}
static struct eventer_impl_data *get_tls_impl_data(pthread_t tid) {
  int i;
  for(i=0;i<__loop_concurrency;i++) {
    if(pthread_equal(eventer_impl_tls_data[i].tid, tid))
      return &eventer_impl_tls_data[i];
  }
  noitL(noit_error, "get_tls_impl_data called from non-eventer thread\n");
  return NULL;
}
static struct eventer_impl_data *get_event_impl_data(eventer_t e) {
  return get_tls_impl_data(e->thr_owner);
}
int eventer_is_loop(pthread_t tid) {
  int i;
  for(i=0;i<__loop_concurrency;i++)
    if(pthread_equal(eventer_impl_tls_data[i].tid, tid)) return 1;
  return 0;
}
void *eventer_get_spec_for_event(eventer_t e) {
  struct eventer_impl_data *t;
  if(e == NULL) t = get_my_impl_data();
  else t = get_event_impl_data(e);
  assert(t);
  if(t->spec == NULL) t->spec = __eventer->alloc_spec();
  return t->spec;
}

int eventer_impl_propset(const char *key, const char *value) {
  if(!strcasecmp(key, "concurrency")) {
    __loop_concurrency = atoi(value);
    if(__loop_concurrency < 1) __loop_concurrency = 0;
    return 0;
  }
  if(!strcasecmp(key, "default_queue_threads")) {
    __default_queue_threads = atoi(value);
    if(__default_queue_threads < 1) {
      noitL(noit_error, "default_queue_threads must be >= 1\n");
      return -1;
    }
    return 0;
  }
  else if(!strcasecmp(key, "rlim_nofiles")) {
    desired_nofiles = atoi(value);
    if(desired_nofiles < 256) {
      noitL(noit_error, "rlim_nofiles must be >= 256\n");
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
  else if(!strncmp(key, "ssl_", 4)) {
    if(eventer_ssl_config(key, value) == 0) return 0;
    /* if we return 1, we'll fall through to the error message */
  }
  noitL(noit_error, "Warning: unknown eventer config '%s'\n", key);
  return 0;
}

eventer_jobq_t *eventer_default_backq(eventer_t e) {
  pthread_t tid;
  struct eventer_impl_data *impl_data;
  tid = e ? e->thr_owner : pthread_self();
  impl_data = get_tls_impl_data(tid);
  assert(impl_data);
  return &impl_data->__global_backq;
}

int eventer_get_epoch(struct timeval *epoch) {
  if(!eventer_impl_epoch) return -1;
  memcpy(epoch, eventer_impl_epoch, sizeof(*epoch));
  return 0;
}

int NE_SOCK_CLOEXEC = 0;
int NE_O_CLOEXEC = 0;

static int
eventer_noit_memory_maintenance(eventer_t e, int mask, void *c,
                                struct timeval *now) {
  noit_memory_maintenance();
  return EVENTER_RECURRENT;
}
static void eventer_per_thread_init(struct eventer_impl_data *t) {
  char qname[80];
  eventer_t e;

  if(t->timed_events != NULL) return;

  t->tid = pthread_self();
  my_impl_data = t;

  pthread_mutex_init(&t->cross_lock, NULL);
  pthread_mutex_init(&t->te_lock, NULL);
  t->timed_events = calloc(1, sizeof(*t->timed_events));
  noit_skiplist_init(t->timed_events);
  noit_skiplist_set_compare(t->timed_events,
                            eventer_timecompare, eventer_timecompare);
  noit_skiplist_add_index(t->timed_events,
                          noit_compare_voidptr, noit_compare_voidptr);

  snprintf(qname, sizeof(qname), "default_back_queue/%d", t->id);
  eventer_jobq_init(&t->__global_backq, qname);
  e = eventer_alloc();
  e->mask = EVENTER_RECURRENT;
  e->closure = &t->__global_backq;
  e->callback = eventer_jobq_consume_available;
  eventer_add_recurrent(e);

  e = eventer_alloc();
  e->mask = EVENTER_RECURRENT;
  e->callback = eventer_noit_memory_maintenance;
  eventer_add_recurrent(e);
  noit_atomic_inc32(&__loops_started);
}

static void *thrloopwrap(void *vid) {
  struct eventer_impl_data *t;
  int id = (int)(vpsized_int)vid;
  t = &eventer_impl_tls_data[id];
  t->id = id;
  noit_memory_init_thread();
  eventer_per_thread_init(t);
  return (void *)(vpsized_int)__eventer->loop(id);
}

void eventer_loop() {
  thrloopwrap((void *)(vpsized_int)0);
}

static void eventer_loop_prime() {
  int i;
  for(i=1; i<__loop_concurrency; i++) {
    pthread_t tid;
    pthread_create(&tid, NULL, thrloopwrap, (void *)(vpsized_int)i);
  }
  while(__loops_started < __loop_concurrency);
}

hwloc_topology_t *topo;
static int assess_hw_topo() {
  topo = calloc(1, sizeof(*topo));
  if(hwloc_topology_init(topo)) goto out;
  if(hwloc_topology_load(*topo)) goto destroy_out;

  return 0;

 destroy_out:
  hwloc_topology_destroy(*topo);
 out:
  free(topo);
  topo = NULL;
  return -1;
}
static int cpu_sockets_and_cores(int *sockets, int *cores) {
  int depth, nsockets = 0, ncores = 0;

  if(!topo) return -1;
  depth = hwloc_get_type_depth(*topo, HWLOC_OBJ_SOCKET);
  if(depth != HWLOC_TYPE_DEPTH_UNKNOWN)
    nsockets = hwloc_get_nbobjs_by_depth(*topo, depth);
  depth = hwloc_get_type_or_below_depth(*topo, HWLOC_OBJ_CORE);
  if(depth != HWLOC_TYPE_DEPTH_UNKNOWN)
    ncores = hwloc_get_nbobjs_by_depth(*topo, depth);

  if(sockets) *sockets = nsockets;
  if(cores) *cores = ncores;
  return 0;
}

int eventer_impl_init() {
  struct rlimit rlim;
  int i, try;
  char *evdeb;

  assess_hw_topo();

#ifdef SOCK_CLOEXEC
  /* We can test, still might not work */
  try = socket(AF_INET, SOCK_CLOEXEC|SOCK_STREAM, IPPROTO_TCP);
  if(try >= 0) {
    close(try);
    NE_SOCK_CLOEXEC = SOCK_CLOEXEC;
  }
#endif
#ifdef O_CLOEXEC
  NE_O_CLOEXEC = O_CLOEXEC;
#endif

  if(__loop_concurrency <= 0) {
    int sockets = 0, cores = 0;
    (void)cpu_sockets_and_cores(&sockets, &cores);
    if(sockets == 0) sockets = 1;
    if(cores == 0) cores = sockets;
    __loop_concurrency = 1 + PARALLELISM_MULTIPLIER * cores;
    noitL(noit_debug, "found %d sockets, %d cores -> concurrency %d\n",
          sockets, cores, __loop_concurrency);
  }

  evdeb = getenv("EVENTER_DEBUGGING");
  if(evdeb) {
    if(strcmp(evdeb, "0")) {
      /* Set to anything but "0" turns debugging on */
      EVENTER_DEBUGGING = 1;
      noitL(noit_error, "Enabling eventer debugging from environment\n");
    }
    else {
      EVENTER_DEBUGGING = 0;
      noitL(noit_error, "Disabling eventer debugging from environment\n");
    }
  }
  eventer_name_callback("eventer_jobq_execute_timeout",
                        eventer_jobq_execute_timeout);
  eventer_name_callback("eventer_jobq_consume_available",
                        eventer_jobq_consume_available);

  getrlimit(RLIMIT_NOFILE, &rlim);
  rlim.rlim_cur = rlim.rlim_max = try = desired_nofiles;
  while(setrlimit(RLIMIT_NOFILE, &rlim) != 0 && errno == EPERM && try > 1024) {
    noit_watchdog_child_heartbeat();
    rlim.rlim_cur = rlim.rlim_max = --try;
  }
  getrlimit(RLIMIT_NOFILE, &rlim);
  noitL(noit_debug, "rlim { %u, %u }\n", (u_int32_t)rlim.rlim_cur, (u_int32_t)rlim.rlim_max);

  eventer_impl_epoch = malloc(sizeof(struct timeval));
  gettimeofday(eventer_impl_epoch, NULL);

  eventer_err = noit_log_stream_find("error/eventer");
  eventer_deb = noit_log_stream_find("debug/eventer");
  if(!eventer_err) eventer_err = noit_stderr;
  if(!eventer_deb) eventer_deb = noit_debug;

  eventer_jobq_init(&__default_jobq, "default_queue");
  for(i=0; i<__default_queue_threads; i++)
    eventer_jobq_increase_concurrency(&__default_jobq);

  assert(eventer_impl_tls_data == NULL);
  eventer_impl_tls_data = calloc(__loop_concurrency, sizeof(*eventer_impl_tls_data));

  eventer_per_thread_init(&eventer_impl_tls_data[0]);
  eventer_loop_prime();
  eventer_ssl_init();
  return 0;
}

void eventer_add_asynch(eventer_jobq_t *q, eventer_t e) {
  eventer_job_t *job;
  /* always use 0, if unspecified */
  if(!eventer_is_loop(e->thr_owner)) e->thr_owner = eventer_impl_tls_data[0].tid;
  job = calloc(1, sizeof(*job));
  job->fd_event = e;
  job->jobq = q ? q : &__default_jobq;
  job->create_hrtime = eventer_gethrtime();
  /* If we're debugging the eventer, these cross thread timeouts will
   * make it impossible for us to slowly trace an asynch job. */
  if(e->whence.tv_sec) {
    job->timeout_event = eventer_alloc();
    job->timeout_event->thr_owner = e->thr_owner;
    memcpy(&job->timeout_event->whence, &e->whence, sizeof(e->whence));
    job->timeout_event->mask = EVENTER_TIMER;
    job->timeout_event->closure = job;
    job->timeout_event->callback = eventer_jobq_execute_timeout;
    eventer_add(job->timeout_event);
  }
  eventer_jobq_enqueue(q ? q : &__default_jobq, job);
}

void eventer_add_timed(eventer_t e) {
  struct eventer_impl_data *t;
  assert(e->mask & EVENTER_TIMER);
  if(EVENTER_DEBUGGING) {
    const char *cbname;
    cbname = eventer_name_for_callback_e(e->callback, e);
    noitL(eventer_deb, "debug: eventer_add timed (%s)\n",
          cbname ? cbname : "???");
  }
  t = get_event_impl_data(e);
  pthread_mutex_lock(&t->te_lock);
  noit_skiplist_insert(t->timed_events, e);
  pthread_mutex_unlock(&t->te_lock);
}
eventer_t eventer_remove_timed(eventer_t e) {
  struct eventer_impl_data *t;
  eventer_t removed = NULL;
  assert(e->mask & EVENTER_TIMER);
  t = get_event_impl_data(e);
  pthread_mutex_lock(&t->te_lock);
  if(noit_skiplist_remove_compare(t->timed_events, e, NULL,
                                  noit_compare_voidptr))
    removed = e;
  pthread_mutex_unlock(&t->te_lock);
  return removed;
}
void eventer_update_timed(eventer_t e, int mask) {
  struct eventer_impl_data *t;
  assert(mask & EVENTER_TIMER);
  t = get_event_impl_data(e);
  pthread_mutex_lock(&t->te_lock);
  noit_skiplist_remove_compare(t->timed_events, e, NULL, noit_compare_voidptr);
  noit_skiplist_insert(t->timed_events, e);
  pthread_mutex_unlock(&t->te_lock);
}
void eventer_dispatch_timed(struct timeval *now, struct timeval *next) {
  struct eventer_impl_data *t;
  int max_timed_events_to_process;
    /* Handle timed events...
     * we could be multithreaded, so if we pop forever we could starve
     * ourselves. */
  t = get_my_impl_data();
  max_timed_events_to_process = t->timed_events->size;
  while(max_timed_events_to_process-- > 0) {
    int newmask;
    const char *cbname = NULL;
    eventer_t timed_event;

    gettimeofday(now, NULL);

    pthread_mutex_lock(&t->te_lock);
    /* Peek at our next timed event, if should fire, pop it.
     * otherwise we noop and NULL it out to break the loop. */
    timed_event = noit_skiplist_peek(t->timed_events);
    if(timed_event) {
      if(compare_timeval(timed_event->whence, *now) < 0) {
        timed_event = noit_skiplist_pop(t->timed_events, NULL);
      }
      else {
        sub_timeval(timed_event->whence, *now, next);
        timed_event = NULL;
      }
    }
    pthread_mutex_unlock(&t->te_lock);
    if(timed_event == NULL) break;
    if(EVENTER_DEBUGGING ||
       EVENTER_CALLBACK_ENTRY_ENABLED() ||
       EVENTER_CALLBACK_RETURN_ENABLED()) {
      cbname = eventer_name_for_callback_e(timed_event->callback, timed_event);
      noitLT(eventer_deb, now, "debug: timed dispatch(%s)\n",
             cbname ? cbname : "???");
    }
    /* Make our call */
    noit_memory_begin();
    EVENTER_CALLBACK_ENTRY((void *)timed_event,
                           (void *)timed_event->callback, (char *)cbname, -1,
                           timed_event->mask, EVENTER_TIMER);
    newmask = timed_event->callback(timed_event, EVENTER_TIMER,
                                    timed_event->closure, now);
    EVENTER_CALLBACK_RETURN((void *)timed_event,
                            (void *)timed_event->callback, (char *)cbname, newmask);
    noit_memory_end();
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
  int i;
  for(i=0;i<__loop_concurrency;i++) {
    struct eventer_impl_data *t = &eventer_impl_tls_data[i];
    pthread_mutex_lock(&t->te_lock);
    for(iter = noit_skiplist_getlist(t->timed_events); iter;
        noit_skiplist_next(t->timed_events,&iter)) {
      if(iter->data) f(iter->data, closure);
    }
    pthread_mutex_unlock(&t->te_lock);
  }
}

void eventer_cross_thread_trigger(eventer_t e, int mask) {
  struct eventer_impl_data *t;
  struct cross_thread_trigger *ctt;
  t = get_event_impl_data(e);
  ctt = malloc(sizeof(*ctt));
  ctt->e = e;
  ctt->mask = mask;
  pthread_mutex_lock(&t->cross_lock);
  ctt->next = t->cross;
  t->cross = ctt;
  pthread_mutex_unlock(&t->cross_lock);
}
void eventer_cross_thread_process() {
  struct eventer_impl_data *t;
  struct cross_thread_trigger *ctt = NULL;
  t = get_my_impl_data();
  while(1) {
    pthread_mutex_lock(&t->cross_lock);
    ctt = t->cross;
    if(ctt) t->cross = ctt->next;
    pthread_mutex_unlock(&t->cross_lock);
    if(ctt) {
      eventer_trigger(ctt->e, ctt->mask);
      free(ctt);
    }
    else break;
  }
}

void eventer_dispatch_recurrent(struct timeval *now) {
  struct eventer_impl_data *t;
  struct recurrent_events *node;
  struct timeval __now;
  if(!now) {
    gettimeofday(&__now, NULL);
    now = &__now;
  }
  t = get_my_impl_data();
  pthread_mutex_lock(&t->recurrent_lock);
  for(node = t->recurrent_events; node; node = node->next) {
    node->e->callback(node->e, EVENTER_RECURRENT, node->e->closure, now);
  }
  pthread_mutex_unlock(&t->recurrent_lock);
}
eventer_t eventer_remove_recurrent(eventer_t e) {
  struct eventer_impl_data *t;
  struct recurrent_events *node, *prev = NULL;
  t = get_event_impl_data(e);
  pthread_mutex_lock(&t->recurrent_lock);
  for(node = t->recurrent_events; node; node = node->next) {
    if(node->e == e) {
      if(prev) prev->next = node->next;
      else t->recurrent_events = node->next;
      free(node);
      pthread_mutex_unlock(&t->recurrent_lock);
      return e;
    }
    prev = node;
  }
  pthread_mutex_unlock(&t->recurrent_lock);
  return NULL;
}
void eventer_wakeup_noop(eventer_t e) { }
void eventer_add_recurrent(eventer_t e) {
  struct eventer_impl_data *t;
  struct recurrent_events *node;
  assert(e->mask & EVENTER_RECURRENT);
  t = get_event_impl_data(e);
  pthread_mutex_lock(&t->recurrent_lock);
  for(node = t->recurrent_events; node; node = node->next)
    if(node->e == e) {
      pthread_mutex_unlock(&t->recurrent_lock);
      return;
    }
  node = calloc(1, sizeof(*node));
  node->e = e;
  node->next = t->recurrent_events;
  t->recurrent_events = node;
  pthread_mutex_unlock(&t->recurrent_lock);
}

int eventer_thread_check(eventer_t e) {
  return pthread_equal(pthread_self(), e->thr_owner);
}

#if defined(linux) || defined(__linux) || defined(__linux__)
#include <time.h>
eventer_hrtime_t eventer_gethrtime() {
  struct timespec ts;
  uint64_t t;
#ifdef CLOCK_MONOTONIC_RAW
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
  clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
  return ((ts.tv_sec * 1000000000) + ts.tv_nsec);
}
#elif defined(__MACH__)
#include <mach/mach.h>
#include <mach/mach_time.h>

static int initialized = 0;
static mach_timebase_info_data_t    sTimebaseInfo;
eventer_hrtime_t eventer_gethrtime() {
  uint64_t t;
  if(!initialized) {
    if(sTimebaseInfo.denom == 0)
      (void) mach_timebase_info(&sTimebaseInfo);
  }
  t = mach_absolute_time();
  return t * sTimebaseInfo.numer / sTimebaseInfo.denom;
}
#else
eventer_hrtime_t eventer_gethrtime() {
  return gethrtime();
}
#endif
