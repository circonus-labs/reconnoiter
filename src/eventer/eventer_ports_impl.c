/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "utils/noit_atomic.h"
#include "utils/noit_skiplist.h"
#include "utils/noit_log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <port.h>
#include <pthread.h>
#include <assert.h>

#define MAX_PORT_EVENTS 1024

static struct timeval __max_sleeptime = { 0, 200000 }; /* 200ms */
static int maxfds;
static struct {
  eventer_t e;
  pthread_t executor;
  noit_spinlock_t lock;
} *master_fds = NULL;

typedef enum { EV_OWNED, EV_ALREADY_OWNED } ev_lock_state_t;

static ev_lock_state_t
acquire_master_fd(int fd) {
  if(noit_spinlock_trylock(&master_fds[fd].lock)) {
    master_fds[fd].executor = pthread_self();
    return EV_OWNED;
  }
  if(pthread_equal(master_fds[fd].executor, pthread_self())) {
    return EV_ALREADY_OWNED;
  }
  noit_spinlock_lock(&master_fds[fd].lock);
  master_fds[fd].executor = pthread_self();
  return EV_OWNED;
}
static void
release_master_fd(int fd, ev_lock_state_t as) {
  if(as == EV_OWNED) {
    memset(&master_fds[fd].executor, 0, sizeof(master_fds[fd].executor));
    noit_spinlock_unlock(&master_fds[fd].lock);
  }
}

static pthread_t master_thread;
static int port_fd = -1;

static pthread_mutex_t te_lock;
static noit_skiplist *timed_events = NULL;

