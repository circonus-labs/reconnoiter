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

#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif
#include <pcre.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "utils/noit_log.h"
#include "utils/noit_security.h"
#include "external_proc.h"

struct check_info {
  int64_t check_no;
  u_int16_t argcnt;
  u_int16_t *arglens;
  char **args;
  u_int16_t envcnt;
  u_int16_t *envlens;
  char **envs;
  noit_check_t *check;
  int exit_code;

  int timedout;
  char *output;
  char *error;
  pcre *matcher;
  eventer_t timeout_event;
};

typedef struct external_closure {
  noit_module_t *self;
  noit_check_t *check;
} external_closure_t;

/* Protocol:
 *   noit 2 ext:
 *     int64(check_no)
 *     uint16(0) -> cancel .end

 *     int64(check_no)
 *     uint16(argcnt) [argcnt > 0]
 *     uint16(arglen) x argcnt  (arglen includes \0)
 *     string of sum(arglen)
 *     uint16(envcnt)
 *     uint16(envlen) x envcnt  (envlen includes \0)
 *     string of sum(envlen) -> execve .end
 *
 *   ext 2 noit:
 *     int64(check_no)
 *     int32(exitcode) [0 -> good, {1,2} -> bad, 3 -> unknown]
 *     uint16(outlen) (includes \0)
 *     string of outlen
 *     uint16(errlen) (includes \0)
 *     string of errlen -> complete .end
 */

static int external_config(noit_module_t *self, noit_hash_table *options) {
  external_data_t *data;
  data = noit_module_get_userdata(self);
  if(data) {
    if(data->options) {
      noit_hash_destroy(data->options, free, free);
      free(data->options);
    }
  }
  else
    data = calloc(1, sizeof(*data));
  data->options = options;
  if(!data->options) data->options = calloc(1, sizeof(*data->options));
  noit_module_set_userdata(self, data);
  return 1;
}

