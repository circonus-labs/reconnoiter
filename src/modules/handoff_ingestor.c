/*
 * Copyright (c) 2011, OmniTI Computer Consulting, Inc.
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
#include "noit_module.h"
#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "utils/noit_b64.h"
#include "utils/noit_str.h"
#include "utils/noit_mkdir.h"
#include "utils/noit_getip.h"
#include "stratcon_datastore.h"
#include "stratcon_realtime_http.h"
#include "stratcon_iep.h"
#include "noit_conf.h"
#include "noit_check.h"
#include "noit_rest.h"
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <assert.h>
#include <errno.h>
#include "handoff_ingestor.xmlh"

static noit_http_session_ctx *the_one_and_only = NULL;
static noit_log_stream_t ds_err = NULL;
static noit_log_stream_t ds_deb = NULL;
static noit_log_stream_t ingest_err = NULL;

static int storage_node_quick_lookup(const char *uuid_str,
                                     const char *remote_cn,
                                     int *sid_out, int *storagenode_id_out,
                                     const char **remote_cn_out,
                                     const char **fqdn_out,
                                     const char **dsn_out);

static char *basejpath = NULL;
static noit_hash_table uuid_map = NOIT_HASH_EMPTY;

static void
stratcon_ingest_iep_check_preload() {
  noitL(noit_debug, "iep_preload is a noop in handoff mode\n");
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

    noitL(noit_debug, "stratcon_ingest_find <- (%d, %s) @ %s\n",
          node->sid, remote_cn ? remote_cn : "(null)", dsn ? dsn : "(null)");

    if(stratcon_find_noit_ip_by_cn(remote_cn,
                                   remote_ip, sizeof(remote_ip)) == 0) {
      node->noit = strdup(remote_ip);
      noitL(noit_debug, "lookup(cache): %s -> %s\n", remote_cn, node->noit);
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
  void *vstr;
  const char *actual_remote_cn = NULL;
  if(remote_cn) actual_remote_cn = remote_cn;
  uuid_t id;
  uuid_parse((char *)uuid_str, id);
  if(noit_hash_retrieve(&uuid_map, (const char *)id, UUID_SIZE, &vstr)) {
    char *str = (char *)vstr;
    if(remote_cn && strcmp(str, remote_cn)) {
      /* replace with new remote */
      void *key = malloc(UUID_SIZE);
      memcpy(key, id, UUID_SIZE);
      actual_remote_cn = strdup(remote_cn);
      noit_hash_replace(&uuid_map, key, UUID_SIZE, (void *)actual_remote_cn,
                        free, free);
    }
  }
  else if(remote_cn) {
    void *key = malloc(UUID_SIZE);
    memcpy(key, id, UUID_SIZE);
    noit_hash_store(&uuid_map, key, UUID_SIZE, strdup(remote_cn));
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
  noit_getip_ipv4(r, &l);
  /* Ignore the error.. what are we going to do anyway */
  if(inet_ntop(AF_INET, &l, ipv4_str, sizeof(ipv4_str)) == NULL)
    strlcpy(ipv4_str, "0.0.0.0", sizeof(ipv4_str));

  buff = noit_conf_xml_in_mem(&len);
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
                                      const char *id_str) {
  char msg[PATH_MAX + 7], hfile[PATH_MAX]; /*file:\r\n*/
  if(strcmp(path + strlen(path) - 2, ".h")) {
    snprintf(hfile, sizeof(hfile), "%s.h", path);
    if(link(path, hfile) < 0 && errno != EEXIST) {
      noitL(noit_error, "cannot link journal %s: %s\n", path, strerror(errno));
      return -1;
    }
  }
  else
    strlcpy(hfile, path, sizeof(hfile));

  noitL(noit_debug, " handoff -> %s\n", hfile);
  if(the_one_and_only) {
    noit_http_session_ctx *ctx = the_one_and_only;
    snprintf(msg, sizeof(msg), "file:%s\r\n", hfile);
    if(noit_http_response_append(ctx,msg,strlen(msg)) == noit_false ||
       noit_http_response_flush(ctx, noit_false) == noit_false) {
      noitL(noit_error, "handoff endpoint disconnected\n");
      the_one_and_only = NULL;
    }
  }
  return 0;
}

static int
handoff_request_dispatcher(noit_http_session_ctx *ctx) {
  char *hello = "message:hello\r\n";
  if(the_one_and_only) {
    hello = "message:already connected\r\n";
    noit_http_response_server_error(ctx, "text/plain");
    noit_http_response_append(ctx, hello, strlen(hello));
    noit_http_response_end(ctx);
    return 0;
  }
  the_one_and_only = ctx;
  noit_http_response_status_set(ctx, 200, "OK");
  noit_http_response_option_set(ctx, NOIT_HTTP_CHUNKED);
  noit_http_response_header_set(ctx, "Content-Type", "text/plain");
  noit_http_response_append(ctx, hello, strlen(hello));
  noit_http_response_flush(ctx, noit_false);
  return EVENTER_EXCEPTION;
}

static int
handoff_http_handler(eventer_t e, int mask, void *closure,
                     struct timeval *now) {
  int done = 0, rv;
  acceptor_closure_t *ac = closure;
  noit_http_session_ctx *http_ctx = ac->service_ctx;
  rv = noit_http_session_drive(e, mask, http_ctx, now, &done);
  if(done) {
    the_one_and_only = NULL;
    acceptor_closure_free(ac);
  }
  return rv;
}

static int
handoff_stream(noit_http_rest_closure_t *restc, int npats, char **pats) {
  noit_http_session_ctx *ctx = restc->http_ctx;
  noit_http_connection *conn = noit_http_session_connection(ctx);
  eventer_t e;
  acceptor_closure_t *ac = restc->ac;

  if(ac->service_ctx_free)
    ac->service_ctx_free(ac->service_ctx);
  ac->service_ctx = ctx;
  ac->service_ctx_free = noit_http_ctx_acceptor_free;

  e = noit_http_connection_event(conn);
  e->callback = handoff_http_handler;
  noit_http_session_set_dispatcher(ctx, handoff_request_dispatcher, NULL);
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

static int handoff_ingestor_config(noit_module_generic_t *self, noit_hash_table *o) {
  return 0;
}
static int handoff_ingestor_onload(noit_image_t *self) {
  return 0;
}
static int handoff_ingestor_init(noit_module_generic_t *self) {
  ds_err = noit_log_stream_find("error/datastore");
  ds_deb = noit_log_stream_find("debug/datastore");
  ingest_err = noit_log_stream_find("error/ingest");
  if(!ds_err) ds_err = noit_error;
  if(!ingest_err) ingest_err = noit_error;
  if(!noit_conf_get_string(NULL, "/stratcon/database/journal/path",
                           &basejpath)) {
    noitL(noit_error, "/stratcon/database/journal/path is unspecified\n");
    exit(-1);
  }
  noitL(noit_error, "registering /handoff/journals REST endpoint\n");
  assert(noit_http_rest_register_auth(
    "GET", "/handoff/", "^journals$", handoff_stream,
    noit_http_rest_client_cert_auth
  ) == 0);
  return stratcon_datastore_set_ingestor(&handoff_ingestor_api);
}

noit_module_generic_t handoff_ingestor = {
  {
    NOIT_GENERIC_MAGIC,
    NOIT_GENERIC_ABI_VERSION,
    "handoff_ingestor",
    "data ingestion that just hands off to another process",
    handoff_ingestor_xml_description,
    handoff_ingestor_onload,
  },
  handoff_ingestor_config,
  handoff_ingestor_init
};
