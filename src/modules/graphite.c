/*
 * Copyright (c) 2018, Circonus, Inc. All rights reserved.
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
#include <ck_pr.h>

#include "noit_metric.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_mtev_bridge.h"
#include "prometheus.pb-c.h"

static mtev_log_stream_t nlerr = NULL;
static mtev_log_stream_t nldeb = NULL;

typedef struct _mod_config {
  mtev_hash_table *options;
} graphite_mod_config_t;

typedef struct graphite_closure_s {
  noit_module_t *self;
  noit_check_t *check;
  mtev_boolean shutdown;
  ck_spinlock_t use_lock;
  int port;
  int rows_per_cycle;
  int ipv4_listen_fd;
  int ipv6_listen_fd;
  mtev_dyn_buffer_t buffer;
  int32_t refcnt;
} graphite_closure_t;

static void graphite_closure_ref(graphite_closure_t *gc) {
  ck_pr_inc_int(&gc->refcnt);
}
static void graphite_closure_deref(graphite_closure_t *gc) {
  bool zero;
  ck_pr_dec_int_zero(&gc->refcnt, &zero);
  if(!zero) return;

  mtev_dyn_buffer_destroy(&gc->buffer);
  /* no need to free `gc` here as the noit cleanup code will 
   * free the check's closure for us */
  free(gc);
}
#define READ_CHUNK 32768

static inline size_t count_integral_digits(const char *str, size_t len, mtev_boolean can_be_signed)
{
  size_t rval = 0;
  if (can_be_signed && len > 0 && *str == '-') {
    rval++;
    str++;
  }
  while (rval < len) {
    if (*str < '0' || *str > '9') return rval;
    str++;
    rval++;
  }
  return rval;
}

/* Count endlines to determine how many full records we
 * have */
static inline int
count_records(char *buffer) {
  char *iter = buffer;
  int count = 0;
  while ((iter = strchr(iter, '\n')) != 0) {
    count++;
    iter++;
  }
  return count;
}


