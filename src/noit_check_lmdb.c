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
#include <mtev_uuid.h>
#include <mtev_watchdog.h>

#include <errno.h>

static int noit_check_lmdb_show_check_json(mtev_http_rest_closure_t *restc, 
                                           uuid_t checkid) {
  return rest_show_check_json(restc, checkid);
}

static int noit_check_lmdb_add_attribute(xmlNodePtr root, xmlNodePtr attr, noit_lmdb_check_data_t *key_data, MDB_val mdb_data, bool separate_stanza) {
  mtevAssert(root != NULL);
  char *val = NULL;
  if (!strcmp(key_data->key, "uuid")) {
    /* This should be set separately */
    return 0;
  }
  if ((mdb_data.mv_data == NULL) && (mdb_data.mv_size > 0)) {
    /* If data is null, but we have a size, something is wrong - just skip
     * this key */
    char uuid_str[UUID_STR_LEN + 1];
    mtev_uuid_unparse_lower(key_data->id, uuid_str);
    mtevL(mtev_error, "noit_check_lmdb_add_attribute: got null data for key with a size - check %s, namespace %s, key %s - skipping\n",
        uuid_str, (key_data->ns) ? key_data->ns : "<null>", key_data->key);
    return 0;
  }
  if ((mdb_data.mv_data != NULL) && (mdb_data.mv_size > 0)) {
    val = (char *)calloc(1, mdb_data.mv_size + 1);
    memcpy(val, mdb_data.mv_data, mdb_data.mv_size);
  }
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
  char *val = NULL;
  if ((mdb_data.mv_data == NULL) && (mdb_data.mv_size > 0)) {
    /* If data is null, but we have a size, something is wrong - just skip
     * this key */
    char uuid_str[UUID_STR_LEN + 1];
    mtev_uuid_unparse_lower(key_data->id, uuid_str);
    mtevL(mtev_error, "noit_check_lmdb_add_config: got null data for key with a size - check %s, namespace %s, key %s - skipping\n",
        uuid_str, (key_data->ns) ? key_data->ns : "<null>", key_data->key);
  }
  if ((mdb_data.mv_data != NULL) && (mdb_data.mv_size > 0)) {
    val = (char *)calloc(1, mdb_data.mv_size + 1);
    memcpy(val, mdb_data.mv_data, mdb_data.mv_size);
  }
  xmlNodePtr child = NULL;
  if (key_data->ns == NULL) {
    if(xmlValidateNameValue((xmlChar *)key_data->key)) {
      child = xmlNewNode(NULL, (xmlChar *)key_data->key);
      xmlNodeAddContent(child, (xmlChar *)val);
    } else {
      child = xmlNewNode(NULL, (xmlChar *)"value");
      xmlSetProp(child, (xmlChar *)"name", (xmlChar *)key_data->key);
      xmlNodeAddContent(child, (xmlChar *)val);
    }
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
      mtevL(mtev_debug, "namespace %s configured in check, but not loaded - skipping\n", key_data->ns);
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
  char uuid_str[UUID_STR_LEN+1];
  xmlNodePtr attr = NULL, config = NULL;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  MDB_val mdb_key, mdb_data;
  char *key = NULL;
  size_t key_size;
  bool locked = false;
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();

  mtevAssert(instance != NULL);

  key = noit_lmdb_make_check_key_for_iterating(checkid, &key_size);
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

  pthread_rwlock_rdlock(&instance->lock);
  locked = true;

  mdb_txn_begin(instance->env, NULL, MDB_RDONLY, &txn);
  mdb_cursor_open(txn, instance->dbi, &cursor);
  rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_SET_RANGE);
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
  
  /* Go ahead and set the UUID before we start */
  mtev_uuid_unparse_lower(checkid, uuid_str);
  if (separate_attributes) {
    xmlNodePtr child = xmlNewNode(NULL, (xmlChar *)"uuid");
    xmlNodeAddContent(child, (xmlChar *)uuid_str);
    xmlAddChild(attr, child);
  }
  else {
    xmlSetProp(root, (xmlChar *)"uuid", (xmlChar *)uuid_str);
  }

  while(rc == 0) {
    noit_lmdb_check_data_t *data = noit_lmdb_check_data_from_key(mdb_key.mv_data);
    if (data) {
      if (memcmp(data->id, checkid, UUID_SIZE) != 0) {
        noit_lmdb_free_check_data(data);
        break;
      }
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
  mdb_cursor_close(cursor);
  mdb_txn_abort(txn);
  if (locked) {
    pthread_rwlock_unlock(&instance->lock);
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
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();

  mtevAssert(instance != NULL);

  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlNewDocNode(doc, NULL, (xmlChar *)"checks", NULL);
  xmlDocSetRootElement(doc, root);

  mtev_conf_section_t checks = mtev_conf_get_section_read(MTEV_CONF_ROOT, "/noit/checks");
  xmlNodePtr checks_xmlnode = mtev_conf_section_to_xmlnodeptr(checks);
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

  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_ok(ctx, "text/xml");
  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);
  goto cleanup;

 error:
  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_standard(ctx, error_code, "ERROR", "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if (doc) xmlFreeDoc(doc);
  mtev_conf_release_section_read(checks);

  return 0;
}

int noit_check_lmdb_show_check(mtev_http_rest_closure_t *restc, int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  xmlDocPtr doc = NULL;
  xmlNodePtr root, state;
  int error_code = 500;
  uuid_t checkid;
  noit_check_t *check;
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
    noit_check_set_db_source_header(restc->http_ctx);
    mtev_http_response_standard(ctx, redirect ? 302 : 200, "NOT IT", "text/xml");
  }
  else {
    noit_check_set_db_source_header(restc->http_ctx);
    mtev_http_response_ok(ctx, "text/xml");
  }

  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);
  goto cleanup;

 not_found:
  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_not_found(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 error:
  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_standard(ctx, error_code, "ERROR", "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  if(doc) xmlFreeDoc(doc);
  noit_check_deref(check);
  return 0;
}

static int
noit_check_lmdb_configure_check(uuid_t checkid, xmlNodePtr a, xmlNodePtr c, int64_t old_seq) {
  xmlNodePtr node;
  int rc = 0;
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();
  mtev_hash_table conf_table;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  char *key, *val;
  size_t key_size;
  MDB_val mdb_key, mdb_data;
  mtev_hash_iter iter;
  const char *_hash_iter_key;
  int _hash_iter_klen;

  mtevAssert(instance != NULL);
  mtevAssert(old_seq >= 0);

put_retry:
  txn = NULL;
  cursor = NULL;
  key = NULL;
  val = NULL;
  key_size = 0;
  memset(&iter, 0, sizeof(mtev_hash_iter));

  pthread_rwlock_rdlock(&instance->lock);
  noit_lmdb_check_keys_to_hash_table(instance, &conf_table, checkid, true);
  rc = mdb_txn_begin(instance->env, NULL, 0, &txn);
  if (rc != 0) {
    mtevFatal(mtev_error, "failure on txn begin - %d (%s)\n", rc, mdb_strerror(rc));
  }
  rc = mdb_cursor_open(txn, instance->dbi, &cursor);
  if (rc != 0) {
    mtevFatal(mtev_error, "failure on cursor open - %d (%s)\n", rc, mdb_strerror(rc));
  }

  for (node = a->children; node; node = node->next) {
#define ATTR2LMDB(attr_name) do { \
  if(!strcmp((char *)node->name, #attr_name)) { \
    val = (char *)xmlNodeGetContent(node); \
    if (val) { \
      key = noit_lmdb_make_check_key(checkid, NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, #attr_name, &key_size); \
      mtevAssert(key); \
      mdb_key.mv_data = key; \
      mdb_key.mv_size = key_size; \
      mdb_data.mv_data = val; \
      mdb_data.mv_size = strlen(val); \
      rc = mdb_cursor_put(cursor, &mdb_key, &mdb_data, 0); \
      if (rc == MDB_MAP_FULL) { \
        mdb_cursor_close(cursor); \
        mdb_txn_abort(txn); \
        mtev_hash_destroy(&conf_table, free, NULL); \
        free(key); \
        xmlFree(val); \
        pthread_rwlock_unlock(&instance->lock); \
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
    ATTR2LMDB(transient_min_period);
    ATTR2LMDB(transient_period_granularity);
    if (!strcmp((char *)node->name, "seq")) {
      xmlChar *v = xmlNodeGetContent(node);
      int64_t new_seq = strtoll((const char *)v, NULL, 10);
      xmlFree(v);
      if (new_seq < 0) {
        new_seq = 0;
      }
      /* If the new sequence is greater than/equal to the old one, that's
       * an error */
      if ((old_seq) && (old_seq >= new_seq)) {
        mtev_hash_destroy(&conf_table, free, NULL);
        mdb_cursor_close(cursor);
        mdb_txn_abort(txn);
        pthread_rwlock_unlock(&instance->lock);
        return -1;
      }
    }
  }

  if (c) {
    key = NULL;
    val = NULL;
    for(node = c->children; node; node = node->next) {
      val = (char *)xmlNodeGetContent(node);
      if (val != NULL) {
        char *prefix = NULL;
        if (node->ns) {
          prefix = (char *)node->ns->prefix;
        }
        /* If there's an attribute here, use it as the key - otherwise, use node->name */
        xmlAttr* attribute = node->properties;
        if (attribute) {
          xmlChar* value = xmlNodeListGetString(node->doc, attribute->children, 1);
          key = noit_lmdb_make_check_key(checkid, NOIT_LMDB_CHECK_CONFIG_TYPE, prefix, (char *)value, &key_size);
          xmlFree(value);
        }
        else {
          key = noit_lmdb_make_check_key(checkid, NOIT_LMDB_CHECK_CONFIG_TYPE, prefix, (char *)node->name, &key_size);
        }
        mtevAssert(key);

        mdb_key.mv_data = key;
        mdb_key.mv_size = key_size;
        mdb_data.mv_data = val;
        mdb_data.mv_size = strlen(val);
        rc = mdb_cursor_put(cursor, &mdb_key, &mdb_data, 0);
        if (rc == MDB_MAP_FULL) {
          mdb_cursor_close(cursor);
          mdb_txn_abort(txn);
          mtev_hash_destroy(&conf_table, free, NULL);
          free(key);
          xmlFree(val);
          pthread_rwlock_unlock(&instance->lock);
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
        mdb_cursor_close(cursor);
        mdb_txn_abort(txn);
        mtev_hash_destroy(&conf_table, free, NULL);
        pthread_rwlock_unlock(&instance->lock);
        noit_lmdb_resize_instance(instance);
        goto put_retry;
      }
      else if (rc != MDB_NOTFOUND) {
        mtevL(mtev_error, "failed to delete key: %d (%s)\n", rc, mdb_strerror(rc));
      }
    }
  }
  mdb_cursor_close(cursor);
  rc = mdb_txn_commit(txn);
  if (rc == MDB_MAP_FULL) {
    mtev_hash_destroy(&conf_table, free, NULL);
    pthread_rwlock_unlock(&instance->lock);
    noit_lmdb_resize_instance(instance);
    goto put_retry;
  }
  else if (rc != 0) {
    mtevFatal(mtev_error, "failure on txn commmit - %d (%s)\n", rc, mdb_strerror(rc));
  }
  mtev_hash_destroy(&conf_table, free, NULL);
  pthread_rwlock_unlock(&instance->lock);
  return 0;
}

typedef struct lmdb_set_check_data {
  xmlNodePtr attr;
  xmlNodePtr config;
  uuid_t checkid;
  int error_code;
  char *error_string;
  mtev_http_rest_closure_t *restc;
  /* Store off the data/free from rest_get_xml_upload here */
  void *xml_data;
  void (*xml_data_free)(void *);
} lmdb_set_check_data_t;

static void
lmdb_set_check_data_free(void *c) {
  lmdb_set_check_data_t *lscd = (lmdb_set_check_data_t *)c;
  /* We are explicitly not freeing lscd->attr and lscd->config - those
   * will be freed when we finish with the restc struct */
  if (lscd) {
    free(lscd->error_string);
    if (lscd->xml_data_free) {
      lscd->xml_data_free(lscd->xml_data);
    }
    free(lscd);
  }
}

static int
noit_check_lmdb_set_check_complete(mtev_http_rest_closure_t *restc,
                                   int npats, char **pats) {
  lmdb_set_check_data_t *lscd = (lmdb_set_check_data_t *)restc->call_closure;
  if (lscd->error_string) {
    mtev_http_rest_closure_t *restc = lscd->restc;
    mtev_http_session_ctx *ctx = restc->http_ctx;
    noit_check_set_db_source_header(ctx);
    mtev_http_response_standard(ctx, lscd->error_code, "ERROR", "text/xml");
    xmlDocPtr doc = xmlNewDoc((xmlChar *)"1.0");
    xmlNodePtr root = xmlNewDocNode(doc, NULL, (xmlChar *)"error", NULL);
    xmlDocSetRootElement(doc, root);
    xmlNodeAddContent(root, (xmlChar *)lscd->error_string);
    mtev_http_response_xml(ctx, doc);
    mtev_http_response_end(ctx);
    xmlFreeDoc(doc);
    return 0;
  }
  else {
    return noit_check_lmdb_show_check(restc, npats, pats);
  }
  /* Unreachable */
  return 0;
}

static int
noit_check_lmdb_set_check_asynch(eventer_t e, int mask, void *closure,
                                 struct timeval *now) {

#define SET_ERROR_CODE(code, str) do { \
  lscd->error_code = code; \
  lscd->error_string = strdup(str); \
  return 0; \
} while(0);

  lmdb_set_check_data_t *lscd = (lmdb_set_check_data_t *)closure;
  mtev_http_rest_closure_t *restc = lscd->restc;
  mtev_http_session_ctx *ctx = restc->http_ctx;
  if(mask == EVENTER_ASYNCH_WORK) {
    int rc = 0;
    mtev_boolean exists = mtev_false;
    noit_check_t *outer_check = noit_poller_lookup(lscd->checkid);
    if(outer_check) {
      exists = mtev_true;
    }
    mtev_boolean in_db = noit_check_lmdb_already_in_db(lscd->checkid);
    if (!in_db) {
      if (exists) {
        noit_check_deref(outer_check);
        SET_ERROR_CODE(403, "uuid not yours");
      }
      int64_t old_seq = 0;
      char *target = NULL, *name = NULL, *module = NULL;
      noit_module_t *m = NULL;
      noit_check_t *check = NULL;
      /* make sure this isn't a dup */
      rest_check_get_attrs(lscd->attr, &target, &name, &module);
      exists = (!target || (check = noit_poller_lookup_by_name(target, name)) != NULL);
      if(check) {
        old_seq = check->config_seq;
      }
      if(module) {
        m = noit_module_lookup(module);
      }
      rest_check_free_attrs(target, name, module);
      if(exists) {
        noit_check_deref(check);
        noit_check_deref(outer_check);
        SET_ERROR_CODE(409, "target name already registered");
      }
      if(!m) {
        noit_check_deref(check);
        noit_check_deref(outer_check);
        SET_ERROR_CODE(412, "module does not exist");
      }
      rc = noit_check_lmdb_configure_check(lscd->checkid, lscd->attr, lscd->config, old_seq);
      if (rc) {
        noit_check_deref(check);
        noit_check_deref(outer_check);
        SET_ERROR_CODE(409, "sequencing error");
      }
    }
    if (exists) {
      int64_t old_seq = 0;
      int module_change;
      char *target = NULL, *name = NULL, *module = NULL, *old_seq_string = NULL;;
      noit_check_t *ocheck;
      if(!outer_check) {
        noit_check_deref(outer_check);
        SET_ERROR_CODE(500, "internal check error");
      }

      /* make sure this isn't a dup */
      rest_check_get_attrs(lscd->attr, &target, &name, &module);

      ocheck = noit_poller_lookup_by_name(target, name);
      module_change = strcmp(outer_check->module, module);
      rest_check_free_attrs(target, name, module);
      if(ocheck && ocheck != outer_check) {
        noit_check_deref(outer_check);
        noit_check_deref(ocheck);
        SET_ERROR_CODE(409, "new target`name would collide");
      }
      noit_check_deref(ocheck);
      if(module_change) {
        noit_check_deref(outer_check);
        SET_ERROR_CODE(400, "cannot change module");
      }
      old_seq_string = noit_check_lmdb_get_specific_field(lscd->checkid, NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "seq", mtev_false);
      if (old_seq_string) {
        old_seq = strtoll(old_seq_string, NULL, 10);
        if (old_seq < 0) {
          old_seq = 0;
        }
      }
      free(old_seq_string);
      rc = noit_check_lmdb_configure_check(lscd->checkid, lscd->attr, lscd->config, old_seq);
      if (rc) {
        noit_check_deref(outer_check);
        SET_ERROR_CODE(409, "sequencing error");
      }
    }

    noit_poller_reload_lmdb(&lscd->checkid, 1);
    noit_check_deref(outer_check);
  }
  if (mask == EVENTER_ASYNCH_COMPLETE) {
    mtev_http_session_resume_after_float(ctx);
  }
  return 0;
}

int
noit_check_lmdb_set_check(mtev_http_rest_closure_t *restc,
                          int npats, char **pats,
                          eventer_jobq_t *jobq) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  xmlDocPtr doc = NULL, indoc = NULL;
  xmlNodePtr root, attr, config;
  uuid_t checkid;
  int error_code = 500, complete = 0, mask = 0;
  const char *error = "internal error";
  lmdb_set_check_data_t *lscd = NULL;

#define GOTO_ERROR(ec, es) do { \
  error_code = ec; \
  error = es; \
  goto error; \
} while(0)

  if(npats != 2) {
    GOTO_ERROR(500, "internal error");
  }

  indoc = rest_get_xml_upload(restc, &mask, &complete);
  if(!complete) {
    return mask;
  }
  if(indoc == NULL) {
    GOTO_ERROR(400, "xml parse error");
  }
  if(!noit_validate_check_rest_post(indoc, &attr, &config, &error)) {
    GOTO_ERROR(500, "could not validate xml check");
  }

  if(mtev_uuid_parse(pats[1], checkid)) {
    GOTO_ERROR(500, "not a valid uuid");
  }

  lscd = (lmdb_set_check_data_t *)calloc(1, sizeof(*lscd));
  mtevAssert(lscd);

  lscd->error_code = 500;
  lscd->attr = attr;
  lscd->config = config;
  /* rest_get_xml_upload sets a closure and free function... we need to set
   * our own closure, though, so we need to save off what was there and call
   * it when we clean up */
  lscd->xml_data = restc->call_closure;
  lscd->xml_data_free = restc->call_closure_free;
  mtev_uuid_copy(lscd->checkid, checkid);
  lscd->restc = restc;

  restc->call_closure = lscd;
  restc->call_closure_free = lmdb_set_check_data_free;
  restc->fastpath = noit_check_lmdb_set_check_complete;

  eventer_t conne;
  eventer_t newe;
  mtev_http_connection *connection = mtev_http_session_connection(ctx);

  conne = mtev_http_connection_event_float(connection);
  if(conne) eventer_remove_fde(conne);

  newe = eventer_alloc_asynch(noit_check_lmdb_set_check_asynch, lscd);
  if(conne) eventer_set_owner(newe, eventer_get_owner(conne));
  eventer_add_asynch(jobq, newe);
  return 0;

 error:
  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_standard(ctx, error_code, "ERROR", "text/xml");
  doc = xmlNewDoc((xmlChar *)"1.0");
  root = xmlNewDocNode(doc, NULL, (xmlChar *)"error", NULL);
  xmlDocSetRootElement(doc, root);
  xmlNodeAddContent(root, (xmlChar *)error);
  mtev_http_response_xml(ctx, doc);
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  lmdb_set_check_data_free(lscd);
  if(doc) xmlFreeDoc(doc);

  return 0;
}

int
noit_check_lmdb_bump_seq_and_mark_deleted(uuid_t checkid) {
  int rc;
  MDB_val mdb_key, mdb_data;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  char *key = NULL;
  char buff[255];
  size_t key_size;
  int new_seq = -1;
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();
  mtevAssert(instance != NULL);

put_retry:
  new_seq = -1;
  txn = NULL;
  cursor = NULL;

  /* First, set deleted */
  key = noit_lmdb_make_check_key(checkid, NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "deleted", &key_size);
  mtevAssert(key);

  mdb_key.mv_data = key;
  mdb_key.mv_size = key_size;
  mdb_data.mv_data = "deleted";
  mdb_data.mv_size = 7;

  pthread_rwlock_rdlock(&instance->lock);
  rc = mdb_txn_begin(instance->env, NULL, 0, &txn);
  if (rc != 0) {
    mtevFatal(mtev_error, "failure on txn begin - %d (%s)\n", rc, mdb_strerror(rc));
  }
  rc = mdb_cursor_open(txn, instance->dbi, &cursor);
  if (rc != 0) {
    mtevFatal(mtev_error, "failure on cursor open - %d (%s)\n", rc, mdb_strerror(rc));
  }

  rc = mdb_cursor_put(cursor, &mdb_key, &mdb_data, 0);
  if (rc == MDB_MAP_FULL) {
    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    free(key);
    pthread_rwlock_unlock(&instance->lock);
    noit_lmdb_resize_instance(instance);
    goto put_retry;
  }
  else if (rc != 0) {
    mtevFatal(mtev_error, "failure on cursor put - %d (%s)\n", rc, mdb_strerror(rc));
  }

  /* Now, find the sequence number - set it to one if it doesn't exist, otherwise bump it by
   * one */

  free(key);
  key = noit_lmdb_make_check_key(checkid, NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "seq", &key_size);
  mtevAssert(key);

  mdb_key.mv_data = key;
  mdb_key.mv_size = key_size;
  rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_SET_KEY);
  if (rc != 0 && rc != MDB_NOTFOUND) {
    /* Weird error, abort */
    mtevFatal(mtev_error, "failure on cursor get - %d (%s)\n", rc, mdb_strerror(rc));
  }
  else if (rc != 0) {
    /* We didn't find it - set to one */
    mdb_key.mv_data = key;
    mdb_key.mv_size = key_size;
    mdb_data.mv_data = "1";
    mdb_key.mv_size = 1;
  }
  else {
    /* We found it - increment by one */
    char *val_string = NULL;
    bool allocated = false;

    if (mdb_data.mv_size < 10) {
      val_string = (char *)alloca(mdb_data.mv_size + 1);
    }
    else {
      val_string = (char *)calloc(1, mdb_data.mv_size + 1);
      allocated = true;
    }

    mdb_key.mv_data = key;
    mdb_key.mv_size = key_size;
    memcpy(val_string, mdb_data.mv_data, mdb_data.mv_size);
    val_string[mdb_data.mv_size] = 0;
    int64_t seq = strtoll(val_string, NULL, 10);
    if (seq >= 0) {
      seq++;
    }
    else {
      seq = 1;
    }
    snprintf(buff, sizeof(buff), "%" PRId64 "", seq);
    mdb_data.mv_data = buff;
    mdb_data.mv_size = strlen(buff);

    if (allocated) {
      free(val_string);
    }
    new_seq = seq;
  }

  rc = mdb_cursor_put(cursor, &mdb_key, &mdb_data, 0);
  if (rc == MDB_MAP_FULL) {
    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    free(key);
    pthread_rwlock_unlock(&instance->lock);
    noit_lmdb_resize_instance(instance);
    goto put_retry;
  }
  else if (rc != 0) {
    mtevFatal(mtev_error, "failure on cursor put - %d (%s)\n", rc, mdb_strerror(rc));
  }

  mdb_cursor_close(cursor);
  rc = mdb_txn_commit(txn);
  if (rc == MDB_MAP_FULL) {
    pthread_rwlock_unlock(&instance->lock);
    noit_lmdb_resize_instance(instance);
    goto put_retry;
  }
  else if (rc != 0) {
    mtevFatal(mtev_error, "failure on txn commmit - %d (%s)\n", rc, mdb_strerror(rc));
  }
  pthread_rwlock_unlock(&instance->lock);

  return new_seq;
}

int
noit_check_lmdb_remove_check_from_db(uuid_t checkid, mtev_boolean force) {
  int rc = 0;
  MDB_val mdb_key, mdb_data;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  char *key = NULL;
  size_t key_size;
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();
  mtevAssert(instance != NULL);

  key = noit_lmdb_make_check_key_for_iterating(checkid, &key_size);
  mtevAssert(key);

  mdb_key.mv_data = key;
  mdb_key.mv_size = key_size;

  pthread_rwlock_wrlock(&instance->lock);

  /* If we're forcing the delete, we don't care if it's marked deleted - just
   * delete it. Otherwise - make sure it's actually flagged */
  if (force == mtev_false) {
    char *deleted_string = noit_check_lmdb_get_specific_field(checkid, NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "deleted", mtev_true);
    if (!deleted_string) {
      /* "deleted" isn't set for some reason - the check has very likely been restored since being
       * flagged for delete, so bail */
      rc = -1;
      goto cleanup;
    }
    else {
      /* If it's set to "deleted" or "true", we continue - otherwise, we bail */
      if ((strcmp(deleted_string, "deleted")) && (strcmp(deleted_string, "true"))) {
        /* Anything but "deleted" or "true" and we bail */
        rc = -1;
        goto cleanup;
      }
    }
  }

  rc = mdb_txn_begin(instance->env, NULL, 0, &txn);
  if (rc != 0) {
    mtevL(mtev_error, "failed to create transaction for delete: %d (%s)\n", rc, mdb_strerror(rc));
    goto cleanup;
  }
  mdb_cursor_open(txn, instance->dbi, &cursor);
  rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_SET_RANGE);
  if (rc != 0) {
    if (rc == MDB_NOTFOUND) {
      mdb_cursor_close(cursor);
      mdb_txn_abort(txn);
      goto cleanup;
    }
    else {
      mtevL(mtev_error, "failed on delete lookup: %d (%s)\n", rc, mdb_strerror(rc));
      mdb_cursor_close(cursor);
      mdb_txn_abort(txn);
      goto cleanup;
    }
  }
  while(rc == 0) {
    noit_lmdb_check_data_t *data = noit_lmdb_check_data_from_key(mdb_key.mv_data);
    if (data) {
      if (memcmp(data->id, checkid, UUID_SIZE) != 0) {
        noit_lmdb_free_check_data(data);
        break;
      }
      noit_lmdb_free_check_data(data);
      rc = mdb_cursor_del(cursor, 0);
      if (rc != 0) {
        mtevL(mtev_error, "failed to delete key in check: %d (%s)\n", rc, mdb_strerror(rc));
        mdb_cursor_close(cursor);
        mdb_txn_abort(txn);
        goto cleanup;
      }
    }
    else {
      break;
    }
    rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_NEXT);
  }
  mdb_cursor_close(cursor);
  rc = mdb_txn_commit(txn);
  if (rc != 0) {
    mtevL(mtev_error, "failed to commit delete txn: %d (%s)\n", rc, mdb_strerror(rc));
    mdb_txn_abort(txn);
    goto cleanup;
  }
  rc = 0;

cleanup:
  pthread_rwlock_unlock(&instance->lock);
  return rc;
}

int
noit_check_lmdb_delete_check(mtev_http_rest_closure_t *restc,
                             int npats, char **pats) {
  mtev_http_session_ctx *ctx = restc->http_ctx;
  uuid_t checkid;
  noit_check_t *check = NULL;
  int rc, error_code = 500;
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();
  mtevAssert(instance != NULL);

  if(npats != 2) goto error;
  if(mtev_uuid_parse(pats[1], checkid)) goto error;

  check = noit_poller_lookup(checkid);

  /* delete this here */
  mtev_boolean just_mark = mtev_false;
  if(check) {
    if(!noit_poller_deschedule(check->checkid, mtev_true, mtev_false)) {
      just_mark = mtev_true;
    }
  }
  if(just_mark) {
    int new_seq = noit_check_lmdb_bump_seq_and_mark_deleted(checkid);
    if (new_seq <= 0) {
      new_seq = 1;
    }
    if (check) {
      check->config_seq = new_seq;
      noit_cluster_mark_check_changed(check, NULL);
    }
  }
  else {
    rc = noit_check_lmdb_remove_check_from_db(checkid, mtev_true);
    if (rc == MDB_NOTFOUND) {
      goto not_found;
    }
    else if (rc) {
      goto error;
    }
  }

  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_ok(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 not_found:
  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_not_found(ctx, "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 error:
  noit_check_set_db_source_header(restc->http_ctx);
  mtev_http_response_standard(ctx, error_code, "ERROR", "text/html");
  mtev_http_response_end(ctx);
  goto cleanup;

 cleanup:
  noit_check_deref(check);
  return 0;
}

static void
noit_check_lmdb_poller_process_all_checks() {
  int rc;
  uint32_t cnt = 0;
  uint64_t start, end, diff;
  double per_record;

  MDB_val mdb_key, mdb_data;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();

  mtevAssert(instance);

  mdb_key.mv_data = NULL;
  mdb_key.mv_size = 0;
  pthread_rwlock_rdlock(&instance->lock);

  mtevL(mtev_error, "begin loading checks from db\n");
  start = mtev_now_us();

  rc = mdb_txn_begin(instance->env, NULL, 0, &txn);
  if (rc != 0) {
    pthread_rwlock_unlock(&instance->lock);
    mtevL(mtev_error, "failed to create transaction for processing all checks: %d (%s)\n", rc, mdb_strerror(rc));
    return;
  }
  mdb_cursor_open(txn, instance->dbi, &cursor);
  rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_FIRST);

  while (rc == 0) {
    uuid_t checkid;
    /* The start of the key is always a uuid */
    mtev_uuid_copy(checkid, mdb_key.mv_data);
    rc = noit_poller_lmdb_create_check_from_database_locked(cursor, checkid);
    if (rc == 0) {
      rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_GET_CURRENT);
    }
    cnt++;
  }
  mdb_cursor_close(cursor);
  mdb_txn_abort(txn);
  pthread_rwlock_unlock(&instance->lock);
  end = mtev_now_us();
  diff = (end - start) / 1000;
  if (cnt) {
    per_record = ((double)(end-start) / (double)cnt) / 1000.0;
  }
  else {
    per_record = 0;
  }

  mtevL(mtev_error, "finished loading %" PRIu32 " checks from db - took %" PRIu64 " ms (average %0.4f ms per check)\n",
    cnt, diff, per_record);
}
static void
noit_check_lmdb_poller_process_check(uuid_t checkid) {
  int rc;
  char *key = NULL;
  size_t key_size;
  MDB_val mdb_key, mdb_data;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();

  mtevAssert(instance);

  key = noit_lmdb_make_check_key_for_iterating(checkid, &key_size);
  mtevAssert(key);

  mdb_key.mv_data = key;
  mdb_key.mv_size = key_size;
  pthread_rwlock_rdlock(&instance->lock);

  rc = mdb_txn_begin(instance->env, NULL, 0, &txn);
  if (rc != 0) {
    pthread_rwlock_unlock(&instance->lock);
    mtevL(mtev_error, "failed to create transaction for processing all checks: %d (%s)\n", rc, mdb_strerror(rc));
    return;
  }
  mdb_cursor_open(txn, instance->dbi, &cursor);
  rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_SET_RANGE);
  if (rc == 0) {
    uuid_t db_checkid;
    /* The start of the key is always a uuid */
    mtev_uuid_copy(db_checkid, mdb_key.mv_data);
    if (mtev_uuid_compare(checkid, db_checkid) == 0) {
      noit_poller_lmdb_create_check_from_database_locked(cursor, checkid);
    }
  }
  mdb_cursor_close(cursor);
  mdb_txn_abort(txn);
  pthread_rwlock_unlock(&instance->lock);
}
void
noit_check_lmdb_poller_process_checks(uuid_t *uuids, int uuid_cnt) {
  int i = 0;
  if (!uuids) {
    noit_check_lmdb_poller_process_all_checks();
  }
  else {
    for (i = 0; i < uuid_cnt; i++) {
      noit_check_lmdb_poller_process_check(uuids[i]);
    }
  }
}

static int
noit_check_lmdb_convert_one_xml_check_to_lmdb(mtev_conf_section_t section, char **namespaces, int namespace_cnt, char **error) {
  int i = 0;
  char uuid_str[37];
  char target[256] = "";
  char module[256] = "";
  char name[256] = "";
  char filterset[256] = "";
  char oncheck[1024] = "";
  char resolve_rtype[16] = "";
  char period_string[16] = "";
  char timeout_string[16] = "";
  char transient_min_period_string[16] = "";
  char transient_period_granularity_string[16] = "";
  char seq_string[16] = "";
  char delstr[8] = "";
  uuid_t checkid;
  int64_t config_seq = 0;
  mtev_boolean deleted = mtev_false, disabled = mtev_false;
  int minimum_period = 1000, maximum_period = 300000, period = 0, timeout = 0;
  int transient_min_period = 0, transient_period_granularity = 0;
  int no_period = 0, no_oncheck = 0;
  mtev_hash_table *options;
  mtev_hash_iter iter;
  const char *_hash_iter_key;
  int _hash_iter_klen;
  char *config_value = NULL;

  /* We only want to write to the config if the attribute is locally
   * defined... track whether we've defined things locally (rather than
   * inheriting them) so we know whether to write or not */
  mtev_boolean target_locally_defined = mtev_true;
  mtev_boolean module_locally_defined = mtev_true;
  mtev_boolean filterset_locally_defined = mtev_true;
  mtev_boolean period_locally_defined = mtev_true;
  mtev_boolean timeout_locally_defined = mtev_true;
  mtev_boolean resolve_rtype_locally_defined = mtev_true;
  mtev_boolean oncheck_locally_defined = mtev_true;
  mtev_boolean deleted_locally_defined = mtev_true;
  mtev_boolean transient_min_period_locally_defined = mtev_true;
  mtev_boolean transient_period_granularity_locally_defined = mtev_true;

  int rc;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  MDB_val mdb_key, mdb_data;
  char *key = NULL;
  size_t key_size;
  mtev_hash_table conf_table;

  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();
  mtevAssert(instance != NULL);

  mtevAssert(error != NULL);
  if (*error) {
    free(*error);
    *error = NULL;
  }

  /* We want to heartbeat here... otherwise, if a lot of checks are 
   * configured or if we're running on a slower system, we could 
   * end up getting watchdog killed before we get a chance to run 
   * any checks */
  mtev_watchdog_child_heartbeat();

#define MYATTR(type,a,...) mtev_conf_get_##type(section, "@" #a, __VA_ARGS__)
#define INHERIT(type,a,...) \
  mtev_conf_get_##type(section, "ancestor::node()/@" #a, __VA_ARGS__)

  if(!MYATTR(stringbuf, uuid, uuid_str, sizeof(uuid_str))) {
    *error = strdup("check has no uuid");
    return -1;
  }
  MYATTR(int64, seq, &config_seq);

  if(mtev_uuid_parse(uuid_str, checkid)) {
    if(asprintf(error, "check uuid '%s' is invalid", uuid_str) < 0)
      *error = strdup("check uuid is invalid");
    return -1;
  }
  memset(delstr, 0, sizeof(delstr));
  if (!MYATTR(stringbuf, deleted, delstr, sizeof(delstr))) {
    deleted_locally_defined = mtev_false;
  }
  if (!strcmp(delstr, "deleted")) {
    deleted = mtev_true;
  }

  if(!MYATTR(stringbuf, target, target, sizeof(target))) {
    target_locally_defined = mtev_false;
    if(!INHERIT(stringbuf, target, target, sizeof(target))) {
      if(!deleted) {
        if(asprintf(error, "check uuid '%s' has no target", uuid_str) < 0)
          *error = strdup("check has no target");
        return -1;
      }
    }
  }

  if(!noit_check_validate_target(target)) {
    if(!deleted) {
      if(asprintf(error, "check uuid '%s' has malformed target", uuid_str) < 0)
        *error = strdup("check has malformed target");
      return -1;
    }
  }

  if(!MYATTR(stringbuf, module, module, sizeof(module))) {
    module_locally_defined = mtev_false;
    if(!INHERIT(stringbuf, module, module, sizeof(module))) {
      if(!deleted) {
        if(asprintf(error, "check uuid '%s' has no module", uuid_str) < 0)
          *error = strdup("check has no module");
        return -1;
      }
    }
  }

  if(!MYATTR(stringbuf, filterset, filterset, sizeof(filterset))) {
    filterset_locally_defined = mtev_false;;
    if(!INHERIT(stringbuf, filterset, filterset, sizeof(filterset))) {
      filterset[0] = '\0';
    }
  }

  if (!MYATTR(stringbuf, resolve_rtype, resolve_rtype, sizeof(resolve_rtype))) {
    resolve_rtype_locally_defined = mtev_false;
    INHERIT(stringbuf, resolve_rtype, resolve_rtype, sizeof(resolve_rtype));
  }

  if(!MYATTR(stringbuf, name, name, sizeof(name))) {
    strlcpy(name, module, sizeof(name));
  }

  if(!noit_check_validate_name(name)) {
    if(!deleted) {
      if(asprintf(error, "check uuid '%s' has malformed name", uuid_str) < 0)
        *error = strdup("check has malformed name");
      return -1;
    }
  }

  INHERIT(int32, minimum_period, &minimum_period);
  INHERIT(int32, maximum_period, &maximum_period);
  if(!MYATTR(int32, period, &period)) {
    period_locally_defined = mtev_false;
    if(!INHERIT(int32, period, &period)) {
      period = 0;
    }
  }
  if (period == 0) {
    no_period = 1;
  }
  else {
    if(period < minimum_period) {
      period = minimum_period;
    }
    if(period > maximum_period) {
      period = maximum_period;
    }
  }
  
  /* We don't care if this is inherited, it's only for setting the database - if it's
   * not specifically ours, we can skip it */
  if(!MYATTR(int32, transient_min_period, &transient_min_period)) {
    transient_min_period_locally_defined = mtev_false;
  }
  if(!MYATTR(int32, transient_period_granularity, &transient_period_granularity)) {
    transient_period_granularity_locally_defined = mtev_false;
  }

  if(!MYATTR(stringbuf, oncheck, oncheck, sizeof(oncheck)) || !oncheck[0]) {
    oncheck_locally_defined = mtev_false;
    if(!INHERIT(stringbuf, oncheck, oncheck, sizeof(oncheck)) || !oncheck[0]) {
      no_oncheck = 1;
    }
  }

  if(deleted) {
    memcpy(target, "none", 5);
    target_locally_defined = mtev_true;
    mtev_uuid_unparse_lower(checkid, name);
  }
  else {
    if(no_period && no_oncheck) {
      if(asprintf(error, "check uuid '%s' has neither period nor oncheck", uuid_str) < 0)
        *error = strdup("check has neither period nor oncheck");
      return -1;
    }
    if(!(no_period || no_oncheck)) {
      if(asprintf(error, "check uuid '%s' has oncheck and period", uuid_str) < 0)
        *error = strdup("check has both oncheck and period");
      return -1;
    }
    if(!MYATTR(int32, timeout, &timeout)) {
      if(!INHERIT(int32, timeout, &timeout)) {
        if(asprintf(error, "check uuid '%s' has no timeout", uuid_str) < 0)
          *error = strdup("check has no timeout");
        return -1;
      }
    }
    if(timeout < 0) {
      timeout = 0;
    }
    if(!no_period && timeout >= period) {
      mtevL(mtev_error, "check uuid: '%s' timeout > period\n", uuid_str);
      timeout = period/2;
    }
    INHERIT(boolean, disable, &disabled);
  }
  snprintf(seq_string, sizeof(seq_string), "%" PRId64 "", config_seq);
  snprintf(period_string, sizeof(period_string), "%d", period);
  snprintf(timeout_string, sizeof(timeout_string), "%d", timeout);
  if (transient_min_period_locally_defined) {
    snprintf(transient_min_period_string, sizeof(transient_min_period_string),
            "%d", transient_min_period);
  }
  if (transient_period_granularity_locally_defined) {
    snprintf(transient_period_granularity_string, sizeof(transient_period_granularity_string),
            "%d", transient_period_granularity);
  }

  options = mtev_conf_get_hash(section, "config");

#define WRITE_ATTR_TO_LMDB(type, ns, name, value, defined_locally, allocated) do { \
  if (defined_locally == mtev_true) { \
    key = noit_lmdb_make_check_key(checkid, type, ns, name, &key_size); \
    mtevAssert(key); \
    mdb_key.mv_data = key; \
    mdb_key.mv_size = key_size; \
    if (value == NULL) { \
      mdb_data.mv_size = 0; \
      mdb_data.mv_data = NULL; \
    } \
    else { \
      mdb_data.mv_size = strlen(value); \
      if (mdb_data.mv_size == 0) { \
        mdb_data.mv_data = NULL; \
      } \
      else { \
        mdb_data.mv_data = value; \
      } \
    } \
    rc = mdb_cursor_put(cursor, &mdb_key, &mdb_data, 0); \
    if (rc == MDB_MAP_FULL) { \
      mdb_cursor_close(cursor); \
      mdb_txn_abort(txn); \
      free(key); \
      if (allocated) { \
        free(name); \
      } \
      mtev_hash_destroy(&conf_table, free, NULL); \
      pthread_rwlock_unlock(&instance->lock); \
      noit_lmdb_resize_instance(instance); \
      goto put_retry; \
    } \
    else if (rc != 0) { \
      mtevFatal(mtev_error, "failure on cursor put - %d (%s)\n", rc, mdb_strerror(rc)); \
    } \
    mtev_hash_delete(&conf_table, key, key_size, free, NULL); \
    free(key); \
    key = NULL; \
    if (allocated) { \
      free(name); \
    } \
  } \
  else if (allocated) { \
    free(name); \
  } \
} while(0)

put_retry:
  mtev_watchdog_child_heartbeat();
  key = NULL;
  txn = NULL;
  cursor = NULL;
  pthread_rwlock_rdlock(&instance->lock);

  noit_lmdb_check_keys_to_hash_table(instance, &conf_table, checkid, true);

  rc = mdb_txn_begin(instance->env, NULL, 0, &txn);
  if (rc != 0) {
    mtevFatal(mtev_error, "failure on txn begin - %d (%s)\n", rc, mdb_strerror(rc));
  }
  rc = mdb_cursor_open(txn, instance->dbi, &cursor);
  if (rc != 0) {
    mtevFatal(mtev_error, "failure on cursor open - %d (%s)\n", rc, mdb_strerror(rc));
  }

  WRITE_ATTR_TO_LMDB(NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "target", target, target_locally_defined, false);
  WRITE_ATTR_TO_LMDB(NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "module", module, module_locally_defined, false);
  /* "name" is always set */
  WRITE_ATTR_TO_LMDB(NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "name", name, mtev_true, false);
  WRITE_ATTR_TO_LMDB(NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "filterset", filterset, filterset_locally_defined, false);
  /* "seq" is always set */
  WRITE_ATTR_TO_LMDB(NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "seq", seq_string, mtev_true, false);
  WRITE_ATTR_TO_LMDB(NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "period", period_string, period_locally_defined, false);
  WRITE_ATTR_TO_LMDB(NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "timeout", timeout_string, timeout_locally_defined, false);
  WRITE_ATTR_TO_LMDB(NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "resolve_rtype", resolve_rtype, resolve_rtype_locally_defined, false);
  WRITE_ATTR_TO_LMDB(NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "oncheck", oncheck, oncheck_locally_defined, false);
  WRITE_ATTR_TO_LMDB(NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "deleted", delstr, deleted_locally_defined, false);
  WRITE_ATTR_TO_LMDB(NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "transient_min_period", transient_min_period_string,
    transient_min_period_locally_defined, false);
  WRITE_ATTR_TO_LMDB(NOIT_LMDB_CHECK_ATTRIBUTE_TYPE, NULL, "transient_period_granularity", transient_period_granularity_string,
    transient_period_granularity_locally_defined, false);

  memset(&iter, 0, sizeof(mtev_hash_iter));
  while(mtev_hash_next(options, &iter, &_hash_iter_key, &_hash_iter_klen, (void **)&config_value)) {
    char *config_name = (char *)calloc(1, _hash_iter_klen+1);
    memcpy(config_name, _hash_iter_key, _hash_iter_klen);
    /* There are always defined locally by definition - always pass "mtev_true" for that parameter */
    WRITE_ATTR_TO_LMDB(NOIT_LMDB_CHECK_CONFIG_TYPE, NULL, config_name, config_value, mtev_true, true);
  }

  for (i=0; i < namespace_cnt; i++) {
    char *namespace = namespaces[i];
    mtev_hash_table *ns_options = mtev_conf_get_namespaced_hash(section, "config",
                                    namespace);
    if (ns_options) {
      memset(&iter, 0, sizeof(mtev_hash_iter));
      while(mtev_hash_next(ns_options, &iter, &_hash_iter_key, &_hash_iter_klen, (void **)&config_value)) {
        char *config_name = (char *)calloc(1, _hash_iter_klen+1);
        memcpy(config_name, _hash_iter_key, _hash_iter_klen);
        /* There are always defined locally by definition - always pass "mtev_true" for that parameter */
        WRITE_ATTR_TO_LMDB(NOIT_LMDB_CHECK_CONFIG_TYPE, namespace, config_name, config_value, mtev_true, true);
      }
    }
  }

  memset(&iter, 0, sizeof(mtev_hash_iter));
  void *unused;
  while(mtev_hash_next(&conf_table, &iter, &_hash_iter_key, &_hash_iter_klen, &unused)) {
    mdb_key.mv_data = (char *)_hash_iter_key;
    mdb_key.mv_size = _hash_iter_klen;
    rc = mdb_del(txn, instance->dbi, &mdb_key, NULL);
    if (rc != 0) {
      if (rc == MDB_MAP_FULL) {
        mdb_cursor_close(cursor);
        mdb_txn_abort(txn);
        mtev_hash_destroy(&conf_table, free, NULL);
        pthread_rwlock_unlock(&instance->lock);
        noit_lmdb_resize_instance(instance);
        goto put_retry;
      }
      else if (rc != MDB_NOTFOUND) {
        mtevL(mtev_error, "failed to delete key: %d (%s)\n", rc, mdb_strerror(rc));
      }
    }
  }

  mdb_cursor_close(cursor);
  rc = mdb_txn_commit(txn);
  if (rc == MDB_MAP_FULL) {
    mtev_hash_destroy(&conf_table, free, NULL);
    pthread_rwlock_unlock(&instance->lock);
    noit_lmdb_resize_instance(instance);
    goto put_retry;
  }
  else if (rc != 0) {
    mtevFatal(mtev_error, "failure on txn commmit - %d (%s)\n", rc, mdb_strerror(rc));
  }
  mtev_hash_destroy(&conf_table, free, NULL);
  pthread_rwlock_unlock(&instance->lock);
  return 0;
}

void
noit_check_lmdb_migrate_xml_checks_to_lmdb() {
  int cnt, i, namespace_cnt;
  const char *xpath = "/noit/checks//check";
  char **namespaces = noit_check_get_namespaces(&namespace_cnt);
  mtev_conf_section_t *sec = mtev_conf_get_sections_write(MTEV_CONF_ROOT, xpath, &cnt);

  if (cnt) {
    mtevL(mtev_error, "converting %d xml checks to lmdb\n", cnt);
  }
  for(i=0; i<cnt; i++) {
    char *error = NULL;
    int rv = noit_check_lmdb_convert_one_xml_check_to_lmdb(sec[i], namespaces, namespace_cnt, &error);
    if (rv || error) {
      mtevL(mtev_error, "noit_check_lmdb_process_repl: failed to convert check: %s\n", error ? error : "(unknown error)");
      free(error);
    }
    else {
      CONF_REMOVE(sec[i]);
      xmlUnlinkNode(mtev_conf_section_to_xmlnodeptr(sec[i]));
      xmlFreeNode(mtev_conf_section_to_xmlnodeptr(sec[i]));
    }
  }
  mtev_conf_release_sections_write(sec, cnt);
  for(i=0; i<namespace_cnt; i++) {
    free(namespaces[i]);
  }
  free(namespaces);
  mtev_conf_mark_changed();
  if(mtev_conf_write_file(NULL) != 0) {
    mtevL(mtev_error, "local config write failed\n");
  }
  if (cnt) {
    mtevL(mtev_error, "done converting %d xml checks to lmdb\n", cnt);
  }
}

int
noit_check_lmdb_process_repl(xmlDocPtr doc) {
  int i = 0, j = 0, namespace_cnt = 0;
  xmlNodePtr root = NULL, child = NULL, next = NULL;
  mtev_conf_section_t section;
  char **namespaces = noit_check_get_namespaces(&namespace_cnt);

  root = xmlDocGetRootElement(doc);
  mtev_conf_section_t checks = mtev_conf_get_section_write(MTEV_CONF_ROOT, "/noit/checks");
  mtevAssert(!mtev_conf_section_is_empty(checks));
  for(child = xmlFirstElementChild(root); child; child = next) {
    next = xmlNextElementSibling(child);

    uuid_t checkid;
    int64_t seq;
    char uuid_str[UUID_STR_LEN+1], seq_str[32];
    char *error = NULL;
    section = mtev_conf_section_from_xmlnodeptr(child);
    mtevAssert(mtev_conf_get_stringbuf(section, "@uuid",
                                       uuid_str, sizeof(uuid_str)));
    mtevAssert(mtev_uuid_parse(uuid_str, checkid) == 0);
    mtevAssert(mtev_conf_get_stringbuf(section, "@seq",
                                       seq_str, sizeof(seq_str)));
    seq = strtoll(seq_str, NULL, 10);

    noit_check_t *check = noit_poller_lookup(checkid);

    /* too old, don't bother */
    if(check && check->config_seq >= seq) {
      noit_check_deref(check);
      i++;
      continue;
    }
    noit_check_deref(check);
    int rv = noit_check_lmdb_convert_one_xml_check_to_lmdb(section, namespaces, namespace_cnt, &error);
    if (rv || error) {
      mtevL(mtev_error, "noit_check_lmdb_process_repl: failed to convert check: %s\n", error ? error : "(unknown error)");
      free(error);
    }
    else {
      noit_check_lmdb_poller_process_checks(&checkid, 1);
    }

    i++;
  }
  mtev_conf_release_section_write(checks);
  for(j=0; j<namespace_cnt; j++) {
    free(namespaces[j]);
  }
  free(namespaces);
  return i;
}

mtev_boolean
noit_check_lmdb_already_in_db(uuid_t checkid) {
  int rc;
  mtev_boolean toRet = mtev_false;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  MDB_val mdb_key, mdb_data;
  char *key = NULL;
  size_t key_size;
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();

  mtevAssert(instance != NULL);

  key = noit_lmdb_make_check_key_for_iterating(checkid, &key_size);
  mtevAssert(key);

  mdb_key.mv_data = key;
  mdb_key.mv_size = key_size;

  pthread_rwlock_rdlock(&instance->lock);

  mdb_txn_begin(instance->env, NULL, MDB_RDONLY, &txn);
  mdb_cursor_open(txn, instance->dbi, &cursor);
  rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_SET_RANGE);
  if (rc == 0) {
    if ((mdb_key.mv_size >= UUID_SIZE) && (mdb_key.mv_data != NULL) && (mtev_uuid_compare(checkid, mdb_key.mv_data) == 0)) {
      toRet = mtev_true;
    }
  }

  mdb_cursor_close(cursor);
  mdb_txn_abort(txn);

  pthread_rwlock_unlock(&instance->lock);
  free(key);
  return toRet;
}

