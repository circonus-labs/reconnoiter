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
  uuid_t check_uuid;
  int port;
  int rows_per_cycle;
  int ipv4_fd;
  int ipv6_fd;
  mtev_dyn_buffer_t buffer;
} graphite_mod_config_t;

typedef struct graphite_closure_s {
  noit_module_t *self;
} graphite_closure_t;

struct value_list {
  char *v;
  struct value_list *next;
};

typedef struct graphite_upload
{
  mtev_dyn_buffer_t data;
  mtev_boolean complete;
  noit_check_t *check;
  uuid_t check_id;
} graphite_upload_t;

#define READ_CHUNK 32768

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

static mtev_boolean
cross_module_reverse_allowed(noit_check_t *check) {
  mtev_hash_table *config;
  static int reverse_check_module_id = -1;
  if(reverse_check_module_id < 0) {
    reverse_check_module_id = noit_check_registered_module_by_name("reverse");
    if(reverse_check_module_id < 0) return mtev_false;
  }
  config = noit_check_get_module_config(check, reverse_check_module_id);
  if(!config) return mtev_false;
  return mtev_true;
}

static int
count_records(char *buffer) {
  char *iter = buffer;
  int count = 0;
  while ((iter = strchr(iter, '\n')) != 0) {
    count++;
    iter++;
  }
  return count;
}

static void
graphite_handle_payload(noit_check_t *check, const char *buffer, size_t len)
{
  char record[4096];
  noit_record_m_t value;
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
    strncpy(record, part, sizeof(record));

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

    value.whence_ms = noit_record_parse_m_timestamp(graphite_timestamp, strlen(graphite_timestamp));

    size_t graphite_value_len = strlen(graphite_value);
    if (count_integral_digits(graphite_value, graphite_value_len, mtev_true) == 0) {
      mtevL(nldeb, "Invalid graphite record, no digits in value: %s\n", record);
      continue;
    }

    value.value.type = METRIC_DOUBLE;

    const char *dot = strchr(graphite_value, '.');
    if (dot == NULL) {
      /* attempt fast integer parse, if we fail, jump to strtod parse */
      if (*graphite_value == '-') {
        errno = 0;
        int64_t v = strtoll(graphite_value, 10, NULL);
        if (errno == ERANGE) {
          goto strtod_parse;
        }
        value.value.storage.v_int64 = v;
        value.value.type = METRIC_INT64;
      } else {
        errno = 0;
        uint64_t v = strtoull(graphite_value, 10, NULL);
        if (errno == ERANGE) {
          goto strtod_parse;
        }
        value.value.storage.v_uint64 = v;
        value.value.type = METRIC_UINT64;
      }
    } else {
    strtod_parse:
      errno = 0;
      value.value.storage.v_double = strtod(graphite_value, NULL);
      if (errno == ERANGE) {
        mtevL(nldeb, "Invalid graphite record, strtod cannot parse value: %s\n", record);
        continue;
      }
    }
    value.value.is_null = mtev_false;

    /* http://graphite.readthedocs.io/en/latest/tags.html
     * 
     * Re-format incoming name string into our tag format for parsing */
    char *semicolon = strchr(graphite_metric_name, ';');
    if (semicolon) {
      tagged_metric_name.append_bytes(graphite_metric_name, semicolon - graphite_metric_name);
      tagged_metric_name.append_bytes("|ST[", 4);
      /* look for K=V pairs */
      char *pair, *lasts;
      bool comma = false;
      for (pair = strtok_r(semicolon, ";", &lasts); pair; pair = strtok_r(NULL, ";", &lasts)) {
        const char *equal = strchr(pair, '=');
        if (equal) {
          size_t pair_len = strlen(pair);
          if (!noit_metric_tagset_is_taggable_key(pair, equal - pair) ||
              !noit_metric_tagset_is_taggable_value(equal + 1, pair_len - ((equal + 1) - pair))) {
            mtevL(mtev_error, "Unacceptable tag key or value: '%s' skipping\n", pair);
            continue;
          }

          if (comma) tagged_metric_name.append_bytes(",", 1);
          tagged_metric_name.append_bytes(pair, equal - pair);
          tagged_metric_name.append_bytes(":", 1);
          tagged_metric_name.append_string(equal + 1);
          comma = true;
        }
      }
      tagged_metric_name.append_bytes("]", 1);
    } else {
      tagged_metric_name.append_bytes(graphite_metric_name, metric_name_len);
    }

    mtevL(graphite_debug_ls, "Reformatted graphite name: %s\n", tagged_metric_name.get_mem());

    mem_reader_t metric_name_buf;
    mem_reader_t tagged_metric_name_buf;
    mem_reader_init_mem(&tagged_metric_name_buf, tagged_metric_name.get_mem(), tagged_metric_name.get_mem_used());
    mem_reader_init_mem(&value.metric_name_buf, tagged_metric_name.get_mem(), 
                        tagged_metric_name.get_mem_used());
    snowth_metric_locator_t *metric_locator =
      snowth_metric_locator_create_full(identifier,
                                        METRIC_TYPE_NUMERIC,
                                        METRIC_SOURCE_GRAPHITE,
                                        mem_reader_get_buffer(&value.check_name_buf),
                                        value.check_uuid, &value.metric_name_buf);
    if (metric_locator == NULL) {
      mtevL(mtev_notice, "Invalid graphite record: cannot parse metric name \"%.*s\"\n",
            (int) mem_reader_get_buffer_size(&value.metric_name_buf),
            mem_reader_get_buffer(&value.metric_name_buf));
      continue;
    }

  
  }
}

