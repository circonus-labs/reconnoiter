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
#include <sys/event.h>
#include <pthread.h>
#include <assert.h>

struct _eventer_impl eventer_kqueue_impl;
#define LOCAL_EVENTER eventer_kqueue_impl
#define LOCAL_EVENTER_foreach_fdevent eventer_kqueue_impl_foreach_fdevent
#define LOCAL_EVENTER_foreach_timedevent eventer_kqueue_impl_foreach_timedevent
#define maxfds LOCAL_EVENTER.maxfds
#define master_fds LOCAL_EVENTER.master_fds

#include "eventer/eventer_impl_private.h"

static pthread_t master_thread;
static int kqueue_fd = -1;
typedef struct kqueue_setup {
  struct kevent *__ke_vec;
  unsigned int __ke_vec_a;
  unsigned int __ke_vec_used;
} *kqs_t;

static pthread_mutex_t kqs_lock;
static kqs_t master_kqs = NULL;
static pthread_key_t kqueue_setup_key;
static int *masks;
#define KQUEUE_DECL kqs_t kqs
#define KQUEUE_SETUP kqs = (kqs_t) pthread_getspecific(kqueue_setup_key)
#define ke_vec kqs->__ke_vec
#define ke_vec_a kqs->__ke_vec_a
#define ke_vec_used kqs->__ke_vec_used

static void kqs_init(kqs_t kqs) {
  enum { initial_alloc = 64 };
  ke_vec_a = initial_alloc;
  ke_vec = (struct kevent *) malloc(ke_vec_a * sizeof (struct kevent));
}
static void
ke_change (register int const ident,
           register int const filter,
           register int const flags,
           register eventer_t e) {
  register struct kevent *kep;
  KQUEUE_DECL;
  KQUEUE_SETUP;
  if(!kqs) kqs = master_kqs;

  if(kqs == master_kqs) pthread_mutex_lock(&kqs_lock);
  if (!ke_vec_a) {
    kqs_init(kqs);
  }
  else if (ke_vec_used == ke_vec_a) {
    ke_vec_a <<= 1;
    ke_vec = (struct kevent *) realloc(ke_vec,
                                       ke_vec_a * sizeof (struct kevent));
  }
  kep = &ke_vec[ke_vec_used++];

  EV_SET(kep, ident, filter, flags, 0, 0, (void *)(vpsized_int)e->fd);
  noitL(eventer_deb, "debug: ke_change(fd:%d, filt:%x, flags:%x)\n",
        ident, filter, flags);
  if(kqs == master_kqs) pthread_mutex_unlock(&kqs_lock);
}

