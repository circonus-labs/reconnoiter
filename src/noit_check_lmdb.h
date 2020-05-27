/* Copyright (c) 2020, Circonus, Inc. All rights reserved.
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

#include <mtev_uuid.h>
#include <mtev_rest.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#ifndef _NOIT_CHECK_LMDB_H
#define _NOIT_CHECK_LMDB_H

typedef enum {
  NOIT_LMDB_CHECK_ATTRIBUTE_TYPE = 'A',
  NOIT_LMDB_CHECK_CONFIG_TYPE = 'C'
} noit_lmdb_check_type_e;

int noit_check_lmdb_show_checks(mtev_http_rest_closure_t *restc, int npats, char **pats);
int noit_check_lmdb_show_check(mtev_http_rest_closure_t *restc, int npats, char **pats);
int noit_check_lmdb_set_check(mtev_http_rest_closure_t *restc, int npats, char **pats);
int noit_check_lmdb_remove_check_from_db(uuid_t checkid, mtev_boolean force);
int noit_check_lmdb_delete_check(mtev_http_rest_closure_t *restc, int npats, char **pats);
void noit_check_lmdb_poller_process_checks(uuid_t *uuids, int uuid_cnt);
void noit_check_lmdb_migrate_xml_checks_to_lmdb();
int noit_check_lmdb_process_repl(xmlDocPtr doc);
mtev_boolean noit_check_lmdb_already_in_db(uuid_t checkid);
char *noit_check_lmdb_get_specific_field(uuid_t checkid, noit_lmdb_check_type_e search_type,
                                         char *search_namespace, char *search_key,
                                         mtev_boolean locked);

#endif