static void external_log_results(noit_module_t *self, noit_check_t *check) {
  external_data_t *data;
  struct check_info *ci;
  stats_t current;
  struct timeval duration;

  noit_check_stats_clear(&current);

  data = noit_module_get_userdata(self);
  ci = (struct check_info *)check->closure;

  noitL(data->nldeb, "external(%s) (timeout: %d, exit: %x)\n",
        check->target, ci->timedout, ci->exit_code);

  gettimeofday(&current.whence, NULL);
  sub_timeval(current.whence, check->last_fire_time, &duration);
  current.duration = duration.tv_sec * 1000 + duration.tv_usec / 1000;
  if(ci->timedout) {
    current.available = NP_UNAVAILABLE;
    current.state = NP_BAD;
  }
  else if(WEXITSTATUS(ci->exit_code) == 3) {
    current.available = NP_UNKNOWN;
    current.state = NP_UNKNOWN;
  }
  else {
    current.available = NP_AVAILABLE;
    current.state = (WEXITSTATUS(ci->exit_code) == 0) ? NP_GOOD : NP_BAD;
  }

  /* Hack the output into metrics */
  if(ci->output && ci->matcher) {
    int rc, len, startoffset = 0;
    int ovector[30];
    len = strlen(ci->output);
    noitL(data->nldeb, "going to match output at %d/%d\n", startoffset, len);
    while((rc = pcre_exec(ci->matcher, NULL, ci->output, len, startoffset, 0,
                          ovector, sizeof(ovector)/sizeof(*ovector))) > 0) {
      char metric[128];
      char value[128];
      startoffset = ovector[1];
      noitL(data->nldeb, "matched at offset %d\n", rc);
      if(pcre_copy_named_substring(ci->matcher, ci->output, ovector, rc,
                                   "key", metric, sizeof(metric)) > 0 &&
         pcre_copy_named_substring(ci->matcher, ci->output, ovector, rc,
                                   "value", value, sizeof(value)) > 0) {
        /* We're able to extract something... */
        noit_stats_set_metric(&current, metric, METRIC_GUESS, value);
      }
      noitL(data->nldeb, "going to match output at %d/%d\n", startoffset, len);
    }
    noitL(data->nldeb, "match failed.... %d\n", rc);
  }

  current.status = ci->output;
  noit_check_set_stats(self, check, &current);

  /* If we didn't exit normally, or we core, or we have stderr to report...
   * provide a full report.
   */
  if((WTERMSIG(ci->exit_code) != SIGQUIT && WTERMSIG(ci->exit_code) != 0) ||
     WCOREDUMP(ci->exit_code) ||
     (ci->error && *ci->error)) {
    char uuid_str[37];
    uuid_unparse_lower(check->checkid, uuid_str);
    noitL(data->nlerr, "external/%s: (sig:%d%s) [%s]\n", uuid_str,
          WTERMSIG(ci->exit_code), WCOREDUMP(ci->exit_code)?", cored":"",
          ci->error ? ci->error : "");
  }
}
static int external_timeout(eventer_t e, int mask,
                            void *closure, struct timeval *now) {
  external_closure_t *ecl = (external_closure_t *)closure;
  struct check_info *data;
  if(!NOIT_CHECK_KILLED(ecl->check) && !NOIT_CHECK_DISABLED(ecl->check)) {
    data = (struct check_info *)ecl->check->closure;
    data->timedout = 1;
    data->exit_code = 3;
    external_log_results(ecl->self, ecl->check);
    data->timeout_event = NULL;
  }
  ecl->check->flags &= ~NP_RUNNING;
  free(ecl);
  return 0;
}
static void check_info_clean(struct check_info *ci) {
  int i;
  for(i=0; i<ci->argcnt; i++)
    if(ci->args[i]) free(ci->args[i]);
  if(ci->arglens) free(ci->arglens);
  if(ci->args) free(ci->args);
  for(i=0; i<ci->envcnt; i++)
    if(ci->envs[i]) free(ci->envs[i]);
  if(ci->envlens) free(ci->envlens);
  if(ci->envs) free(ci->envs);
  if(ci->matcher) pcre_free(ci->matcher);
  memset(ci, 0, sizeof(*ci));
}
static int external_handler(eventer_t e, int mask,
                            void *closure, struct timeval *now) {
  noit_module_t *self = (noit_module_t *)closure;
  external_data_t *data;

  data = noit_module_get_userdata(self);
  while(1) {
    int inlen, expectlen;
    noit_check_t *check;
    struct check_info *ci;
    void *vci;

    if(!data->cr) {
      struct external_response r;
      struct msghdr msg;
      struct iovec v[3];
      memset(&r, 0, sizeof(r));
      v[0].iov_base = &r.check_no;
      v[0].iov_len = sizeof(r.check_no);
      v[1].iov_base = &r.exit_code;
      v[1].iov_len = sizeof(r.exit_code);
      v[2].iov_base = &r.stdoutlen;
      v[2].iov_len = sizeof(r.stdoutlen);
      expectlen = v[0].iov_len + v[1].iov_len + v[2].iov_len;

      /* Make this into a recv'ble message so we can PEEK */
      memset(&msg, 0, sizeof(msg));
      msg.msg_iov = v;
      msg.msg_iovlen = 3;
      inlen = recvmsg(e->fd, &msg, MSG_PEEK);
      if(inlen == 0) goto widowed;
      if((inlen == -1 && errno == EAGAIN) ||
         (inlen > 0 && inlen < expectlen))
        return EVENTER_READ | EVENTER_EXCEPTION;
      if(inlen == -1)
        noitL(noit_error, "recvmsg() failed: %s\n", strerror(errno));
      assert(inlen == expectlen);
      while(-1 == (inlen = recvmsg(e->fd, &msg, 0)) && errno == EINTR);
      assert(inlen == expectlen);
      data->cr = calloc(sizeof(*data->cr), 1);
      memcpy(data->cr, &r, sizeof(r));
      data->cr->stdoutbuff = malloc(data->cr->stdoutlen);
    }
    if(data->cr) {
      while(data->cr->stdoutlen_sofar < data->cr->stdoutlen) {
        while((inlen =
                 read(e->fd,
                      data->cr->stdoutbuff + data->cr->stdoutlen_sofar,
                      data->cr->stdoutlen - data->cr->stdoutlen_sofar)) == -1 &&
               errno == EINTR);
        if(inlen == -1 && errno == EAGAIN)
          return EVENTER_READ | EVENTER_EXCEPTION;
        if(inlen == 0) goto widowed;
        data->cr->stdoutlen_sofar += inlen;
      }
      assert(data->cr->stdoutbuff[data->cr->stdoutlen-1] == '\0');
      if(!data->cr->stderrbuff) {
        while((inlen = read(e->fd, &data->cr->stderrlen,
                            sizeof(data->cr->stderrlen))) == -1 &&
              errno == EINTR);
        if(inlen == -1 && errno == EAGAIN)
          return EVENTER_READ | EVENTER_EXCEPTION;
        if(inlen == 0) goto widowed;
        assert(inlen == sizeof(data->cr->stderrlen));
        data->cr->stderrbuff = malloc(data->cr->stderrlen);
      }
      while(data->cr->stderrlen_sofar < data->cr->stderrlen) {
        while((inlen =
                 read(e->fd,
                      data->cr->stderrbuff + data->cr->stderrlen_sofar,
                      data->cr->stderrlen - data->cr->stderrlen_sofar)) == -1 &&
               errno == EINTR);
        if(inlen == -1 && errno == EAGAIN)
          return EVENTER_READ | EVENTER_EXCEPTION;
        if(inlen == 0) goto widowed;
        data->cr->stderrlen_sofar += inlen;
      }
      assert(data->cr->stderrbuff[data->cr->stderrlen-1] == '\0');
    }
    assert(data->cr && data->cr->stdoutbuff && data->cr->stderrbuff);

    gettimeofday(now, NULL); /* set it, as we care about accuracy */

    /* Lookup data in check_no hash */
    if(noit_hash_retrieve(&data->external_checks,
                          (const char *)&data->cr->check_no,
                          sizeof(data->cr->check_no),
                          &vci) == 0)
      vci = NULL;
    ci = (struct check_info *)vci;

    /* We've seen it, it ain't coming again...
     * remove it, we'll free it ourselves */
    noit_hash_delete(&data->external_checks,
                     (const char *)&data->cr->check_no,
                     sizeof(data->cr->check_no), NULL, NULL);

    /* If there is no timeout_event, the check must have completed.
     * We have nothing to do. */
    if(!ci || !ci->timeout_event) {
      free(data->cr->stdoutbuff);
      free(data->cr->stderrbuff);
      free(data->cr);
      data->cr = NULL;
      continue;
    }
    ci->exit_code = data->cr->exit_code;
    ci->output = data->cr->stdoutbuff;
    ci->error = data->cr->stderrbuff;
    free(data->cr);
    data->cr = NULL;
    check = ci->check;
    external_log_results(self, check);
    eventer_remove(ci->timeout_event);
    free(ci->timeout_event->closure);
    eventer_free(ci->timeout_event);
    ci->timeout_event = NULL;
    check->flags &= ~NP_RUNNING;
  }
  return EVENTER_READ;

 widowed:
  noitL(noit_error, "external module terminated, must restart.\n");
  exit(1);
}

