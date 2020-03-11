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
#include "noit_check.h"

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

int noit_lmdb_check_keys_to_hash_table(mtev_hash_table *table, uuid_t id) {
  int rc;
  MDB_val mdb_key, mdb_data;
  MDB_txn *txn;
  MDB_cursor *cursor;
  char *key = NULL;
  size_t key_size;
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();
  mtevAssert(instance != NULL);

  if (!table) {
    return -1;
  }

  mtev_hash_init(table);

  key = noit_lmdb_make_check_key(id, NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, NULL, &key_size);
  mtevAssert(key);

  mdb_key.mv_data = key;
  mdb_key.mv_size = key_size;

  mdb_txn_begin(instance->env, NULL, MDB_RDONLY, &txn);
  mdb_cursor_open(txn, instance->dbi, &cursor);
  rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_NEXT);
  while(rc == 0) {
    noit_lmdb_check_data_t *data = noit_lmdb_check_data_from_key(mdb_key.mv_data);
    if (data) {
      if (memcmp(data->id, id, UUID_SIZE) != 0) {
        noit_lmdb_free_check_data(data);
        break;
      }
      char *my_key = (char *)malloc(mdb_key.mv_size);
      memcpy(my_key, mdb_key.mv_data, mdb_key.mv_size);
      if (!mtev_hash_store(table, my_key, mdb_key.mv_size, NULL)) {
        free(my_key);
      }
      mtevL(mtev_error, "KEY - TYPE %c, NAMESPACE %s, KEY %s\n", data->type, data->ns, data->key);
      noit_lmdb_free_check_data(data);
    }
    else {
      break;
    }
    rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_NEXT);
  }
  mdb_cursor_close(cursor);
  mdb_txn_abort(txn);
  txn = NULL;
  free(key);

  return 0;
}

inline char *
noit_lmdb_make_check_key(uuid_t id, char type, char *ns, char *key, size_t *size_out)
{
  unsigned short ns_len = 0, key_len = 0;
  unsigned short ns_len_network_byte_order = 0, key_len_network_byte_order = 0;
  char *current_location = NULL;
  char *toRet = NULL;
  if (ns) {
    ns_len = strlen(ns);
  }
  if (key) {
    key_len = strlen(key);
  }
  ns_len_network_byte_order = htons(ns_len);
  key_len_network_byte_order = htons(key_len);

  size_t size = UUID_SIZE + sizeof(char) + sizeof(unsigned short) + sizeof(unsigned short) + ns_len + key_len;
  toRet = current_location = (char *)calloc(1, size);
  mtevAssert(toRet);

  memcpy(current_location, id, UUID_SIZE);
  current_location += UUID_SIZE;
  memcpy(current_location, &type, sizeof(char));
  current_location += sizeof(char);
  memcpy(current_location, &ns_len_network_byte_order, sizeof(unsigned short));
  current_location += sizeof(unsigned short);
  if (ns_len) {
    memcpy(current_location, ns, ns_len);
    current_location += ns_len;
  }
  memcpy(current_location, &key_len_network_byte_order, sizeof(unsigned short));
  current_location += sizeof(unsigned short);
  if (key_len) {
    memcpy(current_location, key, key_len);
    current_location += key_len;
  }
  /* Verify that we copied in what we expected to - this is checking for programming
   * errors */
  mtevAssert((current_location - size) == toRet);
  if (size_out) {
    *size_out = size;
  }
  return toRet;
}
noit_lmdb_check_data_t *noit_lmdb_check_data_from_key(char *key) {
  noit_lmdb_check_data_t *toRet = NULL;
  size_t current_location = 0;
  if (!key) {
    return toRet;
  }
  toRet = (noit_lmdb_check_data_t *)calloc(1, sizeof(noit_lmdb_check_data_t));
  memcpy(&toRet->id, key, UUID_SIZE);
  current_location += UUID_SIZE;
  memcpy(&toRet->type, key + current_location, sizeof(char));
  current_location += sizeof(char);
  memcpy(&toRet->ns_len, key + current_location, sizeof(unsigned short));
  toRet->ns_len = ntohs(toRet->ns_len);
  current_location += sizeof(unsigned short);
  if (toRet->ns_len) {
    toRet->ns = (char *)calloc(1, toRet->ns_len + 1);
    memcpy(toRet->ns, key + current_location, toRet->ns_len);
    current_location += toRet->ns_len;
  }
  memcpy(&toRet->key_len, key + current_location, sizeof(unsigned short));
  toRet->key_len = ntohs(toRet->key_len);
  current_location += sizeof(unsigned short);
  if (toRet->key_len) {
    toRet->key = (char *)calloc(1, toRet->key_len + 1);
    memcpy(toRet->key, key + current_location, toRet->key_len);
    current_location += toRet->ns_len;
  }
  return toRet;
}

void noit_lmdb_free_check_data(noit_lmdb_check_data_t *data) {
  if (data) {
    free(data->ns);
    free(data->key);
    free(data);
  }
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

#define NOIT_LMDB_RESIZE_FACTOR 1.5
void noit_lmdb_resize_instance(noit_lmdb_instance_t *instance)
{

  MDB_envinfo mei;
  MDB_stat mst;
  uint64_t new_mapsize;

  /* prevent new transactions on the write side */
  //ck_rwlock_write_lock(&hh->resize_lock);

  /* check if resize is necessary.. another thread may have already resized. */
  mdb_env_info(instance->env, &mei);
  mdb_env_stat(instance->env, &mst);

  uint64_t size_used = mst.ms_psize * mei.me_last_pgno;

  /* resize on 80% full */
  if ((double)size_used / mei.me_mapsize < 0.8) {
    //ck_rwlock_write_unlock(&hh->resize_lock);
    return;
  }

  new_mapsize = (double)mei.me_mapsize * NOIT_LMDB_RESIZE_FACTOR;
  new_mapsize += (new_mapsize % mst.ms_psize);

  mdb_env_set_mapsize(instance->env, new_mapsize);

  //ck_rwlock_write_unlock(&hh->resize_lock);
}

