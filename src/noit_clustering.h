/*
 * Copyright (c) 2017, Circonus, Inc. All rights reserved.
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

#ifndef _NOIT_CLUSTERING_H
#define _NOIT_CLUSTERING_H

#include <noit_check.h>
#include <mtev_cluster.h>
#include <netinet/in.h>

#define NOIT_MTEV_CLUSTER_NAME "noit"
#define NOIT_MTEV_CLUSTER_APP_ID 43
#define NOIT_MTEV_CLUSTER_CHECK_SEQ_KEY 1
#define NOIT_MTEV_CLUSTER_FILTER_SEQ_KEY 2

void noit_mtev_cluster_init();

mtev_boolean noit_should_run_check(noit_check_t *, mtev_cluster_node_t **);
void noit_cluster_mark_check_changed(noit_check_t *check, void *vpeer);
void noit_cluster_mark_filter_changed(const char *name, void *vpeer);

void
  noit_cluster_xml_check_changes(uuid_t peerid, const char *cn,
                                 int64_t prev_end, int64_t limit, xmlNodePtr parent);

void
  noit_cluster_xml_filter_changes(uuid_t peerid, const char *cn,
                                  int64_t prev_end, int64_t limit, xmlNodePtr parent);

mtev_boolean
  noit_cluster_checkid_replication_pending(uuid_t);

#endif
