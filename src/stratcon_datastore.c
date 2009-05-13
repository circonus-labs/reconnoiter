/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "utils/noit_b64.h"
#include "stratcon_datastore.h"
#include "stratcon_realtime_http.h"
#include "stratcon_iep.h"
#include "noit_conf.h"
#include "noit_check.h"
#include <unistd.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <libpq-fe.h>
#include <zlib.h>

static char *check_loadall = NULL;
static const char *check_loadall_conf = "/stratcon/database/statements/allchecks";
static char *check_find = NULL;
static const char *check_find_conf = "/stratcon/database/statements/findcheck";
static char *check_insert = NULL;
static const char *check_insert_conf = "/stratcon/database/statements/check";
static char *status_insert = NULL;
static const char *status_insert_conf = "/stratcon/database/statements/status";
static char *metric_insert_numeric = NULL;
static const char *metric_insert_numeric_conf = "/stratcon/database/statements/metric_numeric";
static char *metric_insert_text = NULL;
static const char *metric_insert_text_conf = "/stratcon/database/statements/metric_text";
static char *config_insert = NULL;
static const char *config_insert_conf = "/stratcon/database/statements/config";

static struct datastore_onlooker_list {
  void (*dispatch)(stratcon_datastore_op_t, struct sockaddr *, void *);
  struct datastore_onlooker_list *next;
} *onlookers = NULL;

#define GET_QUERY(a) do { \
  if(a == NULL) \
    if(!noit_conf_get_string(NULL, a ## _conf, &(a))) \
      goto bad_row; \
} while(0)

#define MAX_PARAMS 8
#define POSTGRES_PARTS \
  PGresult *res; \
  int rv; \
  int nparams; \
  int metric_type; \
  char *paramValues[MAX_PARAMS]; \
  int paramLengths[MAX_PARAMS]; \
  int paramFormats[MAX_PARAMS]; \
  int paramAllocd[MAX_PARAMS];

typedef struct ds_single_detail {
  POSTGRES_PARTS
} ds_single_detail;
typedef struct ds_job_detail {
  /* Postgres specific stuff */
  POSTGRES_PARTS

  char *data;  /* The raw string, NULL means the stream is done -- commit. */
  struct realtime_tracker *rt;

  int problematic;
  eventer_t completion_event; /* This event should be registered if non NULL */
  struct ds_job_detail *next;
} ds_job_detail;

typedef struct {
  struct sockaddr *remote;
  eventer_jobq_t  *jobq;
  /* Postgres specific stuff */
  PGconn          *dbh;
  ds_job_detail   *head;
  ds_job_detail   *tail;
} conn_q;

static int stratcon_database_connect(conn_q *cq);

static void
free_params(ds_single_detail *d) {
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
    free_params((ds_single_detail *)next);
    if(next->data) free(next->data);
    free(next);
  }
  if(!q->head) q->tail = NULL;
}

noit_hash_table ds_conns;

