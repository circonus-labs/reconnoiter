/*
 * Copyright (c) 2007-2011, OmniTI Computer Consulting, Inc.
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

#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <libpq-fe.h>
#include <zlib.h>
#include <errno.h>

#include <eventer/eventer.h>
#include <mtev_log.h>
#include <mtev_b64.h>
#include <mtev_str.h>
#include <mtev_mkdir.h>
#include <mtev_getip.h>
#include <mtev_watchdog.h>
#include <mtev_conf.h>
#include <mtev_rest.h>

#include "noit_mtev_bridge.h"
#include "noit_module.h"
#include "stratcon_datastore.h"
#include "stratcon_ingest.h"
#include "stratcon_realtime_http.h"
#include "stratcon_iep.h"
#include "noit_check.h"
#include "noit_check_log_helpers.h"
#include "bundle.pb-c.h"

#include "postgres_ingestor.xmlh"

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
DECL_STMT(config_get, findconfig);

static mtev_log_stream_t ds_err = NULL;
static mtev_log_stream_t ds_deb = NULL;
static mtev_log_stream_t ds_pool_deb = NULL;
static mtev_log_stream_t ingest_err = NULL;

#define GET_QUERY(a) do { \
  if(a == NULL) \
    if(!mtev_conf_get_string(NULL, a ## _conf, &(a))) \
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
  char *remote_str;
  char *remote_cn;
  char *fqdn;
  int storagenode_id;
  int fd;
  char *filename;
  conn_pool *cpool;
} pg_interim_journal_t;

static int stratcon_database_connect(conn_q *cq);
static int uuid_to_sid(const char *uuid_str_in, const char *remote_cn);
static int storage_node_quick_lookup(const char *uuid_str,
                                     const char *remote_cn,
                                     int *sid_out, int *storagenode_id_out,
                                     const char **remote_cn_out,
                                     const char **fqdn_out,
                                     const char **dsn_out);

static void
free_params(ds_single_detail *d) {
  int i;
  for(i=0; i<d->nparams; i++)
    if(d->paramAllocd[i] && d->paramValues[i])
      free(d->paramValues[i]);
}

static char *basejpath = NULL;
static pthread_mutex_t ds_conns_lock;
static mtev_hash_table ds_conns;
static mtev_hash_table uuid_to_info_cache;
static pthread_mutex_t storagenode_to_info_cache_lock;
static mtev_hash_table storagenode_to_info_cache;

/* the fqdn cache needs to be thread safe */
typedef struct {
  char *uuid_str;
  char *remote_cn;
  int storagenode_id;
  int sid;
} uuid_info;
typedef struct {
  int storagenode_id;
  char *fqdn;
  char *dsn;
} storagenode_info;

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
  mtevL(ds_pool_deb, "[%p] release %s [%s]\n", (void *)(intptr_t)pthread_self(),
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
    release_conn_q_forceable(prev, 1);
  }
  if(old_cnt != new_cnt)
    mtevL(ds_pool_deb, "reduced db pool %d -> %d [%s]\n", old_cnt, new_cnt,
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
  if(mtev_hash_retrieve(&ds_conns, (const char *)queue_name,
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
    if(!mtev_hash_store(&ds_conns, cpool->queue_name, strlen(cpool->queue_name),
                        cpool)) {
      (void)mtev_hash_retrieve(&ds_conns, (const char *)queue_name,
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
      /* Our job to setup the pool */
      cpool->jobq = eventer_jobq_create(queue_name);
      uint32_t target = MAX(cpool->max_allocated - cpool->max_in_pool, 1);
      eventer_jobq_set_concurrency(cpool->jobq, target);
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
  mtevL(ds_pool_deb, "[%p] requesting [%s]\n", (void *)(intptr_t)pthread_self(),
        cpool->queue_name);
  pthread_mutex_lock(&cpool->lock);
 again:
  if(cpool->head) {
    mtevAssert(cpool->in_pool > 0);
    cq = cpool->head;
    cpool->head = cq->next;
    cpool->in_pool--;
    cpool->outstanding++;
    cq->next = NULL;
    pthread_mutex_unlock(&cpool->lock);
    return cq;
  }
  if(cpool->in_pool + cpool->outstanding >= cpool->max_allocated) {
    mtevL(ds_pool_deb, "[%p] over-subscribed, waiting [%s]\n",
          (void *)(intptr_t)pthread_self(), cpool->queue_name);
    pthread_cond_wait(&cpool->cv, &cpool->lock);
    mtevL(ds_pool_deb, "[%p] waking up and trying again [%s]\n",
          (void *)(intptr_t)pthread_self(), cpool->queue_name);
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

#define DECLARE_PARAM_STR(str, len) do { \
  d->paramValues[d->nparams] = mtev__strndup(str, len); \
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
    const char *pgerr = PQresultErrorMessage(d->res); \
    const char *pgerr_end = strchr(pgerr, '\n'); \
    if(!pgerr_end) pgerr_end = pgerr + strlen(pgerr); \
    mtevL(ds_err, "[%s] stratcon_datasource.c:%d bad (%d): %.*s\n", \
          cq->fqdn ? cq->fqdn : "metanode", __LINE__, d->rv, \
          (int)(pgerr_end - pgerr), pgerr); \
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
    const char *pgerr = PQresultErrorMessage(d->res); \
    const char *pgerr_end = strchr(pgerr, '\n'); \
    if(!pgerr_end) pgerr_end = pgerr + strlen(pgerr); \
    mtevL(ds_err, "stratcon_datasource.c:%d bad (%d): %.*s time: %llu\n", \
          __LINE__, d->rv, (int)(pgerr_end - pgerr), pgerr, \
          (long long unsigned)whence); \
    PQclear(d->res); \
    goto bad_row; \
  } \
} while(0)

static void *
stratcon_ingest_check_loadall(void *vsn) {
  storagenode_info *sn = vsn;
  ds_single_detail *d;
  int i, row_count = 0, good = 0;
  char buff[1024];
  conn_q *cq = NULL;

  d = calloc(1, sizeof(*d));
  GET_QUERY(check_loadall);
  cq = get_conn_q_for_remote(NULL,NULL,sn->fqdn,sn->dsn);
  i = 0;
  while(stratcon_database_connect(cq)) {
    if(i++ > 4) {
      mtevL(noit_error, "giving up on storage node: %s\n", sn->fqdn);
      release_conn_q(cq);
      free(d);
      return (void *)(intptr_t)good;
    }
    sleep(1);
  }
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
        mtevL(noit_stderr, "Cannot translate '%s' to IP\n", remote);
        sin = NULL;
      }
    }

    /* stratcon_iep_line_processor takes an allocated operand and frees it */
    stratcon_iep_line_processor(DS_OP_INSERT, sin, NULL, strdup(buff), NULL);
    good++;
  }
  mtevL(noit_error, "Staged %d/%d remembered checks from %s into IEP\n",
        good, row_count, sn->fqdn);
 bad_row:
  free_params((ds_single_detail *)d);
  free(d);
  if(cq) release_conn_q(cq);
  return (void *)(intptr_t)good;
}
static int
stratcon_ingest_asynch_drive_iep(eventer_t e, int mask, void *closure,
                                 struct timeval *now) {
  storagenode_info self = { 0, NULL, NULL }, **sns = NULL;
  pthread_t *jobs = NULL;
  int nodes, i = 0, tcnt = 0;
  if(!(mask & EVENTER_ASYNCH_WORK)) return 0;
  if(mask & EVENTER_ASYNCH_CLEANUP) return 0;

  pthread_mutex_lock(&storagenode_to_info_cache_lock);
  nodes = mtev_hash_size(&storagenode_to_info_cache);
  jobs = calloc(MAX(1,nodes), sizeof(*jobs));
  sns = calloc(MAX(1,nodes), sizeof(*sns));
  if(nodes == 0) sns[nodes++] = &self;
  else {
    mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
    const char *k;
    void *v;
    int klen;
    while(mtev_hash_next(&storagenode_to_info_cache,
                         &iter, &k, &klen, &v)) {
      sns[i++] = (storagenode_info *)v;
    }
  }
  pthread_mutex_unlock(&storagenode_to_info_cache_lock);

  for(i=0; i<nodes; i++) {
    if(pthread_create(&jobs[i], NULL,
                      stratcon_ingest_check_loadall, sns[i]) != 0) {
      mtevL(noit_error, "Failed to spawn thread: %s\n", strerror(errno));
    }
  }
  for(i=0; i<nodes; i++) {
    void *good;
    pthread_join(jobs[i], &good);
    tcnt += (int)(intptr_t)good;
  }
  free(jobs);
  free(sns);
  mtevL(noit_error, "Loaded all %d check states.\n", tcnt);
  return 0;
}
static void
stratcon_ingest_iep_check_preload() {
  eventer_t e;
  conn_pool *cpool;

  cpool = get_conn_pool_for_remote(NULL,NULL,NULL);
  e = eventer_alloc_asynch(stratcon_ingest_asynch_drive_iep, NULL);
  eventer_add_asynch(cpool->jobq, e);
}
execute_outcome_t
stratcon_ingest_find(ds_rt_detail *d) {
  conn_q *cq;
  char *val;
  int row_count;
  struct realtime_tracker *node;

  for(node = d->rt; node; node = node->next) {
    char uuid_str[UUID_STR_LEN+1];
    const char *fqdn, *dsn, *remote_cn;
    char remote_ip[32];
    int storagenode_id;

    uuid_unparse_lower(node->checkid, uuid_str);
    if(storage_node_quick_lookup(uuid_str, NULL, &node->sid,
                                 &storagenode_id, &remote_cn, &fqdn, &dsn))
      continue;

    mtevL(noit_debug, "stratcon_ingest_find <- (%d, %s) @ %s\n",
          node->sid, remote_cn ? remote_cn : "(null)", dsn ? dsn : "(null)");

    /* We might be able to find the IP from our config if someone has
     * specified the expected cn in the noit definition.
     */
    if(stratcon_find_noit_ip_by_cn(remote_cn,
                                   remote_ip, sizeof(remote_ip)) == 0) {
      node->noit = strdup(remote_ip);
      mtevL(noit_debug, "lookup(cache): %s -> %s\n", remote_cn, node->noit);
      continue;
    }

    cq = get_conn_q_for_remote(NULL, remote_cn, fqdn, dsn);
    if(stratcon_database_connect(cq) != 0) goto bad_row;

    GET_QUERY(check_find);
    DECLARE_PARAM_INT(node->sid);
    PG_EXEC(check_find);
    row_count = PQntuples(d->res);
    if(row_count != 1) {
      mtevL(noit_debug, "lookup (sid:%d): NOT THERE!\n", node->sid);
      PQclear(d->res);
      goto bad_row;
    }

    /* Get the remote_address (which noit owns this) */
    PG_GET_STR_COL(val, 0, "remote_address");
    if(!val) {
      mtevL(noit_debug, "lookup: %s -> NOT THERE!\n", remote_cn);
      PQclear(d->res);
      goto bad_row;
    }
    node->noit = strdup(val);
    mtevL(noit_debug, "lookup: %s -> %s\n", remote_cn, node->noit);
   bad_row: 
    free_params((ds_single_detail *)d);
    d->nparams = 0;
    release_conn_q(cq);
  }
  return DS_EXEC_SUCCESS;
}
execute_outcome_t
stratcon_ingest_execute(conn_q *cq, const char *r, const char *remote_cn,
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
        DECLARE_PARAM_STR(remote_cn, strlen(remote_cn));
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
        len = mtev_b64_decode((char *)token, len,
                              (unsigned char *)token, len);
        if(len <= 0) {
          mtevL(noit_error, "noitd config base64 decoding error.\n");
          free(final_buff);
          goto bad_row;
        }
        actual_final_len = final_len;
        if(Z_OK != uncompress((Bytef *)final_buff, &actual_final_len,
                              (unsigned char *)token, len)) {
          mtevL(noit_error, "noitd config decompression failure.\n");
          free(final_buff);
          goto bad_row;
        }
        if(final_len != actual_final_len) {
          mtevL(noit_error, "noitd config decompression error.\n");
          free(final_buff);
          goto bad_row;
        }
        DECLARE_PARAM_STR(final_buff, final_len);
        free(final_buff);
        break;
      case 'D':
        break;
      case 'C':
        DECLARE_PARAM_STR(raddr, strlen(raddr));
        PROCESS_NEXT_FIELD(token,len);
        DECLARE_PARAM_STR(token,len); /* timestamp */
        d->whence = (time_t)strtoul(token, NULL, 10);
        PROCESS_NEXT_FIELD(token, len);
        /* uuid is last 36 bytes */
        if(len > 36) { token += (len-36); len = 36; }
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
        /* uuid is last 36 bytes */
        if(len > 36) { token += (len-36); len = 36; }
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
        /* uuid is last 36 bytes */
        if(len > 36) { token += (len-36); len = 36; }
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
      PG_TM_EXEC(check_insert, d->whence);
      PQclear(d->res);
      break;
    case 'S':
      GET_QUERY(status_insert);
      PG_TM_EXEC(status_insert, d->whence);
      PQclear(d->res);
      break;
    case 'D':
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
  if(cq->fqdn) {
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
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  const char *k, *v;
  int klen;
  mtev_hash_table *t;

  dsn_meta[0] = '\0';
  if(!cq->dsn) {
    t = mtev_conf_get_hash(NULL, "/stratcon/database/dbconfig");
    while(mtev_hash_next_str(t, &iter, &k, &klen, &v)) {
      if(dsn_meta[0]) strlcat(dsn_meta, " ", sizeof(dsn_meta));
      strlcat(dsn_meta, k, sizeof(dsn_meta));
      strlcat(dsn_meta, "=", sizeof(dsn_meta));
      strlcat(dsn_meta, v, sizeof(dsn_meta));
    }
    mtev_hash_destroy(t, free, free);
    free(t);
    dsn = dsn_meta;
  }
  else {
    char options[32];
    strlcpy(dsn_meta, cq->dsn, sizeof(dsn_meta));
    if(mtev_conf_get_stringbuf(NULL, "/stratcon/database/dbconfig/user",
                               options, sizeof(options))) {
      strlcat(dsn_meta, " ", sizeof(dsn_meta));
      strlcat(dsn_meta, "user", sizeof(dsn_meta));
      strlcat(dsn_meta, "=", sizeof(dsn_meta));
      strlcat(dsn_meta, options, sizeof(dsn_meta));
    }
    if(mtev_conf_get_stringbuf(NULL, "/stratcon/database/dbconfig/password",
                               options, sizeof(options))) {
      strlcat(dsn_meta, " ", sizeof(dsn_meta));
      strlcat(dsn_meta, "password", sizeof(dsn_meta));
      strlcat(dsn_meta, "=", sizeof(dsn_meta));
      strlcat(dsn_meta, options, sizeof(dsn_meta));
    }
    dsn = dsn_meta;
  }

  if(cq->dbh) {
    if(PQstatus(cq->dbh) == CONNECTION_OK) return 0;
    PQreset(cq->dbh);
    if(PQstatus(cq->dbh) != CONNECTION_OK) {
      mtevL(noit_error, "Error reconnecting to database: '%s'\nError: %s\n",
            dsn, PQerrorMessage(cq->dbh));
      return -1;
    }
    if(stratcon_database_post_connect(cq)) return -1;
    if(PQstatus(cq->dbh) == CONNECTION_OK) return 0;
    mtevL(noit_error, "Error reconnecting to database: '%s'\nError: %s\n",
          dsn, PQerrorMessage(cq->dbh));
    return -1;
  }

  cq->dbh = PQconnectdb(dsn);
  if(!cq->dbh) return -1;
  if(PQstatus(cq->dbh) != CONNECTION_OK) {
    mtevL(noit_error, "Error reconnecting to database: '%s'\nError: %s\n",
          dsn, PQerrorMessage(cq->dbh));
    return -1;
  }
  if(stratcon_database_post_connect(cq)) return -1;
  if(PQstatus(cq->dbh) == CONNECTION_OK) return 0;
  mtevL(noit_error, "Error connection to database: '%s'\nError: %s\n",
        dsn, PQerrorMessage(cq->dbh));
  return -1;
}
static int
stratcon_ingest_savepoint_op(conn_q *cq, const char *p,
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
stratcon_ingest_do(conn_q *cq, const char *cmd) {
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
  if(stratcon_ingest_savepoint_op(cq, "SAVEPOINT ", name)) BUSTED(cq); \
  last_sp = current; \
} while(0)
#define ROLLBACK_TO_SAVEPOINT(name) do { \
  if(stratcon_ingest_savepoint_op(cq, "ROLLBACK TO SAVEPOINT ", name)) \
    BUSTED(cq); \
  last_sp = NULL; \
} while(0)
#define RELEASE_SAVEPOINT(name) do { \
  if(stratcon_ingest_savepoint_op(cq, "RELEASE SAVEPOINT ", name)) \
    BUSTED(cq); \
  last_sp = NULL; \
} while(0)

int
stratcon_ingest_asynch_lookup(eventer_t e, int mask, void *closure,
                              struct timeval *now) {
  ds_rt_detail *dsjd = closure;
  if(!(mask & EVENTER_ASYNCH_WORK)) return 0;
  if(mask & EVENTER_ASYNCH_CLEANUP) return 0;

  mtevAssert(dsjd->rt);
  stratcon_ingest_find(dsjd);
  if(dsjd->completion_event)
    eventer_add(dsjd->completion_event);

  free_params((ds_single_detail *)dsjd);
  free(dsjd);
  return 0;
}
static void
stratcon_ingestor_submit_lookup(struct realtime_tracker *rt,
                                eventer_t completion) {
  eventer_t e;
  conn_pool *cpool;
  ds_rt_detail *rtdetail;

  cpool = get_conn_pool_for_remote(NULL,NULL,NULL);
  rtdetail = calloc(1, sizeof(*rtdetail));
  rtdetail->rt = rt;
  rtdetail->completion_event = completion;
  e = eventer_alloc_asynch(stratcon_ingest_asynch_lookup, rtdetail);
  eventer_add_asynch(cpool->jobq, e);
}
static const char *
get_dsn_from_storagenode_id(int id, int can_use_db, char **fqdn_out) {
  void *vinfo;
  char *dsn = NULL, *fqdn = NULL;
  int found = 0;
  storagenode_info *info = NULL;
  pthread_mutex_lock(&storagenode_to_info_cache_lock);
  if(mtev_hash_retrieve(&storagenode_to_info_cache, (void *)&id, sizeof(id),
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
      fqdn = fqdn ? strdup(fqdn) : NULL;
      dsn = dsn ? strdup(dsn) : NULL;
    }
    PQclear(d->res);
   bad_row:
    free_params(d);
    free(d);
    release_conn_q(cq);
  }
  if(fqdn) {
    info = calloc(1, sizeof(*info));
    info->fqdn = fqdn;
    if(fqdn_out) *fqdn_out = info->fqdn;
    info->dsn = dsn;
    info->storagenode_id = id;
    pthread_mutex_lock(&storagenode_to_info_cache_lock);
    mtev_hash_store(&storagenode_to_info_cache,
                    (void *)&info->storagenode_id, sizeof(int), info);
    pthread_mutex_unlock(&storagenode_to_info_cache_lock);
  }
  return info ? info->dsn : NULL;
}
static void
expand_b_record(ds_line_detail **head, ds_line_detail **last,
                const char *line, int len) {
  char **outrows;
  int i, cnt;
  ds_line_detail *next;

  cnt = noit_check_log_b_to_sm(line, len, &outrows, 0);
  for(i=0;i<cnt;i++) {
    if(outrows[i] == NULL) continue;
    next = calloc(sizeof(*next), 1);
    next->data = outrows[i];
    if(!*head) *head = next;
    if(*last) (*last)->next = next;
    *last = next;
  }
  if(outrows) free(outrows);
}
static ds_line_detail *
build_insert_batch(pg_interim_journal_t *ij) {
  int rv;
  off_t len;
  const char *buff, *cp, *lcp;
  struct stat st;
  ds_line_detail *head = NULL, *last = NULL, *next = NULL;

  if(ij->fd < 0) {
    ij->fd = open(ij->filename, O_RDONLY);
    if(ij->fd < 0) {
      mtevL(noit_error, "Cannot open interim journal '%s': %s\n",
            ij->filename, strerror(errno));
      mtevAssert(ij->fd >= 0);
    }
  }
  while((rv = fstat(ij->fd, &st)) == -1 && errno == EINTR);
  if(rv == -1) {
      mtevL(noit_error, "Cannot stat interim journal '%s': %s\n",
            ij->filename, strerror(errno));
    mtevAssert(rv != -1);
  }
  len = st.st_size;
  if(len > 0) {
    buff = mmap(NULL, len, PROT_READ, MAP_PRIVATE, ij->fd, 0);
    if(buff == (void *)-1) {
      mtevL(noit_error, "mmap(%d, %d)(%s) => %s\n", (int)len, ij->fd,
            ij->filename, strerror(errno));
      mtevAssert(buff != (void *)-1);
    }
    lcp = buff;
    while(lcp < (buff + len) &&
          NULL != (cp = strnstrn("\n", 1, lcp, len - (lcp-buff)))) {
      if(lcp[0] == 'B' && lcp[1] != '\0' && lcp[2] == '\t') {
      /* Bundle records are special and need to be expanded into
       * traditional records here
       */
        noit_compression_type_t ctype = NOIT_COMPRESS_NONE;
        switch(lcp[1]) {
          case '1': /* version 1 */
            ctype = NOIT_COMPRESS_ZLIB; /*no break fall through */
          case '2': /* version 2 */
              expand_b_record(&head, &last, lcp, cp - lcp);
            break;
          default:
            mtevL(noit_error, "unknown bundle version %c\n", lcp[1]);
        }
      }
      else {
        next = calloc(1, sizeof(*next));
        next->data = malloc(cp - lcp + 1);
        memcpy(next->data, lcp, cp - lcp);
        next->data[cp - lcp] = '\0';
        if(!head) head = next;
        if(last) last->next = next;
        last = next;
      }
      lcp = cp + 1;
    }
    munmap((void *)buff, len);
  }
  close(ij->fd);
  return head;
}
static void
pg_interim_journal_remove(pg_interim_journal_t *ij) {
  unlink(ij->filename);
  free(ij->filename);
  if(ij->remote_str) free(ij->remote_str);
  if(ij->remote_cn) free(ij->remote_cn);
  if(ij->fqdn) free(ij->fqdn);
  free(ij);
}
static int
stratcon_ingest_asynch_execute(eventer_t e, int mask, void *closure,
                               struct timeval *now) {
  int i, total, success, sp_total, sp_success;
  pg_interim_journal_t *ij;
  ds_line_detail *head = NULL, *current, *last_sp;
  const char *dsn;
  conn_q *cq;
  if(!(mask & EVENTER_ASYNCH_WORK)) return 0;
  if(mask & EVENTER_ASYNCH_CLEANUP) return 0;

  ij = closure;
  if(ij->fqdn == NULL) {
    dsn = get_dsn_from_storagenode_id(ij->storagenode_id, 1, &ij->fqdn);
    if(ij->fqdn) ij->fqdn = strdup(ij->fqdn); /* fqdn is now ours */
  }
  else {
    dsn = get_dsn_from_storagenode_id(ij->storagenode_id, 1, NULL);
  }
  cq = get_conn_q_for_remote(ij->remote_str, ij->remote_cn,
                             ij->fqdn, dsn);
  mtevL(ds_deb, "stratcon_ingest_asynch_execute[%s,%s,%s]\n",
        ij->remote_str, ij->remote_cn, ij->fqdn);
 full_monty:
  /* Make sure we have a connection */
  i = 1;
  while(stratcon_database_connect(cq)) {
    mtevL(noit_error, "Error connecting to database: %s\n",
          ij->fqdn ? ij->fqdn : "(null)");
    sleep(i);
    i *= 2;
    i = MIN(i, 16);
  }

  if(head == NULL) head = build_insert_batch(ij);
  mtevL(ds_deb, "Starting batch from %s/%s to %s\n",
        ij->remote_str ? ij->remote_str : "(null)",
        ij->remote_cn ? ij->remote_cn : "(null)",
        ij->fqdn ? ij->fqdn : "(null)");
  current = head; 
  last_sp = NULL;
  total = success = sp_total = sp_success = 0;
  if(stratcon_ingest_do(cq, "BEGIN")) BUSTED(cq);
  while(current) {
    execute_outcome_t rv;
    if(current->data) {
      if(!last_sp) {
        SAVEPOINT("batch");
        sp_success = success;
        sp_total = total;
      }
 
      if(current->problematic) {
        RELEASE_SAVEPOINT("batch");
        current = current->next;
        total++;
        continue;
      } 
      rv = stratcon_ingest_execute(cq, cq->remote_str, cq->remote_cn,
                                   current);
      switch(rv) {
        case DS_EXEC_SUCCESS:
          total++;
          success++;
          current = current->next;
          break;
        case DS_EXEC_ROW_FAILED:
          /* rollback to savepoint, mark this record as bad and start again */
          if(current->data[0] != 'n')
            mtevL(ingest_err, "%d\t%s\n", ij->storagenode_id, current->data);
          current->problematic = 1;
          current = last_sp;
          success = sp_success;
          total = sp_total;
          ROLLBACK_TO_SAVEPOINT("batch");
          break;
        case DS_EXEC_TXN_FAILED:
          mtevL(noit_error, "txn failed '%s', retrying\n", ij->filename);
          BUSTED(cq);
      }
    }
  }
  if(last_sp) RELEASE_SAVEPOINT("batch");
  if(stratcon_ingest_do(cq, "COMMIT")) {
    mtevL(noit_error, "txn commit failed '%s', retrying\n", ij->filename);
    BUSTED(cq);
  }
  /* Cleanup the mess */
  while(head) {
    ds_line_detail *tofree;
    tofree = head;
    head = head->next;
    if(tofree->data) free(tofree->data);
    free_params((ds_single_detail *)tofree);
    free(tofree);
  }
  mtevL(ds_deb, "Finished batch %s/%s to %s [%d/%d]\n",
        ij->remote_str ? ij->remote_str : "(null)",
        ij->remote_cn ? ij->remote_cn : "(null)",
        ij->fqdn ? ij->fqdn : "(null)", success, total);
  pg_interim_journal_remove(ij);
  release_conn_q(cq);
  return 0;
}
static int
storage_node_quick_lookup(const char *uuid_str, const char *remote_cn,
                          int *sid_out, int *storagenode_id_out,
                          const char **remote_cn_out,
                          const char **fqdn_out, const char **dsn_out) {
  /* only called from the main thread -- no safety issues */
  void *vuuidinfo, *vinfo;
  uuid_info *uuidinfo;
  storagenode_info *info = NULL;
  char *fqdn = NULL;
  char *dsn = NULL;
  char *new_remote_cn = NULL;
  int storagenode_id = 0, sid = 0;
  if(!mtev_hash_retrieve(&uuid_to_info_cache, uuid_str, strlen(uuid_str),
                         &vuuidinfo)) {
    int row_count = 0;
    char *tmpint;
    ds_single_detail *d;
    conn_q *cq;

    /* We can't do a database lookup without the remote_cn */
    if(!remote_cn) {
      if(stratcon_datastore_get_enabled()) {
        /* We have an authoritatively maintained cache, we don't do lookups */
        return -1;
      }
      else
        remote_cn = "[[null]]";
    }

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
      if(!tmpint) {
        row_count = 0;
        PQclear(d->res);
        goto bad_row;
      }
      sid = atoi(tmpint);
      PG_GET_STR_COL(tmpint, 0, "storage_node_id");
      if(tmpint) storagenode_id = atoi(tmpint);
      PG_GET_STR_COL(fqdn, 0, "fqdn");
      PG_GET_STR_COL(dsn, 0, "dsn");
      PG_GET_STR_COL(new_remote_cn, 0, "remote_cn");
      fqdn = fqdn ? strdup(fqdn) : NULL;
      dsn = dsn ? strdup(dsn) : NULL;
      new_remote_cn = new_remote_cn ? strdup(new_remote_cn) : NULL;
      PQclear(d->res);
    }
   bad_row:
    free_params((ds_single_detail *)d);
    free(d);
    release_conn_q(cq);
    if(row_count != 1) {
      return -1;
    }
    /* Place in cache */
    uuidinfo = calloc(1, sizeof(*uuidinfo));
    uuidinfo->sid = sid;
    uuidinfo->uuid_str = strdup(uuid_str);
    uuidinfo->storagenode_id = storagenode_id;
    uuidinfo->remote_cn = new_remote_cn ? strdup(new_remote_cn) : strdup(remote_cn);
    mtev_hash_store(&uuid_to_info_cache,
                    uuidinfo->uuid_str, strlen(uuidinfo->uuid_str), uuidinfo);
    /* Also, we may have just witnessed a new storage node, store it */
    if(storagenode_id) {
      int needs_free = 0;
      info = calloc(1, sizeof(*info));
      info->storagenode_id = storagenode_id;
      info->dsn = dsn ? strdup(dsn) : NULL;
      info->fqdn = fqdn ? strdup(fqdn) : NULL;
      pthread_mutex_lock(&storagenode_to_info_cache_lock);
      if(!mtev_hash_retrieve(&storagenode_to_info_cache,
                             (void *)&storagenode_id, sizeof(int), &vinfo)) {
        /* hack to save memory -- we *never* remove from these caches,
           so we can use the same fqdn value in the above cache for the key
           in the cache below -- (no strdup) */
        mtev_hash_store(&storagenode_to_info_cache,
                        (void *)&info->storagenode_id, sizeof(int), info);
      }
      else needs_free = 1;
      pthread_mutex_unlock(&storagenode_to_info_cache_lock);
      if(needs_free) {
        if(info->dsn) free(info->dsn);
        if(info->fqdn) free(info->fqdn);
        free(info);
        info = NULL;
      }
    }
  }
  else
    uuidinfo = vuuidinfo;

  if(uuidinfo && uuidinfo->storagenode_id) {
    if((!dsn && dsn_out) || (!fqdn && fqdn_out)) {
      /* we don't have dsn and we actually want it */
      pthread_mutex_lock(&storagenode_to_info_cache_lock);
      if(mtev_hash_retrieve(&storagenode_to_info_cache,
                            (void *)&uuidinfo->storagenode_id, sizeof(int),
                            &vinfo))
        info = vinfo;
      pthread_mutex_unlock(&storagenode_to_info_cache_lock);
    }
  }

  if(fqdn_out) *fqdn_out = info ? info->fqdn : NULL;
  if(dsn_out) *dsn_out = info ? info->dsn : NULL;
  mtevAssert(uuidinfo);
  if(remote_cn_out) *remote_cn_out = uuidinfo->remote_cn;
  if(storagenode_id_out) *storagenode_id_out = uuidinfo->storagenode_id;
  if(sid_out) *sid_out = uuidinfo->sid;
  if(fqdn) free(fqdn);
  if(dsn) free(dsn);
  if(new_remote_cn) free(new_remote_cn);
  return 0;
}
static int
uuid_to_sid(const char *uuid_str_in, const char *remote_cn) {
  char uuid_str[UUID_STR_LEN+1];
  int sid = 0;
  strlcpy(uuid_str, uuid_str_in, sizeof(uuid_str));
  storage_node_quick_lookup(uuid_str, remote_cn, &sid, NULL, NULL, NULL, NULL);
  return sid;
}

