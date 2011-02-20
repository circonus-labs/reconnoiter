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
#include "noit_check_tools.h"
#include "noit_check_tools_shared.h"
#include "utils/noit_str.h"

#include <assert.h>

typedef struct {
  noit_module_t *self;
  noit_check_t *check;
  dispatch_func_t dispatch;
} recur_closure_t;

static int
noit_check_recur_handler(eventer_t e, int mask, void *closure,
                              struct timeval *now) {
  recur_closure_t *rcl = closure;
  rcl->check->fire_event = NULL; /* This is us, we get free post-return */
  noit_check_resolve(rcl->check);
  noit_check_schedule_next(rcl->self, &e->whence, rcl->check, now,
                           rcl->dispatch);
  if(NOIT_CHECK_RESOLVED(rcl->check))
    rcl->dispatch(rcl->self, rcl->check);
  else
    noitL(noit_debug, "skipping %s`%s`%s, unresolved\n",
          rcl->check->target, rcl->check->module, rcl->check->name);
  free(rcl);
  return 0;
}

int
noit_check_schedule_next(noit_module_t *self,
                         struct timeval *last_check, noit_check_t *check,
                         struct timeval *now, dispatch_func_t dispatch) {
  eventer_t newe;
  struct timeval period, earliest;
  recur_closure_t *rcl;

  assert(check->fire_event == NULL);
  if(check->period == 0) return 0;
  if(NOIT_CHECK_DISABLED(check) || NOIT_CHECK_KILLED(check)) return 0;

  /* If we have an event, we know when we intended it to fire.  This means
   * we should schedule that point + period.
   */
  if(now)
    memcpy(&earliest, now, sizeof(earliest));
  else
    gettimeofday(&earliest, NULL);

  /* If the check is unconfigured and needs resolving, we'll set the
   * period down a bit lower so we can pick up the resolution quickly.
   */
  if(!NOIT_CHECK_RESOLVED(check) && NOIT_CHECK_SHOULD_RESOLVE(check) &&
      check->period > 1000) {
    period.tv_sec = 1;
    period.tv_usec = 0;
  }
  else {
    period.tv_sec = check->period / 1000;
    period.tv_usec = (check->period % 1000) * 1000;
  }

  newe = eventer_alloc();
  memcpy(&newe->whence, last_check, sizeof(*last_check));
  add_timeval(newe->whence, period, &newe->whence);
  if(compare_timeval(newe->whence, earliest) < 0)
    memcpy(&newe->whence, &earliest, sizeof(earliest));
  newe->mask = EVENTER_TIMER;
  newe->callback = noit_check_recur_handler;
  rcl = calloc(1, sizeof(*rcl));
  rcl->self = self;
  rcl->check = check;
  rcl->dispatch = dispatch;
  newe->closure = rcl;

  eventer_add(newe);
  check->fire_event = newe;
  return 0;
}

void
noit_check_run_full_asynch_opts(noit_check_t *check, eventer_func_t callback,
                                int mask) {
  struct timeval __now, p_int;
  eventer_t e;
  e = eventer_alloc();
  e->fd = -1;
  e->mask = EVENTER_ASYNCH | mask;
  gettimeofday(&__now, NULL);
  memcpy(&e->whence, &__now, sizeof(__now));
  p_int.tv_sec = check->timeout / 1000;
  p_int.tv_usec = (check->timeout % 1000) * 1000;
  add_timeval(e->whence, p_int, &e->whence);
  e->callback = callback;
  e->closure =  check->closure;
  eventer_add(e);
}
void
noit_check_run_full_asynch(noit_check_t *check, eventer_func_t callback) {
  noit_check_run_full_asynch_opts(check, callback,
                                  EVENTER_DEFAULT_ASYNCH_ABORT);
}

void
noit_check_tools_init() {
  noit_check_tools_shared_init();
  eventer_name_callback("noit_check_recur_handler", noit_check_recur_handler);
}

