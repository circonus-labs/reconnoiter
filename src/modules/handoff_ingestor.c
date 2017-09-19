/*
 * Copyright (c) 2011, OmniTI Computer Consulting, Inc.
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
#include <errno.h>

#include <mtev_dso.h>
#include <eventer/eventer.h>
#include <mtev_log.h>
#include <mtev_b64.h>
#include <mtev_str.h>
#include <mtev_mkdir.h>
#include <mtev_getip.h>
#include <mtev_conf.h>
#include <mtev_rest.h>

#include "stratcon_datastore.h"
#include "stratcon_realtime_http.h"
#include "stratcon_iep.h"

#include "handoff_ingestor.xmlh"

static mtev_http_session_ctx *the_one_and_only = NULL;
static mtev_log_stream_t ds_err = NULL;
static mtev_log_stream_t ds_deb = NULL;
static mtev_log_stream_t ingest_err = NULL;
static pthread_mutex_t http_ctx_lock = PTHREAD_MUTEX_INITIALIZER;

static int storage_node_quick_lookup(const char *uuid_str,
                                     const char *remote_cn,
                                     int *sid_out, int *storagenode_id_out,
                                     const char **remote_cn_out,
                                     const char **fqdn_out,
                                     const char **dsn_out);

static char *basejpath = NULL;
static mtev_hash_table uuid_map;

static void
stratcon_ingest_iep_check_preload() {
  mtevL(mtev_debug, "iep_preload is a noop in handoff mode\n");
}
static void
stratcon_ingestor_submit_lookup(struct realtime_tracker *rt,
                                eventer_t completion) {
  struct realtime_tracker *node;

  for(node = rt; node; node = node->next) {
    char uuid_str[UUID_STR_LEN+1];
    const char *fqdn, *dsn, *remote_cn;
    char remote_ip[32];
    int storagenode_id;

    uuid_unparse_lower(node->checkid, uuid_str);
    if(storage_node_quick_lookup(uuid_str, NULL, &node->sid,
                                 &storagenode_id, &remote_cn, &fqdn, &dsn))
      continue;

    mtevL(mtev_debug, "stratcon_ingest_find <- (%d, %s) @ %s\n",
          node->sid, remote_cn ? remote_cn : "(null)", dsn ? dsn : "(null)");

    if(stratcon_find_noit_ip_by_cn(remote_cn,
                                   remote_ip, sizeof(remote_ip)) == 0) {
      node->noit = strdup(remote_ip);
      mtevL(mtev_debug, "lookup(cache): %s -> %s\n", remote_cn, node->noit);
      continue;
    }
  }
  eventer_add(completion);
}

static int
storage_node_quick_lookup(const char *uuid_str, const char *remote_cn,
                          int *sid_out, int *storagenode_id_out,
                          const char **remote_cn_out,
                          const char **fqdn_out, const char **dsn_out) {
  /* only called from the main thread -- no safety issues */
  uuid_t id;
  void *vstr;
  const char *actual_remote_cn = NULL;
  if(remote_cn) actual_remote_cn = remote_cn;
  if(uuid_parse((char *)uuid_str, id) == 0) {
    if(mtev_hash_retrieve(&uuid_map, (const char *)id, UUID_SIZE, &vstr)) {
      char *str = (char *)vstr;
      if(remote_cn && strcmp(str, remote_cn)) {
        /* replace with new remote */
        void *key = malloc(UUID_SIZE);
        memcpy(key, id, UUID_SIZE);
        actual_remote_cn = strdup(remote_cn);
        mtev_hash_replace(&uuid_map, key, UUID_SIZE, (void *)actual_remote_cn,
                          free, free);
      }
    }
    else if(remote_cn) {
      void *key = malloc(UUID_SIZE);
      memcpy(key, id, UUID_SIZE);
      mtev_hash_store(&uuid_map, key, UUID_SIZE, strdup(remote_cn));
    }
  }
  if(!actual_remote_cn) actual_remote_cn = "[[null]]";

  if(sid_out) *sid_out = 0;
  if(storagenode_id_out) *storagenode_id_out = 0;
  if(remote_cn_out) *remote_cn_out = actual_remote_cn;
  if(fqdn_out) *fqdn_out = "";
  if(dsn_out) *dsn_out = "";
  return 0;
}

static int
stratcon_ingest_saveconfig() {
  int rv = -1;
  char *buff;
  char ipv4_str[32], time_as_str[20];
  struct in_addr r, l;
  size_t len;

  r.s_addr = htonl((4 << 24) | (2 << 16) | (2 << 8) | 1);
  memset(&l, 0, sizeof(l));
  mtev_getip_ipv4(r, &l);
  /* Ignore the error.. what are we going to do anyway */
  if(inet_ntop(AF_INET, &l, ipv4_str, sizeof(ipv4_str)) == NULL)
    strlcpy(ipv4_str, "0.0.0.0", sizeof(ipv4_str));

  buff = mtev_conf_xml_in_mem(&len);
  if(!buff) goto bail;

  snprintf(time_as_str, sizeof(time_as_str), "%lu", (long unsigned int)time(NULL));
#if 0
  /* writev this somwhere */
  (ipv4_str, strlen(ipv4_str));
  ("", 0);
  ("stratcond", 9);
  (time_as_str, strlen(time_as_str));
  (buff, len);
#endif
  rv = 0;
  free(buff);
bail:
  return rv;
}