static int graphite_submit(noit_module_t *self, noit_check_t *check,
                           noit_check_t *cause)
{
  graphite_closure_t *ccl;
  struct timeval duration;
  /* We are passive, so we don't do anything for transient checks */
  if(check->flags & NP_TRANSIENT) return 0;

  if(!check->closure) {
    ccl = check->closure = (void *)calloc(1, sizeof(graphite_closure_t));
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
    if (s) {
      mtev_hash_table *metrics = noit_check_stats_metrics(s);
      if (metrics) {
        stats_count = mtev_hash_size(metrics);
      }
    }

    snprintf(human_buffer, sizeof(human_buffer),
             "dur=%ld,run=%d,stats=%d", duration.tv_sec * 1000 + duration.tv_usec / 1000,
             check->generation, stats_count);
    mtevL(nldeb, "graphite(%s) [%s]\n", check->target, human_buffer);

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

static void
graphite_handle_payload(noit_check_t *check, char *buffer, size_t len)
{
  char record[4096];
  char *part;
  char *s = NULL, *e = NULL;
  const size_t c_length = len;
  ptrdiff_t length = c_length;
  ptrdiff_t d = 0;

  for (s = buffer; length && *s; s = e + 1) {

    /* Find end of line. */
    e = (char *)memchr(s, '\n', length);

    if (e == NULL) {

      length = c_length - (s - buffer);
      e = s + length;

      if (length == 0 || *e != '\0') {
        /* nothing left? */
        break;
      }

      *e = '\0';
      d = length + 1;
      length = 0;
    } else {
      /* Update cursor. */
      d = e - s + 1;
      length -= d;
      *e = '\0';
    }
    part = s;
    /* save the row for later logging */
    strncpy(record, part, sizeof(record) - 1);

    size_t record_len = strlen(part);
    /*
     * a graphite record is of the format:
     *
     * <dot.separated.metric.name>{;optional_tag_name=optional_tag_value;...}[:space:]<value>[:space:]<epoch seconds timestamp>\n
     *
     * This makes millisecond level collection impossible unless senders violate the graphite spec.
     * We will use the noit_record_parse_m_timestamp function to deal with milliseconds
     * in case they are sent.
     */
    char *first_space = (char *)memchr(part, ' ', record_len);
    if (first_space == NULL) {
      first_space = (char *)memchr(part, '\t', record_len);
      if (first_space == NULL) {
        mtevL(nldeb, "Invalid graphite record, can't find the first space or tab in: %s\n", record);
        continue;
      }
    }

    char *second_space = (char *)memchr(first_space + 1, ' ', record_len - (part - (first_space + 1)));
    if (second_space == NULL) {
      second_space = (char *)memchr(first_space + 1, '\t', record_len - (part - (first_space + 1)));
      if (second_space == NULL) {
        mtevL(nldeb, "Invalid graphite record, can't find the second space or tab in: %s\n", record);
        continue;
      }
    }

    mtevL(nldeb, "Graphite record: %s\n", record);
    char *graphite_metric_name = part;
    *first_space = '\0';
    first_space++;
    const char *graphite_value = first_space;
    *second_space = '\0';
    second_space++;
    const char *graphite_timestamp = second_space;

    size_t metric_name_len = strlen(graphite_metric_name);

    char *dp;
    uint64_t whence_ms = strtoull(graphite_timestamp, &dp, 10);
    whence_ms *= 1000; /* s -> ms */
    if(dp && *dp == '.')
      whence_ms += (int) (1000.0 * atof(dp));

    size_t graphite_value_len = strlen(graphite_value);
    if (count_integral_digits(graphite_value, graphite_value_len, mtev_true) == 0) {
      mtevL(nldeb, "Invalid graphite record, no digits in value: %s\n", record);
      continue;
    }

    double metric_value = 0.0;
    const char *dot = strchr(graphite_value, '.');
    if (dot == NULL) {
      /* attempt fast integer parse, if we fail, jump to strtod parse */
      if (*graphite_value == '-') {
        errno = 0;
        int64_t v = strtoll(graphite_value, NULL, 10);
        if (errno == ERANGE) {
          goto strtod_parse;
        }
        metric_value = (double)v;
      } else {
        errno = 0;
        uint64_t v = strtoull(graphite_value, NULL, 10);
        if (errno == ERANGE) {
          goto strtod_parse;
        }
        metric_value = (double)v;
      }
    } else {
    strtod_parse:
      errno = 0;
      metric_value = strtod(graphite_value, NULL);
      if (errno == ERANGE) {
        mtevL(nldeb, "Invalid graphite record, strtod cannot parse value: %s\n", record);
        continue;
      }
    }

    /* allow for any length name + tags in the broker */
    mtev_dyn_buffer_t tagged_name;
    mtev_dyn_buffer_init(&tagged_name);

    /* http://graphite.readthedocs.io/en/latest/tags.html
     * 
     * Re-format incoming name string into our tag format for parsing */
    char *semicolon = strchr(graphite_metric_name, ';');
    if (semicolon) {
      mtev_dyn_buffer_add(&tagged_name, (uint8_t *)graphite_metric_name, semicolon - graphite_metric_name);
      mtev_dyn_buffer_add(&tagged_name, (uint8_t *)"|ST[", 4);

      /* look for K=V pairs */
      char *pair, *lasts;
      bool comma = false;
      for (pair = strtok_r(semicolon, ";", &lasts); pair; pair = strtok_r(NULL, ";", &lasts)) {
        const char *equal = strchr(pair, '=');
        if (equal) {
          size_t pair_len = strlen(pair);
          if (!noit_metric_tagset_is_taggable_key(pair, equal - pair) ||
              !noit_metric_tagset_is_taggable_value(equal + 1, pair_len - ((equal + 1) - pair))) {
            mtevL(nldeb, "Unacceptable tag key or value: '%s' skipping\n", pair);
            continue;
          }

          if (comma) mtev_dyn_buffer_add(&tagged_name, (uint8_t *)",", 1);
          mtev_dyn_buffer_add(&tagged_name, (uint8_t *)pair, equal - pair);
          mtev_dyn_buffer_add(&tagged_name, (uint8_t *)":", 1);
          mtev_dyn_buffer_add_printf(&tagged_name, "%s", equal + 1);
          comma = true;
        }
      }
      mtev_dyn_buffer_add(&tagged_name, (uint8_t *)"]", 1);
    } else {
      mtev_dyn_buffer_add(&tagged_name, (uint8_t *)graphite_metric_name, metric_name_len);
    }
    mtev_dyn_buffer_add(&tagged_name, (uint8_t*)"\0", 1);

    mtevL(nldeb, "Reformatted graphite name: %s\n", mtev_dyn_buffer_data(&tagged_name));
    struct timeval tv;
    tv.tv_sec = (time_t)(whence_ms / 1000L);
    tv.tv_usec = (suseconds_t)((whence_ms % 1000L) * 1000);
    noit_stats_log_immediate_metric_timed(check,
                                          (const char *)mtev_dyn_buffer_data(&tagged_name),
                                          METRIC_DOUBLE,
                                          &metric_value,
                                          &tv);
    mtev_dyn_buffer_destroy(&tagged_name);
  }
}

static int
graphite_handler(eventer_t e, int mask, void *closure, struct timeval *now) 
{
  int newmask = EVENTER_READ | EVENTER_EXCEPTION;
  graphite_closure_t *self = (graphite_closure_t *)closure;
  int rows_per_cycle = 0, records_this_loop = 0;
  noit_check_t *check = self->check;
  rows_per_cycle = self->rows_per_cycle;

  ck_spinlock_lock(&self->use_lock);

  if(self->shutdown || (mask & EVENTER_EXCEPTION) || check == NULL) {
socket_close:
    /* Exceptions cause us to simply snip the connection */
    eventer_remove_fde(e);
    eventer_close(e, &newmask);
    ck_spinlock_unlock(&self->use_lock);
    graphite_closure_deref(self);
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

      mtevL(nlerr, "READ ERROR! %d, bailing\n", errno);
      goto socket_close;
    }

    mtev_dyn_buffer_advance(&self->buffer, len);
    *mtev_dyn_buffer_write_pointer(&self->buffer) = '\0';

    num_records = count_records((char *)mtev_dyn_buffer_data(&self->buffer));
    if (num_records > 0) {
      records_this_loop += num_records;
      char *end_ptr = strrchr((char *)mtev_dyn_buffer_data(&self->buffer), '\n');
      *end_ptr = '\0';
      size_t total_size = mtev_dyn_buffer_used(&self->buffer);
      size_t used_size = end_ptr - (char *)mtev_dyn_buffer_data(&self->buffer);

      graphite_handle_payload(check, (char *)mtev_dyn_buffer_data(&self->buffer), used_size);
      if (total_size > used_size) {
        end_ptr++;
        char *leftovers = (char*)malloc(total_size - used_size - 1);
        memcpy(leftovers, end_ptr, total_size - used_size - 1);
        mtev_dyn_buffer_reset(&self->buffer);
        mtev_dyn_buffer_add(&self->buffer, (uint8_t *)leftovers, total_size - used_size - 1);
        free(leftovers);
      }
      if (records_this_loop >= rows_per_cycle) {
        ck_spinlock_unlock(&self->use_lock);
        return EVENTER_READ | EVENTER_WRITE | EVENTER_EXCEPTION;
      }
    }
  }
  /* unreachable */
  ck_spinlock_unlock(&self->use_lock);
  return newmask | EVENTER_EXCEPTION;
}

static int
graphite_mtev_listener(eventer_t e, int mask, void *closure, struct timeval *now)
{
  uuid_t checkid;
  char uuid_str[UUID_STR_LEN+1];
  char secret[64];
  mtev_acceptor_closure_t *ac = closure;
  if(!ac) return 0;

  graphite_closure_t *gc = mtev_acceptor_closure_ctx(ac);
  if(gc) {
    return graphite_handler(e, mask, gc, now);
  }

#define ERR_TO_CLIENT(a...) do { \
  char _b[128]; \
  int _len = snprintf(_b, sizeof(_b), a); \
  eventer_write(e, _b, _len, &mask); \
  mtevL(nlerr, "%s", _b); \
} while(0)

  eventer_ssl_ctx_t *ctx = eventer_get_eventer_ssl_ctx(e);
  if(!ctx) {
    ERR_TO_CLIENT("Cannot support graphite mtev listener without TLS (needs SNI).\n");
    goto bail;
  }
  const char *sni_name = eventer_ssl_get_sni_name(ctx);
  if(!sni_name) {
    mtevL(nlerr, "Cannot support graphite mtev accept without SNI from secure client.\n");
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
  if(strcmp(check->module, gc->self->hdr.name)) {
    ERR_TO_CLIENT("invalid check: bad type\n");
    goto bail;
  }
  const char *expect_secret = NULL;
  if(check->config)
    mtev_hash_retr_str(check->config, "secret", strlen("secret"), &expect_secret);
  if(!expect_secret) expect_secret = "";

  if(strlen(expect_secret) != secret_len ||
     memcmp(secret, expect_secret, secret_len)) {
    ERR_TO_CLIENT("access denied to %s\n", uuid_str);
    goto bail;
  }

  mtev_acceptor_closure_set_ctx(ac, check->closure, NULL);
  graphite_closure_ref((graphite_closure_t *)check->closure);
  return graphite_handler(e, mask, check->closure, now);

  bail:
  eventer_remove_fde(e);
  eventer_close(e, &mask);
  mtev_acceptor_closure_free(ac);
  return 0;
}

static int
graphite_listen_handler(eventer_t e, int mask, void *closure, struct timeval *now)
{
  graphite_closure_t *self = (graphite_closure_t *)closure;
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
      mtevL(nlerr, "graphite error accept: %s\n", strerror(errno));
      ck_spinlock_unlock(&self->use_lock);
      return 0;
    }

    /* otherwise, make a new event to track the accepted fd after we set the fd to non-blocking */
    if(eventer_set_fd_nonblocking(fd)) {
      close(fd);
      mtevL(nlerr,
            "graphite: could not set accept fd (IPv4) non-blocking: %s\n",
            strerror(errno));
      ck_spinlock_unlock(&self->use_lock);
      return 0;
    }

    eventer_t newe;
    graphite_closure_ref(self);
    newe = eventer_alloc_fd(graphite_handler, self, fd,
                            EVENTER_READ | EVENTER_EXCEPTION);
    eventer_add(newe);
    /* continue to accept */
    ck_spinlock_unlock(&self->use_lock);
    return EVENTER_READ | EVENTER_EXCEPTION;
  }
  ck_spinlock_unlock(&self->use_lock);
  return EVENTER_READ | EVENTER_EXCEPTION;
}

