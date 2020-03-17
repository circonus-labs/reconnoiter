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

#include "noit_check_lmdb.h"
#include "noit_check.h"
#include "noit_check_rest.h"
#include "noit_lmdb_tools.h"

#include <errno.h>

static int noit_check_lmdb_show_check_json(mtev_http_rest_closure_t *restc, 
                                           uuid_t checkid) {
  return rest_show_check_json(restc, checkid);
}

static int noit_check_lmdb_add_attribute(xmlNodePtr root, xmlNodePtr attr, noit_lmdb_check_data_t *key_data, MDB_val mdb_data, bool separate_stanza) {
  mtevAssert(root != NULL);
  if ((!mdb_data.mv_data) || (mdb_data.mv_size == 0)) {
    return 0;
  }
  char *val = (char *)calloc(1, mdb_data.mv_size);
  memcpy(val, mdb_data.mv_data, mdb_data.mv_size);
  if (separate_stanza) {
    mtevAssert(attr != NULL);
    xmlNodePtr child = NULL;
    child = xmlNewNode(NULL, (xmlChar *)key_data->key);
    xmlNodeAddContent(child, (xmlChar *)val);
    xmlAddChild(attr, child);
  }
  else {
    xmlSetProp(root, (xmlChar *)key_data->key, (xmlChar *)val);
  }
  free(val);
  return 0;
}
static int noit_check_lmdb_add_config(xmlNodePtr root, xmlNodePtr config, noit_lmdb_check_data_t *key_data, MDB_val mdb_data) {
  mtevAssert(root != NULL);
  mtevAssert(config != NULL);
  if ((!mdb_data.mv_data) || (mdb_data. mv_size == 0)) {
    return 0;
  }
  char *val = (char *)calloc(1, mdb_data.mv_size);
  memcpy(val, mdb_data.mv_data, mdb_data.mv_size);
  xmlNodePtr child = NULL;
  if (key_data->ns == NULL) {
    child = xmlNewNode(NULL, (xmlChar *)key_data->key);
    xmlNodeAddContent(child, (xmlChar *)val);
  }
  else {
    xmlNsPtr ns_found = xmlSearchNs(root->doc, root, (xmlChar *)key_data->ns);
    if (ns_found) {
      char buff[256];
      snprintf(buff, sizeof(buff), "%s:value", key_data->ns);
      child = xmlNewNode(NULL, (xmlChar *)buff);
      xmlSetProp(child, (xmlChar *)"name", (xmlChar *)key_data->key);
      xmlNodeAddContent(child, (xmlChar *)val);
    }
    else {
      mtevL(mtev_error, "PHIL: namespace %s configured in check, but not loaded - skipping\n", key_data->ns);
    }
  }
  if (child) {
    xmlAddChild(config, child);
  }
  free(val);
  return 0;
}