static char *
metric_name_from_graphite()
{
  char final_name[4096] = {0};
  char *name = final_name;
  char buffer[4096] = {0};
  char encode_buffer[512] = {0};
  char *b = buffer;
  size_t tag_count = 0;
  for (size_t i = 0; i < label_count; i++) {
    Prometheus__Label *l = labels[i];
    if (strcmp("__name__", l->name) == 0) {
      strncpy(name, l->value, sizeof(final_name) - 1);
    } else {
      if (tag_count > 0) {
        strlcat(b, ",", sizeof(buffer));
      }
      /* make base64 encoded tags out of the incoming prometheus tags for safety */
      /* TODO base64 encode these */
      size_t tl = strlen(l->name);
      mtev_b64_encode((const unsigned char *)l->name, tl, encode_buffer, sizeof(encode_buffer));

      strlcat(b, "b\"", sizeof(buffer));
      strlcat(b, encode_buffer, sizeof(buffer));
      strlcat(b, "\":b\"", sizeof(buffer));

      tl = strlen(l->value);
      mtev_b64_encode((const unsigned char *)l->value, tl, encode_buffer, sizeof(encode_buffer));

      strlcat(b, encode_buffer, sizeof(buffer));
      strlcat(b, "\"", sizeof(buffer));
      tag_count++;
    }
  }
  strlcat(name, "|ST[", sizeof(final_name));
  strlcat(name, buffer, sizeof(final_name));
  strlcat(name, "]", sizeof(final_name));

  /* we don't have to canonicalize here as reconnoiter will do that for us */
  return strdup(final_name);
}

static int
graphite_handler(eventer_t e, int mask, void *closure, struct timeval *now) 
{
  int newmask = EVENTER_READ | EVENTER_EXCEPTION;
  noit_module_t *self = (noit_module_t *)closure;
  int rows_per_cycle = 0, records_this_loop = 0;
  graphite_mod_config_t *conf;
  noit_check_t *check = NULL;

  conf = noit_module_get_userdata(self);
  check = noit_poller_lookup(conf->check_uuid);
  rows_per_cycle = conf->rows_per_cycle;

  if(mask & EVENTER_EXCEPTION || check == NULL) {
socket_close:
    /* Exceptions cause us to simply snip the connection */
    eventer_remove_fde(e);
    eventer_close(e, &newmask);
    return 0;
  }

  while(true) {
    int len;
    int toRead = READ_CHUNK;
    int num_records = 0;

    mtev_dyn_buffer_ensure(&conf->buffer, toRead);
    errno = 0;
    len = eventer_read(e, mtev_dyn_buffer_write_pointer(&conf->buffer), toRead, &newmask);

    if (len == 0) {
      goto socket_close;
    }
    else if (len < 0) {
      if (errno == EAGAIN) {
        return newmask | EVENTER_EXCEPTION;
      }

      mtevL(nlerr, "READ ERROR! %d, bailing\n", errno);
      goto socket_close;
    }

    mtev_dyn_buffer_advance(&conf->buffer, len);
    *mtev_dyn_buffer_write_pointer(&conf->buffer) = '\0';

    num_records = count_records(mtev_dyn_buffer_data(&conf->buffer));
    if (num_records > 0) {
      records_this_loop += num_records;
      char *end_ptr = strrchr(mtev_dyn_buffer_data(&conf->buffer), '\n');
      *end_ptr = '\0';
      size_t total_size = mtev_dyn_buffer_used(&conf->buffer);
      size_t used_size = end_ptr - mtev_dyn_buffer_data(&conf->buffer);

      graphite_handle_payload(check, mtev_dyn_buffer_data(&conf->buffer), used_size);
      if (total_size > used_size) {
        end_ptr++;
        char *leftovers = (char*)total_size - used_size - 1;
        memcpy(leftovers, end_ptr, total_size - used_size - 1);
        mtev_dyn_buffer_reset(&conf->buffer);
        mtev_dyn_buffer_add(&conf->buffer, leftovers, total_size - used_size - 1)
      }
      if (records_this_loop >= rows_per_cycle) {
        return newmask | EVENTER_EXCEPTION;
      }
    }
  }
  /* unreachable */
  return newmask | EVENTER_EXCEPTION;

}


