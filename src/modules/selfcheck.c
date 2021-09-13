/*
 * Copyright (c) 2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2015-2017, Circonus, Inc. All rights reserved.
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

#include <mtev_defines.h>

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>

#include <mtev_hash.h>
#include <mtev_memory.h>

#include "noit_mtev_bridge.h"
#include "noit_module.h"
#include "noit_jlog_listener.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_version.h"
#include <sys/utsname.h>

typedef struct {
  noit_module_t *self;
  noit_check_t *check;
  mtev_hash_table *attrs;
  size_t logsize;
  int timed_out;
} selfcheck_info_t;

struct threadq_crutch {
  noit_check_t *check;
  bool tagged;
};

static mtev_log_stream_t nlerr = NULL;
static mtev_log_stream_t nldeb = NULL;

/*Function to return operating system information*/
static int selfcheck_os_version(char *buff, int len) {
  struct utsname unameData;

  /*If error returned, print that operating system information is not available.*/
  if (uname(&unameData)<0)
  {
    return snprintf(buff, len, "%s", "N/A");
  }

  /*Else, print operating system information.*/
  else
  {
    return snprintf(buff, len, "%s %s %s", unameData.sysname,unameData.release,unameData.version);
  }
}
static void selfcheck_cleanse(noit_module_t *self, noit_check_t *check) {
  selfcheck_info_t *ci = check->closure;
  if(ci) {
    if(ci->attrs) {
      noit_check_release_attrs(ci->attrs);
      free(ci->attrs);
    }
    memset(ci, 0, sizeof(*ci));
  }
}
static void selfcheck_cleanup(noit_module_t *self, noit_check_t *check) {
  selfcheck_info_t *ci = check->closure;
  check->closure = NULL;
  selfcheck_cleanse(self, check);
  free(ci);
}
static int selfcheck_feed_details(jlog_feed_stats_t *s, void *closure) {
  char buff[256];
  uint64_t ms;
  struct timeval now, diff;
  struct threadq_crutch *crutch = (struct threadq_crutch *)closure;
  mtev_gettimeofday(&now, NULL);

  if(crutch->tagged) {
    snprintf(buff, sizeof(buff), "established|ST[stratcon-cn:%s,feed-type:storage,units:connections]", s->feed_name);
    noit_stats_set_metric(crutch->check, buff, METRIC_UINT32, &s->connections);
  } else {
    snprintf(buff, sizeof(buff), "feed`%s`connections_established", s->feed_name);
    noit_stats_set_metric(crutch->check, buff, METRIC_UINT32, &s->connections);
  }

  if(s->last_checkpoint.tv_sec > 0) {
    sub_timeval(now, s->last_checkpoint, &diff);
    ms = diff.tv_sec * 1000 + diff.tv_usec / 1000;
    if(crutch->tagged) {
      snprintf(buff, sizeof(buff), "delay|ST[stratcon-cn:%s,feed-type:storage,units:seconds]", s->feed_name);
      double seconds = (double)ms / 1000.0;
      noit_stats_set_metric(crutch->check, buff, METRIC_DOUBLE, &seconds);
    } else {
      snprintf(buff, sizeof(buff), "feed`%s`last_checkpoint_ms", s->feed_name);
      noit_stats_set_metric(crutch->check, buff, METRIC_UINT64, &ms);
    }
  }

  if(s->last_connection.tv_sec > 0 && s->connections > 0) {
    sub_timeval(now, s->last_connection, &diff);
    ms = diff.tv_sec * 1000 + diff.tv_usec / 1000;
    if(crutch->tagged) {
      snprintf(buff, sizeof(buff), "uptime|ST[stratcon-cn:%s,feed-type:storage,units:seconds]", s->feed_name);
      double seconds = (double)ms / 1000.0;
      noit_stats_set_metric(crutch->check, buff, METRIC_DOUBLE, &seconds);
    } else {
      snprintf(buff, sizeof(buff), "feed`%s`last_connection_ms", s->feed_name);
      noit_stats_set_metric(crutch->check, buff, METRIC_UINT64, &ms);
    }
  }

  return 1;
}
static void selfcheck_log_results(noit_module_t *self, noit_check_t *check) {
  char buff[256];
  uint64_t u64;
  int64_t s64;
  int32_t s32;
  struct threadq_crutch crutch;
  struct timeval now, duration, epoch, diff;
  selfcheck_info_t *ci = check->closure;

  const char *format = mtev_hash_dict_get(check->config, "format");
  crutch.check = check;
  if(strcmp(self->hdr.name, "selfcheck")) {
    crutch.tagged = (!format || strcmp(format, "tagged"));
  } else {
    crutch.tagged = (format && !strcmp(format, "tagged"));
  }

  mtev_gettimeofday(&now, NULL);
  sub_timeval(now, check->last_fire_time, &duration);
  noit_stats_set_whence(check, &now);
  noit_stats_set_duration(check, duration.tv_sec * 1000 + duration.tv_usec / 1000);
  noit_stats_set_available(check, NP_UNAVAILABLE);
  noit_stats_set_state(check, NP_BAD);
  if(ci->timed_out) noit_stats_set_status(check, "timeout");
  else {
    noit_stats_set_available(check, NP_AVAILABLE);
    noit_stats_set_state(check, NP_GOOD);
    noit_stats_set_status(check, "ok");
  }
  /* Set all the metrics here */
  const char *name_feed_bytes = "feed_bytes";
  const char *name_check_cnt = "check_cnt";
  const char *name_transient_cnt = "transient_cnt";
  const char *name_uptime = "uptime";
  const char *name_checks_run = "checks_run";
  const char *name_metrics_collected = "metrics_collected";
  if(crutch.tagged) {
    name_feed_bytes = "feed|ST[units:bytes]";
    name_check_cnt = "registered|ST[units:checks]";
    name_transient_cnt = "transient|ST[units:transients]";
    name_uptime = "uptime|ST[units:seconds]";
    name_checks_run = "checks|ST[units:executions]";
    name_metrics_collected = "collected|ST[units:tuples]";
  }
  s64 = (int64_t)ci->logsize;
  noit_stats_set_metric(check, name_feed_bytes, METRIC_INT64, &s64);
  s32 = noit_poller_check_count();
  noit_stats_set_metric(check, name_check_cnt, METRIC_INT32, &s32);
  s32 = noit_poller_transient_check_count();
  noit_stats_set_metric(check, name_transient_cnt, METRIC_INT32, &s32);
  if(eventer_get_epoch(&epoch)) s64 = 0;
  else {
    sub_timeval(now, epoch, &diff);
    s64 = diff.tv_sec;
  }
  noit_stats_set_metric(check, name_uptime, METRIC_INT64, &s64);
  noit_build_version(buff, sizeof(buff));
  noit_stats_set_metric(check, "version", METRIC_STRING, buff);

  /*Clear buffer, store operating system in it*/
  memset(buff,'\0',sizeof(buff));
  selfcheck_os_version(buff, sizeof(buff));
  noit_stats_set_metric(check, "OS version", METRIC_STRING, buff);

  u64 = noit_check_completion_count();
  noit_stats_set_metric(check, name_checks_run, METRIC_UINT64, &u64);
  u64 = noit_check_metric_count();
  noit_stats_set_metric(check, name_metrics_collected, METRIC_UINT64, &u64);
  /* feed pull info */
  noit_jlog_foreach_feed_stats(selfcheck_feed_details, &crutch);

  noit_check_set_stats(check);
}

