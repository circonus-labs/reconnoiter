/*
 * Copyright (c) 2007-2009, OmniTI Computer Consulting, Inc.
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
#include <zlib.h>
#include <errno.h>

#include <eventer/eventer.h>
#include <mtev_conf.h>
#include <mtev_rest.h>
#include <mtev_log.h>
#include <mtev_b64.h>
#include <mtev_str.h>
#include <mtev_mkdir.h>
#include <mtev_getip.h>

#include "noit_mtev_bridge.h"
#include "stratcon_datastore.h"
#include "stratcon_ingest.h"
#include "stratcon_realtime_http.h"
#include "stratcon_iep.h"
#include "noit_check.h"

static eventer_jobq_t *push_jobq = NULL;
static mtev_log_stream_t ds_err = NULL;
static mtev_log_stream_t ds_deb = NULL;
static mtev_log_stream_t ds_pool_deb = NULL;
static mtev_log_stream_t ingest_err = NULL;
static char *basejpath = NULL;

static ingestor_api_t *ingestor = NULL;
typedef struct ingest_chain_t {
  ingestor_api_t *ingestor;
  struct ingest_chain_t *next;
} ingest_chain_t;

static ingest_chain_t *ingestor_chain;

static int ds_system_enabled = 1;
int stratcon_datastore_get_enabled() { return ds_system_enabled; }
void stratcon_datastore_set_enabled(int n) { ds_system_enabled = n; }
int stratcon_datastore_set_ingestor(ingestor_api_t *ni) {
  ingest_chain_t *p, *i = calloc(1, sizeof(*i));
  i->ingestor = ni;
  if(!ingestor_chain) ingestor_chain = i;
  else {
    for(p = ingestor_chain; p->next; p = p->next);
    p->next = i;
  }
  ingestor = ingestor_chain->ingestor;
  return 0;
}

static struct datastore_onlooker_list {
  void (*dispatch)(stratcon_datastore_op_t, struct sockaddr *,
                   const char *, void *);
  struct datastore_onlooker_list *next;
} *onlookers = NULL;

typedef struct {
  mtev_hash_table *ws;
  eventer_t completion;
} syncset_t;

static mtev_hash_table working_sets;

static void
interim_journal_free(void *vij) {
  interim_journal_t *ij = vij;
  if(ij->filename) free(ij->filename);
  if(ij->remote_str) free(ij->remote_str);
  if(ij->remote_cn) free(ij->remote_cn);
  if(ij->fqdn) free(ij->fqdn);
  free(ij);
}

static int
stratcon_ingest(const char *fullpath, const char *remote_str,
                const char *remote_cn, const char *id_str,
                const mtev_boolean sweeping) {
  ingest_chain_t *ic;
  int err = 0;
  for(ic = ingestor_chain; ic; ic = ic->next)
    if(ic->ingestor->launch_file_ingestion(fullpath, remote_str,
                                           remote_cn, id_str,
                                           sweeping))
      err = -1;
  if(err == 0) {
    unlink(fullpath);
  }
  return err;
}
static int
stratcon_datastore_journal_sync(eventer_t e, int mask, void *closure,
                                struct timeval *now) {
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  const char *k;
  int klen;
  void *vij;
  interim_journal_t *ij;
  syncset_t *syncset = closure;

  if((mask & EVENTER_ASYNCH) == EVENTER_ASYNCH) {
    if(syncset->completion) {
      eventer_add(syncset->completion);
      eventer_trigger(syncset->completion, EVENTER_READ | EVENTER_WRITE);
    }
    free(syncset);
    return 0;
  }
  if(!((mask & EVENTER_ASYNCH_WORK) == EVENTER_ASYNCH_WORK)) return 0;

  mtevL(ds_deb, "Syncing journal sets...\n");
  if (syncset->ws) {
    while(mtev_hash_next(syncset->ws, &iter, &k, &klen, &vij)) {
      char tmppath[PATH_MAX], id_str[32];
      int suffix_idx;
      ij = vij;
      mtevL(ds_deb, "Syncing journal set [%s,%s,%s]\n",
            ij->remote_str, ij->remote_cn, ij->fqdn);
      strlcpy(tmppath, ij->filename, sizeof(tmppath));
      suffix_idx = strlen(ij->filename) - 4; /* . t m p */
      ij->filename[suffix_idx] = '\0';
      if(rename(tmppath, ij->filename) != 0) {
        if(errno == EEXIST) {
          unlink(ij->filename);
          if(rename(tmppath, ij->filename) != 0) goto rename_failed;
        }
        else {
         rename_failed:
          mtevL(noit_error, "rename failed(%s): (%s->%s)\n", strerror(errno),
                tmppath, ij->filename);
          exit(-1);
        }
      }
      if(ij->fd >= 0) {
        fsync(ij->fd);
        close(ij->fd);
      }
      ij->fd = -1;
      snprintf(id_str, sizeof(id_str), "%d", ij->storagenode_id);
      stratcon_ingest(ij->filename, ij->remote_str,
                      ij->remote_cn, id_str,
                      mtev_false);
    }
    mtev_hash_destroy(syncset->ws, free, interim_journal_free);
    free(syncset->ws);
  }
  else {
    mtevL(noit_error, "attempted to sync non-existing working set\n");
  }

  return 0;
}
static interim_journal_t *
interim_journal_get(struct sockaddr *remote, const char *remote_cn_in,
                    int storagenode_id, const char *fqdn_in) {
  void *vhash, *vij;
  mtev_hash_table *working_set;
  interim_journal_t *ij;
  struct timeval now;
  char jpath[PATH_MAX];
  char remote_str[128];
  const char *remote_cn = remote_cn_in ? remote_cn_in : "default";
  const char *fqdn = fqdn_in ? fqdn_in : "default";

  mtev_convert_sockaddr_to_buff(remote_str, sizeof(remote_str), remote);
  if(!*remote_str) strlcpy(remote_str, "default", sizeof(remote_str));

  /* Lookup the working set */
  if(!mtev_hash_retrieve(&working_sets, remote_cn, strlen(remote_cn), &vhash)) {
    working_set = calloc(1, sizeof(*working_set));
    mtev_hash_init(working_set);
    mtev_hash_store(&working_sets, strdup(remote_cn), strlen(remote_cn),
                    working_set);
  }
  else
    working_set = vhash;

  /* Lookup the interim journal within the working set */
  if(!mtev_hash_retrieve(working_set, fqdn, strlen(fqdn), &vij)) {
    ij = calloc(1, sizeof(*ij));
    mtev_gettimeofday(&now, NULL);
    snprintf(jpath, sizeof(jpath), "%s/%s/%s/%d/%08x%08x.tmp",
             basejpath, remote_str, remote_cn, storagenode_id,
             (unsigned int)now.tv_sec, (unsigned int)now.tv_usec);
    ij->remote_str = strdup(remote_str);
    ij->remote_cn = strdup(remote_cn);
    ij->fqdn = fqdn_in ? strdup(fqdn_in) : NULL;
    ij->storagenode_id = storagenode_id;
    ij->filename = strdup(jpath);
    ij->fd = open(ij->filename, O_RDWR | O_CREAT | O_EXCL, 0640);
    if(ij->fd < 0 && errno == ENOENT) {
      if(mkdir_for_file(ij->filename, 0750)) {
        mtevL(noit_error, "Failed to create dir for '%s': %s\n",
              ij->filename, strerror(errno));
        exit(-1);
      }
      ij->fd = open(ij->filename, O_RDWR | O_CREAT | O_EXCL, 0640);
    }
    if(ij->fd < 0 && errno == EEXIST) {
      /* This can only occur if we crash after before checkpointing */
      unlink(ij->filename);
      ij->fd = open(ij->filename, O_RDWR | O_CREAT | O_EXCL, 0640);
    }
    if(ij->fd < 0) {
      mtevL(noit_error, "Failed to open interim journal '%s': %s\n",
            ij->filename, strerror(errno));
      exit(-1);
    }
    mtev_hash_store(working_set, strdup(fqdn), strlen(fqdn), ij);
  }
  else
    ij = vij;

  return ij;
}
static void
stratcon_datastore_journal(struct sockaddr *remote,
                           const char *remote_cn, char *line) {
  interim_journal_t *ij = NULL;
  char uuid_str[UUID_STR_LEN+1], *cp1, *cp2;
  char rtype[256];
  const char *fqdn = NULL, *dsn = NULL;
  int storagenode_id = 0;
  uuid_t checkid;
  if(!line) {
    mtevL(noit_error, "Error: Line not found for %s in stratcon_datastore_journal\n", remote_cn);
    return;
  }
  cp1 = strchr(line, '\t');
  *rtype = '\0';
  if(cp1 && cp1 - line < sizeof(rtype) - 1) {
    memcpy(rtype, line, cp1 - line);
    rtype[cp1 - line] = '\0';
  }
  /* if it is a UUID based thing, find the storage node */
  switch(*rtype) {
    case 'C':
    case 'S':
    case 'M':
    case 'D':
    case 'B':
    case 'H':
      if((cp1 = strchr(cp1+1, '\t')) != NULL &&
         (cp2 = strchr(cp1+1, '\t')) != NULL &&
         (cp2-cp1 >= UUID_STR_LEN)) {
        strlcpy(uuid_str, cp2 - UUID_STR_LEN, sizeof(uuid_str));
        if(!mtev_uuid_parse(uuid_str, checkid)) {
          ingestor->storage_node_lookup(uuid_str, remote_cn, NULL,
                                        &storagenode_id, NULL,
                                        &fqdn, &dsn);
          ij = interim_journal_get(remote, remote_cn, storagenode_id, fqdn);
        }
      }
      break;
    case 'n':
      ij = interim_journal_get(remote,remote_cn,0,NULL);
      break;
    default:
      mtevL(noit_error, "Error: Line has bad type for %s in stratcon_datastore_journal (%s)\n", remote_cn, line);
      break;
  }
  if(!ij) {
    mtevL(ingest_err, "%d\t%s\n", storagenode_id, line);
  }
  else {
    int len;
    len = write(ij->fd, line, strlen(line));
    if(len < 0) {
      mtevL(noit_error, "write to %s failed: %s\n",
            ij->filename, strerror(errno));
    }
  }
  free(line);
  return;
}
static mtev_hash_table *
stratcon_datastore_journal_remove(struct sockaddr *remote,
                                  const char *remote_cn) {
  void *vhash = NULL;
  if(mtev_hash_retrieve(&working_sets, remote_cn, strlen(remote_cn), &vhash)) {
    /* pluck it out */
    mtev_hash_delete(&working_sets, remote_cn, strlen(remote_cn), free, NULL);
  }
  else {
    mtevL(noit_error, "attempted checkpoint on non-existing workingset: '%s'\n",
          remote_cn);
  }
  return vhash;
}
void
stratcon_datastore_push(stratcon_datastore_op_t op,
                        struct sockaddr *remote,
                        const char *remote_cn, void *operand,
                        eventer_t completion) {
  syncset_t *syncset;
  eventer_t e;
  struct realtime_tracker *rt;
  struct datastore_onlooker_list *nnode;

  for(nnode = onlookers; nnode; nnode = nnode->next)
    nnode->dispatch(op,remote,remote_cn,operand);

  switch(op) {
    case DS_OP_INSERT:
      stratcon_datastore_journal(remote, remote_cn, (char *)operand);
      break;
    case DS_OP_CHKPT:
      syncset = calloc(1, sizeof(*syncset));
      syncset->ws = stratcon_datastore_journal_remove(remote, remote_cn);
      syncset->completion = completion;
      e = eventer_alloc_asynch(stratcon_datastore_journal_sync, syncset);
      eventer_add_asynch(push_jobq, e);
      break;
    case DS_OP_FIND_COMPLETE:
      rt = operand;
      ingestor->submit_realtime_lookup(rt, completion);
      break;
  }
}

