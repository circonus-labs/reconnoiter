/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
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
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
#include <dlfcn.h>

#include <mtev_hash.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_mtev_bridge.h"

#include <libssh2.h>
#ifdef HAVE_GCRYPT_H
#include <gcrypt.h>
#endif

#define DEFAULT_SSH_PORT 22

typedef struct {
  noit_module_t *self;
  noit_check_t *check;
  struct {
    char *kex;
    char *hostkey;
    char *crypt_cs;
    char *crypt_sc;
    char *mac_cs;
    char *mac_sc;
    char *comp_cs;
    char *comp_sc;
  } methods;
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
  struct timeval timeout_timeval;
} ssh2_check_info_t;

static mtev_log_stream_t nlerr = NULL;
static mtev_log_stream_t nldeb = NULL;

static void ssh2_cleanse(noit_module_t *self, noit_check_t *check) {
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
    if(ci->methods.kex) free(ci->methods.kex);
    if(ci->methods.hostkey) free(ci->methods.hostkey);
    if(ci->methods.crypt_cs) free(ci->methods.crypt_cs);
    if(ci->methods.crypt_sc) free(ci->methods.crypt_sc);
    if(ci->methods.mac_cs) free(ci->methods.mac_cs);
    if(ci->methods.mac_sc) free(ci->methods.mac_sc);
    if(ci->methods.comp_cs) free(ci->methods.comp_cs);
    if(ci->methods.comp_sc) free(ci->methods.comp_sc);
    if(ci->error) free(ci->error);
    memset(ci, 0, sizeof(*ci));
  }
}
static void ssh2_cleanup(noit_module_t *self, noit_check_t *check) {
  ssh2_check_info_t *ci = check->closure;
  ssh2_cleanse(self, check);
  free(ci);
}

#ifdef HAVE_GCRYPT_H
GCRY_THREAD_OPTION_PTHREAD_IMPL;
#endif

static int ssh2_init(noit_module_t *self) {
#ifdef HAVE_GCRYPT_H
  gcry_error_t (*control)(enum gcry_ctl_cmds CMD, ...);
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void *)0)
#endif
  control = dlsym(RTLD_DEFAULT, "gcry_control");
  if(control) control(GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);
