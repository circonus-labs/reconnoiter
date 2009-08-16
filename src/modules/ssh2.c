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
#include <errno.h>
#include <assert.h>
#include <math.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"

#include <libssh2.h>

#define DEFAULT_SSH_PORT 22

typedef struct {
  noit_module_t *self;
  noit_check_t *check;
  enum {
    WANT_CONNECT = 0,
    WANT_CLOSE = 1
  } state;
  LIBSSH2_SESSION *session;
  int available;
  int timed_out;
  char *error;
  char fingerprint[33]; /* 32 hex characters */
  eventer_t synch_fd_event;
  eventer_t timeout_event; /* Only used for connect() */
} ssh2_check_info_t;

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;

static void ssh2_cleanup(noit_module_t *self, noit_check_t *check) {
  ssh2_check_info_t *ci = check->closure;
  if(ci) {
    if(ci->timeout_event) {
      eventer_remove(ci->timeout_event);
      eventer_free(ci->timeout_event);
    }
    if(ci->session) {
      libssh2_session_disconnect(ci->session, "Bye!");
      libssh2_session_free(ci->session);
    }
    if(ci->error) free(ci->error);
    memset(ci, 0, sizeof(*ci));
  }
}
static int ssh2_init(noit_module_t *self) {
  return 0;
}
static int ssh2_config(noit_module_t *self, noit_hash_table *options) {
  return 0;
}
static void ssh2_log_results(noit_module_t *self, noit_check_t *check) {
  stats_t current;
  struct timeval duration;
  ssh2_check_info_t *ci = check->closure;

  noit_check_stats_clear(&current);

  gettimeofday(&current.whence, NULL);
  sub_timeval(current.whence, check->last_fire_time, &duration);
  current.duration = duration.tv_sec * 1000 + duration.tv_usec / 1000;
  current.available = ci->available ? NP_AVAILABLE : NP_UNAVAILABLE;
  current.state = ci->fingerprint[0] ? NP_GOOD : NP_BAD;

  if(ci->error) current.status = ci->error;
  else if(ci->timed_out) current.status = "timeout";
  else if(ci->fingerprint[0]) current.status = ci->fingerprint;
  else current.status = "internal error";

  if(ci->fingerprint[0])
    noit_stats_set_metric(&current, "fingerprint", METRIC_STRING,
                          ci->fingerprint);
  noit_check_set_stats(self, check, &current);
}
static int ssh2_drive_session(eventer_t e, int mask, void *closure,
                              struct timeval *now) {
  int i;
  const char *fingerprint;
  ssh2_check_info_t *ci = closure;
  if(ci->state == WANT_CLOSE) {
    noit_check_t *check = ci->check;
    ssh2_log_results(ci->self, ci->check);
    ssh2_cleanup(ci->self, ci->check);
    eventer_remove_fd(e->fd);
    e->opset->close(e->fd, &mask, e);
    check->flags &= ~NP_RUNNING;
    return 0;
  }
  switch(mask) {
    case EVENTER_ASYNCH_WORK:
      if(eventer_set_fd_blocking(e->fd)) {
        ci->timed_out = 0;
        ci->error = strdup("socket error");
        return 0;
      }
      ci->session = libssh2_session_init();
      if (libssh2_session_startup(ci->session, e->fd)) {
        ci->timed_out = 0;
        ci->error = strdup("ssh session startup failed");
        return 0;
      }
      fingerprint = libssh2_hostkey_hash(ci->session, LIBSSH2_HOSTKEY_HASH_MD5);
      for(i=0;i<16;i++) {
        snprintf(ci->fingerprint + (i*2), 3, "%02x",
                 (unsigned char)fingerprint[i]);
      }
      ci->fingerprint[32] = '\0';
      ci->timed_out = 0;
      return 0;
      break;
    case EVENTER_ASYNCH_CLEANUP:
      if(ci->session) {
        libssh2_session_disconnect(ci->session, "Bye!");
        libssh2_session_free(ci->session);
        ci->session = NULL;
      }
      ci->state = WANT_CLOSE;
      break;
    default:
      abort();
  }
  return 0;
}
static int ssh2_connect_complete(eventer_t e, int mask, void *closure,
                                 struct timeval *now) {
  ssh2_check_info_t *ci = closure;
  eventer_t asynch_e;

  if(mask & EVENTER_EXCEPTION) {
    noit_check_t *check = ci->check;
    ci->timed_out = 0;
    ci->error = strdup("ssh connection failed");
    ssh2_log_results(ci->self, ci->check);
    ssh2_cleanup(ci->self, ci->check);
    eventer_remove_fd(e->fd);
    e->opset->close(e->fd, &mask, e);
    check->flags &= ~NP_RUNNING;
    return 0;
  }

  ci->available = 1;

  /* We steal the timeout_event as it has the exact timeout we want. */
  assert(ci->timeout_event);
  asynch_e = eventer_remove(ci->timeout_event);
  assert(asynch_e);
  ci->timeout_event = NULL;

  ci->synch_fd_event = NULL;
  asynch_e->fd = e->fd;
  asynch_e->callback = ssh2_drive_session;
  asynch_e->closure = closure;
  asynch_e->mask = EVENTER_ASYNCH;
  eventer_add(asynch_e);

  eventer_remove_fd(e->fd);
  return 0;
}
static int ssh2_connect_timeout(eventer_t e, int mask, void *closure,
                                struct timeval *now) {
  eventer_t fde;
  ssh2_check_info_t *ci = closure;
  noit_check_t *check = ci->check;
  
  ci->timeout_event = NULL; /* This is us, return 0 will free this */
  ci->error = strdup("ssh connect timeout");
  if(ci->synch_fd_event) {
    fde = ci->synch_fd_event;
    eventer_remove_fd(fde->fd);
    fde->opset->close(fde->fd, &mask, fde);
    eventer_free(fde);
     ci->synch_fd_event = NULL;
  }
  ssh2_log_results(ci->self, ci->check);
  ssh2_cleanup(ci->self, ci->check);
  check->flags &= ~NP_RUNNING;
  return 0;
}
static int ssh2_initiate(noit_module_t *self, noit_check_t *check) {
  ssh2_check_info_t *ci = check->closure;
  struct timeval p_int, __now;
  int fd, rv;
  eventer_t e;
  union {
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
  } sockaddr;
  socklen_t sockaddr_len;
  unsigned short ssh_port = DEFAULT_SSH_PORT;
  const char *port_str = NULL;

  /* We cannot be running */
  assert(!(check->flags & NP_RUNNING));
  check->flags |= NP_RUNNING;

  ci->self = self;
  ci->check = check;

  ci->timed_out = 1;
  if(ci->timeout_event) {
    eventer_remove(ci->timeout_event);
    free(ci->timeout_event->closure);
    eventer_free(ci->timeout_event);
    ci->timeout_event = NULL;
  }
  gettimeofday(&__now, NULL);
  memcpy(&check->last_fire_time, &__now, sizeof(__now));

  /* Open a socket */
  fd = socket(check->target_family, SOCK_STREAM, 0);
  if(fd < 0) goto fail;

  /* Make it non-blocking */
  if(eventer_set_fd_nonblocking(fd)) goto fail;

  if(noit_hash_retr_str(check->config, "port", strlen("port"),
                        &port_str)) {
    ssh_port = (unsigned short)atoi(port_str);
  }
  memset(&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sin6.sin6_family = check->target_family;
  if(check->target_family == AF_INET) {
    memcpy(&sockaddr.sin.sin_addr,
           &check->target_addr.addr, sizeof(sockaddr.sin.sin_addr));
    sockaddr.sin.sin_port = htons(ssh_port);
    sockaddr_len = sizeof(sockaddr.sin);
  }
  else {
    memcpy(&sockaddr.sin6.sin6_addr,
           &check->target_addr.addr6, sizeof(sockaddr.sin6.sin6_addr));
    sockaddr.sin6.sin6_port = htons(ssh_port);
    sockaddr_len = sizeof(sockaddr.sin6);
  }

  /* Initiate a connection */
  rv = connect(fd, (struct sockaddr *)&sockaddr, sockaddr_len);
  if(rv == -1 && errno != EINPROGRESS) goto fail;

  /* Register a handler for connection completion */
  e = eventer_alloc();
  e->fd = fd;
  e->mask = EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
  e->callback = ssh2_connect_complete;
  e->closure =  ci;
  ci->synch_fd_event = e;
  eventer_add(e);

  e = eventer_alloc();
  e->mask = EVENTER_TIMER;
  e->callback = ssh2_connect_timeout;
  e->closure = ci;
  memcpy(&e->whence, &__now, sizeof(__now));
  p_int.tv_sec = check->timeout / 1000;
  p_int.tv_usec = (check->timeout % 1000) * 1000;
  add_timeval(e->whence, p_int, &e->whence);
  ci->timeout_event = e;
  eventer_add(e);
  return 0;

 fail:
  if(fd >= 0) close(fd);
  ssh2_log_results(ci->self, ci->check);
  ssh2_cleanup(ci->self, ci->check);
  check->flags &= ~NP_RUNNING;
  return -1;
}

static int ssh2_initiate_check(noit_module_t *self, noit_check_t *check,
                               int once, noit_check_t *parent) {
  if(!check->closure) check->closure = calloc(1, sizeof(ssh2_check_info_t));
  INITIATE_CHECK(ssh2_initiate, self, check);
  return 0;
}

static int ssh2_onload(noit_image_t *self) {
  nlerr = noit_log_stream_find("error/ssh2");
  nldeb = noit_log_stream_find("debug/ssh2");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;

  eventer_name_callback("http/ssh2_connect_complete", ssh2_connect_complete);
  eventer_name_callback("http/ssh2_drive_session", ssh2_drive_session);
  return 0;
}

#include "ssh2.xmlh"
noit_module_t ssh2 = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "ssh2",
    "Secure Shell version 2 checker",
    ssh2_xml_description,
    ssh2_onload
  },
  ssh2_config,
  ssh2_init,
  ssh2_initiate_check,
  ssh2_cleanup
};