static void 
describe_callback(char *buffer, int size, eventer_t e, void *closure)
{
  graphite_closure_t *gc = (graphite_closure_t *)eventer_get_closure(e);
  if (gc) {
    char check_uuid[UUID_STR_LEN + 1];
    mtev_uuid_unparse_lower(gc->check->checkid, check_uuid);
    snprintf(buffer, size, "graphite(%s)", check_uuid);
  }
  else {
    snprintf(buffer, size, "graphite(...)");
  }
}

static void 
describe_mtev_callback(char *buffer, int size, eventer_t e, void *closure)
{
  mtev_acceptor_closure_t *ac = (mtev_acceptor_closure_t *)eventer_get_closure(e);
  graphite_closure_t *gc = (graphite_closure_t *)mtev_acceptor_closure_ctx(ac);
  if (gc) {
    char check_uuid[UUID_STR_LEN + 1];
    mtev_uuid_unparse_lower(gc->check->checkid, check_uuid);
    snprintf(buffer, size, "graphite(%s)", check_uuid);
  }
  else {
    snprintf(buffer, size, "graphite(...)");
  }
}

static int noit_graphite_initiate_check(noit_module_t *self,
                                        noit_check_t *check,
                                        int once, noit_check_t *cause) {
  check->flags |= NP_PASSIVE_COLLECTION;
  if (check->closure == NULL) {

    graphite_closure_t *ccl;
    ccl = check->closure = (void *)calloc(1, sizeof(graphite_closure_t));
    graphite_closure_ref(ccl);
    ccl->self = self;
    ccl->check = check;
    ccl->ipv4_listen_fd = -1;
    ccl->ipv6_listen_fd = -1;
    ck_spinlock_init(&ccl->use_lock);

    unsigned short port = 2003;
    int rows_per_cycle = 100;
    struct sockaddr_in skaddr;
    int sockaddr_len;
    const char *config_val;

    mtev_dyn_buffer_init(&ccl->buffer);

    if(mtev_hash_retr_str(check->config, "listen_port", strlen("listen_port"),
                          (const char **)&config_val)) {
      port = atoi(config_val);
    }
    ccl->port = port;

    if(mtev_hash_retr_str(check->config, "rows_per_cycle",
                          strlen("rows_per_cycle"),
                          (const char **)&config_val)) {
      rows_per_cycle = atoi(config_val);
    }
    ccl->rows_per_cycle = rows_per_cycle;

    if(port > 0) ccl->ipv4_listen_fd = socket(AF_INET, NE_SOCK_CLOEXEC|SOCK_STREAM, IPPROTO_TCP);
    if(ccl->ipv4_listen_fd < 0) {
      if(port > 0) {
        mtevL(noit_error, "graphite: socket failed: %s\n", strerror(errno));
        return -1;
      }
    }
    else {
      if(eventer_set_fd_nonblocking(ccl->ipv4_listen_fd)) {
        close(ccl->ipv4_listen_fd);
        ccl->ipv4_listen_fd = -1;
        mtevL(noit_error,
              "graphite: could not set socket (IPv4) non-blocking: %s\n",
              strerror(errno));
        return -1;
      }
      memset(&skaddr, 0, sizeof(skaddr));
      skaddr.sin_family = AF_INET;
      skaddr.sin_addr.s_addr = htonl(INADDR_ANY);
      skaddr.sin_port = htons(ccl->port);
      sockaddr_len = sizeof(skaddr);
      if(bind(ccl->ipv4_listen_fd, (struct sockaddr *)&skaddr, sockaddr_len) < 0) {
        mtevL(noit_error, "graphite bind(IPv4) failed[%d]: %s\n", ccl->port, strerror(errno));
        close(ccl->ipv4_listen_fd);
        return -1;
      }
      if (listen(ccl->ipv4_listen_fd, 5) != 0) {
        mtevL(noit_error, "graphite listen(IPv4) failed[%d]: %s\n", ccl->port, strerror(errno));
        close(ccl->ipv4_listen_fd);
        return -1;
      }
  
      eventer_t newe = eventer_alloc_fd(graphite_listen_handler, ccl, ccl->ipv4_listen_fd,
                                        EVENTER_READ | EVENTER_EXCEPTION);
      eventer_add(newe);
    }
    if(port > 0) ccl->ipv6_listen_fd = socket(AF_INET6, NE_SOCK_CLOEXEC|SOCK_STREAM, IPPROTO_TCP);
    if(ccl->ipv6_listen_fd < 0) {
      if(port > 0) {
        mtevL(noit_error, "graphite: IPv6 socket failed: %s\n",
              strerror(errno));
      }
    }
    else {
      if(eventer_set_fd_nonblocking(ccl->ipv6_listen_fd)) {
        close(ccl->ipv6_listen_fd);
        ccl->ipv6_listen_fd = -1;
        mtevL(noit_error,
              "graphite: could not set socket (IPv6) non-blocking: %s\n",
              strerror(errno));
      }
      else {
        struct sockaddr_in6 skaddr6;
        struct in6_addr in6addr_any;
        sockaddr_len = sizeof(skaddr6);
        memset(&skaddr6, 0, sizeof(skaddr6));
        skaddr6.sin6_family = AF_INET6;
        memset(&in6addr_any, 0, sizeof(in6addr_any));
        skaddr6.sin6_addr = in6addr_any;
        skaddr6.sin6_port = htons(ccl->port);

        if(bind(ccl->ipv6_listen_fd, (struct sockaddr *)&skaddr6, sockaddr_len) < 0) {
          mtevL(noit_error, "graphite bind(IPv6) failed[%d]: %s\n",
                ccl->port, strerror(errno));
          close(ccl->ipv6_listen_fd);
          ccl->ipv6_listen_fd = -1;
        }

        else if (listen(ccl->ipv6_listen_fd, 5) != 0) {
          mtevL(noit_error, "graphite listen(IPv6) failed[%d]: %s\n", ccl->port, strerror(errno));
          close(ccl->ipv6_listen_fd);
          if (ccl->ipv4_listen_fd <= 0) return -1;
        }

      }
    }

    if(ccl->ipv6_listen_fd >= 0) {
      eventer_t newe = eventer_alloc_fd(graphite_listen_handler, ccl, ccl->ipv6_listen_fd,
                                        EVENTER_READ | EVENTER_EXCEPTION);
      eventer_add(newe);
    }
  }
  INITIATE_CHECK(graphite_submit, self, check, cause);
  return 0;
}

