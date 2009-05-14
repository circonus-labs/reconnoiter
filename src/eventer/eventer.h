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
  void *closure;
};

API_EXPORT(eventer_t) eventer_alloc();
API_EXPORT(void)      eventer_free(eventer_t);
API_EXPORT(int)       eventer_timecompare(const void *a, const void *b);
API_EXPORT(int)       eventer_name_callback(const char *name, eventer_func_t f);
API_EXPORT(const char *)
                      eventer_name_for_callback(eventer_func_t f);
API_EXPORT(eventer_func_t)
                      eventer_callback_for_name(const char *name);

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
  int               (*loop)();
} *eventer_impl_t;

/* This is the "chosen one" */
#ifndef _EVENTER_C
extern
#endif
eventer_impl_t __eventer;
noit_log_stream_t eventer_err;
noit_log_stream_t eventer_deb;

API_EXPORT(int) eventer_choose(const char *name);

#define eventer_propset       __eventer->propset
#define eventer_init          __eventer->init
#define eventer_add           __eventer->add
#define eventer_remove        __eventer->remove
#define eventer_update        __eventer->update
#define eventer_remove_fd     __eventer->remove_fd
#define eventer_find_fd       __eventer->find_fd
#define eventer_loop          __eventer->loop
#define eventer_trigger       __eventer->trigger

extern eventer_impl_t registered_eventers[];

#include "eventer/eventer_jobq.h"

API_EXPORT(eventer_jobq_t *) eventer_default_backq();
API_EXPORT(int) eventer_impl_propset(const char *key, const char *value);
API_EXPORT(int) eventer_impl_init();
API_EXPORT(void) eventer_add_asynch(eventer_jobq_t *q, eventer_t e);
API_EXPORT(void) eventer_dispatch_recurrent(struct timeval *now);
API_EXPORT(eventer_t) eventer_remove_recurrent(eventer_t e);
API_EXPORT(void) eventer_add_recurrent(eventer_t e);
API_EXPORT(int) eventer_get_epoch(struct timeval *epoch);

#endif