static int eventer_ports_impl_init() {
  struct rlimit rlim;
  int rv;

  /* super init */
  if((rv = eventer_impl_init()) != 0) return rv;

  master_thread = pthread_self();
  signal(SIGPIPE, SIG_IGN);
  port_fd = port_create();
  if(port_fd == -1) {
    return -1;
  }
  pthread_mutex_init(&te_lock, NULL);
  getrlimit(RLIMIT_NOFILE, &rlim);
  maxfds = rlim.rlim_cur;
  master_fds = calloc(maxfds, sizeof(*master_fds));
  timed_events = calloc(1, sizeof(*timed_events));
  noit_skiplist_init(timed_events);
  noit_skiplist_set_compare(timed_events,
                            eventer_timecompare, eventer_timecompare);
  noit_skiplist_add_index(timed_events,
                          noit_compare_voidptr, noit_compare_voidptr);
  return 0;
}
static int eventer_ports_impl_propset(const char *key, const char *value) {
  if(eventer_impl_propset(key, value)) {
    return -1;
  }
  return 0;
}
static void alter_fd(eventer_t e, int mask) {
  if(mask & (EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION)) {
    int events = 0;
    if(mask & EVENTER_READ) events |= POLLIN;
    if(mask & EVENTER_WRITE) events |= POLLOUT;
    if(mask & EVENTER_EXCEPTION) events |= POLLERR;
    if(port_associate(port_fd, PORT_SOURCE_FD, e->fd, events, e) == -1) {
      noitL(eventer_err,
            "eventer port_associate failed: %s\n", strerror(errno));
      abort();
    }
  }
  else {
    if(port_dissociate(port_fd, PORT_SOURCE_FD, e->fd) == -1) {
      if(errno == ENOENT) return; /* Fine */
      if(errno == EBADFD) return; /* Fine */
      noitL(eventer_err,
            "eventer port_dissociate failed: %s\n", strerror(errno));
      abort();
    }
  }
}
static void eventer_ports_impl_add(eventer_t e) {
  assert(e->mask);
  ev_lock_state_t lockstate;
  const char *cbname;
  cbname = eventer_name_for_callback(e->callback);

  if(e->mask & EVENTER_ASYNCH) {
    noitL(eventer_deb, "debug: eventer_add asynch (%s)\n", cbname ? cbname : "???");
    eventer_add_asynch(NULL, e);
    return;
  }

  /* Recurrent delegation */
  if(e->mask & EVENTER_RECURRENT) {
    noitL(eventer_deb, "debug: eventer_add recurrent (%s)\n", cbname ? cbname : "???");
    eventer_add_recurrent(e);
    return;
  }

  /* Timed events are simple */
  if(e->mask & EVENTER_TIMER) {
    noitL(eventer_deb, "debug: eventer_add timed (%s)\n", cbname ? cbname : "???");
    pthread_mutex_lock(&te_lock);
    noit_skiplist_insert(timed_events, e);
    pthread_mutex_unlock(&te_lock);
    return;
  }

  /* file descriptor event */
  noitL(eventer_deb, "debug: eventer_add fd (%s,%d,0x%04x)\n", cbname ? cbname : "???", e->fd, e->mask);
  lockstate = acquire_master_fd(e->fd);
  master_fds[e->fd].e = e;
  alter_fd(e, e->mask);
  release_master_fd(e->fd, lockstate);
}
static eventer_t eventer_ports_impl_remove(eventer_t e) {
  eventer_t removed = NULL;
  if(e->mask & EVENTER_ASYNCH) {
    abort();
  }
  if(e->mask & (EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION)) {
    ev_lock_state_t lockstate;
    lockstate = acquire_master_fd(e->fd);
    if(e == master_fds[e->fd].e) {
      removed = e;
      master_fds[e->fd].e = NULL;
      alter_fd(e, 0);
    }
    release_master_fd(e->fd, lockstate);
  }
  else if(e->mask & EVENTER_TIMER) {
    pthread_mutex_lock(&te_lock);
    if(noit_skiplist_remove_compare(timed_events, e, NULL,
                                    noit_compare_voidptr))
      removed = e;
    pthread_mutex_unlock(&te_lock);
  }
  else if(e->mask & EVENTER_RECURRENT) {
    removed = eventer_remove_recurrent(e);
  }
  else {
    abort();
  }
  return removed;
}
static void eventer_ports_impl_update(eventer_t e, int mask) {
  if(e->mask & EVENTER_TIMER) {
    assert(mask & EVENTER_TIMER);
    pthread_mutex_lock(&te_lock);
    noit_skiplist_remove_compare(timed_events, e, NULL, noit_compare_voidptr);
    noit_skiplist_insert(timed_events, e);
    pthread_mutex_unlock(&te_lock);
    return;
  }
  alter_fd(e, mask);
  e->mask = mask;
}
static eventer_t eventer_ports_impl_remove_fd(int fd) {
  eventer_t eiq = NULL;
  ev_lock_state_t lockstate;
  if(master_fds[fd].e) {
    lockstate = acquire_master_fd(fd);
    eiq = master_fds[fd].e;
    master_fds[fd].e = NULL;
    alter_fd(eiq, 0);
    release_master_fd(fd, lockstate);
  }
  return eiq;
}
static eventer_t eventer_ports_impl_find_fd(int fd) {
  return master_fds[fd].e;
}
static int eventer_ports_impl_loop() {
  int is_master_thread = 0;
  pthread_t self;

  self = pthread_self();
  if(pthread_equal(self, master_thread)) is_master_thread = 1;

  while(1) {
    const char *cbname;
    struct timeval __now, __sleeptime;
    struct timespec __ports_sleeptime;
    unsigned int fd_cnt = 0;
    int ret;
    port_event_t pevents[MAX_PORT_EVENTS];
    int max_timed_events_to_process;
    int newmask;

    __sleeptime = __max_sleeptime;

    /* Handle timed events...
     * we could be multithreaded, so if we pop forever we could starve
     * ourselves. */
    max_timed_events_to_process = timed_events->size;
    while(max_timed_events_to_process-- > 0) {
      eventer_t timed_event;

      gettimeofday(&__now, NULL);

      pthread_mutex_lock(&te_lock);
      /* Peek at our next timed event, if should fire, pop it.
       * otherwise we noop and NULL it out to break the loop. */
      timed_event = noit_skiplist_peek(timed_events);
      if(timed_event) {
        if(compare_timeval(timed_event->whence, __now) < 0) {
          timed_event = noit_skiplist_pop(timed_events, NULL);
        }
        else {
          sub_timeval(timed_event->whence, __now, &__sleeptime);
          timed_event = NULL;
        }
      }
      pthread_mutex_unlock(&te_lock);
      if(timed_event == NULL) break;

      cbname = eventer_name_for_callback(timed_event->callback);
      noitLT(eventer_deb, &__now, "debug: timed dispatch(%s)\n", cbname ? cbname : "???");
      /* Make our call */
      newmask = timed_event->callback(timed_event, EVENTER_TIMER,
                                      timed_event->closure, &__now);
      if(newmask)
        eventer_add(timed_event);
      else
        eventer_free(timed_event);
    }

    if(compare_timeval(__max_sleeptime, __sleeptime) < 0) {
      /* we exceed our configured maximum, set it down */
      memcpy(&__sleeptime, &__max_sleeptime, sizeof(__sleeptime));
    }

    /* Handle recurrent events */
    eventer_dispatch_recurrent(&__now);

    /* Now we move on to our fd-based events */
    __ports_sleeptime.tv_sec = __sleeptime.tv_sec;
    __ports_sleeptime.tv_nsec = __sleeptime.tv_usec * 1000;
    fd_cnt = 1;
    ret = port_getn(port_fd, pevents, MAX_PORT_EVENTS, &fd_cnt,
                    &__ports_sleeptime);
    noitLT(eventer_deb, &__now, "debug: port_getn(%d, [], %d) => %d\n", port_fd,
           fd_cnt, ret);
    if(ret < 0)
      noitLT(eventer_deb, &__now, "port_getn: %s\n", strerror(errno));

    if(ret == -1 && (errno == ETIME)) fd_cnt = 0;
    else if(ret == -1 && (errno == EINTR)) fd_cnt = 0;
    else if(ret == -1)
      noitLT(eventer_err, &__now, "port_getn: %s\n", strerror(errno));

    if(fd_cnt > 0) {
      int idx;
      /* Loop a last time to process */
      for(idx = 0; idx < fd_cnt; idx++) {
        ev_lock_state_t lockstate;
        port_event_t *pe;
        eventer_t e;
        int fd, oldmask, mask;

        pe = &pevents[idx];
        if(pe->portev_source == PORT_SOURCE_FD)
        e = (eventer_t)pe->portev_user;
        fd = (int)pe->portev_object;
        mask = 0;
        if(pe->portev_events & (POLLIN | POLLHUP))
          mask |= EVENTER_READ;
        if(pe->portev_events & (POLLOUT))
          mask |= EVENTER_WRITE;
        if(pe->portev_events & (POLLERR | POLLHUP | POLLNVAL))
          mask |= EVENTER_EXCEPTION;

        /* It's possible that someone removed the event and freed it
         * before we got here.
         */
        if(e != master_fds[fd].e) continue;
        lockstate = acquire_master_fd(fd);
        assert(lockstate == EV_OWNED);

        gettimeofday(&__now, NULL);
        oldmask = e->mask;
        cbname = eventer_name_for_callback(e->callback);
        noitLT(eventer_deb, &__now, "ports: fire on %d/%x to %s(%p)\n",
               fd, mask, cbname?cbname:"???", e->callback);
        newmask = e->callback(e, mask, e->closure, &__now);

        if(newmask) {
          alter_fd(e, newmask);
          /* Set our mask */
          e->mask = newmask;
        }
        else {
          /*
           * Long story long:
           *  When integrating with a few external event systems, we find
           *  it difficult to make their use of remove+add as an update
           *  as it can be recurrent in a single handler call and you cannot
           *  remove completely from the event system if you are going to
           *  just update (otherwise the eventer_t in your call stack could
           *  be stale).  What we do is perform a superficial remove, marking
           *  the mask as 0, but not eventer_remove_fd.  Then on an add, if
           *  we already have an event, we just update the mask (as we
           *  have not yet returned to the eventer's loop.
           *  This leaves us in a tricky situation when a remove is called
           *  and the add doesn't roll in, we return 0 (mask == 0) and hit
           *  this spot.  We have intended to remove the event, but it still
           *  resides at master_fds[fd].e -- even after we free it.
           *  So, in the evnet that we return 0 and the event that
           *  master_fds[fd].e == the event we're about to free... we NULL
           *  it out.
           */
          if(master_fds[fd].e == e) master_fds[fd].e = NULL;
          eventer_free(e);
        }
        release_master_fd(fd, lockstate);
      }
    }
  }
  /* NOTREACHED */
}

struct _eventer_impl eventer_ports_impl = {
  "ports",
  eventer_ports_impl_init,
  eventer_ports_impl_propset,
  eventer_ports_impl_add,
  eventer_ports_impl_remove,
  eventer_ports_impl_update,
  eventer_ports_impl_remove_fd,
  eventer_ports_impl_find_fd,
  eventer_ports_impl_loop
};
