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

static int
opentsdb_handle_payload(noit_check_t *check, char *buffer, size_t len)
{
  char record[4096];
  char *part;
  char *s = NULL, *e = NULL;
  const size_t c_length = len;
  ptrdiff_t length = c_length;
  ptrdiff_t d = 0;
  int rv = 0;

  for (s = buffer; length && *s; s = e + 1) {
    rv++;

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
      length = 0;
    } else {
      /* Update cursor. */
      d = e - s + 1;
      length -= d;
      *e = '\0';
    }
    part = s;
    /* save the row for later logging */
    strlcpy(record, part, sizeof(record) - 1);
    size_t record_len = strlen(part);

    /*
     * a OpenTSDB telnet record is of the format:
     *
     * put <metric> <timestamp> <value> <tagk1=tagv1[ tagk2=tagv2 ...tagkN=tagvN]>\n
     *
     * For example:
     *
     * put sys.cpu.user 1356998400 42.5 host=webserver01 cpu=0
     *
     * A 10 digit timestamp is in seconds since epoch.
     * A 13 digit timestamp or a 10 digit timestamp with a decimal point and 3 more digits
     * are seconds and milliseconds.
     */

    // first space is metric name
    char *first_space = (char *)memchr(part, ' ', record_len);
    if (first_space == NULL) {
      first_space = (char *)memchr(part, '\t', record_len);
      if (first_space == NULL) {
        mtevL(nldeb, "Invalid OpenTSDB record, can't find the first space or tab in: %s\n", record);
        continue;
      }
    }
    *first_space++ = 0;
    while (*first_space == ' ' || *first_space == '\t') first_space++;

    // second space is timestamp
    char *second_space = (char *)memchr(first_space + 1, ' ', record_len - (first_space - part +
                                                                            1));
    if (second_space == NULL) {
      second_space = (char *)memchr(first_space + 1, '\t', record_len - (first_space - part + 1));
      if (second_space == NULL) {
        mtevL(nldeb, "Invalid OpenTSDB record, can't find the second space or tab in: %s\n", record);
        continue;
      }
    }
    *second_space++ = 0;
    while (*second_space == ' ' || *second_space == '\t') second_space++;

    // third space is value
    char *third_space = (char *)memchr(second_space + 1, ' ', record_len - (second_space - part +
                                                                            1));
    if (third_space == NULL) {
      third_space = (char *)memchr(second_space + 1, '\t', record_len - (second_space - part + 1));
      if (third_space == NULL) {
        mtevL(nldeb, "Invalid OpenTSDB record, can't find the third space or tab in: %s\n", record);
        continue;
      }
    }
    *third_space++ = 0;
    while (*third_space == ' ' || *third_space == '\t') third_space++;

    // fourth space is tags
    char *fourth_space = (char *)memchr(third_space + 1, ' ', record_len - (third_space - part +
                                                                            1));
    if (fourth_space == NULL) {
      fourth_space = (char *)memchr(third_space + 1, '\t', record_len - (third_space - part + 1));
      if (fourth_space == NULL) {
        mtevL(nldeb, "Invalid OpenTSDB record, can't find the fourth space or tab in: %s\n", record);
        continue;
      }
    }
    *fourth_space++ = 0;
    while (*fourth_space == ' ' || *fourth_space == '\t') fourth_space++;

    mtevL(nldeb, "OpenTSDB record: %s\n", record);
    char *opentsdb_metric_name = first_space;
    char *opentsdb_timestamp = second_space;
    char *opentsdb_value = third_space;
    char *opentsdb_tags = fourth_space;

    // timestamps for telnet are allowed to contain a '.' before the milliseconds
    // otherwise, if it is 10 digits or less it is seconds, more than 10 digits is ms
    char *dp;
    uint64_t whence_ms = 0;
    if (strlen(opentsdb_timestamp) <= 10)
    {
      // less than or equal to 10 digits is s -> ms
      whence_ms = strtoull(opentsdb_timestamp, &dp, 10) * 1000;
    }
    else {
      // more than 10 is ms
      whence_ms = strtoull(opentsdb_timestamp, &dp, 10);
      // unless it is more than 10 with '.', then it is floating point s
      if (dp && *dp == '.') {
        // s -> ms
        whence_ms *= 1000;
        // add in s (fractional part) -> ms
        whence_ms += (int)(1000.0 * atof(dp));
      }
    }

    size_t opentsdb_value_len = strlen(opentsdb_value);
    if (count_integral_digits(opentsdb_value, opentsdb_value_len, mtev_true) == 0) {
      mtevL(nldeb, "Invalid OpenTSDB record, no digits in value: %s\n", third_space);
      continue;
    }

    double metric_value = 0.0;
    const char *dot = strchr(opentsdb_value, '.');
    if (dot == NULL) {
      /* attempt fast integer parse, if we fail, jump to strtod parse */
      /* OpenTSDB only supports signed int64 */
      errno = 0;
      int64_t v = strtoll(opentsdb_value, NULL, 10);
      if (errno == ERANGE) {
        goto strtod_parse;
      }
      metric_value = (double)v;
    } else {
    strtod_parse:
      errno = 0;
      metric_value = strtod(opentsdb_value, NULL);
      if (errno == ERANGE) {
        mtevL(nldeb, "Invalid opentsdb record, strtod cannot parse value: %s\n", third_space);
        continue;
      }
    }

    /* allow for any length name + tags in the broker */
    mtev_dyn_buffer_t tagged_name;
    mtev_dyn_buffer_init(&tagged_name);

    /* OpenTSDB tags are k=v list with at least one kv, each kv separated by space
     * Re-format incoming name string into our tag format for parsing */
    mtev_dyn_buffer_add(&tagged_name, (uint8_t *)opentsdb_metric_name, strlen(opentsdb_metric_name));
    mtev_dyn_buffer_add(&tagged_name, (uint8_t *)"|ST[", 4);
    /* look for K=V pairs */
    char *pair, *lasts;
    bool comma = false;
    char *space_loc = opentsdb_tags;
    for (pair = strtok_r(space_loc, " ", &lasts); pair; pair = strtok_r(NULL, " ", &lasts)) {
      const char *equal = strchr(pair, '=');
      if (equal) {
        if (comma) mtev_dyn_buffer_add(&tagged_name, (uint8_t *)",", 1);
        mtev_dyn_buffer_add(&tagged_name, (uint8_t *)pair, equal - pair);
        mtev_dyn_buffer_add(&tagged_name, (uint8_t *)":", 1);
        mtev_dyn_buffer_add_printf(&tagged_name, "%s", equal + 1);
        comma = true;
      }
    }
    mtev_dyn_buffer_add(&tagged_name, (uint8_t *)"]", 1);
    mtev_dyn_buffer_add(&tagged_name, (uint8_t*)"\0", 1);

    mtevL(nldeb, "Reformatted OpenTSDB name: %s\n", mtev_dyn_buffer_data(&tagged_name));

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
  return rv;
}

