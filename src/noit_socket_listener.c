/*
 * Copyright (c) 2019, Circonus, Inc. All rights reserved.
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
 *     * Neither the name Circonnus, Inc. nor the names
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
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#define FLUSH_SIZE 100

#include <mtev_defines.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>

#include <mtev_rest.h>
#include <mtev_hash.h>
#include <mtev_json.h>
#include <mtev_uuid.h>
#include <mtev_b64.h>
#include <mtev_dyn_buffer.h>
#include <mtev_memory.h>
#include <ck_pr.h>

#include "noit_metric.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_mtev_bridge.h"
#include "noit_socket_listener.h"

static mtev_log_stream_t nldeb = NULL;
static mtev_log_stream_t nlerr = NULL;

static void metric_local_free(void *vm) {
  if(vm) {
    metric_t *m = vm;
    free(m->metric_name);
    free(m->metric_value.vp);
  }
  free(vm);
}

void
listener_closure_ref(listener_closure_t *lc)
{
  ck_pr_inc_int(&lc->refcnt);
}

static void
listener_closure_deref(listener_closure_t *lc)
{
  bool zero;
  ck_pr_dec_int_zero(&lc->refcnt, &zero);
  if(!zero) return;

  mtev_dyn_buffer_destroy(&lc->buffer);
  mtev_hash_delete_all(lc->immediate_metrics, NULL, metric_local_free);
  /* no need to free `lc` here as the noit cleanup code will
   * free the check's closure for us */
  free(lc);
}

int
listener_submit(noit_module_t *self, noit_check_t *check, noit_check_t *cause)
{
  listener_closure_t *ccl;
  struct timeval duration;
  /* We are passive, so we don't do anything for transient checks */
  if(check->flags & NP_TRANSIENT) return 0;

  if(!check->closure) {
    ccl = check->closure = (void *)calloc(1, sizeof(listener_closure_t));
    ccl->self = self;
  } else {
    // Don't count the first run
    struct timeval now;
    char human_buffer[256];
    int stats_count = 0;
    stats_t *s = noit_check_get_stats_inprogress(check);

    mtev_gettimeofday(&now, NULL);
    sub_timeval(now, check->last_fire_time, &duration);
    noit_stats_set_whence(check, &now);
    noit_stats_set_duration(check, duration.tv_sec * 1000 + duration.tv_usec / 1000);

    /* We just want to set the number of metrics here to the number
     * of metrics in the stats_t struct */
    mtev_memory_begin();
    if (s) {
      mtev_hash_table *metrics = noit_check_stats_metrics(s);
      if (metrics) {
        stats_count = mtev_hash_size(metrics);
      }
    }
    mtev_memory_end();

    snprintf(human_buffer, sizeof(human_buffer),
             "dur=%ld,run=%d,stats=%d", duration.tv_sec * 1000 + duration.tv_usec / 1000,
             check->generation, stats_count);
    mtevL(nldeb, "%s(%s) [%s]\n", check->module, check->target, human_buffer);

    // Not sure what to do here
    noit_stats_set_available(check, (stats_count > 0) ?
        NP_AVAILABLE : NP_UNAVAILABLE);
    noit_stats_set_state(check, (stats_count > 0) ?
        NP_GOOD : NP_BAD);
    noit_stats_set_status(check, human_buffer);
    if(check->last_fire_time.tv_sec)
      noit_check_passive_set_stats(check);

    memcpy(&check->last_fire_time, &now, sizeof(now));
  }
  return 0;
}

void
listener_flush_immediate(listener_closure_t *rxc) {
  struct timeval now;
  mtev_gettimeofday(&now, NULL);
  noit_check_log_bundle_metrics(rxc->check, &now, rxc->immediate_metrics);
  mtev_hash_delete_all(rxc->immediate_metrics, NULL, metric_local_free);
}