static int eventer_kqueue_impl_init() {
  struct rlimit rlim;
  int rv;

  /* super init */
  if((rv = eventer_impl_init()) != 0) return rv;

  master_thread = pthread_self();
  signal(SIGPIPE, SIG_IGN);
  kqueue_fd = kqueue();
  if(kqueue_fd == -1) {
    return -1;
  }
  pthread_mutex_init(&kqs_lock, NULL);
  pthread_key_create(&kqueue_setup_key, NULL);
  master_kqs = calloc(1, sizeof(*master_kqs));
  kqs_init(master_kqs);
  getrlimit(RLIMIT_NOFILE, &rlim);
  maxfds = rlim.rlim_cur;
  master_fds = calloc(maxfds, sizeof(*master_fds));
  masks = calloc(maxfds, sizeof(*masks));
  return 0;
}
static int eventer_kqueue_impl_propset(const char *key, const char *value) {
  if(eventer_impl_propset(key, value)) {
    /* Do our kqueue local properties here */
    return -1;
  }
  return 0;
}
static void eventer_kqueue_impl_add(eventer_t e) {
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
    eventer_add_timed(e);
    return;
  }

  /* file descriptor event */
  noitL(eventer_deb, "debug: eventer_add fd (%s,%d,0x%04x)\n", cbname ? cbname : "???", e->fd, e->mask);
  assert(e->whence.tv_sec == 0 && e->whence.tv_usec == 0);
  lockstate = acquire_master_fd(e->fd);
  master_fds[e->fd].e = e;
  if(e->mask & (EVENTER_READ | EVENTER_EXCEPTION))
    ke_change(e->fd, EVFILT_READ, EV_ADD | EV_ENABLE, e);
  if(e->mask & (EVENTER_WRITE))
    ke_change(e->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, e);
  release_master_fd(e->fd, lockstate);
}
static eventer_t eventer_kqueue_impl_remove(eventer_t e) {
  eventer_t removed = NULL;
  if(e->mask & EVENTER_ASYNCH) {
    abort();
  }
  if(e->mask & (EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION)) {
    ev_lock_state_t lockstate;
    lockstate = acquire_master_fd(e->fd);
    noitL(eventer_deb, "kqueue: remove(%d)\n", e->fd);
    if(e == master_fds[e->fd].e) {
      removed = e;
      master_fds[e->fd].e = NULL;
      if(e->mask & (EVENTER_READ | EVENTER_EXCEPTION))
        ke_change(e->fd, EVFILT_READ, EV_DELETE | EV_DISABLE, e);
      if(e->mask & (EVENTER_WRITE))
        ke_change(e->fd, EVFILT_WRITE, EV_DELETE | EV_DISABLE, e);
    } else
      noitL(eventer_deb, "kqueue: remove(%d) failed.\n", e->fd);
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
static void eventer_kqueue_impl_update(eventer_t e, int mask) {
  if(e->mask & EVENTER_TIMER) {
    eventer_update_timed(e, mask);
    return;
  }
  noitL(eventer_deb, "kqueue: update(%d, %x->%x)\n", e->fd, e->mask, mask);
  /* Disable old, if they aren't active in the new */
  if((e->mask & (EVENTER_READ | EVENTER_EXCEPTION)) &&
     !(mask & (EVENTER_READ | EVENTER_EXCEPTION)))
    ke_change(e->fd, EVFILT_READ, EV_DELETE | EV_DISABLE, e);
  if((e->mask & (EVENTER_WRITE)) &&
     !(mask & (EVENTER_WRITE)))
    ke_change(e->fd, EVFILT_WRITE, EV_DELETE | EV_DISABLE, e);

  /* Enable new, if the weren't in the old */
  if((mask & (EVENTER_READ | EVENTER_EXCEPTION)) &&
     !(e->mask & (EVENTER_READ | EVENTER_EXCEPTION)))
    ke_change(e->fd, EVFILT_READ, EV_ADD | EV_ENABLE, e);
  if((mask & (EVENTER_WRITE)) &&
     !(e->mask & (EVENTER_WRITE)))
    ke_change(e->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, e);

  /* Switch */
  e->mask = mask;
}
static eventer_t eventer_kqueue_impl_remove_fd(int fd) {
  eventer_t eiq = NULL;
  ev_lock_state_t lockstate;
  if(master_fds[fd].e) {
    noitL(eventer_deb, "kqueue: remove_fd(%d)\n", fd);
    lockstate = acquire_master_fd(fd);
    eiq = master_fds[fd].e;
    master_fds[fd].e = NULL;
    if(eiq->mask & (EVENTER_READ | EVENTER_EXCEPTION))
      ke_change(fd, EVFILT_READ, EV_DELETE | EV_DISABLE, eiq);
    if(eiq->mask & (EVENTER_WRITE))
      ke_change(fd, EVFILT_WRITE, EV_DELETE | EV_DISABLE, eiq);
    release_master_fd(fd, lockstate);
  }
  return eiq;
}
static eventer_t eventer_kqueue_impl_find_fd(int fd) {
  return master_fds[fd].e;
}
static void eventer_kqueue_impl_trigger(eventer_t e, int mask) {
  ev_lock_state_t lockstate;
  struct timeval __now;
  int oldmask, newmask;
  const char *cbname;
  int fd;

  fd = e->fd;
  if(e != master_fds[fd].e) return;
  lockstate = acquire_master_fd(fd);
  if(lockstate == EV_ALREADY_OWNED) return;
  assert(lockstate == EV_OWNED);

  gettimeofday(&__now, NULL);
  /* We're going to lie to ourselves.  You'd think this should be:
   * oldmask = e->mask;  However, we just fired with masks[fd], so
   * kqueue is clearly looking for all of the events in masks[fd].
   * So, we combine them "just to be safe."
   */
  oldmask = e->mask | masks[fd];
  cbname = eventer_name_for_callback(e->callback);
  noitLT(eventer_deb, &__now, "kqueue: fire on %d/%x to %s(%p)\n",
         fd, masks[fd], cbname?cbname:"???", e->callback);
  newmask = e->callback(e, mask, e->closure, &__now);

  if(newmask) {
    /* toggle the read bits if needed */
    if(newmask & (EVENTER_READ | EVENTER_EXCEPTION)) {
      if(!(oldmask & (EVENTER_READ | EVENTER_EXCEPTION)))
        ke_change(fd, EVFILT_READ, EV_ADD | EV_ENABLE, e);
    }
    else if(oldmask & (EVENTER_READ | EVENTER_EXCEPTION))
      ke_change(fd, EVFILT_READ, EV_DELETE | EV_DISABLE, e);

    /* toggle the write bits if needed */
    if(newmask & EVENTER_WRITE) {
      if(!(oldmask & EVENTER_WRITE))
        ke_change(fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, e);
    }
    else if(oldmask & EVENTER_WRITE)
      ke_change(fd, EVFILT_WRITE, EV_DELETE | EV_DISABLE, e);

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
static int eventer_kqueue_impl_loop() {
  int is_master_thread = 0;
  pthread_t self;
  KQUEUE_DECL;
  KQUEUE_SETUP;

  self = pthread_self();
  if(pthread_equal(self, master_thread)) is_master_thread = 1;

  if(!kqs) {
    kqs = calloc(1, sizeof(*kqs));
    kqs_init(kqs);
  }
  pthread_setspecific(kqueue_setup_key, kqs);
  while(1) {
    struct timeval __now, __sleeptime;
    struct timespec __kqueue_sleeptime;
    int fd_cnt = 0;

    __sleeptime = eventer_max_sleeptime;

    eventer_dispatch_timed(&__now, &__sleeptime);

    /* Handle recurrent events */
    eventer_dispatch_recurrent(&__now);

    /* If we're the master, we need to lock the master_kqs and make mods */
    if(master_kqs->__ke_vec_used) {
      struct timespec __zerotime = { 0, 0 };
      pthread_mutex_lock(&kqs_lock);
      fd_cnt = kevent(kqueue_fd,
                      master_kqs->__ke_vec, master_kqs->__ke_vec_used,
                      NULL, 0,
                      &__zerotime);
      noitLT(eventer_deb, &__now, "debug: kevent(%d, [], %d) => %d\n", kqueue_fd, master_kqs->__ke_vec_used, fd_cnt);
      if(fd_cnt < 0) {
        noitLT(eventer_err, &__now, "kevent: %s\n", strerror(errno));
      }
      master_kqs->__ke_vec_used = 0;
      pthread_mutex_unlock(&kqs_lock);
    }

    /* Now we move on to our fd-based events */
    __kqueue_sleeptime.tv_sec = __sleeptime.tv_sec;
    __kqueue_sleeptime.tv_nsec = __sleeptime.tv_usec * 1000;
    fd_cnt = kevent(kqueue_fd, ke_vec, ke_vec_used,
                    ke_vec, ke_vec_a,
                    &__kqueue_sleeptime);
    noitLT(eventer_deb, &__now, "debug: kevent(%d, [], %d) => %d\n", kqueue_fd, ke_vec_used, fd_cnt);
    ke_vec_used = 0;
    if(fd_cnt < 0) {
      noitLT(eventer_err, &__now, "kevent: %s\n", strerror(errno));
    }
    else {
      int idx;
      /* loop once to clear */
      for(idx = 0; idx < fd_cnt; idx++) {
        struct kevent *ke;
        ke = &ke_vec[idx];
        if(ke->flags & EV_ERROR) continue;
        masks[ke->ident] = 0;
      }
      /* Loop again to aggregate */
      for(idx = 0; idx < fd_cnt; idx++) {
        struct kevent *ke;
        ke = &ke_vec[idx];
        if(ke->flags & EV_ERROR) continue;
        if(ke->filter == EVFILT_READ) masks[ke->ident] |= EVENTER_READ;
        if(ke->filter == EVFILT_WRITE) masks[ke->ident] |= EVENTER_WRITE;
      }
      /* Loop a last time to process */
      for(idx = 0; idx < fd_cnt; idx++) {
        struct kevent *ke;
        eventer_t e;
        int fd;

        ke = &ke_vec[idx];
        if(ke->flags & EV_ERROR) {
          if(ke->data != EBADF && ke->data != ENOENT)
            noitLT(eventer_err, &__now, "error [%d]: %s\n",
                   (int)ke->ident, strerror(ke->data));
          continue;
        }
        assert((vpsized_int)ke->udata == (vpsized_int)ke->ident);
        fd = ke->ident;
        e = master_fds[fd].e;
        /* If we've seen this fd, don't callback twice */
        if(!masks[fd]) continue;
        /* It's possible that someone removed the event and freed it
         * before we got here.
         */
        if(e) eventer_kqueue_impl_trigger(e, masks[fd]);
        masks[fd] = 0; /* indicates we've processed this fd */
      }
    }
  }
  /* NOTREACHED */
  return 0;
}

struct _eventer_impl eventer_kqueue_impl = {
  "kqueue",
  eventer_kqueue_impl_init,
  eventer_kqueue_impl_propset,
  eventer_kqueue_impl_add,
  eventer_kqueue_impl_remove,
  eventer_kqueue_impl_update,
  eventer_kqueue_impl_remove_fd,
  eventer_kqueue_impl_find_fd,
  eventer_kqueue_impl_trigger,
  eventer_kqueue_impl_loop,
  eventer_kqueue_impl_foreach_fdevent,
  { 0, 200000 },
  0,
  NULL
};