static int
noit_opentsdb_initiate_check(noit_module_t *self,
                                        noit_check_t *check,
                                        int once, noit_check_t *cause) {
  check->flags |= NP_PASSIVE_COLLECTION;
  if (check->closure == NULL)
  {

    listener_closure_t *ccl;
    ccl = check->closure =
      listener_closure_alloc("opentsdb", self, check, nldeb, nlerr,
                             opentsdb_handle_payload, NULL);

    unsigned short port = 4242;
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
        mtevL(noit_error, "opentsdb: socket failed: %s\n", strerror(errno));
        return -1;
      }
    }
    else {
      if(eventer_set_fd_nonblocking(ccl->ipv4_listen_fd)) {
        close(ccl->ipv4_listen_fd);
        ccl->ipv4_listen_fd = -1;
        mtevL(noit_error,
              "opentsdb: could not set socket (IPv4) non-blocking: %s\n",
              strerror(errno));
        return -1;
      }
    socklen_t reuse = 1;
    if(setsockopt(ccl->ipv4_listen_fd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(reuse)) != 0) {
      mtevL(nlerr, "opentsdb listener(IPv4) failed(%s) to set REUSEADDR (doing our best)\n", strerror(errno));
    }
#ifdef SO_REUSEPORT
    reuse = 1;
    if(setsockopt(ccl->ipv4_listen_fd, SOL_SOCKET, SO_REUSEPORT, (void *)&reuse, sizeof(reuse)) != 0) {
      mtevL(nlerr, "opentsdb listener(IPv4) failed(%s) to set REUSEPORT (doing our best)\n", strerror(errno));
    }
#endif
      memset(&skaddr, 0, sizeof(skaddr));
      skaddr.sin_family = AF_INET;
      skaddr.sin_addr.s_addr = htonl(INADDR_ANY);
      skaddr.sin_port = htons(ccl->port);
      sockaddr_len = sizeof(skaddr);
      if(bind(ccl->ipv4_listen_fd, (struct sockaddr *)&skaddr, sockaddr_len) < 0) {
        mtevL(noit_error, "opentsdb bind(IPv4) failed[%d]: %s\n", ccl->port, strerror(errno));
        close(ccl->ipv4_listen_fd);
        return -1;
      }
      if (listen(ccl->ipv4_listen_fd, 5) != 0) {
        mtevL(noit_error, "opentsdb listen(IPv4) failed[%d]: %s\n", ccl->port, strerror(errno));
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
        mtevL(noit_error, "opentsdb: IPv6 socket failed: %s\n",
              strerror(errno));
      }
    }
    else {
      if(eventer_set_fd_nonblocking(ccl->ipv6_listen_fd)) {
        close(ccl->ipv6_listen_fd);
        ccl->ipv6_listen_fd = -1;
        mtevL(noit_error,
              "opentsdb: could not set socket (IPv6) non-blocking: %s\n",
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

        socklen_t reuse = 1;
        if(setsockopt(ccl->ipv6_listen_fd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(reuse)) != 0) {
          mtevL(nlerr, "opentsdb listener(IPv6) failed(%s) to set REUSEADDR (doing our best)\n", strerror(errno));
        }
#ifdef SO_REUSEPORT
        reuse = 1;
        if(setsockopt(ccl->ipv6_listen_fd, SOL_SOCKET, SO_REUSEPORT, (void *)&reuse, sizeof(reuse)) != 0) {
          mtevL(nlerr, "opentsdb listener(IPv6) failed(%s) to set REUSEPORT (doing our best)\n", strerror(errno));
        }
#endif
        if(bind(ccl->ipv6_listen_fd, (struct sockaddr *)&skaddr6, sockaddr_len) < 0) {
          mtevL(noit_error, "opentsdb bind(IPv6) failed[%d]: %s\n",
                ccl->port, strerror(errno));
          close(ccl->ipv6_listen_fd);
          ccl->ipv6_listen_fd = -1;
        }

        else if (listen(ccl->ipv6_listen_fd, 5) != 0) {
          mtevL(noit_error, "opentsdb listen(IPv6) failed[%d]: %s\n", ccl->port, strerror(errno));
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

static int
noit_opentsdb_onload(mtev_image_t *self) {
  if(!nlerr) nlerr = mtev_log_stream_find("error/opentsdb");
  if(!nldeb) nldeb = mtev_log_stream_find("debug/opentsdb");
  listener_onload();
  return 0;
}

static int
noit_opentsdb_init(noit_module_t *self)
{
  eventer_name_callback_ext("opentsdb/opentsdb_handler", listener_handler,
                            listener_describe_callback, self);
  eventer_name_callback_ext("opentsdb/opentsdb_listen_handler", listener_listen_handler,
                            listener_describe_callback, self);

  return 0;
}

#include "opentsdb.xmlh"
noit_module_t opentsdb = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "opentsdb",
    .description = "opentsdb collection",
    .xml_description = opentsdb_xml_description,
    .onload = noit_opentsdb_onload
  },
  noit_listener_config,
  noit_opentsdb_init,
  noit_opentsdb_initiate_check,
  noit_listener_cleanup
};
