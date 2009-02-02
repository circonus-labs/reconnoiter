/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
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

#include <mysql.h>

typedef struct {
  noit_module_t *self;
  noit_check_t *check;
  MYSQL *conn;
  MYSQL_RES *result;
  int rv;
  noit_hash_table attrs;
  int timed_out;
  char *error;
} mysql_check_info_t;

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;

static void mysql_cleanup(noit_module_t *self, noit_check_t *check) {
  mysql_check_info_t *ci = check->closure;
  if(ci) {
    if(ci->result) mysql_free_result(ci->result);
    if(ci->conn) mysql_close(ci->conn);
    noit_check_release_attrs(&ci->attrs);
    if(ci->error) free(ci->error);
    memset(ci, 0, sizeof(*ci));
  }
}
static void mysql_log_results(noit_module_t *self, noit_check_t *check) {
  stats_t current;
  struct timeval duration;
  mysql_check_info_t *ci = check->closure;

  noit_check_stats_clear(&current);

  gettimeofday(&current.whence, NULL);
  sub_timeval(current.whence, check->last_fire_time, &duration);
  current.duration = duration.tv_sec * 1000 + duration.tv_usec / 1000;
  current.available = NP_UNAVAILABLE;
  current.state = NP_BAD;
  if(ci->error) current.status = ci->error;
  else if(ci->timed_out) current.status = "timeout";
  else if(ci->rv == 0) {
    current.available = NP_AVAILABLE;
    current.state = NP_GOOD;
    current.status = "no rows, ok";
  }
  else {
    current.available = NP_AVAILABLE;
    current.state = NP_GOOD;
    current.status = "got rows, ok";
  }

  if(ci->rv > 0) {
    /* metrics */
    int nrows, ncols, i, j;
    nrows = ci->rv;
    ncols = mysql_num_fields(ci->result);
    MYSQL_FIELD *cdesc = mysql_fetch_fields(ci->result);
    for (i=0; i<nrows; i++) {
      noitL(nldeb, "mysql: row %d [%d cols]:\n", i, ncols);
      if(ncols<2) continue;
      MYSQL_ROW row = mysql_fetch_row(ci->result);
      if(NULL == row[0]) continue;
      for (j=1; j<ncols; j++) {
        enum enum_field_types coltype;
        int iv, *piv;
        int64_t lv, *plv;
        double dv, *pdv;
        char *sv;
        char mname[128];
  
        snprintf(mname, sizeof(mname), "%s`%s", row[0], cdesc[j].name);
        coltype = cdesc[j].type;
        noitL(nldeb, "mysql:   col %d (%s) type %d:\n", j, mname, coltype);
        switch(coltype) {
          case FIELD_TYPE_TINY:
          case FIELD_TYPE_SHORT:
          case FIELD_TYPE_LONG:
            if(!row[j]) piv = NULL;
            else {
              iv = strtol(row[j], NULL, 10);
              piv = &iv;
            }
            noit_stats_set_metric(&current, mname, METRIC_INT32, piv);
            break;
          case FIELD_TYPE_INT24:
          case FIELD_TYPE_LONGLONG:
            if(!row[j]) plv = NULL;
            else {
              lv = strtoll(row[j], NULL, 10);
              plv = &lv;
            }
            noit_stats_set_metric(&current, mname, METRIC_INT64, plv);
            break;
          case FIELD_TYPE_DECIMAL:
          case FIELD_TYPE_FLOAT:
          case FIELD_TYPE_DOUBLE:
            if(!row[j]) pdv = NULL;
            else {
              dv = atof(row[j]);
              pdv = &dv;
            }
            noit_stats_set_metric(&current, mname, METRIC_DOUBLE, pdv);
            break;
          default:
            if(!row[j]) sv = NULL;
            else sv = row[j];
            noit_stats_set_metric(&current, mname, METRIC_GUESS, sv);
            break;
        }
      }
    }
  }
  noit_check_set_stats(self, check, &current);
}

#define FETCH_CONFIG_OR(key, str) do { \
  if(!noit_hash_retrieve(check->config, #key, strlen(#key), (void **)&key)) \
    key = str; \
} while(0)

#define AVAIL_BAIL(str) do { \
  ci->timed_out = 0; \
  ci->error = strdup(str); \
  return 0; \
} while(0)

static char *
__noit__strndup(const char *src, int len) {
  int slen;
  char *dst;
  for(slen = 0; slen < len; slen++)
    if(src[slen] == '\0') break;
  dst = malloc(slen + 1);
  memcpy(dst, src, slen);
  dst[slen] = '\0';
  return dst;
}

