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
  if (mkdir_for_file(to_make, 0750)) {
    mtevL(mtev_error, "mkdir %s: %s\n", to_make, strerror(errno));
    return mtev_false;
  }
  return mtev_true;
}

int noit_lmdb_check_keys_to_hash_table(noit_lmdb_instance_t *instance, mtev_hash_table *table, uuid_t id, bool locked) {
  int rc;
  MDB_val mdb_key, mdb_data;
  MDB_txn *txn;
  MDB_cursor *cursor;
  char *key = NULL;
  size_t key_size;

  mtevAssert(instance != NULL);

  if (!table) {
    return -1;
  }

  mtev_hash_init(table);

  key = noit_lmdb_make_check_key_for_iterating(id, &key_size);
  mtevAssert(key);

  mdb_key.mv_data = key;
  mdb_key.mv_size = key_size;

  if (!locked) {
    pthread_rwlock_rdlock(&instance->lock);
  }

  mdb_txn_begin(instance->env, NULL, MDB_RDONLY, &txn);
  mdb_cursor_open(txn, instance->dbi, &cursor);
  rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_SET_RANGE);
  while(rc == 0) {
    noit_lmdb_check_data_t *data = noit_lmdb_check_data_from_key(mdb_key.mv_data);
    if (data) {
      if (memcmp(data->id, id, UUID_SIZE) != 0) {
        noit_lmdb_free_check_data(data);
        break;
      }
      char *my_key = (char *)calloc(1, mdb_key.mv_size + 1);
      memcpy(my_key, mdb_key.mv_data, mdb_key.mv_size);
      if (!mtev_hash_store(table, my_key, mdb_key.mv_size, NULL)) {
        free(my_key);
      }
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

  if (!locked) {
    pthread_rwlock_unlock(&instance->lock);
  }

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
inline char *
noit_lmdb_make_check_key_for_iterating(uuid_t id, size_t *size_out) {
  char *toRet = (char *)malloc(UUID_SIZE);
  memcpy(toRet, id, UUID_SIZE);
  if (size_out) {
    *size_out = UUID_SIZE;
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

inline char *
noit_lmdb_make_filterset_key(char *name, size_t *size_out)
{
  char *toRet = NULL, *current_location = NULL;
  unsigned short name_len = 0;
  unsigned short name_len_network_byte_order = 0;

  mtevAssert(name);
  mtevAssert(size_out);
  *size_out = 0;

  name_len = strlen(name);
  name_len_network_byte_order = htons(name_len);

  size_t size = sizeof(unsigned short) + name_len;
  toRet = current_location = (char *)calloc(1, size);
  mtevAssert(toRet);

  memcpy(current_location, &name_len_network_byte_order, sizeof(unsigned short));
  current_location += sizeof(unsigned short);

  memcpy(current_location, name, name_len);
  current_location += name_len;

  /* Verify that we copied in what we expected to - this is checking for programming
   * errors */
  mtevAssert((current_location - size) == toRet);
  if (size_out) {
    *size_out = size;
  }

  return toRet;
}

noit_lmdb_filterset_rule_data_t *
noit_lmdb_filterset_data_from_key(char *key) {
  noit_lmdb_filterset_rule_data_t *toRet = NULL;
  size_t current_location = 0;
  if (!key) {
    return toRet;
  }
  toRet = (noit_lmdb_filterset_rule_data_t *)calloc(1, sizeof(noit_lmdb_filterset_rule_data_t));
  memcpy(&toRet->filterset_name_len, key + current_location, sizeof(unsigned short));
  toRet->filterset_name_len = ntohs(toRet->filterset_name_len);
  current_location += sizeof(unsigned short);
  if (toRet->filterset_name_len) {
    toRet->filterset_name = (char *)calloc(1, toRet->filterset_name_len + 1);
    memcpy(toRet->filterset_name, key + current_location, toRet->filterset_name_len);
    current_location += toRet->filterset_name_len;
  }
  return toRet;
}

void
noit_lmdb_free_filterset_data(noit_lmdb_filterset_rule_data_t *data) {
  if (data) {
    free(data->filterset_name);
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

  /* Align mapsize to pagesize, use enough pages to reach
   * 64 MB */
  size_t page_size = getpagesize();
  size_t map_size = page_size;
  static const size_t map_size_target = (1024*1024*64);
  while (map_size < map_size_target) {
    map_size += page_size;
  }
  rc = mdb_env_set_mapsize(env, map_size);
  if (rc != 0) {
    errno = rc;
    mdb_env_close(env);
    return NULL;
  }

  rc = mdb_env_open(env, path, 0, 0640);
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
  pthread_rwlock_init(&instance->lock, NULL);
  instance->path = strdup(path);
  instance->generation = 0;

  return instance;
}

void noit_lmdb_tools_close_instance(noit_lmdb_instance_t *instance)
{
  if (!instance) return;
  pthread_rwlock_wrlock(&instance->lock);
  mdb_dbi_close(instance->env, instance->dbi);
  mdb_env_close(instance->env);
  pthread_rwlock_unlock(&instance->lock);
  free(instance->path);
  free(instance);
}

uint64_t noit_lmdb_get_instance_generation(noit_lmdb_instance_t *instance)
{
  return ck_pr_load_64(&instance->generation);
}

static void noit_lmdb_increment_instance_generation(noit_lmdb_instance_t *instance)
{
  ck_pr_inc_64(&instance->generation);
}

#define NOIT_LMDB_RESIZE_FACTOR 4
void noit_lmdb_resize_instance(noit_lmdb_instance_t *instance, const uint64_t initial_generation)
{
  MDB_envinfo mei;
  MDB_stat mst;
  uint64_t new_mapsize;

  /* prevent new transactions on the write side */
  pthread_rwlock_wrlock(&instance->lock);

  mdb_env_info(instance->env, &mei);
  mdb_env_stat(instance->env, &mst);

  /* If the generation has changed, another thread already did the resize, so
   * we don't need to do it again */
  if (initial_generation != noit_lmdb_get_instance_generation(instance)) {
    pthread_rwlock_unlock(&instance->lock);
    return;
  }

  new_mapsize = (double)mei.me_mapsize * NOIT_LMDB_RESIZE_FACTOR;
  new_mapsize += (new_mapsize % mst.ms_psize);

  mdb_env_set_mapsize(instance->env, new_mapsize);

  mtevL(mtev_error, "lmdb db (%s): mapsize increased. old: %" PRIu64 " MiB, new: %" PRIu64 " MiB\n",
        instance->path, mei.me_mapsize / (1024 * 1024), new_mapsize / (1024 * 1024));

  noit_lmdb_increment_instance_generation(instance);

  pthread_rwlock_unlock(&instance->lock);
}

