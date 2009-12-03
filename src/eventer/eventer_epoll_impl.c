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

struct _eventer_impl eventer_epoll_impl;
#define LOCAL_EVENTER eventer_epoll_impl
#define LOCAL_EVENTER_foreach_fdevent eventer_epoll_impl_foreach_fdevent
#define maxfds LOCAL_EVENTER.maxfds
#define master_fds LOCAL_EVENTER.master_fds

#include "eventer/eventer_impl_private.h"

static pthread_t master_thread;
static int *masks;
static int epoll_fd = -1;

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
  getrlimit(RLIMIT_NOFILE, &rlim);
  maxfds = rlim.rlim_cur;
  master_fds = calloc(maxfds, sizeof(*master_fds));
  masks = calloc(maxfds, sizeof(*masks));
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
    eventer_add_timed(e);
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
    removed = eventer_remove_timed(e);
  }
  else if(e->mask & EVENTER_RECURRENT) {
    removed = eventer_remove_recurrent(e);
  }
  else {
    abort();
  }
  return removed;
}
static void eventer_epoll_impl_update(eventer_t e, int mask) {
  struct epoll_event _ev;
  if(e->mask & EVENTER_TIMER) {
    eventer_update_timed(e,mask);
    return;
  }
  memset(&_ev, 0, sizeof(_ev));
  _ev.data.fd = e->fd;
  e->mask = mask;
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

static void eventer_epoll_impl_trigger(eventer_t e, int mask) {
  struct timeval __now;
  int fd, oldmask, newmask;
  const char *cbname;
  ev_lock_state_t lockstate;

  fd = e->fd;
  if(e != master_fds[fd].e) return;
  lockstate = acquire_master_fd(fd);
  if(lockstate == EV_ALREADY_OWNED) return;
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
    /* see kqueue implementation for details on the next line */
    if(master_fds[fd].e == e) master_fds[fd].e = NULL;
    eventer_free(e);
  }
  release_master_fd(fd, lockstate);
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

    __sleeptime = eventer_max_sleeptime;

    eventer_dispatch_timed(&__now, &__sleeptime);

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
        eventer_t e;
        int fd, mask = 0;

        ev = &epev[idx];

        if(ev->events & (EPOLLIN | EPOLLPRI)) mask |= EVENTER_READ;
        if(ev->events & (EPOLLOUT)) mask |= EVENTER_WRITE;
        if(ev->events & (EPOLLERR|EPOLLHUP)) mask |= EVENTER_EXCEPTION;

        fd = ev->data.fd;

        e = master_fds[fd].e;
        /* It's possible that someone removed the event and freed it
         * before we got here.
         */
        if(!e) continue;

        eventer_epoll_impl_trigger(e, mask);
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
  eventer_epoll_impl_trigger,
  eventer_epoll_impl_loop,
  eventer_epoll_impl_foreach_fdevent,
  { 0, 200000 },
  0,
  NULL
};
