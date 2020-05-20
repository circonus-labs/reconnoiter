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

#ifndef _NOIT_FILTERS_LMDB_H
#define _NOIT_FILTERS_LMDB_H

#include <mtev_rest.h>
#include <libxml/tree.h>

int noit_filters_lmdb_populate_filterset_xml_from_lmdb(xmlNodePtr root, char *fs_name);
mtev_boolean noit_filters_lmdb_already_in_db(char *name);
int64_t noit_filters_lmdb_get_seq(char *name);
void noit_filters_lmdb_filters_from_lmdb();
void noit_filters_lmdb_migrate_xml_filtersets_to_lmdb();
int noit_filters_lmdb_process_repl(xmlDocPtr doc);
int noit_filters_lmdb_rest_show_filters(mtev_http_rest_closure_t *restc,
                                        int npats, char **pats);
int noit_filters_lmdb_rest_show_filter(mtev_http_rest_closure_t *restc,
                                       int npats, char **pats);
int noit_filters_lmdb_rest_delete_filter(mtev_http_rest_closure_t *restc,
                                         int npats, char **pats);
int noit_filters_lmdb_cull_unused();
int noit_filters_lmdb_rest_set_filter(mtev_http_rest_closure_t *restc,
                                      int npats, char **pats);
void noit_filters_lmdb_init();

#endif
