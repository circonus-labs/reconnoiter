/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "stratcon_datastore.h"
#include "noit_conf.h"
#include "noit_check.h"
#include <unistd.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <libpq-fe.h>

static char *check_insert = NULL;
static const char *check_insert_conf = "/stratcon/database/statements/check";
static char *status_insert = NULL;
static const char *status_insert_conf = "/stratcon/database/statements/status";
static char *metric_insert_numeric = NULL;
static const char *metric_insert_numeric_conf = "/stratcon/database/statements/metric_numeric";
static char *metric_insert_text = NULL;
static const char *metric_insert_text_conf = "/stratcon/database/statements/metric_text";

#define GET_QUERY(a) do { \
  if(a == NULL) \
    if(!noit_conf_get_string(NULL, a ## _conf, &(a))) \
      goto bad_row; \
} while(0)

#define MAX_PARAMS 8
typedef struct ds_job_detail {
  char *data;  /* The raw string, NULL means the stream is done -- commit. */
  int problematic;
  eventer_t completion_event; /* This event should be registered if non NULL */
  struct ds_job_detail *next;

  /* Postgres specific stuff */
  int nparams;
  int metric_type;
  char *paramValues[MAX_PARAMS];
  int paramLengths[MAX_PARAMS];
  int paramFormats[MAX_PARAMS];
  int paramAllocd[MAX_PARAMS];
} ds_job_detail;

typedef struct {
  struct sockaddr *remote;
  eventer_jobq_t  *jobq;
  /* Postgres specific stuff */
  PGconn          *dbh;
  ds_job_detail   *head;
  ds_job_detail   *tail;
} conn_q;

static void
free_params(ds_job_detail *d) {
  int i;
  for(i=0; i<d->nparams; i++)
    if(d->paramAllocd[i] && d->paramValues[i])
      free(d->paramValues[i]);
}

static void
__append(conn_q *q, ds_job_detail *d) {
  d->next = NULL;
  if(!q->head) q->head = q->tail = d;
  else {
    q->tail->next = d;
    q->tail = d;
  }
}
static void
__remove_until(conn_q *q, ds_job_detail *d) {
  ds_job_detail *next;
  while(q->head && q->head != d) {
    next = q->head;
    q->head = q->head->next;
    free_params(next);
    if(next->data) free(next->data);
    free(next);
  }
  if(!q->head) q->tail = NULL;
}

noit_hash_table ds_conns;

conn_q *
__get_conn_q_for_remote(struct sockaddr *remote) {
  conn_q *cq;
  int len = 0;
  switch(remote->sa_family) {
    case AF_INET: len = sizeof(struct sockaddr_in); break;
    case AF_INET6: len = sizeof(struct sockaddr_in6); break;
    case AF_UNIX: len = SUN_LEN(((struct sockaddr_un *)remote)); break;
    default: return NULL;
  }
  if(noit_hash_retrieve(&ds_conns, (const char *)remote, len, (void **)&cq))
    return cq;
  cq = calloc(1, sizeof(*cq));
  cq->remote = malloc(len);
  memcpy(cq->remote, remote, len);
  cq->jobq = calloc(1, sizeof(*cq->jobq));
  eventer_jobq_init(cq->jobq);
  cq->jobq->backq = eventer_default_backq();
  /* Add one thread */
  eventer_jobq_increase_concurrency(cq->jobq);
  noit_hash_store(&ds_conns, (const char *)cq->remote, len, cq);
  return cq;
}

typedef enum {
  DS_EXEC_SUCCESS = 0,
  DS_EXEC_ROW_FAILED = 1,
  DS_EXEC_TXN_FAILED = 2,
} execute_outcome_t;

static char *
__noit__strndup(const char *src, int len) {
  char *dst;
  dst = malloc(len + 1);
  strlcpy(dst, src, len+1);
  return dst;
}
#define DECLARE_PARAM_STR(str, len) do { \
  d->paramValues[d->nparams] = __noit__strndup(str, len); \
  d->paramLengths[d->nparams] = len; \
  d->paramFormats[d->nparams] = 0; \
  d->paramAllocd[d->nparams] = 1; \
  if(!strcmp(d->paramValues[d->nparams], "[[null]]")) { \
    free(d->paramValues[d->nparams]); \
    d->paramValues[d->nparams] = NULL; \
    d->paramLengths[d->nparams] = 0; \
    d->paramAllocd[d->nparams] = 0; \
  } \
  d->nparams++; \
} while(0)

execute_outcome_t
stratcon_datastore_execute(conn_q *cq, struct sockaddr *r, ds_job_detail *d) {
  int type, len;
  char *token;

  type = d->data[0];

  /* Parse the log line, but only if we haven't already */
  if(!d->nparams) {
    char raddr[128];
    char *scp, *ecp;

    /* setup our remote address */
    raddr[0] = '\0';
    switch(r->sa_family) {
      case AF_INET:
        inet_ntop(AF_INET, &(((struct sockaddr_in *)r)->sin_addr),
                  raddr, sizeof(raddr));
        break;
      case AF_INET6:
        inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)r)->sin6_addr),
                  raddr, sizeof(raddr));
        break;
    }
 
    scp = d->data;
