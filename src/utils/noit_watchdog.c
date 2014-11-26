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
#include <dirent.h>
#if defined(__sun__)
#define _STRUCTURED_PROC 1
#include <sys/procfs.h>
#include <sys/lwp.h>

/* libproc.h pulls... I hate you illumos */
#define PR_ARG_PIDS 0x1 /* Allow pid and /proc file arguments */
#define PR_ARG_CORES    0x2 /* Allow core file arguments */
#define PR_ARG_ANY  (PR_ARG_PIDS | PR_ARG_CORES)

/* Flags accepted by Pgrab() */
#define PGRAB_RETAIN    0x01    /* Retain tracing flags, else clear flags */
#define PGRAB_FORCE 0x02    /* Open the process w/o O_EXCL */
#define PGRAB_RDONLY    0x04    /* Open the process or core w/ O_RDONLY */
#define PGRAB_NOSTOP    0x08    /* Open the process but do not stop it */
#define PGRAB_INCORE    0x10    /* Use in-core data to build symbol tables */

#define PRELEASE_RETAIN 0x20    /* Retain final tracing flags */

#define G_NOPROC    1   /* No such process */

struct ps_prochandle;

typedef int proc_lwp_all_f(void *, const lwpstatus_t *, const lwpsinfo_t *);
extern int Plwp_iter_all(struct ps_prochandle *, proc_lwp_all_f *, void *);

extern int proc_lwp_in_set(const char *, lwpid_t);
extern struct ps_prochandle *proc_arg_xgrab(const char *, const char *, int,
    int, int *, const char **);
extern pid_t proc_arg_psinfo(const char *, int, psinfo_t *, int *);

extern void proc_unctrl_psinfo(psinfo_t *);
extern  const psinfo_t *Ppsinfo(struct ps_prochandle *);
extern  const pstatus_t *Pstatus(struct ps_prochandle *);

extern  void    Prelease(struct ps_prochandle *, int);
extern  void    Pfree(struct ps_prochandle *);

#endif
#if defined(__MACH__) && defined(__APPLE__)
#include <libproc.h>
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
static int allow_async_dumps = 0;

int noit_watchdog_glider(const char *path) {
  glider_path = path;
  if(glider_path) {
    int rv;
    struct stat sb;
    while((rv = stat(path, &sb)) == -1 && errno == EINTR);
    if(rv == -1 || !S_ISREG(sb.st_mode) || (sb.st_mode & 0111) == 0) {
      noitL(noit_error, "glider '%s' doesn't exist or isn't executable.\n",
            glider_path);
      return -1;
    }
    noitL(noit_error, "Setting watchdog glider to '%s'\n", glider_path);
  }
  return 0;
}
int noit_watchdog_glider_trace_dir(const char *path) {
  trace_dir = path;
  if(trace_dir) {
    int rv;
    struct stat sb;
    while((rv = stat(path, &sb)) == -1 && errno == EINTR);
    if(rv == -1 || !S_ISDIR(sb.st_mode) || (sb.st_mode & 0111) == 0) {
      noitL(noit_error, "glider trace_dir '%s': no such directory.\n",
            trace_dir);
      return -1;
    }
  }
  return 0;
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
  if(lifeline) lifeline[1] = CRASHY_CRASH;
}
static void it_ticks_crash_release() {
  if(lifeline) lifeline[1] = CRASHY_RESTART;
}
static int it_ticks_crashed() {
  return (lifeline[1] == CRASHY_CRASH);
}
static int it_ticks_crash_restart() {
  return (lifeline[1] == CRASHY_RESTART);
}
static void it_ticks_zero() {
  if(lifeline) {
    lifeline[0] = 0;
    lifeline[1] = 0;
  }
}
static void it_ticks() {
  if(lifeline) (*lifeline)++;
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
    char *oldpath, oldpath_buf[PATH_MAX];
    oldpath = getcwd(oldpath_buf, sizeof(oldpath_buf));
    if(oldpath) chdir(trace_dir);
    snprintf(cmd, sizeof(cmd), "%s %d > %s/%s.%d.trc",
             glider_path, pid, trace_dir, appname, pid);
    unused = system(cmd);
    if(oldpath) chdir(oldpath);
  }
  (void)unused;
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
#elif defined(linux) || defined(__linux) || defined(__linux__)
  char path[PATH_MAX];
  DIR *root;
  struct dirent *de, *entry;
  int size = 0;

  snprintf(path, sizeof(path), "/proc/%d/fd", getpid());
#ifdef _PC_NAME_MAX
  size = pathconf(path, _PC_NAME_MAX);
