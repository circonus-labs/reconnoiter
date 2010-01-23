/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
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

typedef struct {
  noit_module_t *self;
  noit_check_t *check;
  noit_hash_table attrs;
  size_t logsize;
  int timed_out;
} selfcheck_info_t;

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;

static void selfcheck_cleanup(noit_module_t *self, noit_check_t *check) {
  selfcheck_info_t *ci = check->closure;
  if(ci) {
    noit_check_release_attrs(&ci->attrs);
    memset(ci, 0, sizeof(*ci));
  }
}
static void jobq_thread_helper(eventer_jobq_t *jobq, void *closure) {
  int s32;
  char buffer[128];
  stats_t *current = (stats_t *)closure;
  s32 = jobq->concurrency;
  if(s32 == 0) return; /* omit if no concurrency */
  snprintf(buffer, sizeof(buffer), "%s_threads", jobq->queue_name);
  noit_stats_set_metric(current, buffer, METRIC_INT32, &s32);
}
static void selfcheck_log_results(noit_module_t *self, noit_check_t *check) {
  char buff[128];
  u_int64_t u64;
  int64_t s64;
  int32_t s32;
  stats_t current;
  struct timeval duration, epoch, diff;
  selfcheck_info_t *ci = check->closure;

  noit_check_stats_clear(&current);

  gettimeofday(&current.whence, NULL);
  sub_timeval(current.whence, check->last_fire_time, &duration);
  current.duration = duration.tv_sec * 1000 + duration.tv_usec / 1000;
  current.available = NP_UNAVAILABLE;
  current.state = NP_BAD;
  if(ci->timed_out) current.status = "timeout";
  else {
    current.available = NP_AVAILABLE;
    current.state = NP_GOOD;
    current.status = "ok";
  }
  /* Set all the metrics here */
  s64 = (int64_t)ci->logsize;
  noit_stats_set_metric(&current, "feed_bytes", METRIC_INT64, &s64);
  s32 = noit_poller_check_count();
  noit_stats_set_metric(&current, "check_cnt", METRIC_INT32, &s32);
  s32 = noit_poller_transient_check_count();
  noit_stats_set_metric(&current, "transient_cnt", METRIC_INT32, &s32);
  if(eventer_get_epoch(&epoch)) s64 = 0;
  else {
    sub_timeval(current.whence, epoch, &diff);
    s64 = diff.tv_sec;
  }
  noit_stats_set_metric(&current, "uptime", METRIC_INT64, &s64);
  eventer_jobq_process_each(jobq_thread_helper, &current);
  noit_build_version(buff, sizeof(buff));
  noit_stats_set_metric(&current, "version", METRIC_STRING, buff);
  u64 = noit_check_completion_count();
  noit_stats_set_metric(&current, "checks_run", METRIC_UINT64, &u64);

  noit_check_set_stats(self, check, &current);
}

#define FETCH_CONFIG_OR(key, str) do { \
  if(!noit_hash_retr_str(check->config, #key, strlen(#key), &key)) \
    key = str; \
} while(0)

static int selfcheck_log_size(eventer_t e, int mask, void *closure,
                              struct timeval *now) {
  selfcheck_info_t *ci = closure;
  noit_check_t *check = ci->check;
  const char *feedname;
  char feedname_buff[128];
  noit_log_stream_t feed;

  if(mask & (EVENTER_READ | EVENTER_WRITE)) {
    /* this case is impossible from the eventer.  It is called as
     * such on the synchronous completion of the event.
     */
    selfcheck_log_results(ci->self, ci->check);
    selfcheck_cleanup(ci->self, ci->check);
    check->flags &= ~NP_RUNNING;
    return 0;
  }
  switch(mask) {
    case EVENTER_ASYNCH_WORK:
      /* Check the length of the log */
      FETCH_CONFIG_OR(feedname, "feed");
      noit_check_interpolate(feedname_buff, sizeof(feedname_buff), feedname,
                             &ci->attrs, check->config);
      feed = noit_log_stream_find(feedname_buff);
      if(!feed) ci->logsize = -1;
      else ci->logsize = noit_log_stream_size(feed);
      ci->timed_out = 0;
      return 0;
      break;
    case EVENTER_ASYNCH_CLEANUP:
      /* This sets us up for a completion call. */
      e->mask = EVENTER_READ | EVENTER_WRITE;
      break;
    default:
      abort();
  }
  return 0;
}

static int selfcheck_initiate(noit_module_t *self, noit_check_t *check) {
  selfcheck_info_t *ci = check->closure;
  struct timeval __now;

  /* We cannot be running */
  assert(!(check->flags & NP_RUNNING));
  check->flags |= NP_RUNNING;

  ci->self = self;
  ci->check = check;

  ci->timed_out = 1;
  noit_check_make_attrs(check, &ci->attrs);
  gettimeofday(&__now, NULL);
  memcpy(&check->last_fire_time, &__now, sizeof(__now));

  /* Register a handler for the worker */
  noit_check_run_full_asynch(check, selfcheck_log_size);
  return 0;
}

static int selfcheck_initiate_check(noit_module_t *self, noit_check_t *check,
                                   int once, noit_check_t *parent) {
  if(!check->closure) check->closure = calloc(1, sizeof(selfcheck_info_t));
  INITIATE_CHECK(selfcheck_initiate, self, check);
  return 0;
}

static int selfcheck_onload(noit_image_t *self) {
  nlerr = noit_log_stream_find("error/selfcheck");
  nldeb = noit_log_stream_find("debug/selfcheck");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;

  eventer_name_callback("selfcheck/selfcheck_log_size", selfcheck_log_size);
  return 0;
}

#include "selfcheck.xmlh"
noit_module_t selfcheck = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "selfcheck",
    "noitd self-checker",
    selfcheck_xml_description,
    selfcheck_onload
  },
  NULL,
  NULL,
  selfcheck_initiate_check,
  selfcheck_cleanup
};

