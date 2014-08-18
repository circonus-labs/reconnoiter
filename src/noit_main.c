/*
 * Copyright (c) 2011, OmniTI Computer Consulting, Inc.
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include "utils/noit_log.h"
#include "noit_conf.h"
#include "utils/noit_security.h"
#include "utils/noit_watchdog.h"
#include "utils/noit_lockfile.h"
#include "eventer/eventer.h"

#define MAX_CLI_LOGS 128
static char *enable_logs[MAX_CLI_LOGS];
static int enable_logs_cnt = 0;
static char *disable_logs[MAX_CLI_LOGS];
static int disable_logs_cnt = 0;

void
noit_main_enable_log(const char *name) {
  if(enable_logs_cnt >= MAX_CLI_LOGS) return;
  enable_logs[enable_logs_cnt++] = strdup(name);
}
void
noit_main_disable_log(const char *name) {
  if(disable_logs_cnt >= MAX_CLI_LOGS) return;
  disable_logs[disable_logs_cnt++] = strdup(name);
}
static int
configure_eventer(const char *appname) {
  int rv = 0;
  noit_hash_table *table;
  char appscratch[1024];

  snprintf(appscratch, sizeof(appscratch), "/%s/eventer/config", appname);
  table = noit_conf_get_hash(NULL, appscratch);
  if(table) {
    noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
    const char *key, *value;
    int klen;
    while(noit_hash_next_str(table, &iter, &key, &klen, &value)) {
      int subrv;
      if((subrv = eventer_propset(key, value)) != 0)
        rv = subrv;
    }
    noit_hash_destroy(table, free, free);
    free(table);
  }
  return rv;
}

void cli_log_switches() {
  int i;
  noit_log_stream_t ls;
  for(i=0; i<enable_logs_cnt; i++) {
    ls = noit_log_stream_find(enable_logs[i]);
    if(!ls) noitL(noit_error, "No such log: '%s'\n", enable_logs[i]);
    if(ls && !N_L_S_ON(ls)) {
      noitL(noit_notice, "Enabling %s\n", enable_logs[i]);
      noit_log_stream_set_flags(ls, noit_log_stream_get_flags(ls) | NOIT_LOG_STREAM_ENABLED);
    }
  }
  for(i=0; i<disable_logs_cnt; i++) {
    ls = noit_log_stream_find(disable_logs[i]);
    if(!ls) noitL(noit_error, "No such log: '%s'\n", enable_logs[i]);
    if(ls && N_L_S_ON(ls)) {
      noitL(noit_notice, "Disabling %s\n", disable_logs[i]);
      noit_log_stream_set_flags(ls, noit_log_stream_get_flags(ls) & ~NOIT_LOG_STREAM_ENABLED);
    }
  }
}

int
noit_main(const char *appname,
          const char *config_filename, int debug, int foreground,
          int lock, const char *_glider,
          const char *drop_to_user, const char *drop_to_group,
          int (*passed_child_main)(void)) {
  int fd, lockfd, watchdog_timeout = 0, rv;
  char conf_str[1024];
  char lockfile[PATH_MAX];
  char user[32], group[32];
  char *trace_dir = NULL;
  char appscratch[1024];
  char *glider = (char *)_glider;
  char *watchdog_timeout_str;
  int retry_val;
  int span_val;
  int ret;
  
   
  /* First initialize logging, so we can log errors */
  noit_log_init(debug);
  noit_log_stream_add_stream(noit_debug, noit_stderr);
  noit_log_stream_add_stream(noit_error, noit_stderr);
  noit_log_stream_add_stream(noit_notice, noit_error);

  /* Next load the configs */
  noit_conf_init(appname);
  if(noit_conf_load(config_filename) == -1) {
    fprintf(stderr, "Cannot load config: '%s'\n", config_filename);
    exit(-1);
  }

  /* Reinitialize the logging system now that we have a config */
  snprintf(user, sizeof(user), "%d", getuid());
  snprintf(group, sizeof(group), "%d", getgid());
  if(noit_security_usergroup(drop_to_user, drop_to_group, noit_true)) {
    noitL(noit_stderr, "Failed to drop privileges, exiting.\n");
    exit(-1);
  }
  noit_conf_log_init(appname);
  cli_log_switches();
  if(noit_security_usergroup(user, group, noit_true)) {
    noitL(noit_stderr, "Failed to regain privileges, exiting.\n");
    exit(-1);
  }
  if(debug) {
    noit_log_stream_set_flags(noit_debug, noit_log_stream_get_flags(noit_debug) | NOIT_LOG_STREAM_ENABLED);
  }

  snprintf(appscratch, sizeof(appscratch), "/%s/watchdog/@glider", appname);
  if(!glider) (void) noit_conf_get_string(NULL, appscratch, &glider);
  if(noit_watchdog_glider(glider)) {
    noitL(noit_stderr, "Invalid glider, exiting.\n");
    exit(-1);
  }
  snprintf(appscratch, sizeof(appscratch), "/%s/watchdog/@tracedir", appname);
  (void)noit_conf_get_string(NULL, appscratch, &trace_dir);
  if(trace_dir) {
    if(noit_watchdog_glider_trace_dir(trace_dir)) {
      noitL(noit_stderr, "Invalid glider tracedir, exiting.\n");
      exit(-1);
    }
  }

  snprintf(appscratch, sizeof(appscratch), "/%s/watchdog/@retries", appname);
  ret = noit_conf_get_int(NULL, appscratch, &retry_val);
  if((ret == 0) || (retry_val == 0)){
    retry_val = 5;
  }
  snprintf(appscratch, sizeof(appscratch), "/%s/watchdog/@span", appname);
  ret = noit_conf_get_int(NULL, appscratch, &span_val);
  if((ret == 0) || (span_val == 0)){
    span_val = 60;
  }

  noit_watchdog_ratelimit(retry_val, span_val);

  /* Lastly, run through all other system inits */
  snprintf(appscratch, sizeof(appscratch), "/%s/eventer/@implementation", appname);
  if(!noit_conf_get_stringbuf(NULL, appscratch, conf_str, sizeof(conf_str))) {
    noitL(noit_stderr, "Cannot find '%s' in configuration\n", appscratch);
    exit(-1);
  }
  if(eventer_choose(conf_str) == -1) {
    noitL(noit_stderr, "Cannot choose eventer %s\n", conf_str);
    exit(-1);
  }
  if(configure_eventer(appname) != 0) {
    noitL(noit_stderr, "Cannot configure eventer\n");
    exit(-1);
  }

  noit_watchdog_prefork_init();

  if(foreground != 1 && chdir("/") != 0) {
    noitL(noit_stderr, "Failed chdir(\"/\"): %s\n", strerror(errno));
    exit(-1);
  }

  /* Acquire the lock so that we can throw an error if it doesn't work.
   * If we've started -D, we'll have the lock.
   * If not we will daemon and must reacquire the lock.
   */
  lockfd = -1;
  lockfile[0] = '\0';
  snprintf(appscratch, sizeof(appscratch), "/%s/@lockfile", appname);
  if(noit_conf_get_stringbuf(NULL, appscratch,
                             lockfile, sizeof(lockfile))) {
    if (lock) {
      if((lockfd = noit_lockfile_acquire(lockfile)) < 0) {
        noitL(noit_stderr, "Failed to acquire lock: %s\n", lockfile);
        exit(-1);
      }
    }
  }

  if(foreground == 1) {
    int rv = passed_child_main();
    noit_lockfile_release(lockfd);
    return rv;
  }

  watchdog_timeout_str = getenv("WATCHDOG_TIMEOUT");
  if(watchdog_timeout_str) {
    watchdog_timeout = atoi(watchdog_timeout_str);
    noitL(noit_error, "Setting watchdog timeout to %d\n",
          watchdog_timeout);
  }

  /* This isn't inherited across forks... */
  if(lockfd >= 0) noit_lockfile_release(lockfd);
  lockfd = -1;

  if(foreground == 0) {
    fd = open("/dev/null", O_RDONLY);
    if(fd < 0 || dup2(fd, STDIN_FILENO) < 0) {
      fprintf(stderr, "Failed to setup stdin: %s\n", strerror(errno));
      exit(-1);
    }
    close(fd);
    fd = open("/dev/null", O_WRONLY);
    if(fd < 0 || dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
      fprintf(stderr, "Failed to setup std{out,err}: %s\n", strerror(errno));
      exit(-1);
    }
    close(fd);

    if(fork()) exit(0);
    setsid();
    if(fork()) exit(0);
  }

  /* Reacquire the lock */
  if(*lockfile) {
    if (lock) {
      if((lockfd = noit_lockfile_acquire(lockfile)) < 0) {
        noitL(noit_stderr, "Failed to acquire lock: %s\n", lockfile);
        exit(-1);
      }
    }
  }

  signal(SIGHUP, SIG_IGN);
  rv = noit_watchdog_start_child("noitd", passed_child_main, watchdog_timeout);
  noit_lockfile_release(lockfd);
  return rv;
}