#endif
  size = MAX(size, PATH_MAX + 128);
  de = alloca(size);
  close(3); /* hoping opendir uses 3 */
  root = opendir(path);
  if(!root) return;
  while(portable_readdir_r(root, de, &entry) == 0 && entry != NULL) {
    if(entry->d_name[0] >= '1' && entry->d_name[0] <= '9') {
      int tgt;
      tgt = atoi(entry->d_name);
      if(tgt != 3) close(tgt);
    }
  }
  close(3);
#elif defined(__MACH__) && defined(__APPLE__)
  struct proc_fdinfo files[1024*16];
  int rv, i = 0;

  rv = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, files, sizeof(files));
  if(rv > 0 && (rv % sizeof(files[0])) == 0) {
    rv /= sizeof(files[0]);
    for(i=0;i<rv;i++) {
      (void) close(files[i].proc_fd);
    }
  }
#else
  struct rlimit rl;
  int i, reasonable_max;

  getrlimit(RLIMIT_NOFILE, &rl);
  reasonable_max = MIN(1<<14, rl.rlim_max);
  for (i = 0; i < reasonable_max; i++)
    (void) close(i);
#endif
}
static void stop_other_threads() {
#ifdef UNSAFE_STOP
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
      if(tgt != self) _lwp_suspend(tgt);
    }
  }
  closedir(root);
#endif
#endif
}

void emancipate(int sig) {
  noit_log_enter_sighandler();
  signal(sig, SIG_DFL);
  noitL(noit_error, "emancipate: process %d, monitored %d, signal %d\n", getpid(), noit_monitored_child_pid, sig);
  if(getpid() == watcher) {
    run_glider(noit_monitored_child_pid);
    kill(noit_monitored_child_pid, sig);
  }
  else if (getpid() == noit_monitored_child_pid){
    it_ticks_crash(); /* slow notification path */
    kill(noit_monitored_child_pid, SIGSTOP); /* stop and wait for a glide */

    if(allow_async_dumps) { 
      stop_other_threads(); /* suspend all peer threads... to safely */
      close_all_fds(); /* close all our FDs */
      it_ticks_crash_release(); /* notify parent that it can fork a new one */
      /* the subsequent dump may take a while on big processes and slow disks */
    }
    kill(noit_monitored_child_pid, sig);
  }
  noit_log_leave_sighandler();
}

void subprocess_killed(int sig) {
  noitL(noit_error, "got a signal from spawned process.... exiting\n");
  exit(-1);
}

#if defined(__sun__)

#define LWPFLAGS    \
    (PR_STOPPED|PR_ISTOP|PR_DSTOP|PR_ASLEEP|PR_PCINVAL|PR_STEP \
    |PR_AGENT|PR_DETACH|PR_DAEMON)

#define PROCFLAGS   \
    (PR_ISSYS|PR_VFORKP|PR_ORPHAN|PR_NOSIGCHLD|PR_WAITPID \
    |PR_FORK|PR_RLC|PR_KLC|PR_ASYNC|PR_BPTADJ|PR_MSACCT|PR_MSFORK|PR_PTRACE)

typedef struct look_arg {
    int pflags;
    const char *lwps;
    int count;
    int stopped;
} look_arg_t;


static int
lwpisstopped(look_arg_t *arg, const lwpstatus_t *psp, const lwpsinfo_t *pip) {
  int flags;
  if(!proc_lwp_in_set(arg->lwps, pip->pr_lwpid))
    return 0;
  arg->count++;
  flags = psp->pr_flags & LWPFLAGS;
  if(flags & PR_STOPPED) arg->stopped++;
  return 0;
}

#endif

int wait_for_stop(pid_t pid) {
#if defined(__sun__)
  int gcode, gcode2;
  look_arg_t lookarg;
  pstatus_t pstatus;
  psinfo_t psinfo;
  const char *lwps;
  struct ps_prochandle *Pr;
  char pidstr[32];
  snprintf(pidstr, sizeof(pidstr), "%u", (unsigned int)pid);
  while(1) {
    if ((Pr = proc_arg_xgrab(pidstr, NULL, PR_ARG_ANY,
        PGRAB_RETAIN | PGRAB_FORCE | PGRAB_RDONLY | PGRAB_NOSTOP, &gcode,
        &lookarg.lwps)) == NULL) {
      if (gcode == G_NOPROC &&
          proc_arg_psinfo(pidstr, PR_ARG_PIDS, &psinfo, &gcode2) > 0 &&
          psinfo.pr_nlwp == 0) {
        noitL(noit_error, "child %d defunct\n", (int)psinfo.pr_pid);
        return 0;
      }
      return 0;
    }
    (void) memcpy(&pstatus, Pstatus(Pr), sizeof (pstatus_t));
    (void) memcpy(&psinfo, Ppsinfo(Pr), sizeof (psinfo_t));
    lookarg.pflags = pstatus.pr_flags;
    lookarg.count = 0;
    lookarg.stopped = 0;
    (void) Plwp_iter_all(Pr, (proc_lwp_all_f *)lwpisstopped, &lookarg);
    proc_unctrl_psinfo(&psinfo);
    Prelease(Pr, PRELEASE_RETAIN);
    if(lookarg.count == 0) return 0;
    if(lookarg.count == lookarg.stopped) return 1;
    noitL(noit_error, "Waiting for %d to STOP.\n", (int)psinfo.pr_pid);
    sleep(1);
  }
  return 0;
#else
  return 0;
#endif
}