static int
noit_check_lmdb_populate_check_xml_from_lmdb(xmlNodePtr root, uuid_t checkid, boolean separate_attributes) {
  int rc, mod, mod_cnt;
  xmlNodePtr attr = NULL, config = NULL;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  MDB_val mdb_key, mdb_data;
  char *key = NULL;
  size_t key_size;
  bool locked = false;
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();

  mtevAssert(instance != NULL);

  key = noit_lmdb_make_check_key(checkid, NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, NULL, &key_size);
  mtevAssert(key);

  mdb_key.mv_data = key;
  mdb_key.mv_size = key_size;

  mod_cnt = noit_check_registered_module_cnt();
  for(mod=0; mod<mod_cnt; mod++) {
    xmlNsPtr ns;
    const char *nsname;
    char buff[256];

    nsname = noit_check_registered_module(mod);
 
    snprintf(buff, sizeof(buff), "noit://module/%s", nsname);
    ns = xmlSearchNs(root->doc, root, (xmlChar *)nsname);
    if(!ns) {
      ns = xmlNewNs(root, (xmlChar *)buff, (xmlChar *)nsname);
    }
  }

  if (separate_attributes) {
    attr = xmlNewNode(NULL, (xmlChar *)"attributes");
  }
  config = xmlNewNode(NULL, (xmlChar *)"config");

  ck_rwlock_read_lock(&instance->lock);
  locked = true;

  mdb_txn_begin(instance->env, NULL, MDB_RDONLY, &txn);
  mdb_cursor_open(txn, instance->dbi, &cursor);
  rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_NEXT);
  if (rc != 0) {
    if (rc == MDB_NOTFOUND) {
      goto cleanup;
    }
    else {
      mtevL(mtev_error, "failed on lookup for show: %d (%s)\n", rc, mdb_strerror(rc));
      goto cleanup;
    }
  }
  else {
    noit_lmdb_check_data_t *data = noit_lmdb_check_data_from_key(mdb_key.mv_data);
    mtevAssert(data);
    if (memcmp(data->id, checkid, UUID_SIZE) != 0) {
      noit_lmdb_free_check_data(data);
      rc = MDB_NOTFOUND;
      goto cleanup;
    }
    noit_lmdb_free_check_data(data);
  }
  while(rc == 0) {
    noit_lmdb_check_data_t *data = noit_lmdb_check_data_from_key(mdb_key.mv_data);
    if (data) {
      if (memcmp(data->id, checkid, UUID_SIZE) != 0) {
        noit_lmdb_free_check_data(data);
        break;
      }
      mtevL(mtev_error, "CHECKING ATTR KEY - TYPE %c, NAMESPACE %s, KEY %s\n", data->type, data->ns, data->key);
      if (data->type == NOIT_LMDB_CHECK_ATTRIBUTE_TYPE) {
        noit_check_lmdb_add_attribute(root, attr, data, mdb_data, separate_attributes);
      }
      else if (data->type == NOIT_LMDB_CHECK_CONFIG_TYPE) {
        noit_check_lmdb_add_config(root, config, data, mdb_data);
      }
      noit_lmdb_free_check_data(data);
    }
    else {
      break;
    }
    rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_NEXT);
  }

  if (separate_attributes) {
    xmlAddChild(root, attr);
  }
  xmlAddChild(root, config);

  rc = 0;
  goto cleanup;

 cleanup:
  if (cursor) mdb_cursor_close(cursor);
  if (txn) mdb_txn_abort(txn);
  if (locked) {
    ck_rwlock_read_unlock(&instance->lock);
  }
  free(key);
  return rc;
}

static int
noit_check_lmdb_scan_lmdb_for_each_check_cb(noit_check_t *check, void *closure) {
  xmlNodePtr root = (xmlNodePtr)closure;
  xmlNodePtr check_node = xmlNewNode(NULL, (xmlChar *)"check");
  int rv = noit_check_lmdb_populate_check_xml_from_lmdb(check_node, check->checkid, false);
  if (rv == 0) {
    xmlAddChild(root, check_node);
  }
  else {
    xmlFreeNode(check_node);
  }
  return 1;
}

int noit_check_lmdb_show_checks(mtev_http_rest_closure_t *restc, int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  int error_code = 500;
  xmlDocPtr doc = NULL;
  xmlNodePtr root = NULL;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();

  mtevAssert(instance != NULL);

  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlNewDocNode(doc, NULL, (xmlChar *)"checks", NULL);
  xmlDocSetRootElement(doc, root);

  mtev_conf_section_t checks = mtev_conf_get_section_read(MTEV_CONF_ROOT, "/noit/checks");
  xmlNodePtr checks_xmlnode = mtev_conf_section_to_xmlnodeptr(checks);
  mtev_conf_release_section_read(checks);
  if (!checks_xmlnode) {
    goto error;
  }

  /* First, set the properties */
  xmlAttr* props = checks_xmlnode->properties;
  while (props != NULL) {
    xmlChar* value = xmlNodeListGetString(checks_xmlnode->doc, props->children, 1);
    if (value) {
      xmlSetProp(root, props->name, value);
      xmlFree(value);
    }
    props = props->next;
  }

  /* Next, iterate the checks and set the data from LMDB */
  noit_poller_do(noit_check_lmdb_scan_lmdb_for_each_check_cb, root);

  mtev_http_response_ok(ctx, "text/xml");
  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);
  goto cleanup;

 error:
  mtev_http_response_standard(ctx, error_code, "ERROR", "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if (cursor) mdb_cursor_close(cursor);
  if (txn) mdb_txn_abort(txn);
  if (doc) xmlFreeDoc(doc);

  return 0;
}

