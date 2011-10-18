/*
 * Copyright (c) 2007-2010, OmniTI Computer Consulting, Inc.
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

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"

#include <libpq-fe.h>

/* Ripped from postgres 8.3.3 */
#ifndef BOOLOID
#define BOOLOID                  16
#endif
#ifndef INT2OID
#define INT2OID                  21
#endif
#ifndef INT4OID
#define INT4OID                  23
#endif
#ifndef INT8OID
#define INT8OID                  20
#endif
#ifndef FLOAT4OID
#define FLOAT4OID                700
#endif
#ifndef FLOAT8OID
#define FLOAT8OID                701
#endif
#ifndef NUMERICOID
#define NUMERICOID               1700
#endif

typedef struct {
  noit_module_t *self;
  noit_check_t *check;
  stats_t current;
  PGconn *conn;
  PGresult *result;
  int rv;
  double connect_duration_d;
  double *connect_duration;
  double query_duration_d;
  double *query_duration;
  noit_hash_table attrs;
  int timed_out;
  char *error;
} postgres_check_info_t;

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;

static void postgres_cleanup(noit_module_t *self, noit_check_t *check) {
  postgres_check_info_t *ci = check->closure;
  if(ci) {
    if(ci->result) PQclear(ci->result);
    if(ci->conn) PQfinish(ci->conn);
    noit_check_release_attrs(&ci->attrs);
    if(ci->error) free(ci->error);
    memset(ci, 0, sizeof(*ci));
  }
}
static void postgres_ingest_stats(postgres_check_info_t *ci) {
  if(ci->rv == PGRES_TUPLES_OK) {
    /* metrics */
    int nrows, ncols, i, j;
    nrows = PQntuples(ci->result);
    ncols = PQnfields(ci->result);
    noit_stats_set_metric(&ci->current, "row_count", METRIC_INT32, &nrows);
    for (i=0; i<nrows; i++) {
      noitL(nldeb, "postgres: row %d [%d cols]:\n", i, ncols);
      if(ncols<2) continue;
      if(PQgetisnull(ci->result, i, 0)) continue;
      for (j=1; j<ncols; j++) {
        Oid coltype;
        int iv, *piv;
        int64_t lv, *plv;
        double dv, *pdv;
        char *sv;
        char mname[128];
  
        snprintf(mname, sizeof(mname), "%s`%s",
                 PQgetvalue(ci->result, i, 0), PQfname(ci->result, j));
        coltype = PQftype(ci->result, j);
        noitL(nldeb, "postgres:   col %d (%s) type %d: %s\n", j, mname, coltype,
              PQgetisnull(ci->result, i, j) ? "[[null]]" : PQgetvalue(ci->result, i, j));
        switch(coltype) {
          case BOOLOID:
            if(PQgetisnull(ci->result, i, j)) piv = NULL;
            else {
              iv = strcmp(PQgetvalue(ci->result, i, j), "f") ? 1 : 0;
              piv = &iv;
            }
            noit_stats_set_metric(&ci->current, mname, METRIC_INT32, piv);
            break;
          case INT2OID:
          case INT4OID:
          case INT8OID:
            if(PQgetisnull(ci->result, i, j)) plv = NULL;
            else {
              lv = strtoll(PQgetvalue(ci->result, i, j), NULL, 10);
              plv = &lv;
            }
            noit_stats_set_metric(&ci->current, mname, METRIC_INT64, plv);
            break;
          case FLOAT4OID:
          case FLOAT8OID:
          case NUMERICOID:
            if(PQgetisnull(ci->result, i, j)) pdv = NULL;
            else {
              dv = atof(PQgetvalue(ci->result, i, j));
              pdv = &dv;
            }
            noit_stats_set_metric(&ci->current, mname, METRIC_DOUBLE, pdv);
            break;
          default:
            if(PQgetisnull(ci->result, i, j)) sv = NULL;
            else sv = PQgetvalue(ci->result, i, j);
            noit_stats_set_metric(&ci->current, mname, METRIC_GUESS, sv);
            break;
        }
      }
    }
  }
}
static void postgres_log_results(noit_module_t *self, noit_check_t *check) {
  struct timeval duration;
  postgres_check_info_t *ci = check->closure;

  gettimeofday(&ci->current.whence, NULL);
  sub_timeval(ci->current.whence, check->last_fire_time, &duration);
  ci->current.duration = duration.tv_sec * 1000 + duration.tv_usec / 1000;
  ci->current.available = NP_UNAVAILABLE;
  ci->current.state = NP_BAD;
  if(ci->connect_duration)
    noit_stats_set_metric(&ci->current, "connect_duration", METRIC_DOUBLE,
                          ci->connect_duration);
  if(ci->query_duration)
    noit_stats_set_metric(&ci->current, "query_duration", METRIC_DOUBLE,
                          ci->query_duration);
  if(ci->error) ci->current.status = ci->error;
  else if(ci->timed_out) ci->current.status = "timeout";
  else if(ci->rv == PGRES_COMMAND_OK) {
    ci->current.available = NP_AVAILABLE;
    ci->current.state = NP_GOOD;
    ci->current.status = "command ok";
  }
  else if(ci->rv == PGRES_TUPLES_OK) {
    ci->current.available = NP_AVAILABLE;
    ci->current.state = NP_GOOD;
    ci->current.status = "tuples ok";
  }
  else ci->current.status = "internal error";

  noit_check_set_stats(self, check, &ci->current);
}