static int external_init(noit_module_t *self) {
  external_data_t *data;

  data = noit_module_get_userdata(self);
  if(!data) data = malloc(sizeof(*data));
  data->nlerr = noit_log_stream_find("error/external");
  data->nldeb = noit_log_stream_find("debug/external");

  data->jobq = calloc(1, sizeof(*data->jobq));
  eventer_jobq_init(data->jobq, "external");
  data->jobq->backq = eventer_default_backq();
  eventer_jobq_increase_concurrency(data->jobq);

  if(socketpair(AF_UNIX, SOCK_STREAM, 0, data->pipe_n2e) != 0 ||
     socketpair(AF_UNIX, SOCK_STREAM, 0, data->pipe_e2n) != 0) {
    noitL(noit_error, "external: pipe() failed: %s\n", strerror(errno));
    return -1;
  }

  data->child = fork();
  if(data->child == -1) {
    /* No child, bail. */
    noitL(noit_error, "external: fork() failed: %s\n", strerror(errno));
    return -1;
  }

  /* parent must close the read side of n2e and the write side of e2n */
  /* The child must do the opposite */
  close(data->pipe_n2e[(data->child == 0) ? 1 : 0]);
  close(data->pipe_e2n[(data->child == 0) ? 0 : 1]);

  /* Now the parent must set its bits non-blocking, the child need not */
  if(data->child != 0) {
    long on = 1;
    /* in the parent */
    if(ioctl(data->pipe_e2n[0], FIONBIO, &on) == -1) {
      close(data->pipe_n2e[1]);
      close(data->pipe_e2n[0]);
      noitL(noit_error,
            "external: could not set pipe non-blocking: %s\n",
            strerror(errno));
      return -1;
    }
    eventer_t newe;
    newe = eventer_alloc();
    newe->fd = data->pipe_e2n[0];
    newe->mask = EVENTER_READ | EVENTER_EXCEPTION;
    newe->callback = external_handler;
    newe->closure = self;
    eventer_add(newe);
  }
  else {
    const char *user = NULL, *group = NULL;
    if(data->options) {
      noit_hash_retr_str(data->options, "user", 4, &user);
      noit_hash_retr_str(data->options, "group", 4, &group);
    }
    noit_security_usergroup(user, group);
    exit(external_child(data));
  }
  noit_module_set_userdata(self, data);
  return 0;
}

