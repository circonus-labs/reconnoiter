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

#define DEFAULT_ROWS_PER_CYCLE 100

#include <mtev_defines.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>

#include <mtev_rand.h>
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

/* Count endlines to determine how many full records we
 * have */
static inline int
count_records(char *buffer, size_t inlen, size_t *usedlen)
{
  char *iter = buffer, *previter = buffer;
  int count = 0;
  while ((iter = memchr(iter, '\n', inlen - (iter - buffer))) != 0) {
    count++;
    iter++;
    previter = iter;
  }
  *usedlen = previter - buffer;
  return count;
}

static listener_instance_t *
listener_instance_alloc(void)
{
  listener_instance_t *inst = calloc(1, sizeof(*inst));
  mtev_dyn_buffer_init(&inst->buffer);
  return inst;
}

static void
listener_instance_free(listener_instance_t *inst)
{
  mtev_dyn_buffer_destroy(&inst->buffer);
}

listener_closure_t *
listener_closure_alloc(const char *name, noit_module_t *mod, noit_check_t *check,
                       mtev_log_stream_t deb, mtev_log_stream_t err,
                       int (*handler)(noit_check_t *, char *, size_t),
                       int (*count)(char *, size_t, size_t *))
{
  listener_closure_t *lc = calloc(1, sizeof(*lc));
  listener_closure_ref(lc);
  lc->payload_handler = handler;
  lc->count_records = count;
  lc->self = mod;
  lc->check = check;
  lc->ipv4_listen_fd = -1;
  lc->ipv6_listen_fd = -1;
  lc->nldeb = deb;
  lc->nlerr = err;
  strlcpy(lc->nlname, name, sizeof(lc->nlname));
  pthread_mutex_init(&lc->flushlock, NULL);
  return lc;
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

  pthread_mutex_lock(&lc->flushlock);
  mtev_hash_delete_all(lc->immediate_metrics, NULL, metric_local_free);
  pthread_mutex_unlock(&lc->flushlock);
  pthread_mutex_destroy(&lc->flushlock);
  /* no need to free `lc` here as the noit cleanup code will
   * free the check's closure for us */
  free(lc);
}

int
listener_submit(noit_module_t *self, noit_check_t *check, noit_check_t *cause)
{
  struct timeval duration;
  /* We are passive, so we don't do anything for transient checks */
  if(check->flags & NP_TRANSIENT) return 0;

  if(!check->closure) return 0;

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
  metric_t *m;
  listener_closure_t *rxc = vrxc;
  if(t == METRIC_GUESS) return;
  void *vm;
retry:
  pthread_mutex_lock(&rxc->flushlock);
  if(rxc->immediate_metrics == NULL) {
    mtev_hash_table *tbl = calloc(1, sizeof(*tbl));
    mtev_hash_init_mtev_memory(tbl, MTEV_HASH_DEFAULT_SIZE, MTEV_HASH_LOCK_MODE_MUTEX);
    rxc->immediate_metrics = tbl;
  }
  if(mtev_hash_retrieve(rxc->immediate_metrics, name, strlen(name), &vm) ||
     mtev_hash_size(rxc->immediate_metrics) > rxc->rows_per_cycle) {
    /* collision, just log it out */
    listener_flush_immediate(rxc);
  }
  pthread_mutex_unlock(&rxc->flushlock);
  m = calloc(1, sizeof(*m));
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
  pthread_mutex_lock(&rxc->flushlock);
  mtev_boolean inserted = mtev_hash_store(rxc->immediate_metrics, m->metric_name, strlen(m->metric_name), m);
  pthread_mutex_unlock(&rxc->flushlock);
  if(!inserted) {
    metric_local_free(m);
    goto retry;
  }
}

