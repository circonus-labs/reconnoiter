/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "stratcon_datastore.h"
#include "noit_conf.h"
#include <unistd.h>
#include <libpq-fe.h>

typedef struct ds_job_detail {
  char *data;  /* The raw string, NULL means the stream is done -- commit. */
  int problematic;
  eventer_t completion_event; /* This event should be registered if non NULL */
  struct ds_job_detail *next;
} ds_job_detail;

typedef struct {
  struct sockaddr *remote;
  eventer_jobq_t  *jobq;
  PGconn          *dbh;
  ds_job_detail   *head;
  ds_job_detail   *tail;
} conn_q;

void __append(conn_q *q, ds_job_detail *d) {
  d->next = NULL;
  if(!q->head) q->head = q->tail = d;
  else {
    q->tail->next = d;
    q->tail = d;
  }
}
void __remove_until(conn_q *q, ds_job_detail *d) {
  ds_job_detail *next;
  while(q->head && q->head != d) {
    next = q->head;
    q->head = q->head->next;
    if(next->data) free(next->data);
    free(next);
  }
  if(!q->head) q->tail = NULL;
}

noit_hash_table ds_conns;

conn_q *
__get_conn_q_for_remote(struct sockaddr *remote) {
  conn_q *cq;
  if(noit_hash_retrieve(&ds_conns, (const char *)remote, remote->sa_len,
                        (void **)&cq))
    return cq;
  cq = calloc(1, sizeof(*cq));
  cq->remote = malloc(remote->sa_len);
  memcpy(cq->remote, remote, remote->sa_len);
  cq->jobq = calloc(1, sizeof(*cq->jobq));
  eventer_jobq_init(cq->jobq);
  cq->jobq->backq = eventer_default_backq();
  /* Add one thread */
  eventer_jobq_increase_concurrency(cq->jobq);
  noit_hash_store(&ds_conns, (const char *)cq->remote, cq->remote->sa_len, cq);
  return cq;
}

typedef enum {
  DS_EXEC_SUCCESS = 0,
  DS_EXEC_ROW_FAILED = 1,
  DS_EXEC_TXN_FAILED = 2,
} execute_outcome_t;

execute_outcome_t
stratcon_datastore_execute(conn_q *cq, struct sockaddr *r, const char *data) {
  
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
  t = noit_conf_get_hash(NULL, "/stratcon/noits/dbconfig");
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
  int rv;
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
      rv = stratcon_datastore_execute(cq, cq->remote, current->data);
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