static void external_cleanup(noit_module_t *self, noit_check_t *check) {
  struct check_info *ci = (struct check_info *)check->closure;
  if(ci) {
    if(ci->timeout_event) {
      eventer_remove(ci->timeout_event);
      free(ci->timeout_event->closure);
      eventer_free(ci->timeout_event);
      ci->timeout_event = NULL;
    }
  }
}
#define assert_write(fd, w, s) assert(write(fd, w, s) == s)
static int external_enqueue(eventer_t e, int mask, void *closure,
                            struct timeval *now) {
  external_closure_t *ecl = (external_closure_t *)closure;
  struct check_info *ci = (struct check_info *)ecl->check->closure;
  external_data_t *data;
  int fd, i;

  if(mask == EVENTER_ASYNCH_CLEANUP) {
    e->mask = 0;
    return 0;
  }
  if(!(mask & EVENTER_ASYNCH_WORK)) return 0;
  data = noit_module_get_userdata(ecl->self);
  fd = data->pipe_n2e[1];
  assert_write(fd, &ci->check_no, sizeof(ci->check_no));
  assert_write(fd, &ci->argcnt, sizeof(ci->argcnt));
  assert_write(fd, ci->arglens, sizeof(*ci->arglens)*ci->argcnt);
  for(i=0; i<ci->argcnt; i++)
    assert_write(fd, ci->args[i], ci->arglens[i]);
  assert_write(fd, &ci->envcnt, sizeof(ci->envcnt));
  assert_write(fd, ci->envlens, sizeof(*ci->envlens)*ci->envcnt);
  for(i=0; i<ci->envcnt; i++)
    assert_write(fd, ci->envs[i], ci->envlens[i]);
  return 0;
}
static int external_invoke(noit_module_t *self, noit_check_t *check) {
  struct timeval when, p_int;
  external_closure_t *ecl;
  struct check_info *ci = (struct check_info *)check->closure;
  eventer_t newe;
  external_data_t *data;
  noit_hash_table check_attrs_hash = NOIT_HASH_EMPTY;
  int i, klen;
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *name, *value;
  char interp_fmt[4096], interp_buff[4096];

  data = noit_module_get_userdata(self);

  check->flags |= NP_RUNNING;
  noitL(data->nldeb, "external_invoke(%p,%s)\n",
        self, check->target);

  /* remove a timeout if we still have one -- we should unless someone
   * has set a lower timeout than the period.
   */
  if(ci->timeout_event) {
    eventer_remove(ci->timeout_event);
    free(ci->timeout_event->closure);
    eventer_free(ci->timeout_event);
    ci->timeout_event = NULL;
  }

  check_info_clean(ci);

  gettimeofday(&when, NULL);
  memcpy(&check->last_fire_time, &when, sizeof(when));

  /* Setup all our check bits */
  ci->check_no = noit_atomic_inc64(&data->check_no_seq);
  ci->check = check;
  /* We might want to extract metrics */
  if(noit_hash_retr_str(check->config,
                        "output_extract", strlen("output_extract"),
                        &value) != 0) {
    const char *error;
    int erroffset;
    ci->matcher = pcre_compile(value, 0, &error, &erroffset, NULL);
    if(!ci->matcher) {
      noitL(data->nlerr, "external pcre /%s/ failed @ %d: %s\n",
            value, erroffset, error);
    }
  }

  noit_check_make_attrs(check, &check_attrs_hash);

  /* Count the args */
  i = 1;
  while(1) {
    char argname[10];
    snprintf(argname, sizeof(argname), "arg%d", i);
    if(noit_hash_retr_str(check->config, argname, strlen(argname),
                          &value) == 0) break;
    i++;
  }
  ci->argcnt = i + 1; /* path, arg0, (i-1 more args) */
  ci->arglens = calloc(ci->argcnt, sizeof(*ci->arglens));
  ci->args = calloc(ci->argcnt, sizeof(*ci->args));

  /* Make the command */
  if(noit_hash_retr_str(check->config, "command", strlen("command"),
                        &value) == 0) {
    value = "/bin/true";
  }
  ci->args[0] = strdup(value);
  ci->arglens[0] = strlen(ci->args[0]) + 1;

  i = 0;
  while(1) {
    char argname[10];
    snprintf(argname, sizeof(argname), "arg%d", i);
    if(noit_hash_retr_str(check->config, argname, strlen(argname),
                          &value) == 0) {
      if(i == 0) {
        /* if we don't have arg0, make it last element of path */
        char *cp = ci->args[0] + strlen(ci->args[0]);
        while(cp > ci->args[0] && *(cp-1) != '/') cp--;
        value = cp;
      }
      else break; /* if we don't have argn, we're done */
    }
    noit_check_interpolate(interp_buff, sizeof(interp_buff), value,
                           &check_attrs_hash, check->config);
    ci->args[i+1] = strdup(interp_buff);
    ci->arglens[i+1] = strlen(ci->args[i+1]) + 1;
    i++;
  }

  /* Make the environment */
  memset(&iter, 0, sizeof(iter));
  ci->envcnt = 0;
  while(noit_hash_next_str(check->config, &iter, &name, &klen, &value))
    if(!strncasecmp(name, "env_", 4))
      ci->envcnt++;
  memset(&iter, 0, sizeof(iter));
  ci->envlens = calloc(ci->envcnt, sizeof(*ci->envlens));
  ci->envs = calloc(ci->envcnt, sizeof(*ci->envs));
  ci->envcnt = 0;
  while(noit_hash_next_str(check->config, &iter, &name, &klen, &value))
    if(!strncasecmp(name, "env_", 4)) {
      snprintf(interp_fmt, sizeof(interp_fmt), "%s=%s", name+4, value);
      noit_check_interpolate(interp_buff, sizeof(interp_buff), interp_fmt,
                             &check_attrs_hash, check->config);
      ci->envs[ci->envcnt] = strdup(interp_buff);
      ci->envlens[ci->envcnt] = strlen(ci->envs[ci->envcnt]) + 1;
      ci->envcnt++;
    }

  noit_hash_destroy(&check_attrs_hash, NULL, NULL);

  noit_hash_store(&data->external_checks,
                  (const char *)&ci->check_no, sizeof(ci->check_no),
                  ci);

  /* Setup a timeout */
  newe = eventer_alloc();
  newe->mask = EVENTER_TIMER;
  gettimeofday(&when, NULL);
  p_int.tv_sec = check->timeout / 1000;
  p_int.tv_usec = (check->timeout % 1000) * 1000;
  add_timeval(when, p_int, &newe->whence);
  ecl = calloc(1, sizeof(*ecl));
  ecl->self = self;
  ecl->check = check;
  newe->closure = ecl;
  newe->callback = external_timeout;
  eventer_add(newe);
  ci->timeout_event = newe;

  /* Setup push */
  newe = eventer_alloc();
  newe->mask = EVENTER_ASYNCH;
  add_timeval(when, p_int, &newe->whence);
  ecl = calloc(1, sizeof(*ecl));
  ecl->self = self;
  ecl->check = check;
  newe->closure = ecl;
  newe->callback = external_enqueue;
  eventer_add(newe);

  return 0;
}
static int external_initiate_check(noit_module_t *self, noit_check_t *check,
                                    int once, noit_check_t *cause) {
  if(!check->closure) check->closure = calloc(1, sizeof(struct check_info));
  INITIATE_CHECK(external_invoke, self, check);
  return 0;
}

static int external_onload(noit_image_t *self) {
  eventer_name_callback("external/timeout", external_timeout);
  eventer_name_callback("external/handler", external_handler);
  return 0;
}

#include "external.xmlh"
noit_module_t external = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "external",
    "checks via external programs",
    external_xml_description,
    external_onload
  },
  external_config,
  external_init,
  external_initiate_check,
  external_cleanup
};