int
listener_handler(eventer_t e, int mask, void *closure, struct timeval *now)
{
  int newmask = EVENTER_READ | EVENTER_EXCEPTION;
  listener_instance_t *inst = (listener_instance_t *)closure;
  listener_closure_t *self = (listener_closure_t *)inst->parent;
  int rows_per_cycle = 0, records_this_loop = 0;
  noit_check_t *check = self->check;
  if(self->rows_per_cycle < 1) self->rows_per_cycle = DEFAULT_ROWS_PER_CYCLE;
  rows_per_cycle = self->rows_per_cycle;
  if(!inst->subsequent_invocation) {
    if(N_L_S_ON(self->nldeb)) {
      char uuid_str[UUID_STR_LEN+1];
      mtev_uuid_unparse_lower(check->checkid, uuid_str);
      mtevL(self->nldeb, "handling %s for %s\n", self->self->hdr.name, uuid_str);
    }
    inst->subsequent_invocation = 1;
  }

  if(self->shutdown || (mask & EVENTER_EXCEPTION) || check == NULL) {
socket_close:
    /* Exceptions cause us to simply snip the connection */
    eventer_remove_fde(e);
    eventer_close(e, &newmask);
    pthread_mutex_lock(&self->flushlock);
    listener_flush_immediate(self);
    pthread_mutex_unlock(&self->flushlock);
    listener_instance_free(inst);
    listener_closure_deref(self);
    return 0;
  }

  while(true) {
    int len;
    int toRead = READ_CHUNK;
    int num_records = 0;

    mtev_dyn_buffer_ensure(&inst->buffer, toRead);
    errno = 0;
    len = eventer_read(e, mtev_dyn_buffer_write_pointer(&inst->buffer), toRead, &newmask);

    if (len == 0) {
      goto socket_close;
    }
    else if (len < 0) {
      if (errno == EAGAIN) {
        return newmask | EVENTER_EXCEPTION;
      }

      mtevL(self->nlerr, "%s: READ ERROR! %d, bailing\n", self->nlname, errno);
      goto socket_close;
    }

    mtev_dyn_buffer_advance(&inst->buffer, len);
    *mtev_dyn_buffer_write_pointer(&inst->buffer) = '\0';

    if(!self->count_records) self->count_records = count_records;
    size_t used_size;
    num_records = self->count_records((char *)mtev_dyn_buffer_data(&inst->buffer),
                                      mtev_dyn_buffer_used(&inst->buffer),
                                      &used_size);
    if (num_records < 0) goto socket_close;
    if (num_records > 0) {
      records_this_loop += num_records;
      size_t total_size = mtev_dyn_buffer_used(&inst->buffer);
      mtevAssert(total_size >= used_size);
      if(self->payload_handler(check, (char *)mtev_dyn_buffer_data(&inst->buffer), used_size) < 0)
        goto socket_close;

      void *end_ptr = mtev_dyn_buffer_data(&inst->buffer) + used_size;
      memmove(mtev_dyn_buffer_data(&inst->buffer), end_ptr, total_size - used_size);
      mtev_dyn_buffer_reset(&inst->buffer);
      mtev_dyn_buffer_advance(&inst->buffer, total_size - used_size);
      *mtev_dyn_buffer_write_pointer(&inst->buffer) = '\0';

      if (records_this_loop >= rows_per_cycle) {
        pthread_mutex_lock(&self->flushlock);
        listener_flush_immediate(self);
        pthread_mutex_unlock(&self->flushlock);
        return EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
      }
    }
  }
  /* unreachable */
  return newmask | EVENTER_EXCEPTION;
}