void 
listener_metric_track_or_log(void *vrxc, const char *name, 
                             metric_type_t t, const void *vp, struct timeval *w) {
  listener_closure_t *rxc = vrxc;
  if(t == METRIC_GUESS) return;
  void *vm;
  if(rxc->immediate_metrics == NULL) {
    rxc->immediate_metrics = calloc(1, sizeof(*rxc->immediate_metrics));
    mtev_hash_init(rxc->immediate_metrics);
  }
  if(mtev_hash_retrieve(rxc->immediate_metrics, name, strlen(name), &vm) ||
     mtev_hash_size(rxc->immediate_metrics) > FLUSH_SIZE) {
    /* collision, just log it out */
    listener_flush_immediate(rxc);
  }
  metric_t *m = calloc(1, sizeof(*m));
  memset(m, 0, sizeof(*m));
  m->metric_name = strdup(name);
  m->metric_type = t;
  if(w) {
    memcpy(&m->whence, w, sizeof(struct timeval));
  }
  if(vp) {
    if(t == METRIC_STRING) m->metric_value.s = strdup((const char *)vp);
    else {
      size_t vsize = 0;
      switch(m->metric_type) {
        case METRIC_INT32:
          vsize = sizeof(int32_t);
          break;
        case METRIC_UINT32:
          vsize = sizeof(uint32_t);
          break;
        case METRIC_INT64:
          vsize = sizeof(int64_t);
          break;
        case METRIC_UINT64:
          vsize = sizeof(uint64_t);
          break;
        case METRIC_DOUBLE:
          vsize = sizeof(double);
          break;
        default:
          break;
      }
      if(vsize) {
        m->metric_value.vp = malloc(vsize);
        memcpy(m->metric_value.vp, vp, vsize);
      }
    }
  }
  noit_stats_mark_metric_logged(noit_check_get_stats_inprogress(rxc->check), m, mtev_false);
  mtev_hash_store(rxc->immediate_metrics, m->metric_name, strlen(m->metric_name), m);
}

int
listener_handler(eventer_t e, int mask, void *closure, struct timeval *now)
{
  int newmask = EVENTER_READ | EVENTER_EXCEPTION;
  listener_closure_t *self = (listener_closure_t *)closure;
  int rows_per_cycle = 0, records_this_loop = 0;
  noit_check_t *check = self->check;
  rows_per_cycle = self->rows_per_cycle;

  ck_spinlock_lock(&self->use_lock);

  if(self->shutdown || (mask & EVENTER_EXCEPTION) || check == NULL) {
socket_close:
    /* Exceptions cause us to simply snip the connection */
    eventer_remove_fde(e);
    eventer_close(e, &newmask);
    listener_flush_immediate(self);
    ck_spinlock_unlock(&self->use_lock);
    listener_closure_deref(self);
    return 0;
  }

  while(true) {
    int len;
    int toRead = READ_CHUNK;
    int num_records = 0;

    mtev_dyn_buffer_ensure(&self->buffer, toRead);
    errno = 0;
    len = eventer_read(e, mtev_dyn_buffer_write_pointer(&self->buffer), toRead, &newmask);

    if (len == 0) {
      goto socket_close;
    }
    else if (len < 0) {
      if (errno == EAGAIN) {
        ck_spinlock_unlock(&self->use_lock);
        return newmask | EVENTER_EXCEPTION;
      }

      mtevL(self->nlerr, "%s: READ ERROR! %d, bailing\n", self->nlname, errno);
      goto socket_close;
    }

    mtev_dyn_buffer_advance(&self->buffer, len);
    *mtev_dyn_buffer_write_pointer(&self->buffer) = '\0';

    num_records = count_records((char *)mtev_dyn_buffer_data(&self->buffer));
    if (num_records > 0) {
      records_this_loop += num_records;
      size_t total_size = mtev_dyn_buffer_used(&self->buffer);
      char *end_ptr = memrchr((char *)mtev_dyn_buffer_data(&self->buffer), '\n', total_size);
      *end_ptr = '\0';
      size_t used_size = end_ptr - (char *)mtev_dyn_buffer_data(&self->buffer);

      self->payload_handler(check, (char *)mtev_dyn_buffer_data(&self->buffer), used_size);
      if (total_size > used_size) {
        end_ptr++;
        memmove(mtev_dyn_buffer_data(&self->buffer), end_ptr, total_size - used_size - 1);
        mtev_dyn_buffer_reset(&self->buffer);
        mtev_dyn_buffer_advance(&self->buffer, total_size - used_size - 1);
        *mtev_dyn_buffer_write_pointer(&self->buffer) = '\0';
      }
      if (records_this_loop >= rows_per_cycle) {
        listener_flush_immediate(self);
        ck_spinlock_unlock(&self->use_lock);
        return EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
      }
    }
  }
  /* unreachable */
  ck_spinlock_unlock(&self->use_lock);
  return newmask | EVENTER_EXCEPTION;
}

