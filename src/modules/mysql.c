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

#include <mtev_hash.h>
#include <mtev_str.h>

#include "noit_config.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_mtev_bridge.h"

#ifdef HAVE_MYSQL_H
#include <mysql.h>
#else
#ifdef HAVE_MYSQL_MYSQL_H
#include <mysql/mysql.h>
#else
#error No mysql.h header present.  This is not going to work at all.
#endif
#endif

typedef struct {
  noit_module_t *self;
  noit_check_t *check;
  MYSQL *conn;
  MYSQL_RES *result;
  double connect_duration_d;
  double *connect_duration;
  double query_duration_d;
  double *query_duration;
  int rv;
  mtev_hash_table attrs;
  int timed_out;
  char *error;
} mysql_check_info_t;

static mtev_log_stream_t nlerr = NULL;
static mtev_log_stream_t nldeb = NULL;

static void mysql_cleanse(noit_module_t *self, noit_check_t *check) {
  mysql_check_info_t *ci = check->closure;
  if(ci) {
    if(ci->result) mysql_free_result(ci->result);
    if(ci->conn) mysql_close(ci->conn);
    noit_check_release_attrs(&ci->attrs);
    if(ci->error) free(ci->error);
    memset(ci, 0, sizeof(*ci));
  }
}
static void mysql_cleanup(noit_module_t *self, noit_check_t *check) {
  mysql_check_info_t *ci = check->closure;
  mysql_cleanse(self, check);
  free(ci);
}
static void mysql_ingest_stats(mysql_check_info_t *ci) {
  if(ci->rv > 0) {
    /* metrics */
    int nrows, ncols, i, j;
    nrows = ci->rv;
    ncols = mysql_num_fields(ci->result);
    MYSQL_FIELD *cdesc = mysql_fetch_fields(ci->result);
    for (i=0; i<nrows; i++) {
      mtevL(nldeb, "mysql: row %d [%d cols]:\n", i, ncols);
      if(ncols<2) continue;
      MYSQL_ROW row = mysql_fetch_row(ci->result);
      if(NULL == row[0]) continue;
      for (j=1; j<ncols; j++) {
        enum enum_field_types coltype;
        int iv, *piv;
        int64_t lv, *plv;
        double dv, *pdv;
        char *sv;
        char mname[MAX_METRIC_TAGGED_NAME];
  
        snprintf(mname, sizeof(mname), "%s`%s", row[0], cdesc[j].name);
        coltype = cdesc[j].type;
        mtevL(nldeb, "mysql:   col %d (%s) type %d:\n", j, mname, coltype);
        switch(coltype) {
          case FIELD_TYPE_TINY:
          case FIELD_TYPE_SHORT:
          case FIELD_TYPE_LONG:
            if(!row[j]) piv = NULL;
            else {
              iv = strtol(row[j], NULL, 10);
              piv = &iv;
            }
            noit_stats_set_metric(ci->check, mname, METRIC_INT32, piv);
            break;
          case FIELD_TYPE_INT24:
          case FIELD_TYPE_LONGLONG:
            if(!row[j]) plv = NULL;
            else {
              lv = strtoll(row[j], NULL, 10);
              plv = &lv;
            }
            noit_stats_set_metric(ci->check, mname, METRIC_INT64, plv);
            break;
          case FIELD_TYPE_DECIMAL:
          case FIELD_TYPE_FLOAT:
          case FIELD_TYPE_DOUBLE:
            if(!row[j]) pdv = NULL;
            else {
              dv = atof(row[j]);
              pdv = &dv;
            }
            noit_stats_set_metric(ci->check, mname, METRIC_DOUBLE, pdv);
            break;
          default:
            if(!row[j]) sv = NULL;
            else sv = row[j];
            noit_stats_set_metric(ci->check, mname, METRIC_GUESS, sv);
            break;
        }
      }
    }
  }
}
static void mysql_log_results(noit_module_t *self, noit_check_t *check) {
  struct timeval duration, now;
  mysql_check_info_t *ci = check->closure;

  mtev_gettimeofday(&now, NULL);
  sub_timeval(now, check->last_fire_time, &duration);
  noit_stats_set_whence(check, &now);
  noit_stats_set_duration(check, duration.tv_sec * 1000 + duration.tv_usec / 1000);
  noit_stats_set_available(check, NP_UNAVAILABLE);
  noit_stats_set_state(check, NP_BAD);
  if(ci->error) noit_stats_set_status(check, ci->error);
  else if(ci->timed_out) noit_stats_set_status(check, "timeout");
  else if(ci->rv == 0) {
    noit_stats_set_available(check, NP_AVAILABLE);
    noit_stats_set_state(check, NP_GOOD);
    noit_stats_set_status(check, "no rows, ok");
  }
  else {
    noit_stats_set_available(check, NP_AVAILABLE);
    noit_stats_set_state(check, NP_GOOD);
    noit_stats_set_status(check, "got rows, ok");
  }

  if(ci->rv >= 0)
    noit_stats_set_metric(check, "row_count", METRIC_INT32, &ci->rv);
  if(ci->connect_duration)
    noit_stats_set_metric(check, "connect_duration", METRIC_DOUBLE,
                          ci->connect_duration);
  if(ci->query_duration)
    noit_stats_set_metric(check, "query_duration", METRIC_DOUBLE,
                          ci->query_duration);

  noit_check_set_stats(check);
}

