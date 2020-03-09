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
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOTK
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "noit_lmdb_tools.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "mtev_log.h"
#include "mtev_mkdir.h"

static mtev_boolean
lmdb_instance_mkdir(const char *path)
{
  char to_make[PATH_MAX];
  size_t copy_len = strlen(path);
  memset(to_make, 0, PATH_MAX);
  memcpy(to_make, path, MIN(copy_len, PATH_MAX));
  strlcat(to_make, "/dummy", sizeof(to_make));
  if (mkdir_for_file(to_make, 0777)) {
    mtevL(mtev_error, "mkdir %s: %s\n", to_make, strerror(errno));
    return mtev_false;
  }
  return mtev_true;
}

inline char *
noit_lmdb_make_check_key(uuid_t id, char type, char *ns, char *key)
{
  size_t size = sizeof(uuid_t) + sizeof(char);
  unsigned short ns_len = 0, key_len = 0;
  unsigned short ns_len_network_byte_order = 0, key_len_network_byte_order = 0;
  size_t current_location = 0;
  char *toRet = NULL;
  if (ns) {
    ns_len = strlen(ns);
    size += sizeof(unsigned short);
    size += ns_len;
    ns_len_network_byte_order = htons(ns_len);
  }
  if (key) {
    key_len = strlen(key);
    size += sizeof(unsigned short);
    size += key_len;
    key_len_network_byte_order = htons(key_len);
  }

  toRet = (char *)malloc(size);
  mtevAssert(toRet);

  memcpy(toRet, id, sizeof(uuid_t));
  memcpy(toRet + sizeof(uuid_t), &type, sizeof(char));
  current_location = sizeof(uuid_t) + sizeof(char);
  memcpy(toRet + current_location, &ns_len_network_byte_order, sizeof(unsigned short));
  current_location += sizeof(unsigned short);
  if (ns_len) {
    memcpy(toRet + current_location, ns, ns_len);
    current_location += ns_len;
  }
  memcpy(toRet + current_location, &key_len_network_byte_order, sizeof(unsigned short));
  current_location += sizeof(unsigned short);
  if (key_len) {
    memcpy(toRet + current_location, key, key_len);
  }
  return toRet;
}

noit_lmdb_instance_t *noit_lmdb_tools_open_instance(char *path)
{
  int rc;
  MDB_env *env;

  mtevAssert(lmdb_instance_mkdir(path));

  rc = mdb_env_create(&env);
  if (rc != 0) {
    errno = rc;
    return NULL;
  }

  rc = mdb_env_set_maxreaders(env, 1024);
  if (rc != 0) {
    errno = rc;
    mdb_env_close(env);
    return NULL;
  }

  rc = mdb_env_open(env, path, MDB_NOMETASYNC | MDB_NOSYNC | MDB_NOMEMINIT, 0644);
  if (rc != 0) {
    errno = rc;
    mdb_env_close(env);
    return NULL;
  }

  MDB_txn *txn;
  MDB_dbi dbi;
  rc = mdb_txn_begin(env, NULL, 0, &txn);
  if (rc != 0) {
    errno = rc;
    mdb_env_close(env);
    return NULL;
  }
  rc = mdb_open(txn, NULL, MDB_CREATE, &dbi);
  if (rc != 0) {
    mdb_txn_abort(txn);
    mdb_env_close(env);
    errno = rc;
    return NULL;
  }
  rc = mdb_txn_commit(txn);
  if (rc != 0) {
    mdb_txn_abort(txn);
    mdb_env_close(env);
    errno = rc;
    return NULL;
  }

  noit_lmdb_instance_t *instance = (noit_lmdb_instance_t *)malloc(sizeof(noit_lmdb_instance_t));
  instance->env = env;
  instance->dbi = dbi;
  instance->path = strdup(path);

  return instance;
}

void noit_lmdb_tools_close_instance(noit_lmdb_instance_t *instance)
{
  if (!instance) return;
  mdb_dbi_close(instance->env, instance->dbi);
  mdb_env_close(instance->env);
  free(instance->path);
  free(instance);
}