int
listener_listen_handler(eventer_t e, int mask, void *closure, struct timeval *now)
{
  listener_closure_t *self = (listener_closure_t *)closure;
  ck_spinlock_lock(&self->use_lock);
  if (self->shutdown) {
    ck_spinlock_unlock(&self->use_lock);
    return 0;
  }

  struct sockaddr cli_addr;
  socklen_t clilen = sizeof(cli_addr);
  if (mask & EVENTER_READ) {
    /* accept on the ipv4 socket */
    int fd = accept(self->ipv4_listen_fd, &cli_addr, &clilen);
    if (fd < 0) {
      mtevL(self->nlerr, "%s error accept: %s\n", self->nlname, strerror(errno));
      ck_spinlock_unlock(&self->use_lock);
      return 0;
    }

    /* otherwise, make a new event to track the accepted fd after we set the fd to non-blocking */
    if(eventer_set_fd_nonblocking(fd)) {
      close(fd);
      mtevL(self->nlerr,
            "%s: could not set accept fd (IPv4) non-blocking: %s\n",
            self->nlname, strerror(errno));
      ck_spinlock_unlock(&self->use_lock);
      return 0;
    }

    eventer_t newe;
    listener_closure_ref(self);
    newe = eventer_alloc_fd(listener_handler, self, fd,
                            EVENTER_READ | EVENTER_EXCEPTION);
    eventer_add(newe);
    /* continue to accept */
    ck_spinlock_unlock(&self->use_lock);
    return EVENTER_READ | EVENTER_EXCEPTION;
  }
  ck_spinlock_unlock(&self->use_lock);
  return EVENTER_READ | EVENTER_EXCEPTION;
}

int
listener_mtev_listener(eventer_t e, int mask, void *closure, struct timeval *now)
{
  uuid_t checkid;
  char uuid_str[UUID_STR_LEN+1];
  char secret[64];
  mtev_acceptor_closure_t *ac = closure;
  if(!ac) return 0;

  listener_closure_t *lc = mtev_acceptor_closure_ctx(ac);
  if(lc) {
    return listener_handler(e, mask, lc, now);
  }

#define ERR_TO_CLIENT(a...) do { \
  char _b[128]; \
  int _len = snprintf(_b, sizeof(_b), a); \
  eventer_write(e, _b, _len, &mask); \
  mtevL(nlerr, "%s", _b); \
} while(0)

  eventer_ssl_ctx_t *ctx = eventer_get_eventer_ssl_ctx(e);
  if(!ctx) {
    ERR_TO_CLIENT("Cannot support mtev listener without TLS (needs SNI).\n");
    goto bail;
  }
  const char *sni_name = eventer_ssl_get_sni_name(ctx);
  if(!sni_name) {
    mtevL(nlerr, "Cannot support mtev accept without SNI from secure client.\n");
    goto bail;
  }

  const char *endptr = strchr(sni_name, '.');
  if(!endptr) endptr = sni_name + strlen(sni_name);
  if(endptr - sni_name < UUID_STR_LEN + 1 ||
     endptr - sni_name >= UUID_STR_LEN + sizeof(secret) ||
     sni_name[UUID_STR_LEN] != '-') {
    ERR_TO_CLIENT("bad name format\n");
    goto bail;
  }

  int secret_len = (endptr - sni_name) - (UUID_STR_LEN+1);
  memcpy(secret, sni_name + UUID_STR_LEN+1, secret_len);
  secret[secret_len] = '\0';

  memcpy(uuid_str, sni_name, UUID_STR_LEN);
  uuid_str[UUID_STR_LEN] = '\0';
  if(mtev_uuid_parse(uuid_str, checkid)) {
    ERR_TO_CLIENT("invalid uuid: format\n");
    goto bail;
  }
  noit_check_t *check = noit_poller_lookup(checkid);
  if(!check) {
    ERR_TO_CLIENT("invalid uuid: not found\n");
    goto bail;
  }
  if(!check->closure) {
    ERR_TO_CLIENT("invalid uuid: not configured\n");
    goto bail;
  }
  lc = check->closure;
  if(strcmp(check->module, lc->self->hdr.name)) {
    ERR_TO_CLIENT("invalid check: bad type\n");
    goto bail;
  }
  const char *expect_secret = NULL;
  if(check->config)
    (void)mtev_hash_retr_str(check->config, "secret", strlen("secret"), &expect_secret);
  if(!expect_secret) expect_secret = "";

  if(strlen(expect_secret) != secret_len ||
     memcmp(secret, expect_secret, secret_len)) {
    ERR_TO_CLIENT("access denied to %s\n", uuid_str);
    goto bail;
  }

  mtev_acceptor_closure_set_ctx(ac, check->closure, NULL);
  listener_closure_ref((listener_closure_t *)check->closure);
  return listener_handler(e, mask, check->closure, now);

  bail:
  eventer_remove_fde(e);
  eventer_close(e, &mask);
  mtev_acceptor_closure_free(ac);
  return 0;
}

