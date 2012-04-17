/*
 * Copyright (c) 2007-2009, OmniTI Computer Consulting, Inc.
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
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <time.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "utils/noit_watchdog.h"

#define CHILD_WATCHDOG_TIMEOUT 5 /*seconds*/
const char *appname = "unknown";
const char *glider_path = NULL;
const char *trace_dir = "/var/tmp";

void noit_watchdog_glider(const char *path) {
  glider_path = path;
  if(glider_path)
    noitL(noit_error, "Setting watchdog glider to '%s'\n", glider_path);
}
void noit_watchdog_glider_trace_dir(const char *path) {
  trace_dir = path;
}

/* Watchdog stuff */
static int *lifeline = NULL;
static unsigned long last_tick_time() {
  static struct timeval lastchange = { 0, 0 };
  static int lastcheck = 0;
  struct timeval now, diff;

  gettimeofday(&now, NULL);
  if(lastcheck != *lifeline) {
    lastcheck = *lifeline;
    memcpy(&lastchange, &now, sizeof(lastchange));
  }
  if(lastchange.tv_sec == 0) return 0;

  sub_timeval(now, lastchange, &diff);
  return (unsigned long)diff.tv_sec;
}
static void it_ticks_zero() {
  (*lifeline) = 0;
}
static void it_ticks() {
  (*lifeline)++;
}
int noit_watchdog_child_heartbeat() {
  it_ticks();
  return 0;
}
int noit_watchdog_prefork_init() {
  lifeline = (int *)mmap(NULL, sizeof(int), PROT_READ|PROT_WRITE,
                         MAP_SHARED|MAP_ANON, -1, 0);
  if(lifeline == (void *)-1) {
    noitL(noit_error, "Failed to mmap anon for watchdog\n");
    return -1;
  }
  (*lifeline) = 0;
  return 0;
}

int noit_monitored_child_pid = -1;

void run_glider(int pid) {
  char cmd[1024], unused;
  if(glider_path) {
    snprintf(cmd, sizeof(cmd), "%s %d > %s/%s.%d.trc",
             glider_path, pid, trace_dir, appname, pid);
    unused = system(cmd);
  }
  (void)unused;
}

void glideme(int sig) {
  signal(sig, SIG_DFL);
  run_glider(noit_monitored_child_pid);
  kill(noit_monitored_child_pid, sig);
}

int noit_watchdog_start_child(const char *app, int (*func)(),
                              int child_watchdog_timeout, 
                              int retries, 
                              int span) {
  int child_pid;
  time_t time_data[retries];
  int offset = 0;

  memset(time_data, 0, sizeof(time_data));

  appname = strdup(app);
  if(child_watchdog_timeout == 0)
    child_watchdog_timeout = CHILD_WATCHDOG_TIMEOUT;
  while(1) {
    child_pid = fork();
    if(child_pid == -1) {
      noitL(noit_error, "fork failed: %s\n", strerror(errno));
      exit(-1);
    }
    if(child_pid == 0) {
      /* This sets up things so we start alive */
      it_ticks_zero();
      /* trace handlers */
      noit_monitored_child_pid = getpid();
      if(glider_path) {
        noitL(noit_error, "catching faults with glider\n");
        signal(SIGSEGV, glideme);
        signal(SIGABRT, glideme);
      }
      /* run the program */
      exit(func());
    }
    else {
      int sig = -1, exit_val = -1;
      while(1) {
        unsigned long ltt;
        int status, rv;
        sleep(1); /* Just check child status every second */
        rv = waitpid(child_pid, &status, WNOHANG);
        if(rv == 0) {
          /* Nothing */
        }
        else if (rv == child_pid) {
          /* We died!... we need to relaunch, unless the status was a requested exit (2) */
          int quit;
          sig = WTERMSIG(status);
          exit_val = WEXITSTATUS(status);
          quit = update_retries(retries, span, &offset, time_data);
          if (quit) {
            noitL(noit_error, "noit exceeded retry limit of %d retries in %d seconds... exiting...\n", retries, span);
            exit(0);
          }
          else if(sig == SIGINT || sig == SIGQUIT ||
             (sig == 0 && (exit_val == 2 || exit_val < 0))) {
            noitL(noit_error, "%s shutdown acknowledged.\n", app);
            exit(0);
          }
          break;
        }
        else {
          noitL(noit_error, "Unexpected return from waitpid: %d\n", rv);
          exit(-1);
        }
        /* Now check out timeout */
        if((ltt = last_tick_time()) > child_watchdog_timeout) {
          noitL(noit_error,
                "Watchdog timeout (%lu s)... terminating child\n",
                ltt);
          run_glider(child_pid);
          kill(child_pid, SIGKILL);
        }
      }
      noitL(noit_error, "%s child died [%d/%d], restarting.\n",
            app, exit_val, sig);
    }
  }
}

int update_retries(int retries, int span, int* offset, time_t times[]) {
  int i;
  time_t currtime = time(NULL);
  time_t cutoff = currtime - span;

  times[*offset % retries] = currtime;
  *offset = *offset + 1;

  for (i=0; i < retries; i++) {
    if (times[i] < cutoff) {
      return 0;
    }
  }

  return 1;
}

static int watchdog_tick(eventer_t e, int mask, void *unused, struct timeval *now) {
  it_ticks();
  return 0;
}
int noit_watchdog_child_eventer_heartbeat() {
  eventer_t e;

  assert(__eventer);

 /* Setup our hearbeat */
  e = eventer_alloc();
  e->mask = EVENTER_RECURRENT;
  e->callback = watchdog_tick;
  eventer_add_recurrent(e);

  return 0;
}

