/*
 * Copyright (c) 2007-2009, OmniTI Computer Consulting, Inc.
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
#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "utils/noit_b64.h"
#include "utils/noit_str.h"
#include "utils/noit_mkdir.h"
#include "stratcon_datastore.h"
#include "stratcon_realtime_http.h"
#include "stratcon_iep.h"
#include "noit_conf.h"
#include "noit_check.h"
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <libpq-fe.h>
#include <zlib.h>
#include <assert.h>
#include <errno.h>

#define DECL_STMT(codename,confname) \
static char *codename = NULL; \
static const char *codename##_conf = "/stratcon/database/statements/" #confname

DECL_STMT(storage_post_connect, storagepostconnect);
DECL_STMT(metanode_post_connect, metanodepostconnect);
DECL_STMT(find_storage, findstoragenode);
DECL_STMT(all_storage, allstoragenodes);
DECL_STMT(check_map, mapchecktostoragenode);
DECL_STMT(check_mapall, mapallchecks);
DECL_STMT(check_loadall, allchecks);
DECL_STMT(check_find, findcheck);
DECL_STMT(check_insert, check);
DECL_STMT(status_insert, status);
DECL_STMT(metric_insert_numeric, metric_numeric);
DECL_STMT(metric_insert_text, metric_text);
DECL_STMT(config_insert, config);

static noit_log_stream_t ds_err = NULL;
static noit_log_stream_t ingest_err = NULL;

static struct datastore_onlooker_list {
  void (*dispatch)(stratcon_datastore_op_t, struct sockaddr *,
                   const char *, void *);
  struct datastore_onlooker_list *next;
} *onlookers = NULL;

#define GET_QUERY(a) do { \
  if(a == NULL) \
    if(!noit_conf_get_string(NULL, a ## _conf, &(a))) \
      goto bad_row; \
} while(0)

struct conn_q;

typedef struct {
  char            *queue_name; /* the key fqdn+remote_sn */
  eventer_jobq_t  *jobq;
  struct conn_q   *head;
  pthread_mutex_t  lock;
  pthread_cond_t   cv;
  int              ttl;
  int              in_pool;
  int              outstanding;
  int              max_allocated;
  int              max_in_pool;
} conn_pool;
typedef struct conn_q {
  time_t           last_use;
  char            *dsn;        /* Pg connect string */
  char            *remote_str; /* the IP of the noit*/
  char            *remote_cn;  /* the Cert CN of the noit */
  char            *fqdn;       /* the fqdn of the storage node */
  conn_pool       *pool;
  struct conn_q   *next;
  /* Postgres specific stuff */
  PGconn          *dbh;
} conn_q;


#define MAX_PARAMS 8
#define POSTGRES_PARTS \
  PGresult *res; \
  int rv; \
  time_t whence; \
  int nparams; \
  int metric_type; \
  char *paramValues[MAX_PARAMS]; \
  int paramLengths[MAX_PARAMS]; \
  int paramFormats[MAX_PARAMS]; \
  int paramAllocd[MAX_PARAMS];

typedef struct ds_single_detail {
  POSTGRES_PARTS
} ds_single_detail;
typedef struct {
  /* Postgres specific stuff */
  POSTGRES_PARTS
  struct realtime_tracker *rt;
  conn_q *cq; /* connection on which to perform this job */
  eventer_t completion_event; /* This event should be registered if non NULL */
} ds_rt_detail;
typedef struct ds_line_detail {
  /* Postgres specific stuff */
  POSTGRES_PARTS
  char *data;
  int problematic;
  struct ds_line_detail *next;
} ds_line_detail;

typedef struct {
  noit_hash_table *ws;
  eventer_t completion;
} syncset_t;

typedef struct {
  char *remote_str;
  char *remote_cn;
  char *fqdn;
  int storagenode_id;
  int fd;
  char *filename;
  conn_pool *cpool;
} interim_journal_t;

static int stratcon_database_connect(conn_q *cq);
static int uuid_to_sid(const char *uuid_str_in, const char *remote_cn);

static void
free_params(ds_single_detail *d) {
  int i;
  for(i=0; i<d->nparams; i++)
    if(d->paramAllocd[i] && d->paramValues[i])
      free(d->paramValues[i]);
}

char *basejpath = NULL;
pthread_mutex_t ds_conns_lock;
noit_hash_table ds_conns;
noit_hash_table working_sets;

/* the fqdn cache needs to be thread safe */
typedef struct {
  char *uuid_str;
  int storagenode_id;
  int sid;
} uuid_info;
typedef struct {
  int storagenode_id;
  char *fqdn;
  char *dsn;
} storagenode_info;
noit_hash_table uuid_to_info_cache;
pthread_mutex_t storagenode_to_info_cache_lock;
noit_hash_table storagenode_to_info_cache;

int
convert_sockaddr_to_buff(char *buff, int blen, struct sockaddr *remote) {
  char name[128] = "";
  buff[0] = '\0';
  if(remote) {
    int len = 0;
    switch(remote->sa_family) {
      case AF_INET:
        len = sizeof(struct sockaddr_in);
        inet_ntop(remote->sa_family, &((struct sockaddr_in *)remote)->sin_addr,
                  name, len);
        break;
      case AF_INET6:
       len = sizeof(struct sockaddr_in6);
        inet_ntop(remote->sa_family, &((struct sockaddr_in6 *)remote)->sin6_addr,
                  name, len);
       break;
      case AF_UNIX:
        len = SUN_LEN(((struct sockaddr_un *)remote));
        snprintf(name, sizeof(name), "%s", ((struct sockaddr_un *)remote)->sun_path);
        break;
      default: return 0;
    }
  }
  strlcpy(buff, name, blen);
  return strlen(buff);
}

/* Thread-safe connection pools */