void
listener_describe_callback(char *buffer, int size, eventer_t e, void *closure)
{
  listener_closure_t *lc = (listener_closure_t *)eventer_get_closure(e);
  if (lc) {
    char check_uuid[UUID_STR_LEN + 1];
    mtev_uuid_unparse_lower(lc->check->checkid, check_uuid);
    snprintf(buffer, size, "%s(%s)", lc->nlname, check_uuid);
  }
  else {
    snprintf(buffer, size, "listener(...)");
  }
}

void
listener_describe_mtev_callback(char *buffer, int size, eventer_t e, void *closure)
{
  mtev_acceptor_closure_t *ac = (mtev_acceptor_closure_t *)eventer_get_closure(e);
  listener_closure_t *lc = (listener_closure_t *)mtev_acceptor_closure_ctx(ac);
  if (lc) {
    char check_uuid[UUID_STR_LEN + 1];
    mtev_uuid_unparse_lower(lc->check->checkid, check_uuid);
    snprintf(buffer, size, "%s(%s)", lc->nlname, check_uuid);
  }
  else {
    snprintf(buffer, size, "listener(...)");
  }
}

int
noit_listener_config(noit_module_t *self, mtev_hash_table *options)
{
  listener_mod_config_t *conf;
  conf = noit_module_get_userdata(self);
  if(conf) {
    if(conf->options) {
      mtev_hash_destroy(conf->options, free, free);
      free(conf->options);
    }
  }
  else {
    conf = calloc(1, sizeof(*conf));
  }
  conf->options = options;
  noit_module_set_userdata(self, conf);
  return 1;
}

void
noit_listener_cleanup(noit_module_t *self, noit_check_t *check)
{
  listener_closure_t *lc = (listener_closure_t *)check->closure;

  /* This is administrative shutdown of services. */
  ck_spinlock_lock(&lc->use_lock);
  lc->shutdown = mtev_true;
  ck_spinlock_unlock(&lc->use_lock);
  int mask = 0;
  eventer_t listen_eventer = eventer_find_fd(lc->ipv4_listen_fd);
  if (listen_eventer) {
    eventer_remove_fde(listen_eventer);
    eventer_close(listen_eventer, &mask);
  }
  listen_eventer = eventer_find_fd(lc->ipv6_listen_fd);
  if (listen_eventer) {
    eventer_remove_fde(listen_eventer);
    eventer_close(listen_eventer, &mask);
  }

  /* This is potential memory cleanup */
  listener_closure_deref(lc);
}

void
listener_onload()
{
  if(!nldeb) nldeb = mtev_log_stream_find("debug/listener");
  if(!nlerr) nlerr = mtev_log_stream_find("error/listener");
}