static int
stratcon_ingest_saveconfig() {
  int rv = -1;
  char *buff;
  ds_single_detail _d = { 0 }, *d = &_d;
  conn_q *cq;
  char ipv4_str[32];
  struct in_addr r, l;

  r.s_addr = htonl((4 << 24) | (2 << 16) | (2 << 8) | 1);
  memset(&l, 0, sizeof(l));
  mtev_getip_ipv4(r, &l);
  /* Ignore the error.. what are we going to do anyway */
  if(inet_ntop(AF_INET, &l, ipv4_str, sizeof(ipv4_str)) == NULL)
    strlcpy(ipv4_str, "0.0.0.0", sizeof(ipv4_str));

  cq = get_conn_q_for_metanode();

  if(stratcon_database_connect(cq) == 0) {
    char time_as_str[20];
    size_t len;
    buff = mtev_conf_xml_in_mem(&len);
    if(!buff) goto bad_row;

    snprintf(time_as_str, sizeof(time_as_str), "%lu", (long unsigned int)time(NULL));
    DECLARE_PARAM_STR(ipv4_str, strlen(ipv4_str));
    DECLARE_PARAM_STR("", 0);
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

static int
stratcon_ingest_launch_file_ingestion(const char *path,
                                      const char *remote_str,
                                      const char *remote_cn,
                                      const char *id_str,
                                      const mtev_boolean sweeping) {
  pg_interim_journal_t *ij;
  char pgfile[PATH_MAX];
  eventer_t ingest;

  (void)sweeping;

  if(strcmp(path + strlen(path) - 3, ".pg")) {
    snprintf(pgfile, sizeof(pgfile), "%s.pg", path);
    if(link(path, pgfile) < 0 && errno != EEXIST) {
      mtevL(noit_error, "cannot link journal %s: %s\n", path, strerror(errno));
      return -1;
    }
  }
  else
    strlcpy(pgfile, path, sizeof(pgfile));
  ij = calloc(1, sizeof(*ij));
  ij->fd = open(pgfile, O_RDONLY);
  if(ij->fd < 0) {
    mtevL(noit_error, "cannot open journal '%s': %s\n",
          pgfile, strerror(errno));
    free(ij);
    return -1;
  }
  close(ij->fd);
  ij->fd = -1;
  ij->filename = strdup(pgfile);
  ij->remote_str = strdup(remote_str);
  ij->remote_cn = strdup(remote_cn);
  ij->storagenode_id = atoi(id_str);
  ij->cpool = get_conn_pool_for_remote(ij->remote_str, ij->remote_cn,
                                       ij->fqdn);
  mtevL(noit_debug, "ingesting payload: %s\n", ij->filename);
  ingest = eventer_alloc_asynch(stratcon_ingest_asynch_execute, ij);
  eventer_add_asynch(ij->cpool->jobq, ingest);
  return 0;
}

int
stratcon_ingest_all_storagenode_info() {
  int i, cnt = 0;
  ds_single_detail _d = { 0 }, *d = &_d;
  conn_q *cq;
  cq = get_conn_q_for_metanode();

  while(stratcon_database_connect(cq)) {
    mtevL(noit_error, "Error connecting to database\n");
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

    if(!mtev_hash_retrieve(&storagenode_to_info_cache,
                           (void *)&storagenode_id, sizeof(int), &vinfo)) {
      storagenode_info *info;
      info = calloc(1, sizeof(*info));
      info->storagenode_id = storagenode_id;
      info->fqdn = fqdn ? strdup(fqdn) : NULL;
      info->dsn = dsn ? strdup(dsn) : NULL;
      mtev_hash_store(&storagenode_to_info_cache,
                      (void *)&info->storagenode_id, sizeof(int), info);
      mtevL(ds_deb, "SN[%d] -> { fqdn: '%s', dsn: '%s' }\n",
            info->storagenode_id,
            info->fqdn ? info->fqdn : "", info->dsn ? info->dsn : "");
    }
    mtev_watchdog_child_heartbeat();
  }
  PQclear(d->res);
 bad_row:
  free_params(d);

  release_conn_q(cq);
  mtevL(noit_error, "Loaded %d storage nodes\n", cnt);
  return cnt;
}
int
stratcon_ingest_all_check_info() {
  int i, cnt, loaded = 0;
  ds_single_detail _d = { 0 }, *d = &_d;
  conn_q *cq;
  cq = get_conn_q_for_metanode();

  while(stratcon_database_connect(cq)) {
    mtevL(noit_error, "Error connecting to database\n");
    sleep(1);
  }

  GET_QUERY(check_mapall);
  PG_EXEC(check_mapall);
  cnt = PQntuples(d->res);
  for(i=0; i<cnt; i++) {
    void *vinfo;
    char *tmpint, *fqdn, *dsn, *uuid_str, *remote_cn;
    int sid, storagenode_id;
    uuid_info *uuidinfo;
    PG_GET_STR_COL(uuid_str, i, "id");
    if(!uuid_str) continue;
    PG_GET_STR_COL(tmpint, i, "sid");
    if(!tmpint) continue;
    sid = atoi(tmpint);
    PG_GET_STR_COL(fqdn, i, "fqdn");
    PG_GET_STR_COL(dsn, i, "dsn");
    PG_GET_STR_COL(remote_cn, i, "remote_cn");
    PG_GET_STR_COL(tmpint, i, "storage_node_id");
    storagenode_id = tmpint ? atoi(tmpint) : 0;

    uuidinfo = calloc(1, sizeof(*uuidinfo));
    uuidinfo->uuid_str = strdup(uuid_str);
    uuidinfo->remote_cn = strdup(remote_cn);
    uuidinfo->storagenode_id = storagenode_id;
    uuidinfo->sid = sid;
    mtev_hash_store(&uuid_to_info_cache,
                    uuidinfo->uuid_str, strlen(uuidinfo->uuid_str), uuidinfo);
    mtevL(ds_deb, "CHECK[%s] -> { remote_cn: '%s', storagenode_id: '%d' }\n",
          uuidinfo->uuid_str, uuidinfo->remote_cn, uuidinfo->storagenode_id);
    loaded++;
    if(!mtev_hash_retrieve(&storagenode_to_info_cache,
                           (void *)&storagenode_id, sizeof(int), &vinfo)) {
      storagenode_info *info;
      info = calloc(1, sizeof(*info));
      info->storagenode_id = storagenode_id;
      info->fqdn = fqdn ? strdup(fqdn) : NULL;
      info->dsn = dsn ? strdup(dsn) : NULL;
      mtev_hash_store(&storagenode_to_info_cache,
                      (void *)&info->storagenode_id, sizeof(int), info);
      mtevL(ds_deb, "SN[%d] -> { fqdn: '%s', dsn: '%s' }\n",
            info->storagenode_id,
            info->fqdn ? info->fqdn : "", info->dsn ? info->dsn : "");
    }
    mtev_watchdog_child_heartbeat();
  }
  PQclear(d->res);
 bad_row:
  free_params(d);

  release_conn_q(cq);
  mtevL(noit_error, "Loaded %d uuid -> (sid,storage_node_id) mappings\n", loaded);
  return loaded;
}

static char *
stratcon_get_noit_config(const char *cn) {
  ds_single_detail *d;
  int row_count = 0;
  const char *xml = NULL;
  char *xmlcopy = NULL;
  conn_q *cq = NULL;

  d = calloc(1, sizeof(*d));
  GET_QUERY(config_get);
  cq = get_conn_q_for_metanode();
  if(!cq) goto bad_row;

  DECLARE_PARAM_STR(cn, cn ? strlen(cn) : 0);
  PG_EXEC(config_get);
  row_count = PQntuples(d->res);
  if(row_count == 1) PG_GET_STR_COL(xml, 0, "config");

  if(xml) xmlcopy = strdup(xml);

 bad_row:
  free_params((ds_single_detail *)d);
  d->nparams = 0;
  if(cq) release_conn_q(cq);
  free(d);

  return xmlcopy;
}

static ingestor_api_t postgres_ingestor_api = {
  .launch_file_ingestion = stratcon_ingest_launch_file_ingestion,
  .iep_check_preload = stratcon_ingest_iep_check_preload,
  .storage_node_lookup = storage_node_quick_lookup,
  .submit_realtime_lookup = stratcon_ingestor_submit_lookup,
  .get_noit_config = stratcon_get_noit_config,
  .save_config = stratcon_ingest_saveconfig
};

static int postgres_ingestor_config(mtev_dso_generic_t *self, mtev_hash_table *o) {
  return 0;
}
static int postgres_ingestor_onload(mtev_image_t *self) {
  return 0;
}
static int is_postgres_ingestor_file(const char *file) {
  mtev_watchdog_child_heartbeat();
  return (strlen(file) == 19 && !strcmp(file + 16, ".pg"));
}
static int postgres_ingestor_init(mtev_dso_generic_t *self) {
  stratcon_datastore_core_init();
  mtev_hash_init(&ds_conns);
  mtev_hash_init(&uuid_to_info_cache);
  mtev_hash_init(&storagenode_to_info_cache);
  pthread_mutex_init(&ds_conns_lock, NULL);
  pthread_mutex_init(&storagenode_to_info_cache_lock, NULL);
  ds_err = mtev_log_stream_find("error/datastore");
  ds_deb = mtev_log_stream_find("debug/datastore");
  ds_pool_deb = mtev_log_stream_find("debug/datastore_pool");
  ingest_err = mtev_log_stream_find("error/ingest");
  if(!ds_err) ds_err = noit_error;
  if(!ingest_err) ingest_err = noit_error;
  if(!mtev_conf_get_string(NULL, "/stratcon/database/journal/path",
                           &basejpath)) {
    mtevL(noit_error, "/stratcon/database/journal/path is unspecified\n");
    exit(-1);
  }
  stratcon_ingest_all_check_info();
  stratcon_ingest_all_storagenode_info();
  stratcon_ingest_sweep_journals(basejpath, is_postgres_ingestor_file,
                                 stratcon_ingest_launch_file_ingestion);
  return stratcon_datastore_set_ingestor(&postgres_ingestor_api);
}

mtev_dso_generic_t postgres_ingestor = { 
  {
    .magic = MTEV_GENERIC_MAGIC,
    .version = MTEV_GENERIC_ABI_VERSION,
    .name = "postgres_ingestor",
    .description = "postgres drive for data ingestion",
    .xml_description = postgres_ingestor_xml_description,
    .onload = postgres_ingestor_onload,
  },  
  postgres_ingestor_config,
  postgres_ingestor_init
};