/* Forcefree -> 1 prevents it from going to the pool and it gets freed */
static void
release_conn_q_forceable(conn_q *cq, int forcefree) {
  int putback = 0;
  cq->last_use = time(NULL);
  pthread_mutex_lock(&cq->pool->lock);
  cq->pool->outstanding--;
  if(!forcefree && (cq->pool->in_pool < cq->pool->max_in_pool)) {
    putback = 1;
    cq->next = cq->pool->head;
    cq->pool->head = cq;
    cq->pool->in_pool++;
  }
  pthread_mutex_unlock(&cq->pool->lock);
  noitL(noit_debug, "[%p] release %s [%s]\n", (void *)pthread_self(),
        putback ? "to pool" : "and destroy", cq->pool->queue_name);
  pthread_cond_signal(&cq->pool->cv);
  if(putback) return;

  /* Not put back, release it */
  if(cq->dbh) PQfinish(cq->dbh);
  if(cq->remote_str) free(cq->remote_str);
  if(cq->remote_cn) free(cq->remote_cn);
  if(cq->fqdn) free(cq->fqdn);
  if(cq->dsn) free(cq->dsn);
  free(cq);
}
static void
ttl_purge_conn_pool(conn_pool *pool) {
  int old_cnt, new_cnt;
  time_t now = time(NULL);
  conn_q *cq, *prev = NULL, *iter;
  /* because we always replace on the head and update the last_use time when
     doing so, we know they are ordered LRU on the end.  So, once we hit an
     old one, we know all the others are old too.
   */
  if(!pool->head) return; /* hack short circuit for no locks */
  pthread_mutex_lock(&pool->lock);
  old_cnt = pool->in_pool;
  cq = pool->head;
  while(cq) {
    if(cq->last_use + cq->pool->ttl < now) {
      if(prev) prev->next = NULL;
      else pool->head = NULL;
      break;
    }
    prev = cq;
    cq = cq->next;
  }
  /* Now pool->head is a chain of unexpired and cq is a chain of expired */
  /* Fix accounting */
  for(iter=cq; iter; iter=iter->next) pool->in_pool--;
  new_cnt = pool->in_pool;
  pthread_mutex_unlock(&pool->lock);

  /* Force release these without holding the lock */
  while(cq) {
    prev = cq;
    cq = cq->next;
    release_conn_q_forceable(cq, 1);
  }
  if(old_cnt != new_cnt)
    noitL(noit_debug, "reduced db pool %d -> %d [%s]\n", old_cnt, new_cnt,
          pool->queue_name);
}
static void
release_conn_q(conn_q *cq) {
  ttl_purge_conn_pool(cq->pool);
  release_conn_q_forceable(cq, 0);
}
static conn_pool *
get_conn_pool_for_remote(const char *remote_str,
                         const char *remote_cn, const char *fqdn) {
  void *vcpool;
  conn_pool *cpool = NULL;
  char queue_name[256] = "datastore_";
  snprintf(queue_name, sizeof(queue_name), "datastore_%s_%s_%s",
           (remote_str && *remote_str) ? remote_str : "0.0.0.0",
           fqdn ? fqdn : "default",
           remote_cn ? remote_cn : "default");
  pthread_mutex_lock(&ds_conns_lock);
  if(noit_hash_retrieve(&ds_conns, (const char *)queue_name,
                        strlen(queue_name), &vcpool))
    cpool = vcpool;
  pthread_mutex_unlock(&ds_conns_lock);
  if(!cpool) {
    vcpool = cpool = calloc(1, sizeof(*cpool));
    cpool->queue_name = strdup(queue_name);
    pthread_mutex_init(&cpool->lock, NULL);
    pthread_cond_init(&cpool->cv, NULL);
    cpool->in_pool = 0;
    cpool->outstanding = 0;
    cpool->max_in_pool = 1;
    cpool->max_allocated = 1;
    pthread_mutex_lock(&ds_conns_lock);
    if(!noit_hash_store(&ds_conns, cpool->queue_name, strlen(cpool->queue_name),
                        cpool)) {
      noit_hash_retrieve(&ds_conns, (const char *)queue_name,
                         strlen(queue_name), &vcpool);
    }
    pthread_mutex_unlock(&ds_conns_lock);
    if(vcpool != cpool) {
      /* someone beat us to it */
      free(cpool->queue_name);
      pthread_mutex_destroy(&cpool->lock);
      pthread_cond_destroy(&cpool->cv);
      free(cpool);
    }
    else {
      int i;
      /* Our job to setup the pool */
      cpool->jobq = calloc(1, sizeof(*cpool->jobq));
      eventer_jobq_init(cpool->jobq, queue_name);
      cpool->jobq->backq = eventer_default_backq();
      /* Add one thread */
      for(i=0; i<MAX(cpool->max_allocated - cpool->max_in_pool, 1); i++)
        eventer_jobq_increase_concurrency(cpool->jobq);
    }
    cpool = vcpool;
  }
  return cpool;
}
static conn_q *
get_conn_q_for_remote(const char *remote_str,
                      const char *remote_cn, const char *fqdn,
                      const char *dsn) {
  conn_pool *cpool;
  conn_q *cq;
  cpool = get_conn_pool_for_remote(remote_str, remote_cn, fqdn);
 again:
  noitL(noit_debug, "[%p] requesting [%s]\n", (void *)pthread_self(),
        cpool->queue_name);
  pthread_mutex_lock(&cpool->lock);
  if(cpool->head) {
    assert(cpool->in_pool > 0);
    cq = cpool->head;
    cpool->head = cq->next;
    cpool->in_pool--;
    cpool->outstanding++;
    cq->next = NULL;
    pthread_mutex_unlock(&cpool->lock);
    return cq;
  }
  if(cpool->in_pool + cpool->outstanding >= cpool->max_allocated) {
    noitL(noit_debug, "[%p] over-subscribed, waiting [%s]\n",
          (void *)pthread_self(), cpool->queue_name);
    pthread_cond_wait(&cpool->cv, &cpool->lock);
    goto again;
  }
  else {
    cpool->outstanding++;
    pthread_mutex_unlock(&cpool->lock);
  }
 
  cq = calloc(1, sizeof(*cq));
  cq->pool = cpool;
  cq->remote_str = remote_str ? strdup(remote_str) : NULL;
  cq->remote_cn = remote_cn ? strdup(remote_cn) : NULL;
  cq->fqdn = fqdn ? strdup(fqdn) : NULL;
  cq->dsn = dsn ? strdup(dsn) : NULL;
  return cq;
}
static conn_q *
get_conn_q_for_metanode() {
  return get_conn_q_for_remote(NULL,NULL,NULL,NULL);
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
    noitL(ds_err, "stratcon datasource bad (%d): %s\n'%s'\n", \
          d->rv, PQresultErrorMessage(d->res), cmd); \
    PQclear(d->res); \
    goto bad_row; \
  } \
} while(0)