static int
stratcon_ingest_launch_file_ingestion(const char *path,
                                      const char *remote_str,
                                      const char *remote_cn,
                                      const char *id_str,
                                      const mtev_boolean sweeping) {
  char msg[PATH_MAX + 7], hfile[PATH_MAX]; /*file:\r\n*/

  (void)sweeping;

  if(strcmp(path + strlen(path) - 2, ".h")) {
    snprintf(hfile, sizeof(hfile), "%s.h", path);
    if(link(path, hfile) < 0 && errno != EEXIST) {
      mtevL(mtev_error, "cannot link journal %s: %s\n", path, strerror(errno));
      return -1;
    }
  }
  else
    strlcpy(hfile, path, sizeof(hfile));

  mtevL(mtev_debug, " handoff -> %s\n", hfile);
  pthread_mutex_lock(&http_ctx_lock);
  if(the_one_and_only) {
    mtev_http_session_ctx *ctx = the_one_and_only;
    snprintf(msg, sizeof(msg), "file:%s\r\n", hfile);
    if(mtev_http_response_append(ctx,msg,strlen(msg)) == mtev_false ||
       mtev_http_response_flush(ctx, mtev_false) == mtev_false) {
      mtevL(mtev_error, "handoff endpoint disconnected\n");
      the_one_and_only = NULL;
    }
  }
  pthread_mutex_unlock(&http_ctx_lock);
  return 0;
}

static int
handoff_request_dispatcher(mtev_http_session_ctx *ctx) {
  char *hello = "message:hello\r\n";
  if(the_one_and_only) {
    hello = "message:already connected\r\n";
    mtev_http_response_server_error(ctx, "text/plain");
    mtev_http_response_append(ctx, hello, strlen(hello));
    mtev_http_response_end(ctx);
    return 0;
  }
  pthread_mutex_lock(&http_ctx_lock);
  the_one_and_only = ctx;
  mtev_http_response_status_set(ctx, 200, "OK");
  mtev_http_response_option_set(ctx, MTEV_HTTP_CHUNKED);
  mtev_http_response_header_set(ctx, "Content-Type", "text/plain");
  mtev_http_response_append(ctx, hello, strlen(hello));
  mtev_http_response_flush(ctx, mtev_false);
  pthread_mutex_unlock(&http_ctx_lock);
  return EVENTER_EXCEPTION;
}

static int
handoff_http_handler(eventer_t e, int mask, void *closure,
                     struct timeval *now) {
  int done = 0, rv;
  mtev_acceptor_closure_t *ac = closure;
  mtev_http_session_ctx *http_ctx = mtev_acceptor_closure_ctx(ac);
  rv = mtev_http_session_drive(e, mask, http_ctx, now, &done);
  if(done) {
    pthread_mutex_lock(&http_ctx_lock);
    the_one_and_only = NULL;
    pthread_mutex_unlock(&http_ctx_lock);
    mtev_acceptor_closure_free(ac);
  }
  return rv;
}

static int
handoff_stream(mtev_http_rest_closure_t *restc, int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  mtev_http_connection *conn = mtev_http_session_connection(ctx);
  eventer_t e;
  mtev_acceptor_closure_t *ac = restc->ac;

  mtev_acceptor_closure_ctx_free(ac);
  mtev_acceptor_closure_set_ctx(ac, ctx, mtev_http_ctx_acceptor_free);

  e = mtev_http_connection_event(conn);
  eventer_set_callback(e, handoff_http_handler);
  mtev_http_session_set_dispatcher(ctx, handoff_request_dispatcher, NULL);
  return handoff_request_dispatcher(ctx);
}

static ingestor_api_t handoff_ingestor_api = {
  .launch_file_ingestion = stratcon_ingest_launch_file_ingestion,
  .iep_check_preload = stratcon_ingest_iep_check_preload,
  .storage_node_lookup = storage_node_quick_lookup,
  .submit_realtime_lookup = stratcon_ingestor_submit_lookup,
  .get_noit_config = NULL,
  .save_config = stratcon_ingest_saveconfig
};

static int handoff_ingestor_config(mtev_dso_generic_t *self, mtev_hash_table *o) {
  return 0;
}
static int handoff_ingestor_onload(mtev_image_t *self) {
  return 0;
}
static int handoff_ingestor_init(mtev_dso_generic_t *self) {
  ds_err = mtev_log_stream_find("error/datastore");
  ds_deb = mtev_log_stream_find("debug/datastore");
  ingest_err = mtev_log_stream_find("error/ingest");
  if(!ds_err) ds_err = mtev_error;
  if(!ingest_err) ingest_err = mtev_error;
  mtev_hash_init(&uuid_map);
  if(!mtev_conf_get_string(MTEV_CONF_ROOT, "/stratcon/database/journal/path",
                           &basejpath)) {
    mtevL(mtev_error, "/stratcon/database/journal/path is unspecified\n");
    exit(-1);
  }
  mtevL(mtev_error, "registering /handoff/journals REST endpoint\n");
  mtevAssert(mtev_http_rest_register_auth(
    "GET", "/handoff/", "^journals$", handoff_stream,
    mtev_http_rest_client_cert_auth
  ) == 0);
  return stratcon_datastore_set_ingestor(&handoff_ingestor_api);
}

mtev_dso_generic_t handoff_ingestor = {
  {
    .magic = MTEV_GENERIC_MAGIC,
    .version = MTEV_GENERIC_ABI_VERSION,
    .name = "handoff_ingestor",
    .description = "data ingestion that just hands off to another process",
    .xml_description = handoff_ingestor_xml_description,
    .onload = handoff_ingestor_onload,
  },
  handoff_ingestor_config,
  handoff_ingestor_init
};
