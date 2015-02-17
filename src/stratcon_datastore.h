/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
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

#ifndef _NOIT_STRATCON_DATASTORE_H
#define _NOIT_STRATCON_DATASTORE_H

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "utils/noit_hash.h"
#include "stratcon_realtime_http.h"

#include <sys/types.h>
#include <sys/socket.h>

typedef struct {
  int (*launch_file_ingestion)(const char *file, const char *ip,
                               const char *cn, const char *store);
  void (*iep_check_preload)();
  int (*storage_node_lookup)(const char *uuid_str, const char *remote_cn,
                             int *sid_out, int *storagenode_id_out,
                             const char **remote_cn_out,
                             const char **fqdn_out, const char **dsn_out);
  void (*submit_realtime_lookup)(struct realtime_tracker *rt,
                                 eventer_t completion);
  char *(*get_noit_config)(const char *cn);
  int (*save_config)();
} ingestor_api_t;

API_EXPORT(int) stratcon_datastore_set_ingestor(ingestor_api_t *ni);

typedef struct {
  char *remote_str;
  char *remote_cn;
  char *fqdn;
  int storagenode_id;
  int fd; 
  char *filename;
} interim_journal_t;

typedef enum {
 DS_OP_INSERT = 1,
 DS_OP_CHKPT = 2,
 DS_OP_FIND_COMPLETE = 3
} stratcon_datastore_op_t;

API_EXPORT(void)
  stratcon_datastore_push(stratcon_datastore_op_t,
                          struct sockaddr *, const char *, void *, eventer_t);

API_EXPORT(void)
  stratcon_datastore_register_onlooker(void (*f)(stratcon_datastore_op_t,
                                                 struct sockaddr *,
                                                 const char *, void *));

API_EXPORT(void)
  stratcon_datastore_core_init();

API_EXPORT(void)
  stratcon_datastore_init();

API_EXPORT(int)
  stratcon_datastore_saveconfig(void *unused);

/* Private'ish... called from IEP to populate IEP */
API_EXPORT(void)
  stratcon_datastore_iep_check_preload();

API_EXPORT(int)
  stratcon_datastore_get_enabled();

API_EXPORT(void)
  stratcon_datastore_set_enabled(int);

#endif