#define FETCH_CONFIG_OR(key, str) do { \
  if(!mtev_hash_retrieve(check->config, #key, strlen(#key), (void **)&key)) \
    key = str; \
} while(0)

#define AVAIL_BAIL(str) do { \
  MYSQL *conn_swap = ci->conn; \
  MYSQL_RES *result_swap = ci->result; \
  if(ci->result) { \
    ci->result = NULL; \
    mysql_free_result(result_swap); \
  } \
  if(ci->conn) { \
    ci->conn = NULL; \
    mysql_close(conn_swap); \
  } \
  ci->timed_out = 0; \
  ci->error = strdup(str); \
  mtev_hash_destroy(&dsn_h, free, free); \
  return 0; \
} while(0)

void mysql_parse_dsn(const char *dsn, mtev_hash_table *h) {
  const char *a=dsn, *b=NULL, *c=NULL;
  while (a && (NULL != (b = strchr(a, '=')))) {
    char *key, *val=NULL;
    key = mtev_strndup(a, b-a);
    b++;
    if (b) {
      if (NULL != (c = strchr(b, ' '))) {
        val = mtev_strndup(b, c-b);
      } else {
        val = strdup(b);
      }
    }
    mtev_hash_replace(h, key, key?strlen(key):0, val, free, free);
    a = c;
    if (a) a++;
  }
}