void mysql_parse_dsn(const char *dsn, noit_hash_table *h) {
  const char *a=dsn, *b=NULL, *c=NULL;
  while (a && (NULL != (b = strchr(a, '=')))) {
    char *key, *val=NULL;
    key = __noit__strndup(a, b-a);
    b++;
    if (b) {
      if (NULL != (c = strchr(b, ' '))) {
        val = __noit__strndup(b, c-b);
      } else {
        val = strdup(b);
      }
    }
    noit_hash_replace(h, key, key?strlen(key):0, val, free, free);
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
  noit_hash_table dsn_h = NOIT_HASH_EMPTY;
  const char *host=NULL;
  const char *user=NULL;
  const char *password=NULL;
  const char *dbname=NULL;
  const char *port_s=NULL;
  const char *socket=NULL;
  u_int32_t port;
  unsigned int timeout;

  if(mask & (EVENTER_READ | EVENTER_WRITE)) {
    /* this case is impossible from the eventer.  It is called as
     * such on the synchronous completion of the event.
     */
    mysql_log_results(ci->self, ci->check);
    mysql_cleanup(ci->self, ci->check);
    check->flags &= ~NP_RUNNING;
    return 0;
  }
  switch(mask) {
    case EVENTER_ASYNCH_WORK:
      FETCH_CONFIG_OR(dsn, "");
      noit_check_interpolate(dsn_buff, sizeof(dsn_buff), dsn,
                             &ci->attrs, check->config);

      mysql_parse_dsn(dsn_buff, &dsn_h);
      noit_hash_retrieve(&dsn_h, "host", strlen("host"), (void**)&host);
      noit_hash_retrieve(&dsn_h, "user", strlen("user"), (void**)&user);
      noit_hash_retrieve(&dsn_h, "password", strlen("password"), (void**)&password);
      noit_hash_retrieve(&dsn_h, "dbname", strlen("dbname"), (void**)&dbname);
      noit_hash_retrieve(&dsn_h, "port", strlen("port"), (void**)&port_s);
      port = port_s ? strtol(port_s, NULL, 10) : 3306;
      noit_hash_retrieve(&dsn_h, "socket", strlen("socket"), (void**)&socket);

      ci->conn = mysql_init(NULL); /* allocate us a handle */
      if(!ci->conn) AVAIL_BAIL("mysql_init failed");
      timeout = check->timeout / 1000;
      mysql_options(ci->conn, MYSQL_OPT_CONNECT_TIMEOUT, (const char *)&timeout);
      if(!mysql_real_connect(ci->conn, host, user, password,
                             dbname, port, socket, CLIENT_IGNORE_SIGPIPE)) {
        noitL(noit_stderr, "error during mysql_real_connect: %s\n",
          mysql_error(ci->conn));
        AVAIL_BAIL(mysql_error(ci->conn));
      }
      if(mysql_ping(ci->conn))
        AVAIL_BAIL(mysql_error(ci->conn));

      FETCH_CONFIG_OR(sql, "");
      noit_check_interpolate(sql_buff, sizeof(sql_buff), sql,
                             &ci->attrs, check->config);
      if (mysql_query(ci->conn, sql_buff))
        AVAIL_BAIL(mysql_error(ci->conn));
      ci->result = mysql_store_result(ci->conn);
      if(!ci->result) AVAIL_BAIL("mysql_store_result failed");
      ci->rv = mysql_num_rows(ci->result);
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

static int mysql_initiate(noit_module_t *self, noit_check_t *check) {
  mysql_check_info_t *ci = check->closure;
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
  noit_check_run_full_asynch(check, mysql_drive_session);
  return 0;
}

static int mysql_initiate_check(noit_module_t *self, noit_check_t *check,
                                   int once, noit_check_t *parent) {
  if(!check->closure) check->closure = calloc(1, sizeof(mysql_check_info_t));
  INITIATE_CHECK(mysql_initiate, self, check);
  return 0;
}

static int mysql_onload(noit_image_t *self) {
  nlerr = noit_log_stream_find("error/mysql");
  nldeb = noit_log_stream_find("debug/mysql");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;

  eventer_name_callback("mysql/mysql_drive_session", mysql_drive_session);
  return 0;
}

#include "mysql.xmlh"
noit_module_t mysql = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "mysql",
    "MySQL Checker",
    mysql_xml_description,
    mysql_onload
  },
  NULL,
  NULL,
  mysql_initiate_check,
  mysql_cleanup
};

