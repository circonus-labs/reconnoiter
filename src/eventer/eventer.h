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

#ifndef _EVENTER_EVENTER_H
#define _EVENTER_EVENTER_H

#include "noit_defines.h"
#include "utils/noit_log.h"
#include "utils/noit_atomic.h"
#include <sys/time.h>
#include <sys/socket.h>

#define EVENTER_READ             0x01
#define EVENTER_WRITE            0x02
#define EVENTER_EXCEPTION        0x04
#define EVENTER_TIMER            0x08
#define EVENTER_ASYNCH_WORK      0x10
#define EVENTER_ASYNCH_CLEANUP   0x20
#define EVENTER_ASYNCH           (EVENTER_ASYNCH_WORK | EVENTER_ASYNCH_CLEANUP)
#define EVENTER_RECURRENT        0x80
#define EVENTER_EVIL_BRUTAL     0x100
#define EVENTER_CANCEL_DEFERRED 0x200
#define EVENTER_CANCEL_ASYNCH   0x400
#define EVENTER_CANCEL          (EVENTER_CANCEL_DEFERRED|EVENTER_CANCEL_ASYNCH)

#define EVENTER_DEFAULT_ASYNCH_ABORT EVENTER_EVIL_BRUTAL

/* All of these functions act like their POSIX couterparts with two
 * additional arguments.  The first is the mask they require to be active
 * to make progress in the event of an EAGAIN.  The second is a closure
 * which is the event itself.
 */
typedef int (*eventer_fd_accept_t)
            (int, struct sockaddr *, socklen_t *, int *mask, void *closure);
typedef int (*eventer_fd_read_t)
            (int, void *, size_t, int *mask, void *closure);
typedef int (*eventer_fd_write_t)
            (int, const void *, size_t, int *mask, void *closure);
typedef int (*eventer_fd_close_t)
            (int, int *mask, void *closure);

typedef struct _fd_opset {
  eventer_fd_accept_t accept;
  eventer_fd_read_t   read;
  eventer_fd_write_t  write;
  eventer_fd_close_t  close;
  const char *name;
} *eventer_fd_opset_t;

typedef struct _event *eventer_t;

#include "eventer/eventer_POSIX_fd_opset.h"
#include "eventer/eventer_SSL_fd_opset.h"

typedef int (*eventer_func_t)
            (eventer_t e, int mask, void *closure, struct timeval *tv);

struct _event {
  eventer_func_t      callback;
  struct timeval      whence;
  int                 fd;
  int                 mask;
  eventer_fd_opset_t  opset;
  void               *opset_ctx;
  void               *closure;
  pthread_t           thr_owner;
};

API_EXPORT(eventer_t) eventer_alloc();
API_EXPORT(void)      eventer_free(eventer_t);
API_EXPORT(int)       eventer_timecompare(const void *a, const void *b);
API_EXPORT(int)       eventer_name_callback(const char *name, eventer_func_t f);
API_EXPORT(int)       eventer_name_callback_ext(const char *name,
                                                eventer_func_t f,
                                                void (*fn)(char *,int,eventer_t,void *),
                                                void *);
API_EXPORT(const char *)
                      eventer_name_for_callback(eventer_func_t f);
API_EXPORT(const char *)
                      eventer_name_for_callback_e(eventer_func_t, eventer_t);
API_EXPORT(eventer_func_t)
                      eventer_callback_for_name(const char *name);

/* These values are set on initialization and are safe to use
 * on any platform.  They will be set to zero on platforms that
 * do not support them.  As such, you can always bit-or them.
 */
API_EXPORT(int) NE_SOCK_CLOEXEC;
API_EXPORT(int) NE_O_CLOEXEC;

typedef struct _eventer_impl {
  const char         *name;
  int               (*init)();
  int               (*propset)(const char *key, const char *value);
  void              (*add)(eventer_t e);
  eventer_t         (*remove)(eventer_t e);
  void              (*update)(eventer_t e, int newmask);
  eventer_t         (*remove_fd)(int fd);
  eventer_t         (*find_fd)(int fd);
  void              (*trigger)(eventer_t e, int mask);
  int               (*loop)(int);
  void              (*foreach_fdevent)(void (*f)(eventer_t, void *), void *);
  void              (*wakeup)(eventer_t);
  void             *(*alloc_spec)();
  struct timeval    max_sleeptime;
  int               maxfds;
  struct {
    eventer_t e;
    pthread_t executor;
    noit_spinlock_t lock;
  }                 *master_fds;
} *eventer_impl_t;