void
stratcon_datastore_register_onlooker(void (*f)(stratcon_datastore_op_t,
                                               struct sockaddr *,
                                               const char *, void *)) {
  struct datastore_onlooker_list *nnode;
  nnode = calloc(1, sizeof(*nnode));
  nnode->dispatch = f;
  do {
    nnode->next = ck_pr_load_ptr(&onlookers);
  } while(!ck_pr_cas_ptr(&onlookers, nnode->next, nnode));
}

static int
rest_get_noit_config(mtev_http_rest_closure_t *restc,
                     int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  char *xml = NULL;

  if(npats != 0) {
    mtev_http_response_server_error(ctx, "text/xml");
    mtev_http_response_end(ctx);
    return 0;
  }

  xml = ingestor->get_noit_config(restc->remote_cn);

  if(xml == NULL) {
    char buff[1024];
    snprintf(buff, sizeof(buff), "<error><remote_cn>%s</remote_cn>"
                                 "<row_count>%d</row_count></error>\n",
             restc->remote_cn, 0);
    mtev_http_response_append(ctx, buff, strlen(buff));
    mtev_http_response_not_found(ctx, "text/xml");
  }
  else {
    mtev_http_response_append(ctx, xml, strlen(xml));
    mtev_http_response_ok(ctx, "text/xml");
  }

  if(xml) free(xml);
  mtev_http_response_end(ctx);
  return 0;
}