conn_q *
__get_conn_q_for_remote(struct sockaddr *remote) {
  void *vcq;
  conn_q *cq;
  char queue_name[128] = "datastore_";
  static const char __zeros[4] = { 0 };
  int len = 0;
  if(remote) {
    switch(remote->sa_family) {
      case AF_INET:
        len = sizeof(struct sockaddr_in);
        inet_ntop(remote->sa_family, &((struct sockaddr_in *)remote)->sin_addr,
                  queue_name + strlen("datastore_"), len);
        break;
      case AF_INET6:
       len = sizeof(struct sockaddr_in6);
        inet_ntop(remote->sa_family, &((struct sockaddr_in6 *)remote)->sin6_addr,
                  queue_name + strlen("datastore_"), len);
       break;
      case AF_UNIX:
        len = SUN_LEN(((struct sockaddr_un *)remote));
        snprintf(queue_name, sizeof(queue_name), "datastore_%s", ((struct sockaddr_un *)remote)->sun_path);
        break;
      default: return NULL;
    }
  }
  else {
    /* This is a dummy connection */
    remote = (struct sockaddr *)__zeros;
    snprintf(queue_name, sizeof(queue_name), "datastore_default");
    len = 4;
  }
  if(noit_hash_retrieve(&ds_conns, (const char *)remote, len, &vcq))
    return vcq;
  cq = calloc(1, sizeof(*cq));
  cq->remote = malloc(len);
  memcpy(cq->remote, remote, len);
  cq->jobq = calloc(1, sizeof(*cq->jobq));
  eventer_jobq_init(cq->jobq, queue_name);
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
  int slen;
  char *dst;
  for(slen = 0; slen < len; slen++)
    if(src[slen] == '\0') break;
  dst = malloc(slen + 1);
  memcpy(dst, src, slen);
  dst[slen] = '\0';
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
#define DECLARE_PARAM_INT(i) do { \
  int buffer__len; \
  char buffer__[32]; \
  snprintf(buffer__, sizeof(buffer__), "%d", (i)); \
  buffer__len = strlen(buffer__); \
  DECLARE_PARAM_STR(buffer__, buffer__len); \
} while(0)

#define PG_GET_STR_COL(dest, row, name) do { \
  int colnum = PQfnumber(d->res, name); \
  dest = NULL; \
  if (colnum >= 0) \
    dest = PQgetisnull(d->res, row, colnum) \
         ? NULL : PQgetvalue(d->res, row, colnum); \
} while(0)

#define PG_EXEC(cmd) do { \
  d->res = PQexecParams(cq->dbh, cmd, d->nparams, NULL, \
                        (const char * const *)d->paramValues, \
                        d->paramLengths, d->paramFormats, 0); \
  d->rv = PQresultStatus(d->res); \
  if(d->rv != PGRES_COMMAND_OK && \
     d->rv != PGRES_TUPLES_OK) { \
    noitL(noit_error, "stratcon datasource bad (%d): %s\n", \
          d->rv, PQresultErrorMessage(d->res)); \
    PQclear(d->res); \
    goto bad_row; \
  } \
} while(0)