static void noit_graphite_cleanup(noit_module_t *self, noit_check_t *check)
{
  graphite_closure_t *gc = (graphite_closure_t *)check->closure;

  /* This is administrative shutdown of services. */
  ck_spinlock_lock(&gc->use_lock);
  gc->shutdown = mtev_true;
  ck_spinlock_unlock(&gc->use_lock);
  int mask = 0;
  eventer_t listen_eventer = eventer_find_fd(gc->ipv4_listen_fd);
  if (listen_eventer) {
    eventer_remove_fde(listen_eventer);
    eventer_close(listen_eventer, &mask);
  }
  listen_eventer = eventer_find_fd(gc->ipv6_listen_fd);
  if (listen_eventer) {
    eventer_remove_fde(listen_eventer);
    eventer_close(listen_eventer, &mask);
  }

  /* This is potential memory cleanup */
  graphite_closure_deref(gc);
}

static int noit_graphite_config(noit_module_t *self, mtev_hash_table *options) {
  graphite_mod_config_t *conf;
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

static int noit_graphite_onload(mtev_image_t *self) {
  if(!nlerr) nlerr = mtev_log_stream_find("error/graphite");
  if(!nldeb) nldeb = mtev_log_stream_find("debug/graphite");
  if(!nlerr) nlerr = noit_error;
  if(!nldeb) nldeb = noit_debug;
  return 0;
}

static int noit_graphite_init(noit_module_t *self) 
{
  if(!strcmp(self->hdr.name, "graphite_tls")) {
    eventer_name_callback_ext("graphite/graphite_listener", graphite_mtev_listener, describe_mtev_callback, self);
  }
  eventer_name_callback_ext("graphite/graphite_handler", graphite_handler, describe_callback, self);
  eventer_name_callback_ext("graphite/graphite_listen_handler", graphite_listen_handler, describe_callback, self);

  return 0;
}

/* This is here for legacy */
noit_module_t graphite = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "graphite",
    .description = "graphite(carbon) collection",
    .xml_description = "",
    .onload = noit_graphite_onload
  },
  noit_graphite_config,
  noit_graphite_init,
  noit_graphite_initiate_check,
  noit_graphite_cleanup
};

#include "graphite_tls.xmlh"
noit_module_t graphite_tls = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "graphite_tls",
    .description = "graphite_tls(carbon) collection",
    .xml_description = graphite_tls_xml_description,
    .onload = noit_graphite_onload
  },
  noit_graphite_config,
  noit_graphite_init,
  noit_graphite_initiate_check,
  noit_graphite_cleanup
};

#include "graphite_plain.xmlh"
noit_module_t graphite_plain = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "graphite_plain",
    .description = "graphite_plain(carbon) collection",
    .xml_description = graphite_plain_xml_description,
    .onload = noit_graphite_onload
  },
  noit_graphite_config,
  noit_graphite_init,
  noit_graphite_initiate_check,
  noit_graphite_cleanup
};