#define FETCH_CONFIG_OR(key, str) do { \
  if(!noit_hash_retr_str(check->config, #key, strlen(#key), &key)) \
    key = str; \
} while(0)

#define AVAIL_BAIL(str) do { \
  PGconn *conn_swap = ci->conn; \
  PGresult *result_swap = ci->result; \
  if(ci->result) { ci->result = NULL; PQclear(result_swap); } \
  if(ci->conn) { ci->conn = NULL; PQfinish(conn_swap); } \
  ci->timed_out = 0; \
  ci->error = strdup(str); \
  return 0; \
} while(0)

static int postgres_drive_session(eventer_t e, int mask, void *closure,
                                  struct timeval *now) {
  const char *dsn, *sql;
  char sql_buff[8192];
  char dsn_buff[512];
  struct timeval t1, t2, diff;
  postgres_check_info_t *ci = closure;
  noit_check_t *check = ci->check;

  if(mask & (EVENTER_READ | EVENTER_WRITE)) {
    /* this case is impossible from the eventer.  It is called as
     * such on the synchronous completion of the event.
     */
    postgres_log_results(ci->self, ci->check);
    postgres_cleanup(ci->self, ci->check);
    check->flags &= ~NP_RUNNING;
    return 0;
  }

  switch(mask) {
    case EVENTER_ASYNCH_WORK:
      noit_check_stats_clear(&ci->current);
      ci->connect_duration = NULL;
      ci->query_duration = NULL;

      FETCH_CONFIG_OR(dsn, "");
      noit_check_interpolate(dsn_buff, sizeof(dsn_buff), dsn,
                             &ci->attrs, check->config);
      ci->conn = PQconnectdb(dsn_buff);
      if(!ci->conn) AVAIL_BAIL("PQconnectdb failed");
      if(PQstatus(ci->conn) != CONNECTION_OK)
        AVAIL_BAIL(PQerrorMessage(ci->conn));

      FETCH_CONFIG_OR(sql, "");
      noit_check_interpolate(sql_buff, sizeof(sql_buff), sql,
                             &ci->attrs, check->config);
      gettimeofday(&t1, NULL);
      sub_timeval(t1, check->last_fire_time, &diff);
      ci->connect_duration_d = diff.tv_sec * 1000.0 + diff.tv_usec / 1000.0;
      ci->connect_duration = &ci->connect_duration_d;

      ci->result = PQexec(ci->conn, sql_buff);

      gettimeofday(&t2, NULL);
      sub_timeval(t2, t1, &diff);
      ci->query_duration_d = diff.tv_sec * 1000.0 + diff.tv_usec / 1000.0;
      ci->query_duration = &ci->query_duration_d;

      if(!ci->result) AVAIL_BAIL("PQexec failed");
      ci->rv = PQresultStatus(ci->result);
      switch(ci->rv) {
       case PGRES_TUPLES_OK:
        postgres_ingest_stats(ci);
       case PGRES_COMMAND_OK:
        break;
       default:
        AVAIL_BAIL(PQresultErrorMessage(ci->result));
      }
      if(ci->result) {
        PGresult *result_swap = ci->result;
        ci->result = NULL;
        PQclear(result_swap);
      }
      if(ci->conn) {
        PGconn *conn_swap = ci->conn;
        ci->conn = NULL;
        PQfinish(conn_swap);
      }
      ci->timed_out = 0;
      return 0;
      break;
    case EVENTER_ASYNCH_CLEANUP:
      /* This sets us up for a completion call. */
      e->mask = EVENTER_READ | EVENTER_WRITE;
      break;
    default:
      abort();
  }
  return 0;
}

static int postgres_initiate(noit_module_t *self, noit_check_t *check,
                             noit_check_t *cause) {
  postgres_check_info_t *ci = check->closure;
  struct timeval __now;

  /* We cannot be running */
  assert(!(check->flags & NP_RUNNING));
  check->flags |= NP_RUNNING;

  ci->self = self;
  ci->check = check;

  ci->timed_out = 1;
  ci->rv = -1;
  noit_check_make_attrs(check, &ci->attrs);
  gettimeofday(&__now, NULL);
  memcpy(&check->last_fire_time, &__now, sizeof(__now));

  /* Register a handler for the worker */
  noit_check_run_full_asynch(check, postgres_drive_session);
  return 0;
}

static int postgres_initiate_check(noit_module_t *self, noit_check_t *check,
                                   int once, noit_check_t *cause) {
  if(!check->closure) check->closure = calloc(1, sizeof(postgres_check_info_t));
  INITIATE_CHECK(postgres_initiate, self, check, cause);
  return 0;
}

static int postgres_onload(noit_image_t *self) {
  nlerr = noit_log_stream_find("error/postgres");
  nldeb = noit_log_stream_find("debug/postgres");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;

  eventer_name_callback("http/postgres_drive_session", postgres_drive_session);
  return 0;
}

#include "postgres.xmlh"
noit_module_t postgres = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "postgres",
    "PostgreSQL Checker",
    postgres_xml_description,
    postgres_onload
  },
  NULL,
  NULL,
  postgres_initiate_check,
  postgres_cleanup
};