/* monitoring...
 *
 *  /-----------------/                /---------------------------------/
 *  /  Child running  / --> (hang) --> / Parent tick age > timeout       /
 *  /-----------------/   (no crash)   /     SIGSTOP -> glide -> SIGKILL /
 *         |                           /---------------------------------/
 *       (segv)
 *         |
 *  /--------------------------------------------------/
 *  /   `emancipate`                                   /
 *  /  Child annotates shared memory CRASHED indicator / -(notices crash)
 *  /  Child STOPs itself.                             /         |
 *  /     [ possible fd shutdown and mark as RESTART ] /         |
 *  /--------------------------------------------------/         |
 *         |                                                     |
 *         |                           /---------------------------------/
 *         |                           /  Parent runs run glider.        /
 *         |<----(wakeup)--------------/  Parent SIGCONT Child.          /
 *         |                           /  if RESTART, clear indicator    /
 *  /-----------------------/          /---------------------------------/
 *  / Child reraises signal / 
 *  /-----------------------/
 *
 */

int noit_watchdog_start_child(const char *app, int (*func)(),
                              int child_watchdog_timeout) {
  int child_pid, crashing_pid = -1;
  time_t time_data[retries];
  int offset = 0;

  memset(time_data, 0, sizeof(time_data));

  appname = strdup(app);
  if(child_watchdog_timeout == 0)
    child_watchdog_timeout = CHILD_WATCHDOG_TIMEOUT;
  while(1) {
    unsigned long ltt = 0;
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
        int status, rv;
        sleep(1); /* Just check child status every second */
        if(child_pid != crashing_pid && crashing_pid != -1) {
          rv = waitpid(crashing_pid, &status, WNOHANG);
          if(rv == crashing_pid) {
            noitL(noit_error, "[monitor] emancipated child %d [%d/%d] reaped.\n",
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
          if(child_pid == crashing_pid) {
            lifeline[1] = 0;
            crashing_pid = -1;
          }
          noit_monitored_child_pid = -1;
          sig = WTERMSIG(status);
          exit_val = WEXITSTATUS(status);
          quit = update_retries(&offset, time_data);
          if (quit) {
            noitL(noit_error, "[monitor] noit exceeded retry limit of %d retries in %d seconds... exiting...\n", retries, span);
            exit(0);
          }
          else if(sig == SIGINT || sig == SIGQUIT ||
             (sig == 0 && (exit_val == 2 || exit_val < 0))) {
            noitL(noit_error, "[monitor] %s shutdown acknowledged.\n", app);
            exit(0);
          }
          break;
        }
        else if(errno != ECHILD) {
          noitL(noit_error, "[monitor] unexpected return from waitpid: %d (%s)\n", rv, strerror(errno));
          exit(-1);
        }
        /* Now check out timeout */
        if(it_ticks_crash_restart()) {
          noitL(noit_error, "[monitor] %s %d is emancipated for dumping.\n", app, crashing_pid);
          lifeline[1] = 0;
          noit_monitored_child_pid = -1;
          break;
        }
        else if(it_ticks_crashed() && crashing_pid == -1) {
          crashing_pid = noit_monitored_child_pid;
          noitL(noit_error, "[monitor] %s %d has crashed.\n", app, crashing_pid);
          /* We expect the child to be stopped here... */
          if(wait_for_stop(crashing_pid) == 0) sleep(1);
          run_glider(crashing_pid);
          kill(crashing_pid, SIGCONT);
        }
        else if(lifeline[1] == 0 && noit_monitored_child_pid == child_pid &&
                (ltt = last_tick_time()) > child_watchdog_timeout) {
          noitL(noit_error,
                "[monitor] Watchdog timeout (%lu s)... terminating child\n",
                ltt);
          if(glider_path) {
            kill(child_pid, SIGSTOP);
            run_glider(child_pid);
            kill(child_pid, SIGCONT);
          }
          kill(child_pid, SIGKILL);
          noit_monitored_child_pid = -1;
        }
      }
      if(sig >= 0) {
        noitL(noit_error, "[monitor] %s child died [%d/%d], restarting.\n",
              app, exit_val, sig);
      }
    }
    if((ltt = last_tick_time()) > 1) {
      noitL(noit_debug, "[monitor] child hearbeat age: %lu\n", ltt);
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

