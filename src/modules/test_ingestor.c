/*
 * Copyright (c) 2018, Circonus, Inc. All rights reserved.
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
 *     * Neither the name Circonus, Inc. nor the names of its contributors
 *       may be used to endorse or promote products derived from this
 *       software without specific prior written permission.
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

#include "test_ingestor.xmlh"

static mtev_log_stream_t ds_err = NULL;
static mtev_log_stream_t ds_deb = NULL;

static void
noop_iep_check_preload() {
  mtevL(mtev_debug, "iep_preload is a noop in test mode\n");
}

static int
noop_storage_node_lookup(const char *uuid_str, const char *remote_cn,
                         int *sid_out, int *storagenode_id_out,
                         const char **remote_cn_out,
                         const char **fqdn_out, const char **dsn_out) {
  if(sid_out) *sid_out = 0;
  if(storagenode_id_out) *storagenode_id_out = 0;
  if(remote_cn_out) *remote_cn_out = remote_cn;
  if(fqdn_out) *fqdn_out = "";
  if(dsn_out) *dsn_out = "";
  return 0;
}

static int
noop_saveconfig() {
  return 0;
}

static int
noop_launch(const char *path, const char *remote_str,
            const char *remote_cn, const char *id_str,
            const mtev_boolean sweeping) {
  return 0;
}

static void
noop_submit_lookup(struct realtime_tracker *rt, eventer_t completion) {
  eventer_add(completion);
}

static ingestor_api_t test_ingestor_api = {
  .launch_file_ingestion = noop_launch,
  .iep_check_preload = noop_iep_check_preload,
  .storage_node_lookup = noop_storage_node_lookup,
  .submit_realtime_lookup = noop_submit_lookup,
  .get_noit_config = NULL,
  .save_config = noop_saveconfig
};

static int test_ingestor_config(mtev_dso_generic_t *self, mtev_hash_table *o) {
  return 0;
}
static int test_ingestor_onload(mtev_image_t *self) {
  return 0;
}
static int test_ingestor_init(mtev_dso_generic_t *self) {
  ds_err = mtev_log_stream_find("error/datastore");
  ds_deb = mtev_log_stream_find("debug/datastore");
  mtevL(ds_err, "Installing test ingestor... you better be testing.\n");
  return stratcon_datastore_set_ingestor(&test_ingestor_api);
}

mtev_dso_generic_t test_ingestor = {
  {
    .magic = MTEV_GENERIC_MAGIC,
    .version = MTEV_GENERIC_ABI_VERSION,
    .name = "test_ingestor",
    .description = "dummy ingestor for test suite",
    .xml_description = test_ingestor_xml_description,
    .onload = test_ingestor_onload,
  },
  test_ingestor_config,
  test_ingestor_init
};