#define FETCH_CONFIG_OR(key, str) do { \
  if(!mtev_hash_retr_str(check->config, #key, strlen(#key), &key)) \
    key = str; \
} while(0)

static int selfcheck_log_size(eventer_t e, int mask, void *closure,
                              struct timeval *now) {
  selfcheck_info_t *ci = closure;
  noit_check_t *check = ci->check;
  const char *feedname;
  char feedname_buff[128];
  mtev_log_stream_t feed;

  mtev_memory_begin();

  if(mask & (EVENTER_READ | EVENTER_WRITE)) {
    /* this case is impossible from the eventer.  It is called as
     * such on the synchronous completion of the event.
     */
    selfcheck_log_results(ci->self, ci->check);
    selfcheck_cleanse(ci->self, ci->check);
    noit_check_end(check);
    return 0;
  }
  switch(mask) {
    case EVENTER_ASYNCH_WORK:
      /* Check the length of the log */
      FETCH_CONFIG_OR(feedname, "feed");
      noit_check_interpolate(feedname_buff, sizeof(feedname_buff), feedname,
                             ci->attrs, check->config);
      feed = mtev_log_stream_find(feedname_buff);
      if(!feed) ci->logsize = -1;
      else ci->logsize = mtev_log_stream_size(feed);
      ci->timed_out = 0;
      return 0;
      break;
    case EVENTER_ASYNCH_CLEANUP:
      /* This sets us up for a completion call. */
      eventer_set_mask(e, EVENTER_READ | EVENTER_WRITE);
      break;
    default:
      mtevFatal(mtev_error, "Unknown mask: 0x%04x\n", mask);
  }
  mtev_memory_end();
  return 0;
}

static int selfcheck_initiate(noit_module_t *self, noit_check_t *check,
                              noit_check_t *cause) {
  selfcheck_info_t *ci = check->closure;
  struct timeval __now;

  /* We cannot be running */
  BAIL_ON_RUNNING_CHECK(check);
  noit_check_begin(check);

  ci->self = self;
  ci->check = check;

  ci->timed_out = 1;
  ci->attrs = calloc(1, sizeof(*ci->attrs));
  noit_check_make_attrs(check, ci->attrs);
  mtev_gettimeofday(&__now, NULL);
  memcpy(&check->last_fire_time, &__now, sizeof(__now));

  /* Register a handler for the worker */
  noit_check_run_full_asynch(check, selfcheck_log_size);
  return 0;
}

static int selfcheck_initiate_check(noit_module_t *self, noit_check_t *check,
                                   int once, noit_check_t *cause) {
  if(!check->closure) {
    selfcheck_info_t *check_info = calloc(1, sizeof(selfcheck_info_t));
    check->closure = (void*)check_info;
  }
  INITIATE_CHECK(selfcheck_initiate, self, check, cause);
  return 0;
}

static int selfcheck_onload(mtev_image_t *self) {
  nlerr = mtev_log_stream_find("error/selfcheck");
  nldeb = mtev_log_stream_find("debug/selfcheck");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;

  eventer_name_callback("selfcheck/selfcheck_log_size", selfcheck_log_size);
  return 0;
}


#include "selfcheck.xmlh"
noit_module_t selfcheck = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "selfcheck",
    .description = "noitd self-checker",
    .xml_description = selfcheck_xml_description,
    .onload = selfcheck_onload
  },
  NULL,
  NULL,
  selfcheck_initiate_check,
  selfcheck_cleanup
};

#include "broker.xmlh"
noit_module_t broker = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "broker",
    .description = "noitd self-checker",
    .xml_description = broker_xml_description,
    .onload = selfcheck_onload
  },
  NULL,
  NULL,
  selfcheck_initiate_check,
  selfcheck_cleanup
};