#endif
  return 0;
}
static int ssh2_config(noit_module_t *self, mtev_hash_table *options) {
  return 0;
}
static void ssh2_log_results(noit_module_t *self, noit_check_t *check) {
  struct timeval duration, now;
  uint32_t mduration;
  ssh2_check_info_t *ci = check->closure;

  mtev_gettimeofday(&now, NULL);
  sub_timeval(now, check->last_fire_time, &duration);
  mduration = duration.tv_sec * 1000 + duration.tv_usec / 1000;
  noit_stats_set_whence(check, &now);
  noit_stats_set_duration(check, mduration);
  noit_stats_set_available(check, ci->available ? NP_AVAILABLE : NP_UNAVAILABLE);
  noit_stats_set_state(check, ci->fingerprint[0] ? NP_GOOD : NP_BAD);

  if(ci->error) noit_stats_set_status(check, ci->error);
  else if(ci->timed_out) noit_stats_set_status(check, "timeout");
  else if(ci->fingerprint[0]) noit_stats_set_status(check, ci->fingerprint);
  else noit_stats_set_status(check, "internal error");

  if(ci->fingerprint[0]) {
    noit_stats_set_metric(check, "duration", METRIC_UINT32, &mduration);
    noit_stats_set_metric(check, "fingerprint", METRIC_STRING,
                          ci->fingerprint);
  }
  noit_check_set_stats(check);
}
static int ssh2_drive_session(eventer_t e, int mask, void *closure,
                              struct timeval *now) {
  int i;
  const char *fingerprint;
  ssh2_check_info_t *ci = closure;
  struct timeval diff;
  int timeout_ms = 10; /* 10ms, gets set below */
  if(ci->state == WANT_CLOSE) {
    noit_check_t *check = ci->check;
    ssh2_log_results(ci->self, ci->check);
    ssh2_cleanse(ci->self, ci->check);
    eventer_remove_fde(e);
    eventer_close(e, &mask);
    noit_check_end(check);
    return 0;
  }
  switch(mask) {
    case EVENTER_ASYNCH_WORK:
      if(eventer_set_fd_blocking(eventer_get_fd(e))) {
        ci->timed_out = 0;
        ci->error = strdup("socket error");
        return 0;
      }
      ci->session = libssh2_session_init();
#define set_method(a,b) do { \
  int rv; \
  if(ci->methods.a && \
     (rv = libssh2_session_method_pref(ci->session, b, ci->methods.a)) != 0) { \
    ci->timed_out = 0; \
    ci->error = strdup((rv == LIBSSH2_ERROR_METHOD_NOT_SUPPORTED) ? \
                         #a " method not supported" : "error setting " #a); \
    return 0; \
  } \
} while(0)
      set_method(kex, LIBSSH2_METHOD_KEX);
      set_method(hostkey, LIBSSH2_METHOD_HOSTKEY);
      set_method(crypt_cs, LIBSSH2_METHOD_CRYPT_CS);
      set_method(crypt_sc, LIBSSH2_METHOD_CRYPT_SC);
      set_method(mac_cs, LIBSSH2_METHOD_MAC_CS);
      set_method(mac_sc, LIBSSH2_METHOD_MAC_SC);
      set_method(comp_cs, LIBSSH2_METHOD_COMP_CS);
      set_method(comp_sc, LIBSSH2_METHOD_COMP_SC);
#if LIBSSH2_VERSION_NUM >= 0x010209
      if(compare_timeval(*now, ci->timeout_timeval) < 0) {
        sub_timeval(ci->timeout_timeval, *now, &diff);
        timeout_ms = diff.tv_sec * 1000 + diff.tv_usec / 1000;
      }
      libssh2_session_set_timeout(ci->session, timeout_ms);
#endif
      if (libssh2_session_startup(ci->session, eventer_get_fd(e))) {
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
      mtevFatal(mtev_error, "Unknown mask: 0x%04x\n", mask);
  }
  return 0;
}
static int ssh2_needs_bytes_as_libssh2_is_impatient(eventer_t e, int mask, void *closure,
                                                    struct timeval *now) {
  ssh2_check_info_t *ci = closure;
  eventer_t asynch_e;
  struct timeval timeout_tv;

  if(mask & EVENTER_EXCEPTION) {
    noit_check_t *check = ci->check;
    ci->timed_out = 0;
    ci->error = strdup("ssh connection failed");
    ssh2_log_results(ci->self, ci->check);
    ssh2_cleanse(ci->self, ci->check);
    eventer_remove_fde(e);
    eventer_close(e, &mask);
    noit_check_end(check);
    return 0;
  }

  /* We steal the timeout_event as it has the exact timeout we want and copy
   * the value into ci->timeout_timeval */
  mtevAssert(ci->timeout_event);
  asynch_e = eventer_remove(ci->timeout_event);
  mtevAssert(asynch_e);
  timeout_tv = eventer_get_whence(asynch_e);
  memcpy(&ci->timeout_timeval, &timeout_tv, sizeof(ci->timeout_timeval));

  eventer_free(asynch_e);
  ci->timeout_event = NULL;

  ci->synch_fd_event = NULL;
  asynch_e = eventer_alloc_fd(ssh2_drive_session, closure,
                              eventer_get_fd(e), EVENTER_ASYNCH);
  eventer_add(asynch_e);

  eventer_remove_fde(e);
  return 0;
}
static int ssh2_connect_complete(eventer_t e, int mask, void *closure,
                                 struct timeval *now) {
  ssh2_check_info_t *ci = closure;

  if(mask & EVENTER_EXCEPTION) {
    noit_check_t *check = ci->check;
    ci->timed_out = 0;
    ci->error = strdup("ssh connection failed");
    ssh2_log_results(ci->self, ci->check);
    ssh2_cleanse(ci->self, ci->check);
    eventer_remove_fde(e);
    eventer_close(e, &mask);
    noit_check_end(check);
    return 0;
  }

  ci->available = 1;
  eventer_set_callback(e, ssh2_needs_bytes_as_libssh2_is_impatient);
  return EVENTER_READ | EVENTER_EXCEPTION;
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
    eventer_remove_fde(fde);
    eventer_close(fde, &mask);
    eventer_free(fde);
     ci->synch_fd_event = NULL;
  }
  ssh2_log_results(ci->self, ci->check);
  ssh2_cleanse(ci->self, ci->check);
  noit_check_end(check);
  return 0;
}
static int ssh2_initiate(noit_module_t *self, noit_check_t *check,
                         noit_check_t *cause) {
  ssh2_check_info_t *ci = check->closure;
  struct timeval __now;
  int fd = -1, rv = -1;
  eventer_t e;
  union {
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
  } sockaddr;
  socklen_t sockaddr_len;
  unsigned short ssh_port = DEFAULT_SSH_PORT;
  const char *port_str = NULL;

  /* We cannot be running */
  BAIL_ON_RUNNING_CHECK(check);
  noit_check_begin(check);

  ci->self = self;
  ci->check = check;

  ci->timed_out = 1;
  if(ci->timeout_event) {
    eventer_remove(ci->timeout_event);
    free(eventer_get_closure(ci->timeout_event));
    eventer_free(ci->timeout_event);
    ci->timeout_event = NULL;
  }
  mtev_gettimeofday(&__now, NULL);
  memcpy(&check->last_fire_time, &__now, sizeof(__now));

  if(check->target_ip[0] == '\0') {
    ci->error = strdup("name resolution failure");
    goto fail;
  }
  /* Open a socket */
  fd = socket(check->target_family, NE_SOCK_CLOEXEC|SOCK_STREAM, 0);
  if(fd < 0) goto fail;

  /* Make it non-blocking */
  if(eventer_set_fd_nonblocking(fd)) goto fail;

  if(mtev_hash_retr_str(check->config, "port", strlen("port"),
                        &port_str)) {
    ssh_port = (unsigned short)atoi(port_str);
  }
#define config_method(a) do { \
  const char *v; \
  if(mtev_hash_retr_str(check->config, "method_" #a, strlen("method_" #a), \
                        &v)) \
    ci->methods.a = strdup(v); \
} while(0)
  config_method(kex);
  config_method(hostkey);
  config_method(crypt_cs);
  config_method(crypt_sc);
  config_method(mac_cs);
  config_method(mac_sc);
  config_method(comp_cs);
  config_method(comp_sc);
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
  e = eventer_alloc_fd(ssh2_connect_complete, ci, fd,
                       EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION);
  ci->synch_fd_event = e;
  eventer_add(e);

  e = eventer_in_s_us(ssh2_connect_timeout, ci,
                      check->timeout / 1000, (check->timeout % 1000) * 1000);
  ci->timeout_event = e;
  eventer_add(e);
  return 0;

 fail:
  if(fd >= 0) close(fd);
  ssh2_log_results(ci->self, ci->check);
  ssh2_cleanse(ci->self, ci->check);
  noit_check_end(check);
  return -1;
}

static int ssh2_initiate_check(noit_module_t *self, noit_check_t *check,
                               int once, noit_check_t *cause) {
  if(!check->closure) check->closure = calloc(1, sizeof(ssh2_check_info_t));
  INITIATE_CHECK(ssh2_initiate, self, check, cause);
  return 0;
}

static int ssh2_onload(mtev_image_t *self) {
  nlerr = mtev_log_stream_find("error/ssh2");
  nldeb = mtev_log_stream_find("debug/ssh2");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;

  eventer_name_callback("http/ssh2_connect_complete", ssh2_connect_complete);
  eventer_name_callback("http/ssh2_drive_session", ssh2_drive_session);
  return 0;
}

#include "ssh2.xmlh"
noit_module_t ssh2 = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "ssh2",
    .description = "Secure Shell version 2 checker",
    .xml_description = ssh2_xml_description,
    .onload = ssh2_onload
  },
  ssh2_config,
  ssh2_init,
  ssh2_initiate_check,
  ssh2_cleanup
};