#define PG_TM_EXEC(cmd, whence) do { \
  time_t __w = whence; \
  char cmdbuf[4096]; \
  struct tm tbuf, *tm; \
  tm = gmtime_r(&__w, &tbuf); \
  strftime(cmdbuf, sizeof(cmdbuf), cmd, tm); \
  d->res = PQexecParams(cq->dbh, cmdbuf, d->nparams, NULL, \
                        (const char * const *)d->paramValues, \
                        d->paramLengths, d->paramFormats, 0); \
  d->rv = PQresultStatus(d->res); \
  if(d->rv != PGRES_COMMAND_OK && \
     d->rv != PGRES_TUPLES_OK) { \
    noitL(ds_err, "stratcon datasource bad (%d): %s\n'%s' time: %llu\n", \
          d->rv, PQresultErrorMessage(d->res), cmdbuf, \
          (long long unsigned)whence); \
    PQclear(d->res); \
    goto bad_row; \
  } \
} while(0)

static int
stratcon_datastore_asynch_drive_iep(eventer_t e, int mask, void *closure,
                                    struct timeval *now) {
  conn_q *cq = closure;
  ds_single_detail *d;
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
    int rv;
    int8_t family;
    struct sockaddr *sin;
    struct sockaddr_in sin4 = { .sin_family = AF_INET };
    struct sockaddr_in6 sin6 = { .sin6_family = AF_INET6 };
    char *remote, *id, *target, *module, *name;
    PG_GET_STR_COL(remote, i, "remote_address");
    PG_GET_STR_COL(id, i, "id");
    PG_GET_STR_COL(target, i, "target");
    PG_GET_STR_COL(module, i, "module");
    PG_GET_STR_COL(name, i, "name");
    snprintf(buff, sizeof(buff), "C\t0.000\t%s\t%s\t%s\t%s\n", id, target, module, name);

    family = AF_INET;
    sin = (struct sockaddr *)&sin4;
    rv = inet_pton(family, remote, &sin4.sin_addr);
    if(rv != 1) {
      family = AF_INET6;
      sin = (struct sockaddr *)&sin6;
      rv = inet_pton(family, remote, &sin6.sin6_addr);
      if(rv != 1) {
        noitL(noit_stderr, "Cannot translate '%s' to IP\n", remote);
        sin = NULL;
      }
    }

    /* stratcon_iep_line_processor takes an allocated operand and frees it */
    stratcon_iep_line_processor(DS_OP_INSERT, sin, NULL, strdup(buff), NULL);
    good++;
  }
  noitL(noit_error, "Staged %d/%d remembered checks into IEP\n", good, row_count);
 bad_row:
  free_params((ds_single_detail *)d);
  free(d);
  release_conn_q(cq);
  return 0;
}
void
stratcon_datastore_iep_check_preload() {
  eventer_t e;
  conn_q *cq;
  cq = get_conn_q_for_metanode();

  e = eventer_alloc();
  e->mask = EVENTER_ASYNCH;
  e->callback = stratcon_datastore_asynch_drive_iep;
  e->closure = cq;
  eventer_add_asynch(cq->pool->jobq, e);
}
execute_outcome_t
stratcon_datastore_find(conn_q *cq, ds_rt_detail *d) {
  char *val;
  int row_count;
  struct realtime_tracker *node;

  GET_QUERY(check_find);
  for(node = d->rt; node; node = node->next) {
    DECLARE_PARAM_INT(node->sid);
    PG_EXEC(check_find);
    row_count = PQntuples(d->res);
    if(row_count != 1) {
      PQclear(d->res);
      goto bad_row;
    }

    /* Get the check uuid */
    PG_GET_STR_COL(val, 0, "id");
    if(!val) {
      PQclear(d->res);
      goto bad_row;
    }
    if(uuid_parse(val, node->checkid)) {
      PQclear(d->res);
      goto bad_row;
    }
  
    /* Get the remote_address (which noit owns this) */
    PG_GET_STR_COL(val, 0, "remote_address");
    if(!val) {
      PQclear(d->res);
      goto bad_row;
    }
    node->noit = strdup(val);
 
   bad_row: 
    free_params((ds_single_detail *)d);
    d->nparams = 0;
  }
  return DS_EXEC_SUCCESS;
}
execute_outcome_t
stratcon_datastore_execute(conn_q *cq, const char *r, const char *remote_cn,
                           ds_line_detail *d) {
  int type, len, sid;
  char *final_buff;
  uLong final_len, actual_final_len;
  char *token;
  char raddr_blank[1] = "";
  const char *raddr;

  type = d->data[0];
  raddr = r ? r : raddr_blank;

  /* Parse the log line, but only if we haven't already */
  if(!d->nparams) {
    char *scp, *ecp;

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
        d->whence = (time_t)strtoul(token, NULL, 10);
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
        d->whence = (time_t)strtoul(token, NULL, 10);
        PROCESS_NEXT_FIELD(token, len);
        sid = uuid_to_sid(token, remote_cn);
        if(sid == 0) goto bad_row;
        DECLARE_PARAM_INT(sid); /* sid */
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
        d->whence = (time_t)strtoul(token, NULL, 10);
        PROCESS_NEXT_FIELD(token, len);
        sid = uuid_to_sid(token, remote_cn);
        if(sid == 0) goto bad_row;
        DECLARE_PARAM_INT(sid); /* sid */
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
        d->whence = (time_t)strtoul(token, NULL, 10);
        PROCESS_NEXT_FIELD(token, len);
        sid = uuid_to_sid(token, remote_cn);
        if(sid == 0) goto bad_row;
        DECLARE_PARAM_INT(sid); /* sid */
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
      PG_TM_EXEC(status_insert, d->whence);
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
          PG_TM_EXEC(metric_insert_numeric, d->whence);
          PQclear(d->res);
          break;
        case METRIC_STRING:
          GET_QUERY(metric_insert_text);
          PG_TM_EXEC(metric_insert_text, d->whence);
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
stratcon_database_post_connect(conn_q *cq) {
  int rv = 0;
  ds_single_detail _d = { 0 }, *d = &_d;
  if(cq->remote_str) {
    char *remote_str, *remote_cn;
    /* This is the silly way we get null's in through our declare_param_str */
    remote_str = cq->remote_str ? cq->remote_str : "[[null]]";
    remote_cn = cq->remote_cn ? cq->remote_cn : "[[null]]";
    /* This is a storage node, it gets the storage node post_connect */
    GET_QUERY(storage_post_connect);
    rv = -1; /* now we're serious */
    DECLARE_PARAM_STR(remote_str, strlen(remote_str));
    DECLARE_PARAM_STR(remote_cn, strlen(remote_cn));
    PG_EXEC(storage_post_connect);
    PQclear(d->res);
    rv = 0;
  }
  else {
    /* Metanode post_connect */
    GET_QUERY(metanode_post_connect);
    rv = -1; /* now we're serious */
    PG_EXEC(metanode_post_connect);
    PQclear(d->res);
    rv = 0;
  }
 bad_row:
  free_params(d);
  if(rv == -1) {
    /* Post-connect intentions are serious and fatal */
    PQfinish(cq->dbh);
    cq->dbh = NULL;
  }
  return rv;
}
static int
stratcon_database_connect(conn_q *cq) {
  char *dsn, dsn_meta[512];
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *k, *v;
  int klen;
  noit_hash_table *t;

  dsn_meta[0] = '\0';
  if(!cq->dsn) {
    t = noit_conf_get_hash(NULL, "/stratcon/database/dbconfig");
    while(noit_hash_next_str(t, &iter, &k, &klen, &v)) {
      if(dsn_meta[0]) strlcat(dsn_meta, " ", sizeof(dsn_meta));
      strlcat(dsn_meta, k, sizeof(dsn_meta));
      strlcat(dsn_meta, "=", sizeof(dsn_meta));
      strlcat(dsn_meta, v, sizeof(dsn_meta));
    }
    noit_hash_destroy(t, free, free);
    free(t);
    dsn = dsn_meta;
  }
  else dsn = cq->dsn;

  if(cq->dbh) {
    if(PQstatus(cq->dbh) == CONNECTION_OK) return 0;
    PQreset(cq->dbh);
    if(stratcon_database_post_connect(cq)) return -1;
    if(PQstatus(cq->dbh) == CONNECTION_OK) return 0;
    noitL(noit_error, "Error reconnecting to database: '%s'\nError: %s\n",
          dsn, PQerrorMessage(cq->dbh));
    return -1;
  }

  cq->dbh = PQconnectdb(dsn);
  if(!cq->dbh) return -1;
  if(stratcon_database_post_connect(cq)) return -1;
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
  ds_rt_detail *dsjd = closure;
  conn_q *cq = dsjd->cq;
  if(!(mask & EVENTER_ASYNCH_WORK)) return 0;

  stratcon_database_connect(cq);
  assert(dsjd->rt);
  stratcon_datastore_find(cq, dsjd);
  if(dsjd->completion_event)
    eventer_add(dsjd->completion_event);

  free_params((ds_single_detail *)dsjd);
  free(dsjd);
  release_conn_q(cq);
  return 0;
}
static const char *
get_dsn_from_storagenode_id(int id, int can_use_db, char **fqdn_out) {
  void *vinfo;
  const char *dsn = NULL, *fqdn = NULL;
  int found = 0;
  storagenode_info *info = NULL;
  pthread_mutex_lock(&storagenode_to_info_cache_lock);
  if(noit_hash_retrieve(&storagenode_to_info_cache, (void *)&id, sizeof(id),
                        &vinfo)) {
    found = 1;
    info = vinfo;
  }
  pthread_mutex_unlock(&storagenode_to_info_cache_lock);
  if(found) {
    if(fqdn_out) *fqdn_out = info->fqdn;
    return info->dsn;
  }

  if(!found && can_use_db) {
    ds_single_detail *d;
    conn_q *cq;
    int row_count;
    /* Look it up and store it */
    d = calloc(1, sizeof(*d));
    cq = get_conn_q_for_metanode();
    GET_QUERY(find_storage);
    DECLARE_PARAM_INT(id);
    PG_EXEC(find_storage);
    row_count = PQntuples(d->res);
    if(row_count) {
      PG_GET_STR_COL(dsn, 0, "dsn");
      PG_GET_STR_COL(fqdn, 0, "fqdn");
    }
    PQclear(d->res);
   bad_row:
    free_params(d);
    free(d);
  }
  if(fqdn) {
    info = calloc(1, sizeof(*info));
    info->fqdn = strdup(fqdn);
    if(fqdn_out) *fqdn_out = info->fqdn;
    info->dsn = dsn ? strdup(dsn) : NULL;
    info->storagenode_id = id;
    pthread_mutex_lock(&storagenode_to_info_cache_lock);
    noit_hash_store(&storagenode_to_info_cache,
                    (void *)&info->storagenode_id, sizeof(int), info);
    pthread_mutex_unlock(&storagenode_to_info_cache_lock);
  }
  return info ? info->dsn : NULL;
}
static ds_line_detail *
build_insert_batch(interim_journal_t *ij) {
  int rv;
  off_t len;
  const char *buff, *cp, *lcp;
  struct stat st;
  ds_line_detail *head = NULL, *last = NULL, *next = NULL;

  while((rv = fstat(ij->fd, &st)) == -1 && errno == EINTR);
  assert(rv != -1);
  len = st.st_size;
  buff = mmap(NULL, len, PROT_READ, MAP_PRIVATE, ij->fd, 0);
  if(buff == (void *)-1) {
    noitL(noit_error, "mmap(%s) => %s\n", ij->filename, strerror(errno));
    assert(buff != (void *)-1);
  }
  lcp = buff;
  while(lcp < (buff + len) &&
        NULL != (cp = strnstrn("\n", 1, lcp, len - (lcp-buff)))) {
    next = calloc(1, sizeof(*next));
    next->data = malloc(cp - lcp + 1);
    memcpy(next->data, lcp, cp - lcp);
    next->data[cp - lcp] = '\0';
    if(!head) head = next;
    if(last) last->next = next;
    last = next;
    lcp = cp + 1;
  }
  munmap((void *)buff, len);
  return head;
}
static void
interim_journal_remove(interim_journal_t *ij) {
  unlink(ij->filename);
  if(ij->filename) free(ij->filename);
  if(ij->remote_str) free(ij->remote_str);
  if(ij->remote_cn) free(ij->remote_cn);
  if(ij->fqdn) free(ij->fqdn);
}
int
stratcon_datastore_asynch_execute(eventer_t e, int mask, void *closure,
                                  struct timeval *now) {
  int i;
  interim_journal_t *ij;
  ds_line_detail *head, *current, *last_sp;
  const char *dsn;
  conn_q *cq;
  if(!(mask & EVENTER_ASYNCH_WORK)) return 0;
  if(mask & EVENTER_ASYNCH_CLEANUP) return 0;

  ij = closure;
  dsn = get_dsn_from_storagenode_id(ij->storagenode_id, 1, &ij->fqdn);
  cq = get_conn_q_for_remote(ij->remote_str, ij->remote_cn,
                             ij->fqdn, dsn);
  noitL(noit_debug, "stratcon_datastore_asynch_execute[%s,%s,%s]\n",
        ij->remote_str, ij->remote_cn, ij->fqdn);
 full_monty:
  /* Make sure we have a connection */
  i = 1;
  while(stratcon_database_connect(cq)) {
    noitL(noit_error, "Error connecting to database\n");
    sleep(i);
    i *= 2;
    i = MIN(i, 16);
  }

  head = build_insert_batch(ij);
  current = head; 
  last_sp = NULL;
  if(stratcon_datastore_do(cq, "BEGIN")) BUSTED(cq);
  while(current) {
    execute_outcome_t rv;
    if(current->data) {
      if(!last_sp) SAVEPOINT("batch");
 
      if(current->problematic) {
        noitL(ingest_err, "%d\t%s\n", ij->storagenode_id, current->data);
        RELEASE_SAVEPOINT("batch");
        current = current->next;
        continue;
      } 
      rv = stratcon_datastore_execute(cq, cq->remote_str, cq->remote_cn,
                                      current);
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
  }
  if(last_sp) RELEASE_SAVEPOINT("batch");
  if(stratcon_datastore_do(cq, "COMMIT")) BUSTED(cq);
  /* Cleanup the mess */
  while(head) {
    ds_line_detail *tofree;
    tofree = head;
    head = head->next;
    if(tofree->data) free(tofree->data);
    free_params((ds_single_detail *)tofree);
    free(tofree);
  }
  interim_journal_remove(ij);
  release_conn_q(cq);
  return 0;
}
static int
stratcon_datastore_journal_sync(eventer_t e, int mask, void *closure,
                                struct timeval *now) {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *k;
  int klen;
  void *vij;
  interim_journal_t *ij;
  syncset_t *syncset = closure;

  if(!(mask & EVENTER_ASYNCH_WORK)) return 0;
  if(mask & EVENTER_ASYNCH_CLEANUP) return 0;

  noitL(noit_debug, "Syncing journal sets...\n");
  while(noit_hash_next(syncset->ws, &iter, &k, &klen, &vij)) {
    eventer_t ingest;
    ij = vij;
    noitL(noit_debug, "Syncing journal set [%s,%s,%s]\n",
          ij->remote_str, ij->remote_cn, ij->fqdn);
    fsync(ij->fd);
    ingest = eventer_alloc();
    ingest->mask = EVENTER_ASYNCH;
    ingest->callback = stratcon_datastore_asynch_execute;
    ingest->closure = ij;
    eventer_add_asynch(ij->cpool->jobq, ingest);
  }
  noit_hash_destroy(syncset->ws, free, NULL);
  free(syncset->ws);
  eventer_add(syncset->completion);
  free(syncset);
  return 0;
}
static interim_journal_t *
interim_journal_get(struct sockaddr *remote, const char *remote_cn_in,
                    int storagenode_id, const char *fqdn_in) {
  void *vhash, *vij;
  noit_hash_table *working_set;
  interim_journal_t *ij;
  struct timeval now;
  char jpath[PATH_MAX];
  char remote_str[128];
  const char *remote_cn = remote_cn_in ? remote_cn_in : "default";
  const char *fqdn = fqdn_in ? fqdn_in : "default";

  convert_sockaddr_to_buff(remote_str, sizeof(remote_str), remote);
  if(!*remote_str) strlcpy(remote_str, "default", sizeof(remote_str));

  /* Lookup the working set */
  if(!noit_hash_retrieve(&working_sets, remote_cn, strlen(remote_cn), &vhash)) {
    working_set = calloc(1, sizeof(*working_set));
    noit_hash_store(&working_sets, strdup(remote_cn), strlen(remote_cn),
                    working_set);
  }
  else
    working_set = vhash;

  /* Lookup the interim journal within the working set */
  if(!noit_hash_retrieve(working_set, fqdn, strlen(fqdn), &vij)) {
    ij = calloc(1, sizeof(*ij));
    gettimeofday(&now, NULL);
    snprintf(jpath, sizeof(jpath), "%s/%s/%s/%d/%08x%08x",
             basejpath, remote_str, remote_cn, storagenode_id,
             (unsigned int)now.tv_sec, (unsigned int)now.tv_usec);
    ij->remote_str = strdup(remote_str);
    ij->remote_cn = strdup(remote_cn);
    ij->fqdn = strdup(fqdn);
    ij->storagenode_id = storagenode_id;
    ij->filename = strdup(jpath);
    ij->cpool = get_conn_pool_for_remote(ij->remote_str, ij->remote_cn,
                                         ij->fqdn);
    ij->fd = open(ij->filename, O_RDWR | O_CREAT | O_EXCL, 0640);
    if(ij->fd < 0 && errno == ENOENT) {
      if(mkdir_for_file(ij->filename, 0750)) {
        noitL(noit_error, "Failed to create dir for '%s': %s\n",
              ij->filename, strerror(errno));
        exit(-1);
      }
      ij->fd = open(ij->filename, O_RDWR | O_CREAT | O_EXCL, 0640);
    }
    if(ij->fd < 0) {
      noitL(noit_error, "Failed to open interim journal '%s': %s\n",
            ij->filename, strerror(errno));
      exit(-1);
    }
    noit_hash_store(working_set, strdup(fqdn), strlen(fqdn), ij);
  }
  else
    ij = vij;

  return ij;
}
static void
storage_node_quick_lookup(const char *uuid_str, const char *remote_cn,
                          int *sid_out, int *storagenode_id_out,
                          char **fqdn_out, char **dsn_out) {
  /* only called from the main thread -- no safety issues */
  void *vuuidinfo, *vinfo;
  uuid_info *uuidinfo;
  storagenode_info *info = NULL;
  char *fqdn = NULL;
  char *dsn = NULL;
  int storagenode_id = 0, sid = 0;
  if(!noit_hash_retrieve(&uuid_to_info_cache, uuid_str, strlen(uuid_str),
                         &vuuidinfo)) {
    int row_count;
    char *tmpint;
    ds_single_detail *d;
    conn_q *cq;
    d = calloc(1, sizeof(*d));
    cq = get_conn_q_for_metanode();
    if(stratcon_database_connect(cq) == 0) {
      /* Blocking call to service the cache miss */
      GET_QUERY(check_map);
      DECLARE_PARAM_STR(uuid_str, strlen(uuid_str));
      DECLARE_PARAM_STR(remote_cn, strlen(remote_cn));
      PG_EXEC(check_map);
      row_count = PQntuples(d->res);
      if(row_count != 1) {
        PQclear(d->res);
        goto bad_row;
      }
      PG_GET_STR_COL(tmpint, 0, "sid");
      sid = atoi(tmpint);
      PG_GET_STR_COL(tmpint, 0, "storage_node_id");
      if(tmpint) storagenode_id = atoi(tmpint);
      PG_GET_STR_COL(fqdn, 0, "fqdn");
      PG_GET_STR_COL(dsn, 0, "dsn");
      PQclear(d->res);
    }
   bad_row:
    free_params((ds_single_detail *)d);
    free(d);
    release_conn_q(cq);
    /* Place in cache */
    if(fqdn) fqdn = strdup(fqdn);
    uuidinfo = calloc(1, sizeof(*uuidinfo));
    uuidinfo->sid = sid;
    uuidinfo->uuid_str = strdup(uuid_str);
    noit_hash_store(&uuid_to_info_cache,
                    uuidinfo->uuid_str, strlen(uuidinfo->uuid_str), uuidinfo);
    /* Also, we may have just witnessed a new storage node, store it */
    if(storagenode_id) {
      int needs_free = 0;
      info = calloc(1, sizeof(*info));
      info->storagenode_id = storagenode_id;
      info->dsn = dsn ? strdup(dsn) : NULL;
      info->fqdn = fqdn ? strdup(fqdn) : NULL;
      pthread_mutex_lock(&storagenode_to_info_cache_lock);
      if(!noit_hash_retrieve(&storagenode_to_info_cache,
                             (void *)&storagenode_id, sizeof(int), &vinfo)) {
        /* hack to save memory -- we *never* remove from these caches,
           so we can use the same fqdn value in the above cache for the key
           in the cache below -- (no strdup) */
        noit_hash_store(&storagenode_to_info_cache,
                        (void *)&info->storagenode_id, sizeof(int), info);
      }
      else needs_free = 1;
      pthread_mutex_unlock(&storagenode_to_info_cache_lock);
      if(needs_free) {
        if(info->dsn) free(info->dsn);
        if(info->fqdn) free(info->fqdn);
        free(info);
      }
    }
  }
  else
    uuidinfo = vuuidinfo;

  if(storagenode_id) {
    if(uuidinfo &&
       ((!dsn && dsn_out) || (!fqdn && fqdn_out))) {
      /* we don't have dsn and we actually want it */
      pthread_mutex_lock(&storagenode_to_info_cache_lock);
      if(noit_hash_retrieve(&storagenode_to_info_cache,
                            (void *)&storagenode_id, sizeof(int), &vinfo))
        info = vinfo;
      pthread_mutex_unlock(&storagenode_to_info_cache_lock);
    }
  }

  if(fqdn_out) *fqdn_out = fqdn ? fqdn : (info ? info->fqdn : NULL);
  if(dsn_out) *dsn_out = dsn ? dsn : (info ? info->dsn : NULL);
  if(storagenode_id_out) *storagenode_id_out = uuidinfo->storagenode_id;
  if(sid_out) *sid_out = uuidinfo->sid;
}
static int
uuid_to_sid(const char *uuid_str_in, const char *remote_cn) {
  char uuid_str[UUID_STR_LEN+1];
  int sid = 0;
  strlcpy(uuid_str, uuid_str_in, sizeof(uuid_str));
  storage_node_quick_lookup(uuid_str, remote_cn, &sid, NULL, NULL, NULL);
  return sid;
}
static void
stratcon_datastore_journal(struct sockaddr *remote,
                           const char *remote_cn, const char *line) {
  interim_journal_t *ij = NULL;
  char uuid_str[UUID_STR_LEN+1], *cp, *fqdn, *dsn;
  int storagenode_id = 0;
  uuid_t checkid;
  if(!line) return;
  /* if it is a UUID based thing, find the storage node */
  switch(*line) {
    case 'C':
    case 'S':
    case 'M':
      if(line[1] == '\t' && (cp = strchr(line+2, '\t')) != NULL) {
        strlcpy(uuid_str, cp + 1, sizeof(uuid_str));
        if(!uuid_parse(uuid_str, checkid)) {
          storage_node_quick_lookup(uuid_str, remote_cn, NULL,
                                    &storagenode_id, &fqdn, &dsn);
          ij = interim_journal_get(remote, remote_cn, storagenode_id, fqdn);
        }
      }
      break;
    case 'n':
      ij = interim_journal_get(remote,remote_cn,0,NULL);
      break;
    default:
      break;
  }
  if(!ij && fqdn) {
    noitL(ingest_err, "%d\t%s\n", storagenode_id, line);
  }
  else {
    int len;
    len = write(ij->fd, line, strlen(line));
    if(len < 0) {
      noitL(noit_error, "write to %s failed: %s\n",
            ij->filename, strerror(errno));
    }
  }
  return;
}
static noit_hash_table *
stratcon_datastore_journal_remove(struct sockaddr *remote,
                                  const char *remote_cn) {
  void *vhash = NULL;
  if(noit_hash_retrieve(&working_sets, remote_cn, strlen(remote_cn), &vhash)) {
    /* pluck it out */
    noit_hash_delete(&working_sets, remote_cn, strlen(remote_cn), free, NULL);
  }
  else {
    noitL(noit_error, "attempted checkpoint on non-existing workingset: '%s'\n",
          remote_cn);
    abort();
  }
  return vhash;
}
void
stratcon_datastore_push(stratcon_datastore_op_t op,
                        struct sockaddr *remote,
                        const char *remote_cn, void *operand,
                        eventer_t completion) {
  conn_q *cq;
  syncset_t *syncset;
  eventer_t e;
  ds_rt_detail *rtdetail;
  struct datastore_onlooker_list *nnode;

  for(nnode = onlookers; nnode; nnode = nnode->next)
    nnode->dispatch(op,remote,remote_cn,operand);

  switch(op) {
    case DS_OP_INSERT:
      stratcon_datastore_journal(remote, remote_cn, (const char *)operand);
      break;
    case DS_OP_CHKPT:
      e = eventer_alloc();
      syncset = calloc(1, sizeof(*syncset));
      e->mask = EVENTER_ASYNCH;
      e->callback = stratcon_datastore_journal_sync;
      syncset->ws = stratcon_datastore_journal_remove(remote, remote_cn);
      syncset->completion = completion;
      e->closure = syncset;
      eventer_add(e);
      break;
    case DS_OP_FIND_COMPLETE:
      cq = get_conn_q_for_metanode();
      rtdetail = calloc(1, sizeof(*rtdetail));
      rtdetail->rt = operand;
      rtdetail->completion_event = completion;
      e = eventer_alloc();
      e->mask = EVENTER_ASYNCH;
      e->callback = stratcon_datastore_asynch_lookup;
      e->closure = rtdetail;
      eventer_add_asynch(cq->pool->jobq, e);
      break;
  }
}

int
stratcon_datastore_saveconfig(void *unused) {
  int rv = -1;
  char *buff;
  ds_single_detail _d = { 0 }, *d = &_d;
  conn_q *cq;
  cq = get_conn_q_for_metanode();

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
  release_conn_q(cq);
  return rv;
}

void
stratcon_datastore_register_onlooker(void (*f)(stratcon_datastore_op_t,
                                               struct sockaddr *,
                                               const char *, void *)) {
  struct datastore_onlooker_list *nnode;
  nnode = calloc(1, sizeof(*nnode));
  nnode->dispatch = f;
  nnode->next = onlookers;
  while(noit_atomic_casptr((void **)&onlookers, nnode, nnode->next) != (void *)nnode->next)
    nnode->next = onlookers;
}
static void
stratcon_datastore_launch_file_ingestion(char *remote_str, char *remote_cn,
                                         char *id_str, char *file) {
  char path[PATH_MAX];
  interim_journal_t *ij;
  eventer_t ingest;

  snprintf(path, sizeof(path), "%s/%s/%s/%s/%s",
           basejpath, remote_str, remote_cn, id_str, file);
  ij = calloc(1, sizeof(*ij));
  ij->fd = open(path, O_RDONLY);
  if(ij->fd < 0) {
    noitL(noit_error, "cannot open journal '%s': %s\n",
          path, strerror(errno));
    free(ij);
    return;
  }
  ij->filename = strdup(path);
  ij->remote_str = strdup(remote_str);
  ij->remote_cn = strdup(remote_cn);
  ij->storagenode_id = atoi(id_str);
  ij->cpool = get_conn_pool_for_remote(ij->remote_str, ij->remote_cn,
                                       ij->fqdn);

  noitL(noit_error, "ingesting old payload: %s\n", ij->filename);
  ingest = eventer_alloc();
  ingest->mask = EVENTER_ASYNCH;
  ingest->callback = stratcon_datastore_asynch_execute;
  ingest->closure = ij;
  eventer_add_asynch(ij->cpool->jobq, ingest);
}
static void
stratcon_datastore_sweep_journals_int(char *first, char *second, char *third) {
  char path[PATH_MAX];
  DIR *root;
  struct dirent de, *entry;
  int i = 0, cnt = 0;
  char **entries;

  snprintf(path, sizeof(path), "%s%s%s%s%s%s%s", basejpath,
           first ? "/" : "", first ? first : "",
           second ? "/" : "", second ? second : "",
           third ? "/" : "", third ? third : "");
  root = opendir(path);
  if(!root) return;
  while(readdir_r(root, &de, &entry) == 0 && entry != NULL) cnt++;
  rewinddir(root);
  entries = malloc(sizeof(*entries) * cnt);
  while(readdir_r(root, &de, &entry) == 0 && entry != NULL) {
    if(i < cnt) {
      entries[i++] = strdup(entry->d_name);
    }
  }
  closedir(root);
  cnt = i; /* could have changed, directories are fickle */
  qsort(entries, i, sizeof(*entries),
        (int (*)(const void *, const void *))strcasecmp);
  for(i=0; i<cnt; i++) {
    if(!strcmp(entries[i], ".") || !strcmp(entries[i], "..")) continue;
    if(!first)
      stratcon_datastore_sweep_journals_int(entries[i], NULL, NULL);
    else if(!second)
      stratcon_datastore_sweep_journals_int(first, entries[i], NULL);
    else if(!third)
      stratcon_datastore_sweep_journals_int(first, second, entries[i]);
    else if(strlen(entries[i]) == 16)
      stratcon_datastore_launch_file_ingestion(first,second,third,entries[i]);
  }
}
static void
stratcon_datastore_sweep_journals() {
  stratcon_datastore_sweep_journals_int(NULL,NULL,NULL);
}

int
stratcon_datastore_ingest_all_storagenode_info() {
  int i, cnt;
  ds_single_detail _d = { 0 }, *d = &_d;
  conn_q *cq;
  cq = get_conn_q_for_metanode();

  while(stratcon_database_connect(cq)) {
    noitL(noit_error, "Error connecting to database\n");
    sleep(1);
  }

  GET_QUERY(all_storage);
  PG_EXEC(all_storage);
  cnt = PQntuples(d->res);
  for(i=0; i<cnt; i++) {
    void *vinfo;
    char *tmpint, *fqdn, *dsn;
    int storagenode_id;
    PG_GET_STR_COL(tmpint, i, "storage_node_id");
    storagenode_id = atoi(tmpint);
    PG_GET_STR_COL(fqdn, i, "fqdn");
    PG_GET_STR_COL(dsn, i, "dsn");
    PG_GET_STR_COL(tmpint, i, "storage_node_id");
    storagenode_id = tmpint ? atoi(tmpint) : 0;

    if(!noit_hash_retrieve(&storagenode_to_info_cache,
                           (void *)&storagenode_id, sizeof(int), &vinfo)) {
      storagenode_info *info;
      info = calloc(1, sizeof(*info));
      info->storagenode_id = storagenode_id;
      info->fqdn = fqdn ? strdup(fqdn) : NULL;
      info->dsn = dsn ? strdup(dsn) : NULL;
      noit_hash_store(&storagenode_to_info_cache,
                      (void *)&info->storagenode_id, sizeof(int), info);
    }
  }
  PQclear(d->res);
 bad_row:
  free_params(d);

  release_conn_q(cq);
  noitL(noit_error, "Loaded %d storage nodes\n", cnt);
  return cnt;
}
int
stratcon_datastore_ingest_all_check_info() {
  int i, cnt, loaded = 0;
  ds_single_detail _d = { 0 }, *d = &_d;
  conn_q *cq;
  cq = get_conn_q_for_metanode();

  while(stratcon_database_connect(cq)) {
    noitL(noit_error, "Error connecting to database\n");
    sleep(1);
  }

  GET_QUERY(check_mapall);
  PG_EXEC(check_mapall);
  cnt = PQntuples(d->res);
  for(i=0; i<cnt; i++) {
    void *vinfo;
    char *tmpint, *fqdn, *dsn, *uuid_str;
    int sid, storagenode_id;
    uuid_info *uuidinfo;
    PG_GET_STR_COL(uuid_str, i, "id");
    if(!uuid_str) continue;
    PG_GET_STR_COL(tmpint, i, "sid");
    if(!tmpint) continue;
    sid = atoi(tmpint);
    PG_GET_STR_COL(fqdn, i, "fqdn");
    PG_GET_STR_COL(dsn, i, "dsn");
    PG_GET_STR_COL(tmpint, i, "storage_node_id");
    storagenode_id = tmpint ? atoi(tmpint) : 0;

    uuidinfo = calloc(1, sizeof(*uuidinfo));
    uuidinfo->uuid_str = strdup(uuid_str);
    uuidinfo->sid = sid;
    noit_hash_store(&uuid_to_info_cache,
                    uuidinfo->uuid_str, strlen(uuidinfo->uuid_str), uuidinfo);
    loaded++;
    if(!noit_hash_retrieve(&storagenode_to_info_cache,
                           (void *)&storagenode_id, sizeof(int), &vinfo)) {
      storagenode_info *info;
      info = calloc(1, sizeof(*info));
      info->storagenode_id = storagenode_id;
      info->fqdn = fqdn ? strdup(fqdn) : NULL;
      info->dsn = dsn ? strdup(dsn) : NULL;
      noit_hash_store(&storagenode_to_info_cache,
                      (void *)&info->storagenode_id, sizeof(int), info);
    }
  }
  PQclear(d->res);
 bad_row:
  free_params(d);

  release_conn_q(cq);
  noitL(noit_error, "Loaded %d uuid -> (sid,storage_node_id) mappings\n", loaded);
  return loaded;
}
void
stratcon_datastore_init() {
  pthread_mutex_init(&ds_conns_lock, NULL);
  pthread_mutex_init(&storagenode_to_info_cache_lock, NULL);
  ds_err = noit_log_stream_find("error/datastore");
  ingest_err = noit_log_stream_find("error/ingest");
  if(!ds_err) ds_err = noit_error;
  if(!ingest_err) ingest_err = noit_error;
  if(!noit_conf_get_string(NULL, "/stratcon/database/journal/path",
                           &basejpath)) {
    noitL(noit_error, "/stratcon/database/journal/path is unspecified\n");
    exit(-1);
  }
  stratcon_datastore_ingest_all_check_info();
  stratcon_datastore_ingest_all_storagenode_info();
  stratcon_datastore_sweep_journals();
}
