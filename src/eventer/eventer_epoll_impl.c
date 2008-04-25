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
#include <sys/epoll.h>
#include <signal.h>
#include <pthread.h>
#include <assert.h>

static struct timeval __max_sleeptime = { 0, 200000 }; /* 200ms */
static int maxfds;
static struct {
  eventer_t e;
  pthread_t executor;
  noit_spinlock_t lock;
} *master_fds = NULL;
static int *masks;

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
static int epoll_fd = -1;
static pthread_mutex_t te_lock;
static noit_skiplist *timed_events = NULL;

static int eventer_epoll_impl_init() {
  struct rlimit rlim;
  int rv;

  /* super init */
  if((rv = eventer_impl_init()) != 0) return rv;

  master_thread = pthread_self();
  signal(SIGPIPE, SIG_IGN);
  epoll_fd = epoll_create(1024);
  if(epoll_fd == -1) {
    return -1;
  }
  pthread_mutex_init(&te_lock, NULL);
  getrlimit(RLIMIT_NOFILE, &rlim);
  maxfds = rlim.rlim_cur;
  master_fds = calloc(maxfds, sizeof(*master_fds));
  masks = calloc(maxfds, sizeof(*masks));
  timed_events = calloc(1, sizeof(*timed_events));
  noit_skiplist_init(timed_events);
  noit_skiplist_set_compare(timed_events,
                            eventer_timecompare, eventer_timecompare);
  noit_skiplist_add_index(timed_events,
                          noit_compare_voidptr, noit_compare_voidptr);
  return 0;
}
static int eventer_epoll_impl_propset(const char *key, const char *value) {
  if(eventer_impl_propset(key, value)) {
    /* Do our epoll local properties here */
    return -1;
  }
  return 0;
}
static void eventer_epoll_impl_add(eventer_t e) {
  struct epoll_event _ev;
  ev_lock_state_t lockstate;
  assert(e->mask);

  if(e->mask & EVENTER_ASYNCH) {
    eventer_add_asynch(NULL, e);
    return;
  }

  /* Recurrent delegation */
  if(e->mask & EVENTER_RECURRENT) {
    eventer_add_recurrent(e);
    return;
  }

  /* Timed events are simple */
  if(e->mask & EVENTER_TIMER) {
    pthread_mutex_lock(&te_lock);
    noit_skiplist_insert(timed_events, e);
    pthread_mutex_unlock(&te_lock);
    return;
  }

  /* file descriptor event */
  memset(&_ev, 0, sizeof(_ev));
  _ev.data.fd = e->fd;
  if(e->mask & EVENTER_READ) _ev.events |= (EPOLLIN|EPOLLPRI);
  if(e->mask & EVENTER_WRITE) _ev.events |= (EPOLLOUT);
  if(e->mask & EVENTER_EXCEPTION) _ev.events |= (EPOLLERR|EPOLLHUP);

  lockstate = acquire_master_fd(e->fd);
  master_fds[e->fd].e = e;

  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, e->fd, &_ev);

  release_master_fd(e->fd, lockstate);
}
static eventer_t eventer_epoll_impl_remove(eventer_t e) {
  eventer_t removed = NULL;
  if(e->mask & EVENTER_ASYNCH) {
    abort();
  }
  if(e->mask & (EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION)) {
    ev_lock_state_t lockstate;
    struct epoll_event _ev;
    memset(&_ev, 0, sizeof(_ev));
    _ev.data.fd = e->fd;
    lockstate = acquire_master_fd(e->fd);
    if(e == master_fds[e->fd].e) {
      removed = e;
      master_fds[e->fd].e = NULL;
      epoll_ctl(epoll_fd, EPOLL_CTL_DEL, e->fd, &_ev);
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
static void eventer_epoll_impl_update(eventer_t e) {
  struct epoll_event _ev;
  if(e->mask & EVENTER_TIMER) {
    pthread_mutex_lock(&te_lock);
    noit_skiplist_remove_compare(timed_events, e, NULL, noit_compare_voidptr);
    noit_skiplist_insert(timed_events, e);
    pthread_mutex_unlock(&te_lock);
    return;
  }
  memset(&_ev, 0, sizeof(_ev));
  _ev.data.fd = e->fd;
  if(e->mask & (EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION)) {
    if(e->mask & EVENTER_READ) _ev.events |= (EPOLLIN|EPOLLPRI);
    if(e->mask & EVENTER_WRITE) _ev.events |= (EPOLLOUT);
    if(e->mask & EVENTER_EXCEPTION) _ev.events |= (EPOLLERR|EPOLLHUP);
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, e->fd, &_ev);
  }
}
static eventer_t eventer_epoll_impl_remove_fd(int fd) {
  eventer_t eiq = NULL;
  ev_lock_state_t lockstate;
  if(master_fds[fd].e) {
    struct epoll_event _ev;
    memset(&_ev, 0, sizeof(_ev));
    _ev.data.fd = fd;
    lockstate = acquire_master_fd(fd);
    eiq = master_fds[fd].e;
    master_fds[fd].e = NULL;
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &_ev);
    release_master_fd(fd, lockstate);
  }
  return eiq;
}
static eventer_t eventer_epoll_impl_find_fd(int fd) {
  return master_fds[fd].e;
}
static int eventer_epoll_impl_loop() {
  int is_master_thread = 0;
  struct epoll_event *epev;
  pthread_t self;

  self = pthread_self();
  if(pthread_equal(self, master_thread)) is_master_thread = 1;

  epev = malloc(sizeof(*epev) * maxfds);

  while(1) {
    struct timeval __now, __sleeptime;
    int fd_cnt = 0;
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
    fd_cnt = epoll_wait(epoll_fd, epev, maxfds,
                        __sleeptime.tv_sec * 1000 + __sleeptime.tv_usec / 1000);
    noitLT(eventer_deb, &__now, "debug: epoll_wait(%d, [], %d) => %d\n", epoll_fd, maxfds, fd_cnt);
    if(fd_cnt < 0) {
      noitLT(eventer_err, &__now, "epoll_wait: %s\n", strerror(errno));
    }
    else {
      int idx;
      /* loop once to clear */
      for(idx = 0; idx < fd_cnt; idx++) {
        struct epoll_event *ev;
        const char *cbname;
        ev_lock_state_t lockstate;
        eventer_t e;
        int fd, oldmask, mask = 0;

        ev = &epev[idx];

        if(ev->events & (EPOLLIN | EPOLLPRI)) mask |= EVENTER_READ;
        if(ev->events & (EPOLLOUT)) mask |= EVENTER_WRITE;
        if(ev->events & (EPOLLERR|EPOLLHUP)) mask |= EVENTER_EXCEPTION;

        fd = ev->data.fd;
        e = master_fds[fd].e;

        lockstate = acquire_master_fd(fd);
        assert(lockstate == EV_OWNED);

        gettimeofday(&__now, NULL);
        oldmask = e->mask;
        cbname = eventer_name_for_callback(e->callback);
        noitLT(eventer_deb, &__now, "epoll: fire on %d/%x to %s(%p)\n",
               fd, mask, cbname?cbname:"???", e->callback);
        newmask = e->callback(e, mask, e->closure, &__now);

        if(newmask) {
          struct epoll_event _ev;
          memset(&_ev, 0, sizeof(_ev));
          _ev.data.fd = fd;
          if(newmask & EVENTER_READ) _ev.events |= (EPOLLIN|EPOLLPRI);
          if(newmask & EVENTER_WRITE) _ev.events |= (EPOLLOUT);
          if(newmask & EVENTER_EXCEPTION) _ev.events |= (EPOLLERR|EPOLLHUP);
          epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &_ev);
          /* Set our mask */
          e->mask = newmask;
        }
        else {
          eventer_free(e);
        }
        release_master_fd(fd, lockstate);
      }
    }
  }
  /* NOTREACHED */
  return 0;
}

struct _eventer_impl eventer_epoll_impl = {
  "epoll",
  eventer_epoll_impl_init,
  eventer_epoll_impl_propset,
  eventer_epoll_impl_add,
  eventer_epoll_impl_remove,
  eventer_epoll_impl_update,
  eventer_epoll_impl_remove_fd,
  eventer_epoll_impl_find_fd,
  eventer_epoll_impl_loop
};