int
listener_listen_handler(eventer_t e, int mask, void *closure, struct timeval *now)
{
  listener_closure_t *self = (listener_closure_t *)closure;
  if (self->shutdown) {
    eventer_close(e, &mask);
    return 0;
  }

  struct sockaddr cli_addr;
  socklen_t clilen = sizeof(cli_addr);
  if (mask & EVENTER_READ) {
    /* accept on the ipv4 socket */
    int fd = accept(self->ipv4_listen_fd, &cli_addr, &clilen);
    if (fd < 0) {
      mtevL(self->nlerr, "%s error accept: %s\n", self->nlname, strerror(errno));
      return 0;
    }

    /* otherwise, make a new event to track the accepted fd after we set the fd to non-blocking */
    if(eventer_set_fd_nonblocking(fd)) {
      close(fd);
      mtevL(self->nlerr,
            "%s: could not set accept fd (IPv4) non-blocking: %s\n",
            self->nlname, strerror(errno));
      return 0;
    }

    eventer_t newe;
    listener_closure_ref(self);
    listener_instance_t *inst = listener_instance_alloc();
    inst->parent = self;
    newe = eventer_alloc_fd(listener_handler, inst, fd,
                            EVENTER_READ | EVENTER_EXCEPTION);

    eventer_pool_t *dp = noit_check_choose_pool(self->check);
    if(dp) eventer_set_owner(newe, eventer_choose_owner_pool(dp, mtev_rand()));
    else eventer_set_owner(newe, eventer_choose_owner(mtev_rand()));
    eventer_add(newe);
    /* continue to accept */
    return EVENTER_READ | EVENTER_EXCEPTION;
  }
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

  listener_instance_t *inst = mtev_acceptor_closure_ctx(ac);
  if(inst) {
    return listener_handler(e, mask, inst, now);
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
  listener_closure_t *lc = check->closure;
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

  inst = listener_instance_alloc();
  inst->parent = lc;
  listener_closure_ref(lc);

  mtev_acceptor_closure_set_ctx(ac, inst, NULL);
  noit_check_deref(check);
  return listener_handler(e, mask, inst, now);

  bail:
  noit_check_deref(check);
  eventer_remove_fde(e);
  eventer_close(e, &mask);
  mtev_acceptor_closure_free(ac);
  return 0;
}

void
listener_describe_callback(char *buffer, int size, eventer_t e, void *closure)
{
  listener_instance_t *inst = (listener_instance_t *)eventer_get_closure(e);
  listener_closure_t *lc = inst ? inst->parent : NULL;
  if (lc && lc->check) {
    char check_uuid[UUID_STR_LEN + 1];
    mtev_uuid_unparse_lower(lc->check->checkid, check_uuid);
    snprintf(buffer, size, "%s(%s)", lc->nlname, check_uuid);
  }
  else {
    snprintf(buffer, size, "listener(...)");
  }
}

void
listener_listen_describe_callback(char *buffer, int size, eventer_t e, void *closure)
{
  listener_closure_t *lc = (listener_closure_t *)eventer_get_closure(e);
  if (lc && lc->check) {
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
  listener_instance_t *inst = (listener_instance_t *)mtev_acceptor_closure_ctx(ac);
  listener_closure_t *lc = inst ? inst->parent : NULL;
  if (lc && lc->check) {
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
  if(lc == NULL) return;
  check->closure = NULL;

  /* This is administrative shutdown of services. */
  lc->shutdown = mtev_true;
  int mask = 0;
  /* shutdown IPv4 */
  eventer_t listen_eventer = eventer_find_fd(lc->ipv4_listen_fd);
  if (listen_eventer) {
    eventer_remove_fde(listen_eventer);
    eventer_close(listen_eventer, &mask);
    mtevL(nldeb, "listener %s cleanup IPv4 on fd %d\n", self->hdr.name, lc->ipv4_listen_fd);
  }
  else {
    close(lc->ipv4_listen_fd);
  }
  lc->ipv4_listen_fd = -1;

  /* shutdown IPv6 */
  listen_eventer = eventer_find_fd(lc->ipv6_listen_fd);
  if (listen_eventer) {
    eventer_remove_fde(listen_eventer);
    eventer_close(listen_eventer, &mask);
    mtevL(nldeb, "listener %s cleanup IPv6 on fd %d\n", self->hdr.name, lc->ipv6_listen_fd);
  }
  else {
    close(lc->ipv6_listen_fd);
  }
  lc->ipv6_listen_fd = -1;

  /* This is potential memory cleanup */
  listener_closure_deref(lc);

  char uuid_str[UUID_STR_LEN+1];
  mtev_uuid_unparse_lower(check->checkid, uuid_str);
  mtevL(nldeb, "listener %s cleaned up %s\n", self->hdr.name, uuid_str);
}

void
listener_onload()
{
  if(!nldeb) nldeb = mtev_log_stream_find("debug/listener");
  if(!nlerr) nlerr = mtev_log_stream_find("error/listener");
}