static int
stratcon_datastore_asynch_drive_iep(eventer_t e, int mask, void *closure,
                                    struct timeval *now) {
  conn_q *cq = closure;
  ds_job_detail *d;
  int i, row_count = 0, good = 0;
  char buff[1024];

  if(!(mask & EVENTER_ASYNCH_WORK)) return 0;
  if(mask & EVENTER_ASYNCH_CLEANUP) return 0;

  stratcon_database_connect(cq);
  d = calloc(1, sizeof(*d));
  GET_QUERY(check_loadall);
  PG_EXEC(check_loadall);
  row_count = PQntuples(d->res);
  
  for(i=0; i<row_count; i++) {
    char *remote, *id, *target, *module, *name;
    PG_GET_STR_COL(remote, i, "remote_address");
    PG_GET_STR_COL(id, i, "id");
    PG_GET_STR_COL(target, i, "target");
    PG_GET_STR_COL(module, i, "module");
    PG_GET_STR_COL(name, i, "name");
    snprintf(buff, sizeof(buff), "C\t0.000\t%s\t%s\t%s\t%s\n", id, target, module, name);
    stratcon_iep_line_processor(DS_OP_INSERT, NULL, buff);
    good++;
  }
  noitL(noit_error, "Staged %d/%d remembered checks into IEP\n", good, row_count);
 bad_row:
  PQclear(d->res);
  return 0;
}
void
stratcon_datastore_iep_check_preload() {
  eventer_t e;
  conn_q *cq;
  cq = __get_conn_q_for_remote(NULL);

  e = eventer_alloc();
  e->mask = EVENTER_ASYNCH;
  e->callback = stratcon_datastore_asynch_drive_iep;
  e->closure = cq;
  eventer_add_asynch(cq->jobq, e);
}
execute_outcome_t
stratcon_datastore_find(conn_q *cq, ds_job_detail *d) {
  char *val;
  int row_count;

  if(!d->nparams) DECLARE_PARAM_INT(d->rt->sid);
  GET_QUERY(check_find);
  PG_EXEC(check_find);
  row_count = PQntuples(d->res);
  if(row_count != 1) goto bad_row;

  /* Get the check uuid */
  PG_GET_STR_COL(val, 0, "id");
  if(!val) goto bad_row;
  if(uuid_parse(val, d->rt->checkid)) goto bad_row;

  /* Get the remote_address (which noit owns this) */
  PG_GET_STR_COL(val, 0, "remote_address");
  if(!val) goto bad_row;
  d->rt->noit = strdup(val);

  PQclear(d->res);
  return DS_EXEC_SUCCESS;
 bad_row:
  return DS_EXEC_ROW_FAILED;
}
execute_outcome_t
stratcon_datastore_execute(conn_q *cq, struct sockaddr *r, ds_job_detail *d) {
  int type, len;
  char *final_buff;
  uLong final_len, actual_final_len;;
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
      default:
        noitL(noit_error, "remote address of family %d\n", r->sa_family);
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
      case 'n':
        DECLARE_PARAM_STR(raddr, strlen(raddr));
        DECLARE_PARAM_STR("noitd",5); /* node_type */
        PROCESS_NEXT_FIELD(token,len);
        DECLARE_PARAM_STR(token,len); /* timestamp */

        /* This is the expected uncompressed len */
        PROCESS_NEXT_FIELD(token,len);
        final_len = atoi(token);
        final_buff = malloc(final_len);
        if(!final_buff) goto bad_row;
  
        /* The last token is b64 endoded and compressed.
         * we need to decode it, declare it and then free it.
         */
        PROCESS_LAST_FIELD(token, len);
        /* We can in-place decode this */
        len = noit_b64_decode((char *)token, len,
                              (unsigned char *)token, len);
        if(len <= 0) {
          noitL(noit_error, "noitd config base64 decoding error.\n");
          free(final_buff);
          goto bad_row;
        }
        actual_final_len = final_len;
        if(Z_OK != uncompress((Bytef *)final_buff, &actual_final_len,
                              (unsigned char *)token, len)) {
          noitL(noit_error, "noitd config decompression failure.\n");
          free(final_buff);
          goto bad_row;
        }
        if(final_len != actual_final_len) {
          noitL(noit_error, "noitd config decompression error.\n");
          free(final_buff);
          goto bad_row;
        }
        DECLARE_PARAM_STR(final_buff, final_len);
        free(final_buff);
        break;
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

  /* Now execute the query */
  switch(type) {
    case 'n':
      GET_QUERY(config_insert);
      PG_EXEC(config_insert);
      PQclear(d->res);
      break;
    case 'C':
      GET_QUERY(check_insert);
      PG_EXEC(check_insert);
      PQclear(d->res);
      break;
    case 'S':
      GET_QUERY(status_insert);
      PG_EXEC(status_insert);
      PQclear(d->res);
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
          PQclear(d->res);
          break;
        case METRIC_STRING:
          GET_QUERY(metric_insert_text);
          PG_EXEC(metric_insert_text);
          PQclear(d->res);
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

  dsn[0] = '\0';
  t = noit_conf_get_hash(NULL, "/stratcon/database/dbconfig");
  while(noit_hash_next_str(t, &iter, &k, &klen, &v)) {
    if(dsn[0]) strlcat(dsn, " ", sizeof(dsn));
    strlcat(dsn, k, sizeof(dsn));
    strlcat(dsn, "=", sizeof(dsn));
    strlcat(dsn, v, sizeof(dsn));
  }

  if(cq->dbh) {
    if(PQstatus(cq->dbh) == CONNECTION_OK) return 0;
    PQreset(cq->dbh);
    if(PQstatus(cq->dbh) == CONNECTION_OK) return 0;
    noitL(noit_error, "Error reconnecting to database: '%s'\nError: %s\n",
          dsn, PQerrorMessage(cq->dbh));
    return -1;
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
stratcon_datastore_asynch_lookup(eventer_t e, int mask, void *closure,
                                 struct timeval *now) {
  conn_q *cq = closure;
  ds_job_detail *current, *next;
  if(!(mask & EVENTER_ASYNCH_WORK)) return 0;

  if(!cq->head) return 0; 

  stratcon_database_connect(cq);

  current = cq->head; 
  while(current) {
    if(current->rt) {
      next = current->next;
      stratcon_datastore_find(cq, current);
      current = next;
    }
    else if(current->completion_event) {
      next = current->next;
      eventer_add(current->completion_event);
      current = next;
      __remove_until(cq, current);
    }
    else current = current->next;
  }
  return 0;
}
int
stratcon_datastore_asynch_execute(eventer_t e, int mask, void *closure,
                                  struct timeval *now) {
  int i;
  conn_q *cq = closure;
  ds_job_detail *current, *last_sp;
  if(!(mask & EVENTER_ASYNCH_WORK)) return 0;
  if(mask & EVENTER_ASYNCH_CLEANUP) return 0;
  if(!cq->head) return 0; 

 full_monty:
  /* Make sure we have a connection */
  i = 1;
  while(stratcon_database_connect(cq)) {
    noitL(noit_error, "Error connecting to database\n");
    sleep(i);
    i *= 2;
    i = MIN(i, 16);
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
      current = current->next;
      __remove_until(cq, current);
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
  struct datastore_onlooker_list *nnode;

  for(nnode = onlookers; nnode; nnode = nnode->next)
    nnode->dispatch(op,remote,operand);

  cq = __get_conn_q_for_remote(remote);
  dsjd = calloc(1, sizeof(*dsjd));
  switch(op) {
    case DS_OP_FIND:
      dsjd->rt = operand;
      __append(cq, dsjd);
      break;
    case DS_OP_INSERT:
      dsjd->data = operand;
      __append(cq, dsjd);
      break;
    case DS_OP_FIND_COMPLETE:
    case DS_OP_CHKPT:
      dsjd->completion_event = operand;
      __append(cq,dsjd);
      e = eventer_alloc();
      e->mask = EVENTER_ASYNCH;
      if(op == DS_OP_FIND_COMPLETE)
        e->callback = stratcon_datastore_asynch_lookup;
      else if(op == DS_OP_CHKPT)
        e->callback = stratcon_datastore_asynch_execute;
      e->closure = cq;
      eventer_add_asynch(cq->jobq, e);
      break;
  }
}

int
stratcon_datastore_saveconfig(void *unused) {
  int rv = -1;
  conn_q _cq = { 0 }, *cq = &_cq;
  char *buff;
  ds_single_detail _d = { 0 }, *d = &_d;

  if(stratcon_database_connect(cq) == 0) {
    char time_as_str[20];
    size_t len;
    buff = noit_conf_xml_in_mem(&len);
    if(!buff) goto bad_row;

    snprintf(time_as_str, sizeof(time_as_str), "%lu", (long unsigned int)time(NULL));
    DECLARE_PARAM_STR("0.0.0.0", 7);
    DECLARE_PARAM_STR("stratcond", 9);
    DECLARE_PARAM_STR(time_as_str, strlen(time_as_str));
    DECLARE_PARAM_STR(buff, len);
    free(buff);

    GET_QUERY(config_insert);
    PG_EXEC(config_insert);
    PQclear(d->res);
    rv = 0;

    bad_row:
      free_params(d);
  }
  if(cq->dbh) PQfinish(cq->dbh);
  return rv;
}

void
stratcon_datastore_register_onlooker(void (*f)(stratcon_datastore_op_t,
                                               struct sockaddr *, void *)) {
  struct datastore_onlooker_list *nnode;
  nnode = calloc(1, sizeof(*nnode));
  nnode->dispatch = f;
  nnode->next = onlookers;
  while(noit_atomic_casptr((void **)&onlookers, nnode, nnode->next) != (void *)nnode->next)
    nnode->next = onlookers;
}
