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

#ifndef _NOIT_LMDB_TOOLS_H
#define _NOIT_LMDB_TOOLS_H

#include <lmdb.h>
#include <mtev_hash.h>
#include <mtev_uuid.h>

#define NOIT_LMDB_CHECK_ATTRIBUTE_TYPE 'A'
#define NOIT_LMDB_CHECK_CONFIG_TYPE 'C'

typedef struct noit_lmdb_instance {
  MDB_env *env;
  MDB_dbi dbi;
  char *path;
} noit_lmdb_instance_t;

typedef struct noit_lmdb_check_data {
  uuid_t id;
  char type;
  unsigned short ns_len;
  char *ns;
  unsigned short key_len;
  char *key;
} noit_lmdb_check_data_t;

int noit_lmdb_check_keys_to_hash_table(mtev_hash_table *table, uuid_t id);
char* noit_lmdb_make_check_key(uuid_t id, char type, char *ns, char *key, size_t *size_out);
noit_lmdb_check_data_t *noit_lmdb_check_data_from_key(char *key);
void noit_lmdb_free_check_data(noit_lmdb_check_data_t *data);
noit_lmdb_instance_t *noit_lmdb_tools_open_instance(char *path);
void noit_lmdb_tools_close_instance(noit_lmdb_instance_t *instance);

#endif