int noit_check_lmdb_show_check(mtev_http_rest_closure_t *restc, int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  xmlDocPtr doc = NULL;
  xmlNodePtr root, state;
  int error_code = 500;
  uuid_t checkid;
  noit_check_t *check;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();

  mtevAssert(instance != NULL);

  if(npats != 2 && npats != 3) {
    goto error;
  }
  /* pats[0] was an XML path in the XML-backed store.... we can no longer
   * respect that, so just ignore it */
  if(mtev_uuid_parse(pats[1], checkid)) goto error;

  if(npats == 3 && !strcmp(pats[2], ".json")) {
    return noit_check_lmdb_show_check_json(restc, checkid);
  }

  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlNewDocNode(doc, NULL, (xmlChar *)"check", NULL);
  xmlDocSetRootElement(doc, root);

  int rc = noit_check_lmdb_populate_check_xml_from_lmdb(root, checkid, true);
  if (rc != 0) {
    if (rc == MDB_NOTFOUND) {
      goto not_found;
    }
    else {
      goto error;
    }
  }

  mtev_http_request *req = mtev_http_session_request(ctx);
  const char *redirect_s = mtev_http_request_querystring(req, "redirect");
  mtev_boolean redirect = !redirect_s || strcmp(redirect_s, "0");
  const char *metrics = mtev_http_request_querystring(req, "metrics");

  /* Add the state */
  check = noit_poller_lookup(checkid);
  if(!check) {
    state = xmlNewNode(NULL, (xmlChar *)"state");
    xmlSetProp(state, (xmlChar *)"error", (xmlChar *)"true");
  }
  else {
    int full = 1;
    if(metrics && strtoll(metrics, NULL, 10) == 0) full = -1;
    state = noit_check_state_as_xml(check, full);
  }

  xmlAddChild(root, state);

  mtev_cluster_node_t *owner = NULL;
  if(check && !noit_should_run_check(check, &owner) && owner) {
    const char *cn = mtev_cluster_node_get_cn(owner);
    char url[1024];
    struct sockaddr *addr;
    socklen_t addrlen;
    unsigned short port;
    switch(mtev_cluster_node_get_addr(owner, &addr, &addrlen)) {
      case AF_INET:
        port = ntohs(((struct sockaddr_in *)addr)->sin_port);
        break;
      case AF_INET6:
        port = ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
        break;
      default:
        port = 43191;
    }
    char uuid_str[UUID_STR_LEN+1];
    mtev_uuid_unparse_lower(checkid, uuid_str);
    snprintf(url, sizeof(url), "https://%s:%u/checks/show/%s",
             cn, port, uuid_str);
    mtev_http_response_header_set(restc->http_ctx, "Location", url);
    mtev_http_response_standard(ctx, redirect ? 302 : 200, "NOT IT", "text/xml");
  }
  else {
    mtev_http_response_ok(ctx, "text/xml");
  }

  mtev_http_response_ok(ctx, "text/xml");
  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);
  goto cleanup;

 not_found:
  mtev_http_response_not_found(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 error:
  mtev_http_response_standard(ctx, error_code, "ERROR", "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if (cursor) mdb_cursor_close(cursor);
  if (txn) mdb_txn_abort(txn);
  if(doc) xmlFreeDoc(doc);
  return 0;
}

void noit_check_lmdb_configure_check(uuid_t checkid, xmlNodePtr a, xmlNodePtr c, int64_t *seq_in) {
  xmlNodePtr n;
  int rc = 0;
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();
  mtev_hash_table conf_table;
  MDB_txn *txn;
  MDB_cursor *cursor;
  char *key, *val;
  size_t key_size;
  MDB_val mdb_key, mdb_data;
  mtev_hash_iter iter;
  const char *_hash_iter_key;
  int _hash_iter_klen;

  mtevAssert(instance != NULL);

  if (seq_in) *seq_in = 0;

put_retry:
  txn = NULL;
  cursor = NULL;
  key = NULL;
  val = NULL;
  key_size = 0;
  memset(&iter, 0, sizeof(mtev_hash_iter));

  ck_rwlock_read_lock(&instance->lock);
  noit_lmdb_check_keys_to_hash_table(instance, &conf_table, checkid, true);
  rc = mdb_txn_begin(instance->env, NULL, 0, &txn);
  if (rc != 0) {
    mtevFatal(mtev_error, "failure on txn begin - %d (%s)\n", rc, mdb_strerror(rc));
  }
  rc = mdb_cursor_open(txn, instance->dbi, &cursor);
  if (rc != 0) {
    mtevFatal(mtev_error, "failure on cursor open - %d (%s)\n", rc, mdb_strerror(rc));
  }

  for (n = a->children; n; n = n->next) {
#define ATTR2LMDB(attr_name) do { \
  if(!strcmp((char *)n->name, #attr_name)) { \
    val = (char *)xmlNodeGetContent(n); \
    if (val) { \
      key = noit_lmdb_make_check_key(checkid, NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, #attr_name, &key_size); \
      mtevAssert(key); \
      mdb_key.mv_data = key; \
      mdb_key.mv_size = key_size; \
      mdb_data.mv_data = val; \
      mdb_data.mv_size = strlen(val); \
      rc = mdb_cursor_put(cursor, &mdb_key, &mdb_data, 0); \
      if (rc == MDB_MAP_FULL) { \
        ck_rwlock_read_unlock(&instance->lock); \
        mdb_cursor_close(cursor); \
        mdb_txn_abort(txn); \
        mtev_hash_destroy(&conf_table, free, NULL); \
        free(key); \
        xmlFree(val); \
        noit_lmdb_resize_instance(instance); \
        goto put_retry; \
      } \
      else if (rc != 0) { \
        mtevFatal(mtev_error, "failure on cursor put - %d (%s)\n", rc, mdb_strerror(rc)); \
      } \
      mtev_hash_delete(&conf_table, key, key_size, free, NULL); \
      free(key); \
      xmlFree(val); \
      val = NULL; \
    } \
    continue; \
  } \
} while(0)

    ATTR2LMDB(name);
    ATTR2LMDB(target);
    ATTR2LMDB(resolve_rtype);
    ATTR2LMDB(module);
    ATTR2LMDB(period);
    ATTR2LMDB(timeout);
    ATTR2LMDB(disable);
    ATTR2LMDB(filterset);
    ATTR2LMDB(seq);
  }

  /* TODO: Add UUID as check attribute */

  if (c) {
    key = NULL;
    val = NULL;
    for(n = c->children; n; n = n->next) {
      val = (char *)xmlNodeGetContent(n);
      if (val != NULL) {
        char *prefix = NULL;
        if (n->ns) {
          prefix = (char *)n->ns->prefix;
        }
        
        key = noit_lmdb_make_check_key(checkid, NOIT_LMDB_CHECK_CONFIG_TYPE, prefix, (char *)n->name, &key_size);
        mtevAssert(key);

        mdb_key.mv_data = key;
        mdb_key.mv_size = key_size;
        mdb_data.mv_data = val;
        mdb_data.mv_size = strlen(val);
        rc = mdb_cursor_put(cursor, &mdb_key, &mdb_data, 0);
        if (rc == MDB_MAP_FULL) {
          ck_rwlock_read_unlock(&instance->lock);
          mdb_cursor_close(cursor);
          mdb_txn_abort(txn);
          mtev_hash_destroy(&conf_table, free, NULL);
          free(key);
          xmlFree(val);
          noit_lmdb_resize_instance(instance);
          goto put_retry;
        }
        else if (rc != 0) {
          mtevFatal(mtev_error, "failure on cursor put - %d (%s)\n", rc, mdb_strerror(rc));
        }
        mtev_hash_delete(&conf_table, key, key_size, free, NULL);
        free(key);
        if (val) xmlFree(val);
        key = NULL;
        val = NULL;
      }
    }
  }
  void *unused;
  while(mtev_hash_next(&conf_table, &iter, &_hash_iter_key, &_hash_iter_klen, &unused)) {
    mdb_key.mv_data = (char *)_hash_iter_key;
    mdb_key.mv_size = _hash_iter_klen;
    rc = mdb_del(txn, instance->dbi, &mdb_key, NULL);
    if (rc != 0) {
      if (rc == MDB_MAP_FULL) {
        ck_rwlock_read_unlock(&instance->lock);
        mdb_cursor_close(cursor);
        mdb_txn_abort(txn);
        mtev_hash_destroy(&conf_table, free, NULL);
        noit_lmdb_resize_instance(instance);
        goto put_retry;
      }
      else if (rc != MDB_NOTFOUND) {
        mtevL(mtev_error, "failed to delete key: %d (%s)\n", rc, mdb_strerror(rc));
      }
    }
  }
  rc = mdb_txn_commit(txn);
  if (rc == MDB_MAP_FULL) {
    ck_rwlock_read_unlock(&instance->lock);
    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    mtev_hash_destroy(&conf_table, free, NULL);
    noit_lmdb_resize_instance(instance);
    goto put_retry;
  }
  else if (rc != 0) {
    mtevFatal(mtev_error, "failure on txn commmit - %d (%s)\n", rc, mdb_strerror(rc));
  }
  mdb_cursor_close(cursor);
  mtev_hash_destroy(&conf_table, free, NULL);
}

int
noit_check_lmdb_delete_check(mtev_http_rest_closure_t *restc,
                             int npats, char **pats) {
  //mtev_http_session_ctx *ctx = restc->http_ctx;
  uuid_t checkid;
  int rc;//, error_code = 500;
  MDB_val mdb_key, mdb_data;
  MDB_txn *txn;
  MDB_cursor *cursor;
  char *key = NULL;
  size_t key_size;
  bool locked = false;
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();
  mtevAssert(instance != NULL);

  if(npats != 2) goto error;
  if(mtev_uuid_parse(pats[1], checkid)) goto error;

  key = noit_lmdb_make_check_key(checkid, NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, NULL, &key_size);
  mtevAssert(key);

  mdb_key.mv_data = key;
  mdb_key.mv_size = key_size;

  ck_rwlock_read_lock(&instance->lock);
  locked = true;

  rc = mdb_txn_begin(instance->env, NULL, 0, &txn);
  if (rc != 0) {
    mtevL(mtev_error, "failed to create transaction for delete: %d (%s)\n", rc, mdb_strerror(rc));
    goto error;
  }
  mdb_cursor_open(txn, instance->dbi, &cursor);
  rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_NEXT);
  if (rc != 0) {
    if (rc == MDB_NOTFOUND) {
      mdb_cursor_close(cursor);
      mdb_txn_abort(txn);
      goto not_found;
    }
    else {
      mtevL(mtev_error, "failed on delete lookup: %d (%s)\n", rc, mdb_strerror(rc));
      mdb_cursor_close(cursor);
      mdb_txn_abort(txn);
      goto error;
    }
  }
  while(rc == 0) {
    noit_lmdb_check_data_t *data = noit_lmdb_check_data_from_key(mdb_key.mv_data);
    if (data) {
      if (memcmp(data->id, checkid, UUID_SIZE) != 0) {
        noit_lmdb_free_check_data(data);
        break;
      }
      mtevL(mtev_error, "DELETING KEY - TYPE %c, NAMESPACE %s, KEY %s\n", data->type, data->ns, data->key);
      noit_lmdb_free_check_data(data);
      rc = mdb_cursor_del(cursor, 0);
      if (rc != 0) {
        mtevL(mtev_error, "failed to delete key in check: %d (%s)\n", rc, mdb_strerror(rc));
        mdb_cursor_close(cursor);
        mdb_txn_abort(txn);
        goto error;
      }
    }
    else {
      break;
    }
    rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_NEXT);
  }
  rc = mdb_txn_commit(txn);
  if (rc != 0) {
    mtevL(mtev_error, "failed to commit delete txn: %d (%s)\n", rc, mdb_strerror(rc));
    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    goto error;
  }
  mdb_cursor_close(cursor);

  /* TODO: Uncomment these when done */
  //mtev_http_response_ok(ctx, "text/html");
  //mtev_http_response_end(ctx);
  goto cleanup;

 not_found:
  //mtev_http_response_not_found(ctx, "text/html");
  //mtev_http_response_end(ctx);
  goto cleanup;

 error:
  //mtev_http_response_standard(ctx, error_code, "ERROR", "text/html");
  //mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if (locked) {
    ck_rwlock_read_unlock(&instance->lock);
  }
  free(key);
  return 0;
}