#define PROCESS_NEXT_FIELD(t,l) do { \
  if(!*scp) goto bad_row; \
  ecp = strchr(scp, '\t'); \
  if(!ecp) goto bad_row; \
  token = scp; \
  len = (ecp-scp); \
  scp = ecp + 1; \
} while(0)
#define PROCESS_LAST_FIELD(t,l) do { \
  if(!*scp) ecp = scp; \
  else { \
    ecp = scp + strlen(scp); /* Puts us at the '\0' */ \
    if(*(ecp-1) == '\n') ecp--; /* We back up on letter if we ended in \n */ \
  } \
  t = scp; \
  l = (ecp-scp); \
} while(0)

    PROCESS_NEXT_FIELD(token,len); /* Skip the leader, we know what we are */
    switch(type) {
      /* See noit_check_log.c for log description */
      case 'C':
        DECLARE_PARAM_STR(raddr, strlen(raddr));
        PROCESS_NEXT_FIELD(token,len);
        DECLARE_PARAM_STR(token,len); /* timestamp */
        PROCESS_NEXT_FIELD(token, len);
        DECLARE_PARAM_STR(token,len); /* uuid */
        PROCESS_NEXT_FIELD(token, len);
        DECLARE_PARAM_STR(token,len); /* target */
        PROCESS_NEXT_FIELD(token, len);
        DECLARE_PARAM_STR(token,len); /* module */
        PROCESS_LAST_FIELD(token, len);
        DECLARE_PARAM_STR(token,len); /* name */
        break;
      case 'M':
        PROCESS_NEXT_FIELD(token,len);
        DECLARE_PARAM_STR(token,len); /* timestamp */
        PROCESS_NEXT_FIELD(token, len);
        DECLARE_PARAM_STR(token,len); /* uuid */
        PROCESS_NEXT_FIELD(token, len);
        DECLARE_PARAM_STR(token,len); /* name */
        PROCESS_NEXT_FIELD(token,len);
        d->metric_type = *token;
        PROCESS_LAST_FIELD(token,len);
        DECLARE_PARAM_STR(token,len); /* value */
        break;
      case 'S':
        PROCESS_NEXT_FIELD(token,len);
        DECLARE_PARAM_STR(token,len); /* timestamp */
        PROCESS_NEXT_FIELD(token, len);
        DECLARE_PARAM_STR(token,len); /* uuid */
        PROCESS_NEXT_FIELD(token, len);
        DECLARE_PARAM_STR(token,len); /* state */
        PROCESS_NEXT_FIELD(token, len);
        DECLARE_PARAM_STR(token,len); /* availability */
        PROCESS_NEXT_FIELD(token, len);
        DECLARE_PARAM_STR(token,len); /* duration */
        PROCESS_LAST_FIELD(token,len);
        DECLARE_PARAM_STR(token,len); /* status */
        break;
      default:
        goto bad_row;
    }

  }