void
stratcon_datastore_iep_check_preload() {
  if(!ingestor) {
    mtevL(noit_error, "No ingestor!\n");
    exit(2);
  }
  ingestor->iep_check_preload();
}

int
stratcon_datastore_saveconfig(void *unused) {
  if(!ingestor) {
    mtevL(noit_error, "No ingestor!\n");
    exit(2);
  }
  return ingestor->save_config();
}

static int is_raw_ingestion_file(const char *file) {
  return (strlen(file) == 16);
}
static int should_unlink_invalid_raw_ingestion_file(const char *file) {
  return ((strlen(file) == 20) && (!strncmp(file+16, ".tmp", 4)));
}

void
stratcon_datastore_core_init() {
  static int initialized = 0;
  if(initialized) return;
  initialized = 1;
  ds_err = mtev_log_stream_find("error/datastore");
  ds_deb = mtev_log_stream_find("debug/datastore");
  ds_pool_deb = mtev_log_stream_find("debug/datastore_pool");
  ingest_err = mtev_log_stream_find("error/ingest");
  if(!ds_err) ds_err = noit_error;
  if(!ingest_err) ingest_err = noit_error;
  if(!mtev_conf_get_string(MTEV_CONF_ROOT, "//database/journal/path",
                           &basejpath)) {
    mtevL(noit_error, "//database/journal/path is unspecified\n");
    exit(-1);
  }
}
void
stratcon_datastore_init() {
  static int initialized = 0;
  if(initialized) return;
  initialized = 1;
  int concurrency = 32;

  push_jobq = eventer_jobq_create("stratcon_datastore_push_jobq");
  mtev_conf_get_int32(MTEV_CONF_ROOT, "//pools/stratcon_datastore_push_jobq/@concurrency", &concurrency);
  eventer_jobq_set_concurrency(push_jobq, concurrency);

  stratcon_datastore_core_init();

  stratcon_ingest_sweep_journals(basejpath,
                                 is_raw_ingestion_file,
                                 should_unlink_invalid_raw_ingestion_file,
                                 stratcon_ingest);

  mtevAssert(mtev_http_rest_register_auth(
    "GET", "/noits/", "^config$", rest_get_noit_config,
             mtev_http_rest_client_cert_auth
  ) == 0);
}
void 
stratcon_datastore_init_globals(void) {
  mtev_hash_init(&working_sets);
}