static int mysql_drive_session(eventer_t e, int mask, void *closure,
                                  struct timeval *now) {
  const char *dsn, *sql;
  char sql_buff[8192];
  char dsn_buff[512];
  mysql_check_info_t *ci = closure;
  noit_check_t *check = ci->check;
  struct timeval t1, t2, diff;
  mtev_hash_table dsn_h;
  const char *host=NULL;
  const char *user=NULL;
  const char *password=NULL;
  const char *dbname=NULL;
  const char *port_s=NULL;
  const char *socket=NULL;
  const char *sslmode=NULL;
  uint32_t port;
  unsigned long client_flag = CLIENT_IGNORE_SIGPIPE;
  unsigned int timeout;

  mtev_hash_init(&dsn_h);

  if(mask & (EVENTER_READ | EVENTER_WRITE)) {
    /* this case is impossible from the eventer.  It is called as
     * such on the synchronous completion of the event.
     */
    mysql_log_results(ci->self, ci->check);
    mysql_cleanse(ci->self, ci->check);
    noit_check_end(check);
    return 0;
  }
  switch(mask) {
    case EVENTER_ASYNCH_WORK:
      ci->connect_duration = NULL;
      ci->query_duration = NULL;

      FETCH_CONFIG_OR(dsn, "");
      noit_check_interpolate(dsn_buff, sizeof(dsn_buff), dsn,
                             &ci->attrs, check->config);

      mysql_parse_dsn(dsn_buff, &dsn_h);
      (void)mtev_hash_retrieve(&dsn_h, "host", strlen("host"), (void**)&host);
      (void)mtev_hash_retrieve(&dsn_h, "user", strlen("user"), (void**)&user);
      (void)mtev_hash_retrieve(&dsn_h, "password", strlen("password"), (void**)&password);
      (void)mtev_hash_retrieve(&dsn_h, "dbname", strlen("dbname"), (void**)&dbname);
      (void)mtev_hash_retrieve(&dsn_h, "port", strlen("port"), (void**)&port_s);
      if(mtev_hash_retrieve(&dsn_h, "sslmode", strlen("sslmode"), (void**)&sslmode) &&
         !strcmp(sslmode, "require"))
        client_flag |= CLIENT_SSL;
      port = port_s ? strtol(port_s, NULL, 10) : 3306;
      (void)mtev_hash_retrieve(&dsn_h, "socket", strlen("socket"), (void**)&socket);

      ci->conn = mysql_init(NULL); /* allocate us a handle */
      if(!ci->conn) AVAIL_BAIL("mysql_init failed");
      timeout = check->timeout / 1000;
      mysql_options(ci->conn, MYSQL_OPT_CONNECT_TIMEOUT, (const char *)&timeout);
      if(!mysql_real_connect(ci->conn, host, user, password,
                             dbname, port, socket, client_flag)) {
        mtevL(noit_stderr, "error during mysql_real_connect: %s\n",
          mysql_error(ci->conn));
        AVAIL_BAIL(mysql_error(ci->conn));
      }
      if(mysql_ping(ci->conn))
        AVAIL_BAIL(mysql_error(ci->conn));

#if MYSQL_VERSION_ID >= 50000
      if (sslmode && !strcmp(sslmode, "require")) {
        /* mysql has a bad habit of silently failing to establish ssl and
         * falling back to unencrypted, so after making the connection, let's 
         * check that we're actually using SSL by checking for a non-NULL 
         * return value from mysql_get_ssl_cipher().
         */
        if (mysql_get_ssl_cipher(ci->conn) == NULL) {
          mtevL(nldeb, "mysql_get_ssl_cipher() returns NULL, but SSL mode required.");
          AVAIL_BAIL("mysql_get_ssl_cipher() returns NULL, but SSL mode required.");
        }
      }
#endif

      mtev_gettimeofday(&t1, NULL);
      sub_timeval(t1, check->last_fire_time, &diff);
      ci->connect_duration_d = diff.tv_sec * 1000.0 + diff.tv_usec / 1000.0;
      ci->connect_duration = &ci->connect_duration_d;

      FETCH_CONFIG_OR(sql, "");
      noit_check_interpolate(sql_buff, sizeof(sql_buff), sql,
                             &ci->attrs, check->config);
      if (mysql_query(ci->conn, sql_buff))
        AVAIL_BAIL(mysql_error(ci->conn));

      mtev_gettimeofday(&t2, NULL);
      sub_timeval(t2, t1, &diff);
      ci->query_duration_d = diff.tv_sec * 1000.0 + diff.tv_usec / 1000.0;
      ci->query_duration = &ci->query_duration_d;

      ci->result = mysql_store_result(ci->conn);
      if(!ci->result) AVAIL_BAIL("mysql_store_result failed");
      ci->rv = mysql_num_rows(ci->result);
      mysql_ingest_stats(ci);
      if(ci->result) {
        MYSQL_RES *result_swap = ci->result;
        ci->result = NULL;
        mysql_free_result(result_swap);
      }
      if(ci->conn) {
        MYSQL *conn_swap = ci->conn;
        ci->conn = NULL;
        mysql_close(conn_swap);
      }
      ci->timed_out = 0;
      mtev_hash_destroy(&dsn_h, free, free);
      return 0;
      break;
    case EVENTER_ASYNCH_CLEANUP:
      /* This sets us up for a completion call. */
      eventer_set_mask(e, EVENTER_READ | EVENTER_WRITE);
      break;
    default:
      mtevFatal(mtev_error, "Unknown mask: 0x%04x\n", mask);
  }
  return 0;
}

static int mysql_initiate(noit_module_t *self, noit_check_t *check,
                          noit_check_t *cause) {
  mysql_check_info_t *ci = check->closure;
  struct timeval __now;

  /* We cannot be running */
  BAIL_ON_RUNNING_CHECK(check);
  noit_check_begin(check);

  ci->self = self;
  ci->check = check;

  ci->timed_out = 1;
  ci->rv = -1;
  noit_check_make_attrs(check, &ci->attrs);
  mtev_gettimeofday(&__now, NULL);
  memcpy(&check->last_fire_time, &__now, sizeof(__now));

  /* Register a handler for the worker */
  noit_check_run_full_asynch(check, mysql_drive_session);
  return 0;
}

static int mysql_initiate_check(noit_module_t *self, noit_check_t *check,
                                   int once, noit_check_t *cause) {
  if(!check->closure) {
    mysql_check_info_t *check_info = calloc(1, sizeof(mysql_check_info_t));
    check->closure = (void*)check_info;
  }
  INITIATE_CHECK(mysql_initiate, self, check, cause);
  return 0;
}

static int mysql_onload(mtev_image_t *self) {
  nlerr = mtev_log_stream_find("error/mysql");
  nldeb = mtev_log_stream_find("debug/mysql");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;

  eventer_name_callback("mysql/mysql_drive_session", mysql_drive_session);
  return 0;
}

#include "mysql.xmlh"
noit_module_t mysql = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "mysql",
    .description = "MySQL Checker",
    .xml_description = mysql_xml_description,
    .onload = mysql_onload
  },
  NULL,
  NULL,
  mysql_initiate_check,
  mysql_cleanup
};

