/*
 * Copyright (c) 2007-2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2013, Circonus, Inc.
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
#if defined(__sun__)
#include <dirent.h>
#include <sys/lwp.h>
#endif
#include <signal.h>
#include <time.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "utils/noit_watchdog.h"

#define CHILD_WATCHDOG_TIMEOUT 5 /*seconds*/
#define CRASHY_CRASH 0x00dead00
#define CRASHY_RESTART 0x99dead99

const static char *appname = "unknown";
const static char *glider_path = NULL;
const static char *trace_dir = "/var/tmp";
static int retries = 5;
static int span = 60;
static int allow_async_dumps = 1;

void noit_watchdog_glider(const char *path) {
  glider_path = path;
  if(glider_path)
    noitL(noit_error, "Setting watchdog glider to '%s'\n", glider_path);
}
void noit_watchdog_glider_trace_dir(const char *path) {
  trace_dir = path;
}
void noit_watchdog_ratelimit(int retry_val, int span_val) {
    retries = retry_val;
    span = span_val;
}

/* Watchdog stuff */
static pid_t watcher = -1;
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
static void it_ticks_crash() {
  lifeline[1] = CRASHY_CRASH;
}
static void it_ticks_crash_release() {
  lifeline[1] = CRASHY_RESTART;
}
static int it_ticks_crashed() {
  return (lifeline[1] == CRASHY_CRASH);
}
static int it_ticks_crash_restart() {
  return (lifeline[1] == CRASHY_RESTART);
}
static void it_ticks_zero() {
  lifeline[0] = 0;
  lifeline[1] = 0;
}
static void it_ticks() {
  (*lifeline)++;
}
int noit_watchdog_child_heartbeat() {
  it_ticks();
  return 0;
}
int noit_watchdog_prefork_init() {
  const char *async;
  if(NULL != (async = getenv("ASYNCH_CORE_DUMP")))
    allow_async_dumps = atoi(async);
  watcher = getpid();
  lifeline = (int *)mmap(NULL, 2*sizeof(int), PROT_READ|PROT_WRITE,
                         MAP_SHARED|MAP_ANON, -1, 0);
  if(lifeline == (void *)-1) {
    noitL(noit_error, "Failed to mmap anon for watchdog\n");
    return -1;
  }
  it_ticks_zero();
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

void glidechild(int sig) {
  signal(sig, SIG_DFL);
  if(noit_monitored_child_pid > 0) {
    run_glider(noit_monitored_child_pid);
    kill(noit_monitored_child_pid, SIGCONT);
  }
  signal(SIGUSR2, glidechild);
}

#ifdef HAVE_FDWALK
static int fdwalker_close(void *unused, int fd) {
  close(fd);
  return 0;
}
#endif
static void close_all_fds() {
#if HAVE_FDWALK
  fdwalk(fdwalker_close, NULL);
#else
  struct rlimit rl;
  int i;

  getrlimit(RLIMIT_NOFILE, &rl);
  for (i = 0; i < rl.rlim_max; i++)
    (void) close(i);
#endif
}
static void stop_other_threads() {
#if defined(__sun__)
  lwpid_t self;
  char path[PATH_MAX];
  DIR *root;
  struct dirent *de, *entry;
  int size = 0;

  self = _lwp_self();
  snprintf(path, sizeof(path), "/proc/%d/lwp", getpid());
#ifdef _PC_NAME_MAX
  size = pathconf(path, _PC_NAME_MAX);
#endif
  size = MAX(size, PATH_MAX + 128);
  de = alloca(size);
  root = opendir(path);
  if(!root) return;
  while(portable_readdir_r(root, de, &entry) == 0 && entry != NULL) {
    if(entry->d_name[0] >= '1' && entry->d_name[0] <= '9') {
      lwpid_t tgt;
      tgt = atoi(entry->d_name);
#ifdef UNSAFE_STOP
      if(tgt != self) _lwp_suspend(tgt);
#endif
    }
  }
  closedir(root);
#endif
}

void emancipate(int sig) {
  signal(sig, SIG_DFL);
  if(getpid() == watcher) {
    run_glider(noit_monitored_child_pid);
  }
  else {
    kill(getppid(), SIGUSR2); /* fast notification path */
    it_ticks_crash(); /* slow notification path */
    kill(noit_monitored_child_pid, SIGSTOP); /* stop and wait for a glide */

    if(allow_async_dumps) { 
      stop_other_threads(); /* suspend all peer threads... to safely */
      close_all_fds(); /* close all our FDs */
      it_ticks_crash_release(); /* notify parent that it can fork a new one */
      /* the subsequent dump may take a while on big processes and slow disks */
    }
  }
  kill(noit_monitored_child_pid, sig);
}

int noit_watchdog_start_child(const char *app, int (*func)(),
                              int child_watchdog_timeout) {
  int child_pid, crashing_pid = -1;
  time_t time_data[retries];
  int offset = 0;

  memset(time_data, 0, sizeof(time_data));

  appname = strdup(app);
  if(child_watchdog_timeout == 0)
    child_watchdog_timeout = CHILD_WATCHDOG_TIMEOUT;
  signal(SIGUSR2, glidechild);
  while(1) {
    /* This sets up things so we start alive */
    it_ticks_zero();
    child_pid = fork();
    if(child_pid == -1) {
      noitL(noit_error, "fork failed: %s\n", strerror(errno));
      exit(-1);
    }
    if(child_pid == 0) {
      /* trace handlers */
      noit_monitored_child_pid = getpid();
      if(glider_path)
        noitL(noit_error, "catching faults with glider\n");
      else
        noitL(noit_error, "no glider, allowing a single emancipated minor.\n");
      signal(SIGSEGV, emancipate);
      signal(SIGABRT, emancipate);
      /* run the program */
      exit(func());
    }
    else {
      int sig = -1, exit_val = -1;
      noit_monitored_child_pid = child_pid;
      while(1) {
        unsigned long ltt;
        int status, rv;
        sleep(1); /* Just check child status every second */
        if(child_pid != crashing_pid && crashing_pid != -1) {
          rv = waitpid(crashing_pid, &status, WNOHANG);
          if(rv == crashing_pid) {
            noitL(noit_error, "emancipated child %d [%d/%d] reaped.\n",
                  crashing_pid, WEXITSTATUS(status), WTERMSIG(status));
            crashing_pid = -1;
          }
        }
        rv = waitpid(child_pid, &status, WNOHANG);
        if(rv == 0) {
          /* Nothing */
        }
        else if (rv == child_pid) {
          /* We died!... we need to relaunch, unless the status was a requested exit (2) */
          int quit;
          if(child_pid == crashing_pid) crashing_pid = -1;
          noit_monitored_child_pid = -1;
          sig = WTERMSIG(status);
          exit_val = WEXITSTATUS(status);
          quit = update_retries(&offset, time_data);
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
        if(it_ticks_crash_restart()) {
          noitL(noit_error, "%s %d is emancipated for dumping.\n", app, crashing_pid);
          noit_monitored_child_pid = -1;
          break;
        }
        else if(it_ticks_crashed() && crashing_pid == -1) {
          crashing_pid = noit_monitored_child_pid;
          noitL(noit_error, "%s %d has crashed.\n", app, crashing_pid);
          run_glider(crashing_pid);
          lifeline[1] = 0;
          kill(crashing_pid, SIGCONT);
        }
        else if((ltt = last_tick_time()) > child_watchdog_timeout) {
          noitL(noit_error,
                "Watchdog timeout (%lu s)... terminating child\n",
                ltt);
          kill(child_pid, SIGSTOP);
          run_glider(child_pid);
          kill(child_pid, SIGKILL);
          noit_monitored_child_pid = -1;
        }
        noitL(noit_debug, "last_tick_time -> %lu\n", ltt);
      }
      if(sig >= 0) {
        noitL(noit_error, "%s child died [%d/%d], restarting.\n",
              app, exit_val, sig);
      }
    }
  }
}

int update_retries(int* offset, time_t times[]) {
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

