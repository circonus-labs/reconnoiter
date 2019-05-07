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
#include "noit_socket_listener.h"

static mtev_log_stream_t nlerr = NULL;
static mtev_log_stream_t nldeb = NULL;

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

static int noit_graphite_initiate_check(noit_module_t *self,
                                        noit_check_t *check,
                                        int once, noit_check_t *cause) {
  check->flags |= NP_PASSIVE_COLLECTION;
  check->nldeb = nldeb;
  check->nlerr = nlerr;
  if (check->closure == NULL) {

    listener_closure_t *ccl;
    ccl = check->closure = (void *)calloc(1, sizeof(listener_closure_t));
    listener_closure_ref(ccl);
    ccl->self = self;
    ccl->check = check;
    ccl->ipv4_listen_fd = -1;
    ccl->ipv6_listen_fd = -1;
    ccl->nldeb = nldeb;
    ccl->nlerr = nlerr;
    strcpy(ccl->nlname, "graphite");
    ccl->payload_handler = graphite_handle_payload;
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

    /* Skip setting the port for the tls variant, it works differently */
    if(strcmp(check->module, "graphite_tls")) {
      ccl->port = port;
    }

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
  
      eventer_t newe = eventer_alloc_fd(listener_listen_handler, ccl, ccl->ipv4_listen_fd,
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
      eventer_t newe = eventer_alloc_fd(listener_listen_handler, ccl, ccl->ipv6_listen_fd,
                                        EVENTER_READ | EVENTER_EXCEPTION);
      eventer_add(newe);
    }
  }
  INITIATE_CHECK(listener_submit, self, check, cause);
  return 0;
}

static int noit_graphite_onload(mtev_image_t *self) {
  if(!nlerr) nlerr = mtev_log_stream_find("error/graphite");
  if(!nldeb) nldeb = mtev_log_stream_find("debug/graphite");
  listener_onload();
  return 0;
}

static int noit_graphite_init(noit_module_t *self) 
{
  if(!strcmp(self->hdr.name, "graphite_tls")) {
    eventer_name_callback_ext("graphite/graphite_listener", listener_mtev_listener,
                              listener_describe_mtev_callback, self);
  }
  eventer_name_callback_ext("graphite/graphite_handler", listener_handler,
                            listener_describe_callback, self);
  eventer_name_callback_ext("graphite/graphite_listen_handler", listener_listen_handler,
                            listener_describe_callback, self);

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
  noit_listener_config,
  noit_graphite_init,
  noit_graphite_initiate_check,
  noit_listener_cleanup
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
  noit_listener_config,
  noit_graphite_init,
  noit_graphite_initiate_check,
  noit_listener_cleanup
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
  noit_listener_config,
  noit_graphite_init,
  noit_graphite_initiate_check,
  noit_listener_cleanup
};