char *
noit_check_lmdb_get_specific_field(uuid_t checkid, noit_lmdb_check_type_e search_type, char *search_namespace, char *search_key,
                                  mtev_boolean locked) {
  int rc;
  char *toRet = NULL;
  MDB_txn *txn = NULL;
  MDB_cursor *cursor = NULL;
  MDB_val mdb_key, mdb_data;
  char *key = NULL;
  size_t key_size;
  noit_lmdb_instance_t *instance = noit_check_get_lmdb_instance();

  mtevAssert(instance != NULL);

  key = noit_lmdb_make_check_key(checkid, search_type, search_namespace, search_key, &key_size);
  mtevAssert(key);

  mdb_key.mv_data = key;
  mdb_key.mv_size = key_size;

  /* Take the lock if we're not already locked */
  if (locked == mtev_false) {
    pthread_rwlock_rdlock(&instance->lock);
  }

  mdb_txn_begin(instance->env, NULL, MDB_RDONLY, &txn);
  mdb_cursor_open(txn, instance->dbi, &cursor);
  rc = mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_SET_KEY);

  if (rc == 0) {
    toRet = (char *)calloc(1, mdb_data.mv_size + 1);
    memcpy(toRet, mdb_data.mv_data, mdb_data.mv_size);
  }

  mdb_cursor_close(cursor);
  mdb_txn_abort(txn);

  /* If we locked in this function, we have to unlock here */
  if (locked == mtev_false) {
    pthread_rwlock_unlock(&instance->lock);
  }

  free(key);
  return toRet;
}