#define PG_EXEC(cmd) do { \
  PGresult *res; \
  int rv; \
  res = PQexecParams(cq->dbh, cmd, d->nparams, NULL, \
                     (const char * const *)d->paramValues, \
                     d->paramLengths, d->paramFormats, 0); \
  rv = PQresultStatus(res); \
  if(rv != PGRES_COMMAND_OK) { \
    noitL(noit_error, "stratcon datasource bad row: %s\n", \
          PQresultErrorMessage(res)); \
    PQclear(res); \
    goto bad_row; \
  } \
  PQclear(res); \
} while(0)

  /* Now execute the query */
  switch(type) {
    case 'C':
      GET_QUERY(check_insert);
      PG_EXEC(check_insert);
      break;
    case 'S':
      GET_QUERY(status_insert);
      PG_EXEC(status_insert);
      break;
    case 'M':
      switch(d->metric_type) {
        case METRIC_INT32:
        case METRIC_UINT32:
        case METRIC_INT64:
        case METRIC_UINT64:
        case METRIC_DOUBLE:
          GET_QUERY(metric_insert_numeric);
          PG_EXEC(metric_insert_numeric);
          break;
        case METRIC_STRING:
          GET_QUERY(metric_insert_text);
          PG_EXEC(metric_insert_text);
          break;
        default:
          goto bad_row;
      }
      break;
    default:
      /* should never get here */
      goto bad_row;
  }
  return DS_EXEC_SUCCESS;
 bad_row:
  return DS_EXEC_ROW_FAILED;
}
static int
stratcon_database_connect(conn_q *cq) {
  char dsn[512];
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *k, *v;
  int klen;
  noit_hash_table *t;

  if(cq->dbh) {
    if(PQstatus(cq->dbh) == CONNECTION_OK) return 0;
    PQreset(cq->dbh);
    if(PQstatus(cq->dbh) == CONNECTION_OK) return 0;
    noitL(noit_error, "Error reconnecting to database: '%s'\nError: %s\n",
          dsn, PQerrorMessage(cq->dbh));
    return -1;
  }

  dsn[0] = '\0';
  t = noit_conf_get_hash(NULL, "/stratcon/database/dbconfig");
  while(noit_hash_next(t, &iter, &k, &klen, (void **)&v)) {
    if(dsn[0]) strlcat(dsn, " ", sizeof(dsn));
    strlcat(dsn, k, sizeof(dsn));
    strlcat(dsn, "=", sizeof(dsn));
    strlcat(dsn, v, sizeof(dsn));
  }
  cq->dbh = PQconnectdb(dsn);
  if(!cq->dbh) return -1;
  if(PQstatus(cq->dbh) == CONNECTION_OK) return 0;
  noitL(noit_error, "Error connection to database: '%s'\nError: %s\n",
        dsn, PQerrorMessage(cq->dbh));
  return -1;
}
static int
stratcon_datastore_savepoint_op(conn_q *cq, const char *p,
                                const char *name) {
  int rv = -1;
  PGresult *res;
  char cmd[128];
  strlcpy(cmd, p, sizeof(cmd));
  strlcat(cmd, name, sizeof(cmd));
  if((res = PQexec(cq->dbh, cmd)) == NULL) return -1;
  if(PQresultStatus(res) == PGRES_COMMAND_OK) rv = 0;
  PQclear(res);
  return rv;
}
static int
stratcon_datastore_do(conn_q *cq, const char *cmd) {
  PGresult *res;
  int rv = -1;
  if((res = PQexec(cq->dbh, cmd)) == NULL) return -1;
  if(PQresultStatus(res) == PGRES_COMMAND_OK) rv = 0;
  PQclear(res);
  return rv;
}
#define BUSTED(cq) do { \
  PQfinish((cq)->dbh); \
  (cq)->dbh = NULL; \
  goto full_monty; \
} while(0)
#define SAVEPOINT(name) do { \
  if(stratcon_datastore_savepoint_op(cq, "SAVEPOINT ", name)) BUSTED(cq); \
  last_sp = current; \
} while(0)
#define ROLLBACK_TO_SAVEPOINT(name) do { \
  if(stratcon_datastore_savepoint_op(cq, "ROLLBACK TO SAVEPOINT ", name)) \
    BUSTED(cq); \
  last_sp = NULL; \
} while(0)
#define RELEASE_SAVEPOINT(name) do { \
  if(stratcon_datastore_savepoint_op(cq, "RELEASE SAVEPOINT ", name)) \
    BUSTED(cq); \
  last_sp = NULL; \
} while(0)
int
stratcon_datastore_asynch_execute(eventer_t e, int mask, void *closure,
                                  struct timeval *now) {
  conn_q *cq = closure;
  ds_job_detail *current, *last_sp;
  if(!(mask & EVENTER_ASYNCH_WORK)) return 0;

  if(!cq->head) return 0; 

 full_monty:
  /* Make sure we have a connection */
  while(stratcon_database_connect(cq)) {
    noitL(noit_error, "Error connecting to database\n");
    sleep(1);
  }

  current = cq->head; 
  last_sp = NULL;
  if(stratcon_datastore_do(cq, "BEGIN")) BUSTED(cq);
  while(current) {
    execute_outcome_t rv;
    if(current->data) {
      if(!last_sp) SAVEPOINT("batch");
 
      if(current->problematic) {
        noitL(noit_error, "Failed noit line: %s", current->data);
        RELEASE_SAVEPOINT("batch");
        current = current->next;
        continue;
      } 
      rv = stratcon_datastore_execute(cq, cq->remote, current);
      switch(rv) {
        case DS_EXEC_SUCCESS:
          current = current->next;
          break;
        case DS_EXEC_ROW_FAILED:
          /* rollback to savepoint, mark this record as bad and start again */
          current->problematic = 1;
          current = last_sp;
          ROLLBACK_TO_SAVEPOINT("batch");
          break;
        case DS_EXEC_TXN_FAILED:
          BUSTED(cq);
      }
    }
    if(current->completion_event) {
      if(last_sp) RELEASE_SAVEPOINT("batch");
      if(stratcon_datastore_do(cq, "COMMIT")) BUSTED(cq);
      eventer_add(current->completion_event);
      __remove_until(cq, current->next);
      current = current->next;
    }
  }
  return 0;
}
void
stratcon_datastore_push(stratcon_datastore_op_t op,
                        struct sockaddr *remote, void *operand) {
  conn_q *cq;
  eventer_t e;
  ds_job_detail *dsjd;

  cq = __get_conn_q_for_remote(remote);
  dsjd = calloc(1, sizeof(*dsjd));
  switch(op) {
    case DS_OP_INSERT:
      dsjd->data = operand;
      __append(cq, dsjd);
      break;
    case DS_OP_CHKPT:
      dsjd->completion_event = operand;
      __append(cq,dsjd);
      e = eventer_alloc();
      e->mask = EVENTER_ASYNCH;
      e->callback = stratcon_datastore_asynch_execute;
      e->closure = cq;
      eventer_add(e);
      break;
  }
}
