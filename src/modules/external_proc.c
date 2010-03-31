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
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <assert.h>
#include <sys/mman.h>
#include <poll.h>
#include <errno.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include "utils/noit_skiplist.h"
#include "utils/noit_log.h"
#include "external_proc.h"

static void finish_procs();
static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;
int in_fd, out_fd;

struct proc_state {
  int64_t check_no;
  int cancelled;
  pid_t pid;
  int32_t status;
  char *path;
  char **argv;
  char **envp;
  int stdout_fd;
  int stderr_fd;
};

void proc_state_free(struct proc_state *ps) {
  int i;
  free(ps->path);
  for(i=0; ps->argv[i]; i++)
    free(ps->argv[i]);
  free(ps->argv);
  for(i=0; ps->envp[i]; i++)
    free(ps->envp[i]);
  free(ps->envp);
  if(ps->stdout_fd >= 0) close(ps->stdout_fd);
  if(ps->stderr_fd >= 0) close(ps->stderr_fd);
}

noit_skiplist active_procs;
noit_skiplist done_procs;

static int __proc_state_check_no(const void *av, const void *bv) {
  struct proc_state *a = (struct proc_state *)av;
  struct proc_state *b = (struct proc_state *)bv;
  if(a->check_no == b->check_no) return 0;
  if(a->check_no < b->check_no) return -1;
  return 1;
}
static int __proc_state_check_no_key(const void *akv, const void *bv) {
  int64_t *acheck_no = (int64_t *)akv;
  struct proc_state *b = (struct proc_state *)b;
  if(*acheck_no == b->check_no) return 0;
  if(*acheck_no < b->check_no) return -1;
  return 1;
}

static int __proc_state_pid(const void *av, const void *bv) {
  struct proc_state *a = (struct proc_state *)av;
  struct proc_state *b = (struct proc_state *)bv;
  if(a->pid == b->pid) return 0;
  if(a->pid < b->pid) return -1;
  return 1;
}
static int __proc_state_pid_key(const void *akv, const void *bv) {
  pid_t *apid = (pid_t *)akv;
  struct proc_state *b = (struct proc_state *)bv;
  if(*apid == b->pid) return 0;
  if(*apid < b->pid) return -1;
  return 1;
}

static void external_sigchld(int sig) {
  noit_skiplist_node *iter = NULL;
  struct proc_state *ps;
  int status = 0;
  pid_t pid;
  pid = waitpid(0, &status, WNOHANG);
  ps = noit_skiplist_find_compare(&active_procs, &pid, &iter, __proc_state_pid);
  noitL(nldeb, "reaped pid %d (check: %lld) -> %x\n",
        pid, ps?ps->check_no:-1, status);
  if(iter) {
    ps->status = status;
    noit_skiplist_remove_compare(&active_procs, &pid, NULL,  __proc_state_pid);
    noit_skiplist_insert(&done_procs, ps);
  }
}

static void fetch_and_kill_by_check(int64_t check_no) {
  struct proc_state *ps;
  ps = noit_skiplist_find(&active_procs, &check_no, NULL);
  if(ps) {
    ps->cancelled = 1;
    kill(ps->pid, SIGKILL);
  }
}

#define assert_read(fd, d, l) do { \
  int len; \
  while((len = read(fd,d,l)) == -1 && errno == EINTR) finish_procs(); \
  assert(len == l); \
} while(0)
#define assert_write(fd, s, l) do { \
  int len; \
  while((len = write(fd,s,l)) == -1 && errno == EINTR) finish_procs(); \
  assert(len == l); \
} while(0)

int write_out_backing_fd(int ofd, int bfd) {
  char *mmap_buf;
  u_int16_t outlen;
  struct stat buf;

  if(fstat(bfd, &buf) == -1) {
    noitL(nldeb, "external: fstat error: %s\n", strerror(errno));
    goto bail;
  }
  /* Our output length is limited to 64k (including a \0) */
  /* So, we'll limit the mapping of the file to 0xfffe */
  if(buf.st_size > 0xfffe) outlen = 0xfffe;
  outlen = buf.st_size & 0xffff;
  /* If we have no length, we can skip all this nonsense */
  if(outlen == 0) goto bail;

  mmap_buf = mmap(NULL, outlen, PROT_READ, MAP_SHARED, bfd, 0);
  if(mmap_buf == (char *)-1) {
    noitL(nldeb, "external: mmap error: %s\n", strerror(errno));
    goto bail;
  }
  outlen++; /* no null on the end, but we're reporting one */
  assert_write(ofd, &outlen, sizeof(outlen));
  outlen--; /* set it back to write and munmap */
  assert_write(ofd, mmap_buf, outlen);
  assert_write(ofd, "", 1);
  munmap(mmap_buf, outlen);
  return outlen+1;

 bail:
  outlen = 1;
  assert_write(ofd, &outlen, sizeof(outlen));
  assert_write(ofd, "", 1);
  return 1;
}

