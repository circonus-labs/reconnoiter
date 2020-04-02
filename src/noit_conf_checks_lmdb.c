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

#include "noit_conf_checks_lmdb.h"
#include "noit_conf_checks.h"
#include "noit_check.h"
#include "noit_check_lmdb.h"
#include "noit_lmdb_tools.h"

int
noit_conf_checks_lmdb_console_show_check(mtev_console_closure_t ncct,
                                         int argc,
                                         char **argv,
                                         mtev_console_state_t *state,
                                         void *closure) {
  int rc;
  int toRet = -1;
  uuid_t checkid;
  char *key;
  size_t key_size;
  char error_str[255];
  char *name = NULL,
       *module = NULL,
       *target = NULL,
       *seq = NULL,
       *resolve_rtype = NULL, 
       *period = NULL,
       *timeout = NULL,
       *oncheck = NULL,
       *filterset = NULL,
       *disable = NULL;
  mtev_boolean error = mtev_false;
  MDB_val mdb_key, mdb_data;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  mtev_hash_table configh;
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  const char *k;
  int klen;
  void *data;
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();
  mtevAssert(instance);

  if(argc != 1) {
    nc_printf(ncct, "requires one argument\n");
    return toRet;
  }

  if (mtev_uuid_parse(argv[0], checkid) != 0) {
    nc_printf(ncct, "%s is invalid uuid\n", argv[0]);
    return toRet;
  }

  mtev_hash_init(&configh);

  key = noit_lmdb_make_check_key_for_iterating(checkid, &key_size);
  mtevAssert(key);
  mdb_key.mv_data = key;
  mdb_key.mv_size = key_size;

  ck_rwlock_read_lock(&instance->lock);

  do {
    rc = mdb_txn_begin(instance->env, NULL, 0, &txn);
    if (rc != 0) {
      snprintf(error_str, sizeof(error_str), "failed to create lmdb transaction: %d (%s)", rc, mdb_strerror(rc));
      error = mtev_true;
      break;
    }
    mdb_cursor_open(txn, instance->dbi, &cursor);
    rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_SET_RANGE);
    if (rc != 0) {
      if (rc == MDB_NOTFOUND) {
        snprintf(error_str, sizeof(error_str), "check %s not found", argv[0]);
      }
      else {
        snprintf(error_str, sizeof(error_str), "failed to perform lmdb cursor get: %d (%s)", rc, mdb_strerror(rc));
      }
      error = mtev_true;
      break;
    }
    else {
      if (mtev_uuid_compare(checkid, mdb_key.mv_data) != 0) {
        snprintf(error_str, sizeof(error_str), "check %s not found", argv[0]);
        error = mtev_true;
        break;
      }
    }
    nc_printf(ncct, "==== %s ====\n", argv[0]);
    while (rc == 0) {
      if (mtev_uuid_compare(checkid, mdb_key.mv_data) != 0) {
        /* This means we've hit the next uuid, break out  */
        break;
      }
      noit_lmdb_check_data_t *data = noit_lmdb_check_data_from_key(mdb_key.mv_data);
      mtevAssert(data);

      if (data->type == NOIT_LMDB_CHECK_ATTRIBUTE_TYPE) {
#define SET_ATTR_STRING(attribute) do { \
  if (!strcmp(data->key, #attribute)) { \
    if ((mdb_data.mv_data != NULL) && (mdb_data.mv_size > 0)) { \
      attribute = (char *)calloc(1, mdb_data.mv_size + 1); \
      memcpy(attribute, mdb_data.mv_data, mdb_data.mv_size); \
    } \
  } \
} while (0);
        SET_ATTR_STRING(name);
        SET_ATTR_STRING(module);
        SET_ATTR_STRING(target);
        SET_ATTR_STRING(seq);
        SET_ATTR_STRING(resolve_rtype);
        SET_ATTR_STRING(period);
        SET_ATTR_STRING(timeout);
        SET_ATTR_STRING(oncheck);
        SET_ATTR_STRING(filterset);
        SET_ATTR_STRING(disable);
      }
      else if (data->type == NOIT_LMDB_CHECK_CONFIG_TYPE) {
        /* We don't care about namespaced config stuff here */
        if (data->ns == NULL) {
          char *out_data = (char *)calloc(1, mdb_data.mv_size + 1);
          memcpy(out_data, mdb_data.mv_data, mdb_data.mv_size);
          mtev_hash_store(&configh, strdup(data->key), strlen(data->key), out_data);
        }
      }
      else {
        /* Will hopefully never happen */
        snprintf(error_str, sizeof(error_str), "received unknown data type: %c - possible lmdb corruption?\n", data->type);
        error = mtev_true;
        break;
      }

      noit_lmdb_free_check_data(data);
      rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_NEXT);
    }
  } while (0);

  if (cursor) mdb_cursor_close(cursor);
  if (txn) mdb_txn_abort(txn);
  ck_rwlock_read_unlock(&instance->lock);

#define NC_PRINT_ATTRIBUTE(attribute) do {\
  nc_printf(ncct, " %s: %s\n", #attribute, attribute ? attribute : "[undef]"); \
} while (0);

  if (error == mtev_false) {
    NC_PRINT_ATTRIBUTE(name);
    NC_PRINT_ATTRIBUTE(module);
    NC_PRINT_ATTRIBUTE(target);
    NC_PRINT_ATTRIBUTE(seq);
    NC_PRINT_ATTRIBUTE(resolve_rtype);
    NC_PRINT_ATTRIBUTE(period);
    NC_PRINT_ATTRIBUTE(timeout);
    NC_PRINT_ATTRIBUTE(oncheck);
    NC_PRINT_ATTRIBUTE(filterset);
    NC_PRINT_ATTRIBUTE(disable);

    while(mtev_hash_next(&configh, &iter, &k, &klen, &data)) {
      nc_printf(ncct, " config::%s: %s\n", k, (const char *)data);
    }

    noit_console_get_running_stats(ncct, checkid);

    toRet = 0;
  }
  else {
    nc_printf(ncct, "%s\n", error_str);
    toRet = -1;
  }
  free(name);
  free(module);
  free(target);
  free(seq);
  free(resolve_rtype);
  free(period);
  free(timeout);
  free(oncheck);
  free(filterset);
  free(disable);
  mtev_hash_destroy(&configh, free, free);
  return toRet;
}
