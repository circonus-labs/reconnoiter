/*
 * Copyright (c) 2007-2010, OmniTI Computer Consulting, Inc.
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

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <math.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"

#ifndef BOOLOID
#define BOOLOID                  16
#endif
#ifndef INT2OID
#define INT2OID                  21
#endif
#ifndef INT4OID
#define INT4OID                  23
#endif
#ifndef INT8OID
#define INT8OID                  20
#endif
#ifndef FLOAT4OID
#define FLOAT4OID                700
#endif
#ifndef FLOAT8OID
#define FLOAT8OID                701
#endif
#ifndef NUMERICOID
#define NUMERICOID               1700
#endif

typedef struct {
  noit_module_t *self;
  noit_check_t *check;
  int rv;
  int timed_out;
  double timeout;
  int method;
  int ignore_signals;
} test_abort_check_info_t;

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;

static void test_abort_cleanup(noit_module_t *self, noit_check_t *check) {
  test_abort_check_info_t *ci = check->closure;
  if(ci) {
    memset(ci, 0, sizeof(*ci));
  }
}

static int test_abort_drive_session(eventer_t e, int mask, void *closure,
                                  struct timeval *passed_now) {
  struct timespec rqtp;
  struct timeval target_time, now, diff;
  double i, r;
  test_abort_check_info_t *ci = closure;
  noit_check_t *check = ci->check;

  if(mask & (EVENTER_READ | EVENTER_WRITE)) {
    /* this case is impossible from the eventer.  It is called as
     * such on the synchronous completion of the event.
     */
    stats_t current;
    noit_check_stats_clear(&current);
    current.available = NP_AVAILABLE;
    current.state = ci->timed_out ? NP_BAD : NP_GOOD;
    noitL(nlerr, "test_abort: EVENTER_READ | EVENTER_WRITE\n");
    noit_check_set_stats(ci->self, check, &current);
    check->flags &= ~NP_RUNNING;
    return 0;
  }
  switch(mask) {
    case EVENTER_ASYNCH_WORK:
      noitL(nlerr, "test_abort: EVENTER_ASYNCH_WORK\n");
      r = modf(ci->timeout, &i);
      ci->timed_out = 1;
      
      if(ci->ignore_signals) { /* compuational loop */
        double trash = 1.0;
        gettimeofday(&now, NULL);
        diff.tv_sec = (int)i;
        diff.tv_usec = (int)(r * 1000000.0);
        add_timeval(now, diff, &target_time);

        do {
          for(i=0; i<100000; i++) {
            trash += drand48();
            trash = log(trash);
            trash += 1.1;
            trash = exp(trash);
          }
          gettimeofday(&now, NULL);
          sub_timeval(target_time, now, &diff);
        } while(diff.tv_sec >= 0 && diff.tv_usec >= 0);
      }
      else {
        rqtp.tv_sec = (int)i;
        rqtp.tv_nsec = (int)(r * 1000000000.0);
        nanosleep(&rqtp,NULL);
      }
      noitL(nlerr, "test_abort: EVENTER_ASYNCH_WORK (done)\n");
      ci->timed_out = 0;
      return 0;
      break;
    case EVENTER_ASYNCH_CLEANUP:
      /* This sets us up for a completion call. */
      noitL(nlerr, "test_abort: EVENTER_ASYNCH_CLEANUP\n");
      e->mask = EVENTER_READ | EVENTER_WRITE;
      break;
    default:
      abort();
  }
  return 0;
}

static int test_abort_initiate(noit_module_t *self, noit_check_t *check) {
  test_abort_check_info_t *ci = check->closure;
  struct timeval __now;
  const char *v;

  noitL(nlerr, "test_abort_initiate\n");
  /* We cannot be running */
  assert(!(check->flags & NP_RUNNING));
  check->flags |= NP_RUNNING;

  ci->self = self;
  ci->check = check;
  ci->timeout = 30;
  if(noit_hash_retr_str(check->config, "sleep", strlen("sleep"), &v)) {
    ci->timeout = atof(v);
  }
  ci->ignore_signals = 0;
  if(noit_hash_retr_str(check->config, "ignore_signals", strlen("ignore_signals"), &v)) {
    if(!strcmp(v, "true")) ci->ignore_signals = 1;
  }
  ci->timed_out = 1;

  ci->method = 0;
  if(noit_hash_retr_str(check->config, "method", strlen("method"), &v)) {
    if(!strcmp(v, "evil")) ci->method = EVENTER_EVIL_BRUTAL;
    else if(!strcmp(v, "deferred")) ci->method = EVENTER_CANCEL_DEFERRED;
    else if(!strcmp(v, "asynch")) ci->method = EVENTER_CANCEL_ASYNCH;
  }
  memcpy(&check->last_fire_time, &__now, sizeof(__now));

  /* Register a handler for the worker */
  noit_check_run_full_asynch_opts(check, test_abort_drive_session, ci->method);
  return 0;
}

static int test_abort_initiate_check(noit_module_t *self, noit_check_t *check,
                                   int once, noit_check_t *parent) {
  if(!check->closure) check->closure = calloc(1, sizeof(test_abort_check_info_t));
  INITIATE_CHECK(test_abort_initiate, self, check);
  return 0;
}

static int test_abort_onload(noit_image_t *self) {
  nlerr = noit_log_stream_find("error/test_abort");
  nldeb = noit_log_stream_find("debug/test_abort");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;

  eventer_name_callback("http/test_abort_drive_session", test_abort_drive_session);
  return 0;
}

noit_module_t test_abort = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "test_abort",
    "test_abort internal tool for eventer testing",
    "",
    test_abort_onload
  },
  NULL,
  NULL,
  test_abort_initiate_check,
  test_abort_cleanup
};