static void finish_procs() {
  struct proc_state *ps;
  noitL(noit_error, "%d done procs to cleanup\n", done_procs.size);
  while((ps = noit_skiplist_pop(&done_procs, NULL)) != NULL) {
    noitL(noit_error, "finished %lld/%d\n", ps->check_no, ps->pid);
    if(ps->cancelled == 0) {
      assert_write(out_fd, &ps->check_no,
                   sizeof(ps->check_no));
      assert_write(out_fd, &ps->status,
                   sizeof(ps->status));
      write_out_backing_fd(out_fd, ps->stdout_fd);
      write_out_backing_fd(out_fd, ps->stderr_fd);
    }
    proc_state_free(ps);
  }
}

int external_proc_spawn(struct proc_state *ps) {
  int i, stdin_fd;
  char stdoutfile[PATH_MAX];
  char stderrfile[PATH_MAX];

  noitL(nldeb, "About to spawn: (%s)\n", ps->path);
  strlcpy(stdoutfile, "/tmp/noitext.XXXXXX", PATH_MAX);
  ps->stdout_fd = mkstemp(stdoutfile);
  if(ps->stdout_fd < 0) goto prefork_fail;
  unlink(stdoutfile);
  strlcpy(stderrfile, "/tmp/noitext.XXXXXX", PATH_MAX);
  ps->stderr_fd = mkstemp(stderrfile);
  if(ps->stderr_fd < 0) goto prefork_fail;
  unlink(stderrfile);
  ps->pid = fork();
  if(ps->pid == -1) goto prefork_fail;

  /* Here.. fork has succeeded */
  if(ps->pid) {
    noit_skiplist_insert(&active_procs, ps);
    return 0;
  }
  /* Run the process */
  stdin_fd = open("/dev/null", O_RDONLY);
  if(stdin_fd < 0) close(0);
  else dup2(stdin_fd, 0);
  dup2(ps->stdout_fd, 1);
  dup2(ps->stderr_fd, 2);
  /* Shut off everything but std{in,out,err} */
  for(i=3;i<256;i++) close(i);
  execve(ps->path, ps->argv, ps->envp);
  exit(-1);
 prefork_fail:
  ps->status = -1;
  noit_skiplist_insert(&done_procs, ps);
  return -1;
}

int external_child(external_data_t *data) {
  in_fd = data->pipe_n2e[0];
  out_fd = data->pipe_e2n[1];
  nlerr = data->nlerr;
  nldeb = data->nldeb;

  /* switch to / */
  if(chdir("/") != 0) {
    noitL(noit_error, "Failed chdir(\"/\"): %s\n", strerror(errno));
    return -1;
  }

  signal(SIGCHLD, external_sigchld);
  noit_skiplist_init(&active_procs);
  noit_skiplist_set_compare(&active_procs, __proc_state_check_no,
                            __proc_state_check_no_key);
  noit_skiplist_add_index(&active_procs, __proc_state_pid,
                          __proc_state_pid_key);
  noit_skiplist_init(&done_procs);
  noit_skiplist_set_compare(&done_procs, __proc_state_check_no,
                            __proc_state_check_no_key);

  while(1) {
    struct pollfd pfd;
    struct proc_state *proc_state;
    int64_t check_no;
    int16_t argcnt, *arglens, envcnt, *envlens;
    int i;

    /* We poll here so that we can be interrupted by the SIGCHLD */
    pfd.fd = in_fd;
    pfd.events = POLLIN;
    while(poll(&pfd, 1, -1) == -1 && errno == EINTR) finish_procs();

    assert_read(in_fd, &check_no, sizeof(check_no));
    assert_read(in_fd, &argcnt, sizeof(argcnt));
    if(argcnt == 0) {
      /* cancellation */
      fetch_and_kill_by_check(check_no);
      continue;
    }
    assert(argcnt > 1);
    proc_state = calloc(1, sizeof(*proc_state));
    proc_state->stdout_fd = -1;
    proc_state->stderr_fd = -1;
    proc_state->check_no = check_no;

    /* read in the argument lengths */
    arglens = malloc(argcnt * sizeof(*arglens));
    assert_read(in_fd, arglens, argcnt * sizeof(*arglens));
    /* first string is the path, second is the first argv[0] */
    /* we need to allocate argcnt + 1 (NULL), but the first is path */
    proc_state->argv = malloc(argcnt * sizeof(*proc_state->argv));
    /* read each string, first in path, second into argv[0], ... */
    proc_state->path = malloc(arglens[0]);
    assert_read(in_fd, proc_state->path, arglens[0]);
    for(i=0; i<argcnt-1; i++) {
      proc_state->argv[i] = malloc(arglens[i+1]);
      assert_read(in_fd, proc_state->argv[i], arglens[i+1]);
    }
    proc_state->argv[i] = NULL;
    free(arglens);

    /* similar thing with envp, but no path trickery */
    assert_read(in_fd, &envcnt, sizeof(envcnt));
    envlens = malloc(envcnt * sizeof(*envlens));
    assert_read(in_fd, envlens, envcnt * sizeof(*envlens));
    proc_state->envp = malloc((envcnt+1) * sizeof(*proc_state->envp));
    for(i=0; i<envcnt; i++) {
      proc_state->envp[i] = malloc(envlens[i]);
      assert_read(in_fd, proc_state->envp[i], envlens[i]);
    }
    proc_state->envp[i] = NULL;
    free(envlens);

    /* All set, this just needs to be run */
    external_proc_spawn(proc_state);

    finish_procs();
  }
}