/* This is the "chosen one" */
#ifndef _EVENTER_C
extern
#endif
eventer_impl_t __eventer;
noit_log_stream_t eventer_err;
noit_log_stream_t eventer_deb;

API_EXPORT(int) eventer_choose(const char *name);
API_EXPORT(void) eventer_loop();
API_EXPORT(int) eventer_is_loop(pthread_t tid);

#define eventer_propset       __eventer->propset
#define eventer_init          __eventer->init
#define eventer_add           __eventer->add
#define eventer_remove        __eventer->remove
#define eventer_update        __eventer->update
#define eventer_remove_fd     __eventer->remove_fd
#define eventer_find_fd       __eventer->find_fd
#define eventer_trigger       __eventer->trigger
#define eventer_max_sleeptime __eventer->max_sleeptime
#define eventer_foreach_fdevent  __eventer->foreach_fdevent
#define eventer_wakeup        __eventer->wakeup

extern eventer_impl_t registered_eventers[];

#if defined(__MACH__)
typedef u_int64_t eventer_hrtime_t;
#elif defined(linux) || defined(__linux) || defined(__linux__)
typedef long long unsigned int eventer_hrtime_t;
#else
typedef hrtime_t eventer_hrtime_t;
#endif
API_EXPORT(eventer_hrtime_t) eventer_gethrtime(void);

#include "eventer/eventer_jobq.h"

API_EXPORT(eventer_jobq_t *) eventer_default_backq(eventer_t);
API_EXPORT(int) eventer_impl_propset(const char *key, const char *value);
API_EXPORT(int) eventer_impl_init();
API_EXPORT(void) eventer_add_asynch(eventer_jobq_t *q, eventer_t e);
API_EXPORT(void) eventer_add_timed(eventer_t e);
API_EXPORT(eventer_t) eventer_remove_timed(eventer_t e);
API_EXPORT(void) eventer_update_timed(eventer_t e, int mask);
API_EXPORT(void) eventer_dispatch_timed(struct timeval *now,
                                        struct timeval *next);
API_EXPORT(void)
  eventer_foreach_timedevent (void (*f)(eventer_t e, void *), void *closure);
API_EXPORT(void) eventer_dispatch_recurrent(struct timeval *now);
API_EXPORT(eventer_t) eventer_remove_recurrent(eventer_t e);
API_EXPORT(void) eventer_add_recurrent(eventer_t e);
API_EXPORT(int) eventer_get_epoch(struct timeval *epoch);
API_EXPORT(void *) eventer_get_spec_for_event(eventer_t);
API_EXPORT(int) eventer_cpu_sockets_and_cores(int *sockets, int *cores);
API_EXPORT(pthread_t) eventer_choose_owner(int);

/* Helpers to schedule timed events */
#define eventer_add_at(func, cl, t) do { \
  eventer_t e = eventer_alloc(); \
  e->whence = t; \
  e->mask = EVENTER_TIMER; \
  e->callback = func; \
  e->closure = cl; \
  eventer_add(e); \
} while(0)
#define eventer_add_in(func, cl, t) do { \
  struct timeval __now; \
  eventer_t e = eventer_alloc(); \
  gettimeofday(&__now, NULL); \
  add_timeval(__now, t, &e->whence); \
  e->mask = EVENTER_TIMER; \
  e->callback = func; \
  e->closure = cl; \
  eventer_add(e); \
} while(0)
#define eventer_add_in_s_us(func, cl, s, us) do { \
  struct timeval __now, diff = { s, us }; \
  eventer_t e = eventer_alloc(); \
  gettimeofday(&__now, NULL); \
  add_timeval(__now, diff, &e->whence); \
  e->mask = EVENTER_TIMER; \
  e->callback = func; \
  e->closure = cl; \
  eventer_add(e); \
} while(0)

/* Helpers to set sockets non-blocking / blocking */
API_EXPORT(int) eventer_set_fd_nonblocking(int fd);
API_EXPORT(int) eventer_set_fd_blocking(int fd);
API_EXPORT(int) eventer_thread_check(eventer_t);

#endif