static int noit_graphite_initiate_check(noit_module_t *self,
                                        noit_check_t *check,
                                        int once, noit_check_t *cause) {
  check->flags |= NP_PASSIVE_COLLECTION;
  if (check->closure == NULL) {
    graphite_closure_t *ccl;
    ccl = check->closure = (void *)calloc(1, sizeof(graphite_closure_t));
    ccl->self = self;
  }
  INITIATE_CHECK(graphite_submit, self, check, cause);
  return 0;
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

static int noit_graphite_init(noit_module_t *self) {
  unsigned short port = 2003;
  int rows_per_cycle = 100;
  int payload_len = 256*1024;
  struct sockaddr_in skaddr;
  int sockaddr_len;
  const char *config_val;
  graphite_mod_config_t *conf;

  conf = noit_module_get_userdata(self);
  mtev_dyn_buffer_init(&conf->buffer);
  eventer_name_callback("graphite/graphite_handler", graphite_handler);

  if(mtev_hash_retr_str(conf->options, "check", strlen("check"),
                        (const char **)&config_val)) {
    if(mtev_uuid_parse((char *)config_val, conf->check_uuid) != 0) {
      mtevL(noit_error, "graphite check isn't a UUID\n");
    }
  }

  if(mtev_hash_retr_str(conf->options, "listen_port", strlen("listen_port"),
                        (const char **)&config_val)) {
    port = atoi(config_val);
  }
  conf->port = port;

  if(mtev_hash_retr_str(conf->options, "rows_per_cycle",
                        strlen("rows_per_cycle"),
                        (const char **)&config_val)) {
    rows_per_cycle = atoi(config_val);
  }
  conf->rows_per_cycle = rows_per_cycle;

  conf->ipv4_fd = socket(AF_INET, NE_SOCK_CLOEXEC|SOCK_STREAM, IPPROTO_TCP);
  if(conf->ipv4_fd < 0) {
    mtevL(noit_error, "statsd: socket failed: %s\n", strerror(errno));
    return -1;
  }
  else {
    if(eventer_set_fd_nonblocking(conf->ipv4_fd)) {
      close(conf->ipv4_fd);
      conf->ipv4_fd = -1;
      mtevL(noit_error,
            "collectd: could not set socket non-blocking: %s\n",
            strerror(errno));
      return -1;
    }
  }
  memset(&skaddr, 0, sizeof(skaddr));
  skaddr.sin_family = AF_INET;
  skaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  skaddr.sin_port = htons(conf->port);
  sockaddr_len = sizeof(skaddr);
  if(bind(conf->ipv4_fd, (struct sockaddr *)&skaddr, sockaddr_len) < 0) {
    mtevL(noit_error, "bind failed[%d]: %s\n", conf->port, strerror(errno));
    close(conf->ipv4_fd);
    return -1;
  }

  if(conf->ipv4_fd >= 0) {
    eventer_t newe;
    newe = eventer_alloc_fd(graphite_handler, self, conf->ipv4_fd,
                            EVENTER_READ | EVENTER_EXCEPTION);
    eventer_add(newe);
  }

  conf->ipv6_fd = socket(AF_INET6, NE_SOCK_CLOEXEC|SOCK_STREAM, IPPROTO_TCP);
  if(conf->ipv6_fd < 0) {
    mtevL(noit_error, "statsd: IPv6 socket failed: %s\n",
          strerror(errno));
  }
  else {
    if(eventer_set_fd_nonblocking(conf->ipv6_fd)) {
      close(conf->ipv6_fd);
      conf->ipv6_fd = -1;
      mtevL(noit_error,
            "statsd: could not set socket non-blocking: %s\n",
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
      skaddr6.sin6_port = htons(conf->port);

      if(bind(conf->ipv6_fd, (struct sockaddr *)&skaddr6, sockaddr_len) < 0) {
        mtevL(noit_error, "bind(IPv6) failed[%d]: %s\n",
              conf->port, strerror(errno));
        close(conf->ipv6_fd);
        conf->ipv6_fd = -1;
      }
    }
  }

  if(conf->ipv6_fd >= 0) {
    eventer_t newe;
    newe = eventer_alloc_fd(graphite_handler, self, conf->ipv6_fd,
                            EVENTER_READ | EVENTER_EXCEPTION);
    eventer_add(newe);
  }

  noit_module_set_userdata(self, conf);
  return 0;
}

#include "graphite.xmlh"
noit_module_t graphite = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "graphite",
    .description = "graphite(carbon) collection",
    .xml_description = graphite_xml_description,
    .onload = noit_graphite_onload
  },
  noit_graphite_config,
  noit_graphite_init,
  noit_graphite_initiate_check,
  NULL
};
