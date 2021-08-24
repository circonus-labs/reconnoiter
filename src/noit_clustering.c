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

/* BIG IDEA:
 *
 * The ideal behind clustering in reconnoiter is to ensure that if a single
 * instance of reconnoiter goes down, another instance will continue running
 * the checks.  This is more complicated for push checks (data sent to broker)
 * than for pull checks (broker queries for data).
 *
 * Regardless, there are two things in reconnoiter that make checks "go":
 * (1) checks, (2) filtersets.  In order to synchronize broker configuration
 * we need to replicate these two things.  It is up to the operator to make
 * sure the noitd's are all configured similarly enough to run the checks.
 *
 * clustering leverages libmtev "cluster" tech.
 * Each time the configuration of the cluster changes, node state is cleared.
 * Each time a node changes state, that node state is cleared.
 * Each noitd maintains an outbound queue of changes to filtersets and checks
 * from which other noitds consume.
 * When the state of a node is cleared, all checks and all filtersets are
 * considered changed and queued for that broker.
 *
 * The state is small enough on the brokers that simple "replay from scratch"
 * is tenable.
 * Checks and filtersets have a "seq" that must increase which resolves
 * conflicts. The operator is responible for assigned "seq" numbers.
 */

#include <mtev_defines.h>
#include <mtev_cluster.h>
#include <mtev_uuid.h>
#include <mtev_memory.h>
#include "noit_clustering.h"
#include "noit_check.h"
#include "noit_filters.h"
#include "noit_filters_lmdb.h"
#include <curl/curl.h>
#include <sys/mman.h>
#include <errno.h>

MTEV_HOOK_IMPL(noit_should_run_check,
               (noit_check_t *check, mtev_cluster_t *cluster,
                mtev_boolean *iown, mtev_cluster_node_t **node),
               void *, closure,
               (void *closure, noit_check_t *check, mtev_cluster_t *cluster,
                mtev_boolean *iown, mtev_cluster_node_t **node),
               (closure,check,cluster,iown,node))

#define MAX_CLUSTER_NODES 128 /* 128 this is insanely high */
#define REPL_FAIL_WAIT_US 500000

static char *cainfo;
static char *certinfo;
static char *keyinfo;
static uint32_t batch_size = 500; /* for fetching clusters and filters */

static void
noit_cluster_setup_ssl(int port) {
  char xpath[1024];
  if(cainfo && certinfo && keyinfo) return;
  snprintf(xpath, sizeof(xpath), "//listeners//listener[@port=\"%d\"]", port);
  mtev_conf_section_t listener = mtev_conf_get_section_read(MTEV_CONF_ROOT, xpath);
  if(mtev_conf_section_is_empty(listener)) {
    mtev_conf_release_section_read(listener);
    return;
  }
  mtev_hash_table *sslconfig = mtev_conf_get_hash(listener, "sslconfig");
#define SSLSETUP(sslconfig, name, var) do { \
  const char *v; \
  free(var); \
  var = NULL; \
  if(mtev_hash_retr_str(sslconfig, name, strlen(name), &v)) \
    var = strdup(v); \
} while(0)
  SSLSETUP(sslconfig, "ca_chain", cainfo);
  SSLSETUP(sslconfig, "certificate_file", certinfo);
  SSLSETUP(sslconfig, "key_file", keyinfo);
  mtev_hash_destroy(sslconfig, free, free);
  free(sslconfig);
#undef SSLSETUP
  mtev_conf_release_section_read(listener);
}

static mtev_log_stream_t clerr, cldeb;
static eventer_jobq_t *repl_jobq;
static int64_t checks_produced = 0;
static int64_t checks_produced_netseq = 0;
static int64_t filters_produced = 0;
static int64_t filters_produced_netseq = 0;

struct check_changes {
  uuid_t checkid;
  int64_t seq;
  struct check_changes *next;
};
struct filter_changes {
  char *name;
  int64_t seq;
  struct filter_changes *next;
};
typedef struct {
  uuid_t id;
  char *cn;
  struct sockaddr *addr;
  socklen_t addrlen;
  struct timeval boot;
  struct {
    /* these are inbound */
    int64_t prev_fetched;
    int64_t fetched;
    int64_t available;
    int last_batch;

    /* these out outbound */
    int64_t sent;
    struct check_changes *head, *tail;
  } checks;
  struct {
    /* these are inbound */
    int64_t prev_fetched;
    int64_t fetched;
    int64_t available;
    int last_batch;

    /* these out outbound */
    int64_t sent;
    struct filter_changes *head, *tail;
  } filters;
  int64_t generation;
  mtev_boolean job_inflight;
} noit_peer_t;

typedef struct repl_job_t {
  uuid_t peerid;
  struct {
    int64_t prev, end;
    int batch_size;
    mtev_boolean success;
  } checks, filters;
} repl_job_t;

static int64_t generation = 0;
static mtev_cluster_t *my_cluster = NULL;
static pthread_mutex_t noit_peer_lock = PTHREAD_MUTEX_INITIALIZER;
static mtev_hash_table peers;

static void possibly_start_job(noit_peer_t *peer);
static void check_changes_free(void *v) { free(v); }
static void filter_changes_free(void *v) {
  free(((struct filter_changes *)v)->name);
  free(v);
}

static void noit_peer_clear_changelog(noit_peer_t *p) {
  while(p->checks.head) {
    struct check_changes *tofree = p->checks.head;
    p->checks.head = p->checks.head->next;
    check_changes_free(tofree);
  }
  while(p->filters.head) {
    struct filter_changes *tofree = p->filters.head;
    p->filters.head = p->filters.head->next;
    filter_changes_free(tofree);
  }
  p->checks.head = p->checks.tail = NULL;
  p->filters.head = p->filters.tail = NULL;
}
static void noit_peer_free(void *vnp) {
  noit_peer_t *p = vnp;
  free(p->cn);
  free(p->addr);
  noit_peer_clear_changelog(p);
  free(p);
}

static void noit_peer_rebuild_changelog(noit_peer_t *peer) {
  noit_peer_clear_changelog(peer);
  noit_filtersets_build_cluster_changelog(peer);
  noit_check_build_cluster_changelog(peer);
}

void
noit_cluster_mark_check_changed(noit_check_t *check, void *vpeer) {
  if(!strcmp(check->module, "selfcheck")) return;
  if(check->config_seq <= 0) return;
  mtevL(cldeb, "marking check %s [%s] %"PRId64" for repl\n", check->name, check->module, check->config_seq);
  pthread_mutex_lock(&noit_peer_lock);
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  checks_produced++;
  while(mtev_hash_adv(&peers, &iter)) {
    noit_peer_t *peer = iter.value.ptr;
    if(vpeer && (void *)peer != vpeer) continue;
    struct check_changes *change = calloc(1, sizeof(*change));
    mtev_uuid_copy(change->checkid, check->checkid);
    change->seq = checks_produced;
    if(peer->checks.tail) peer->checks.tail->next = change;
    else {
      peer->checks.head = change;
    }
    peer->checks.tail = change;
  }
  checks_produced_netseq = htonll(checks_produced);
  pthread_mutex_unlock(&noit_peer_lock);
}

void
noit_cluster_mark_filter_changed(const char *name, void *vpeer) {
  pthread_mutex_lock(&noit_peer_lock);
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  filters_produced++;
  while(mtev_hash_adv(&peers, &iter)) {
    noit_peer_t *peer = iter.value.ptr;
    if(vpeer && (void *)peer != vpeer) continue;
    struct filter_changes *change = calloc(1, sizeof(*change));
    change->name = strdup(name);
    change->seq = filters_produced;
    if(peer->filters.tail) peer->filters.tail->next = change;
    else {
      peer->filters.head = change;
    }
    peer->filters.tail = change;
  }
  filters_produced_netseq = htonll(filters_produced);
  pthread_mutex_unlock(&noit_peer_lock);
}

void
noit_cluster_xml_check_changes(uuid_t peerid, const char *cn,
                               int64_t prev_end, int64_t limit,
                               xmlNodePtr parent) {
  void *vp;
  int64_t last_seen = 0;
  noit_peer_t *peer;
  mtev_hash_table dedup;
  mtev_hash_init(&dedup);
  pthread_mutex_lock(&noit_peer_lock);

  if(!mtev_hash_retrieve(&peers, (const char *)peerid, UUID_SIZE, &vp)) {
    char peerid_str[UUID_STR_LEN + 1];
    mtev_uuid_unparse_lower(peerid, peerid_str);
    mtevL(clerr, "Check changes request by unknown peer [%s].\n", peerid_str);
    pthread_mutex_unlock(&noit_peer_lock);
    return;
  }
  peer = vp;
  if(strcmp(peer->cn, cn)) {
    mtevL(clerr, "Check changes request by peer with bad cn [%s != %s].\n", cn, peer->cn);
    pthread_mutex_unlock(&noit_peer_lock);
    return;
  }

  struct check_changes *node = peer->checks.head;
  /* First eat anything we know they've seen */
  while(peer->checks.head && peer->checks.head->seq <= prev_end) {
    struct check_changes *tofree = peer->checks.head;
    peer->checks.head = peer->checks.head->next;
    if(NULL == peer->checks.head) peer->checks.tail = NULL;
    check_changes_free(tofree);
  }
  for(node = peer->checks.head; node && node->seq <= limit; node = node->next) {
    if(mtev_hash_store(&dedup, (const char *)node->checkid, UUID_SIZE, NULL)) {
      noit_check_t *check = noit_poller_lookup(node->checkid);
      if(check && 0 != strcmp(check->module, "selfcheck")) {
        xmlNodePtr checknode = noit_check_to_xml(check, parent->doc, parent);
        xmlAddChild(parent, checknode);
        last_seen = node->seq;
      }
      noit_check_deref(check);
    }
  }
  pthread_mutex_unlock(&noit_peer_lock);
  char produced_str[32];
  snprintf(produced_str, sizeof(produced_str), "%"PRId64, last_seen);
  xmlSetProp(parent, (xmlChar *)"seq", (xmlChar *)produced_str);
  mtev_hash_destroy(&dedup, NULL, NULL);
}

void
noit_cluster_xml_filter_changes(uuid_t peerid, const char *cn,
                               int64_t prev_end, int64_t limit,
                               xmlNodePtr parent) {
  void *vp;
  int64_t last_seen = 0;
  noit_peer_t *peer;
  mtev_hash_table dedup;
  mtev_hash_init(&dedup);
  pthread_mutex_lock(&noit_peer_lock);

  if(!mtev_hash_retrieve(&peers, (const char *)peerid, UUID_SIZE, &vp)) {
    char peerid_str[UUID_STR_LEN + 1];
    mtev_uuid_unparse_lower(peerid, peerid_str);
    mtevL(clerr, "Filter changes request by unknown peer [%s].\n", peerid_str);
    pthread_mutex_unlock(&noit_peer_lock);
    return;
  }
  peer = vp;
  if(strcmp(peer->cn, cn)) {
    mtevL(clerr, "Filter changes request by peer with bad cn [%s != %s].\n", cn, peer->cn);
    pthread_mutex_unlock(&noit_peer_lock);
    return;
  }

  struct filter_changes *node = peer->filters.head;
  /* First eat anything we know they've seen */
  while(peer->filters.head && peer->filters.head->seq <= prev_end) {
    struct filter_changes *tofree = peer->filters.head;
    peer->filters.head = peer->filters.head->next;
    if(NULL == peer->filters.head) peer->filters.tail = NULL;
    filter_changes_free(tofree);
  }
  for(node = peer->filters.head; node && node->seq <= limit; node = node->next) {
    if(mtev_hash_store(&dedup, (const char *)node->name, strlen(node->name), NULL)) {
      char xpath[512];
      snprintf(xpath, sizeof(xpath), "//filtersets//filterset[@name=\"%s\"]", node->name);
      mtev_conf_section_t filternode = mtev_conf_get_section_read(MTEV_CONF_ROOT, xpath);
      if(!mtev_conf_section_is_empty(filternode)) {
        xmlAddChild(parent, xmlCopyNode(mtev_conf_section_to_xmlnodeptr(filternode),1));
        last_seen = node->seq;
      }
      mtev_conf_release_section_read(filternode);
    }
  }
  pthread_mutex_unlock(&noit_peer_lock);
  char produced_str[32];
  snprintf(produced_str, sizeof(produced_str), "%"PRId64, last_seen);
  xmlSetProp(parent, (xmlChar *)"seq", (xmlChar *)produced_str);
  mtev_hash_destroy(&dedup, NULL, NULL);
}

void
noit_cluster_lmdb_filter_changes(uuid_t peerid, const char *cn,
                                int64_t prev_end, int64_t limit,
                                xmlNodePtr parent) {
  void *vp;
  int64_t last_seen = 0;
  noit_peer_t *peer;
  mtev_hash_table dedup;
  mtev_hash_init(&dedup);
  pthread_mutex_lock(&noit_peer_lock);

  if(!mtev_hash_retrieve(&peers, (const char *)peerid, UUID_SIZE, &vp)) {
    char peerid_str[UUID_STR_LEN + 1];
    mtev_uuid_unparse_lower(peerid, peerid_str);
    mtevL(clerr, "Filter changes request by unknown peer [%s].\n", peerid_str);
    pthread_mutex_unlock(&noit_peer_lock);
    return;
  }
  peer = vp;
  if(strcmp(peer->cn, cn)) {
    mtevL(clerr, "Filter changes request by peer with bad cn [%s != %s].\n", cn, peer->cn);
    pthread_mutex_unlock(&noit_peer_lock);
    return;
  }

  struct filter_changes *node = peer->filters.head;
  /* First eat anything we know they've seen */
  while(peer->filters.head && peer->filters.head->seq <= prev_end) {
    struct filter_changes *tofree = peer->filters.head;
    peer->filters.head = peer->filters.head->next;
    if(NULL == peer->filters.head) peer->filters.tail = NULL;
    filter_changes_free(tofree);
  }
  for(node = peer->filters.head; node && node->seq <= limit; node = node->next) {
    if(mtev_hash_store(&dedup, (const char *)node->name, strlen(node->name), NULL)) {
      if(noit_filters_lmdb_already_in_db(node->name) == mtev_true) {
        xmlNodePtr new_node = xmlNewNode(NULL, (xmlChar *)"filterset");
        if (noit_filters_lmdb_populate_filterset_xml_from_lmdb(new_node, node->name) == 0) {
          xmlAddChild(parent, new_node);
          last_seen = node->seq;
        }
        else {
          mtevL(mtev_error, "noit_filters_lmdb_populate_filterset_xml_from_lmdb: could not add node %s\n", node->name);
          xmlFreeNode(new_node);
        }
      }
    }
  }
  pthread_mutex_unlock(&noit_peer_lock);
  char produced_str[32];
  snprintf(produced_str, sizeof(produced_str), "%"PRId64, last_seen);
  xmlSetProp(parent, (xmlChar *)"seq", (xmlChar *)produced_str);
  mtev_hash_destroy(&dedup, NULL, NULL);
}

static void
clear_old_peers() {
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  while(mtev_hash_adv(&peers, &iter)) {
    noit_peer_t *peer = iter.value.ptr;
    if(peer->generation < generation) {
      mtevL(cldeb, "removing peer %s\n", peer->cn);
      mtev_hash_delete(&peers, iter.key.str, UUID_SIZE, NULL, noit_peer_free);
    }
  }
}

struct curl_tls {
  CURL *curl;
  mtev_boolean ssl_is_setup;
};
static pthread_key_t curl_tls;
void curl_tls_free(void *v) {
  struct curl_tls *ctls = v;
  curl_easy_cleanup(ctls->curl);
  free(v);
}

static CURL *get_curl_handle() {
  struct curl_tls *ctls;
  ctls = pthread_getspecific(curl_tls);
  if(!ctls) {
    CURL *curl;
    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2000);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 120000);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 131072);
    ctls = calloc(1, sizeof(*ctls));
    ctls->curl = curl;
    pthread_setspecific(curl_tls, ctls);
  }
  if(cainfo && certinfo && keyinfo && !ctls->ssl_is_setup) {
    curl_easy_setopt(ctls->curl, CURLOPT_CAINFO, cainfo);
    curl_easy_setopt(ctls->curl, CURLOPT_SSLCERT, certinfo);
    curl_easy_setopt(ctls->curl, CURLOPT_SSLKEY, keyinfo);
    ctls->ssl_is_setup = mtev_true;
  }
  return ctls->ssl_is_setup ? ctls->curl : NULL;
}
static size_t write_data_to_file(void *buff, size_t s, size_t n, void *vd) {
  int *fd = vd;
  ssize_t len = s*n;
  mtevL(cldeb, "XML[%.*s]\n", (int)len, (char *)buff);
  while((len = write(*fd, buff, (size_t)len)) == -1 && errno == EINTR);
  return len < 0 ? 0 : (size_t)len;
}
static xmlDocPtr
fetch_xml_from_noit(CURL *curl, const char *url, struct curl_slist *connect_to) {
  xmlDocPtr doc = NULL;
  int fd = -1;
  char tfile[PATH_MAX];
  long code, httpcode;

  strlcpy(tfile, "/tmp/noitext.XXXXXX", PATH_MAX);
  fd = mkstemp(tfile);
  if(fd < 0) return NULL;
  unlink(tfile);

  mtevL(cldeb, "REPL_JOB Pulling %s\n", url);
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_CONNECT_TO, connect_to);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &fd);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data_to_file);
  httpcode = 0;
  code = curl_easy_perform(curl);
  if(code == CURLE_OK &&
     curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpcode) == CURLE_OK &&
     httpcode == 200) {
    struct stat sb;
    int rv;
    while((rv = fstat(fd, &sb)) == -1 && errno == EINTR);
    if(rv == 0) {
      void *buff = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
      if(buff != MAP_FAILED) {
        doc = xmlParseMemory(buff, sb.st_size);
        munmap(buff, sb.st_size);
      } else {
        mtevL(clerr, "curl mmap failed: %s\n", strerror(errno));
      }
    } else {
      mtevL(clerr, "curl stat error: %s\n", strerror(errno));
    }
  } else {
    mtevL(clerr, "Error fetching %s: %ld/%ld\n", url, code, httpcode);
  }
  close(fd);
  return doc;
}
static int
repl_work(eventer_t e, int mask, void *closure, struct timeval *now) {
  repl_job_t *rj = closure;
  void *vp;
  if(mask == EVENTER_ASYNCH_WORK) {
    uuid_t my_id;
    char my_id_str[UUID_STR_LEN+1];
    pthread_mutex_lock(&noit_peer_lock);
    if(!mtev_hash_retrieve(&peers, (const char *)rj->peerid, UUID_SIZE, &vp)) {
      pthread_mutex_unlock(&noit_peer_lock);
      return 0;
    }
    mtevL(cldeb, "REPL_JOB start [%s] F[%"PRId64",%"PRId64"] C:[%"PRId64",%"PRId64"]\n",
      ((noit_peer_t *)vp)->cn, rj->filters.prev, rj->filters.end, rj->checks.prev, rj->checks.end);
    if(rj->filters.end == 0 && rj->checks.end == 0) {
      mtevL(cldeb, "REPL_JOB noop, nothing to do.\n");
      pthread_mutex_unlock(&noit_peer_lock);
      sleep(1);
      return 0;
    }
    pthread_mutex_unlock(&noit_peer_lock);

    mtev_cluster_get_self(my_id);
    mtev_uuid_unparse_lower(my_id, my_id_str);
    CURL *curl = get_curl_handle();
    if(curl == NULL) {
      mtevL(clerr, "Can't get curl handle: waiting %fs\n", (double)REPL_FAIL_WAIT_US/1000000);
      usleep(REPL_FAIL_WAIT_US);
      return 0;
    }

    mtev_memory_begin();
    mtev_cluster_node_t *node = mtev_cluster_get_node(my_cluster, rj->peerid);
    if(node) {
      struct sockaddr *addr;
      socklen_t addrlen;
      const char *cn = mtev_cluster_node_get_cn(node);
      char port_str[10];
      char host_port[128];
      char connect_str[256];
      char url[1024];
      struct curl_slist *connect_to = NULL;
      switch(mtev_cluster_node_get_addr(node, &addr, &addrlen)) {
        case AF_INET:
          inet_ntop(AF_INET, &((struct sockaddr_in *)addr)->sin_addr,
                    host_port, sizeof(host_port));
          strlcat(host_port, ":", sizeof(host_port));
          snprintf(port_str, sizeof(port_str), "%u",
                   ntohs(((struct sockaddr_in *)addr)->sin_port));
          strlcat(host_port, port_str, sizeof(host_port));
          break;
        case AF_INET6:
          host_port[0] = '[';
          inet_ntop(AF_INET, &((struct sockaddr_in6 *)addr)->sin6_addr,
                    host_port+1, sizeof(host_port)-1);
          strlcat(host_port, "]:", sizeof(host_port));
          snprintf(port_str, sizeof(port_str), "%u",
                   ntohs(((struct sockaddr_in6 *)addr)->sin6_port));
          strlcat(host_port, port_str, sizeof(host_port));
          break;
        default:
          strlcpy(host_port, mtev_cluster_node_get_cn(node), sizeof(host_port));
      }
      snprintf(connect_str, sizeof(connect_str), "%s:43191:%s", cn, host_port);
      connect_to = curl_slist_append(NULL, connect_str);

      /* First pull filtersets */
      if(rj->filters.end) {
        snprintf(url, sizeof(url),
                 "https://%s:43191/filters/updates?peer=%s&prev=%"PRId64"&end=%"PRId64,
                 cn, my_id_str, rj->filters.prev, rj->filters.end);
        xmlDocPtr doc = fetch_xml_from_noit(curl, url, connect_to);
        if(doc) {
          rj->filters.batch_size = noit_filters_process_repl(doc);
          if(rj->filters.batch_size >= 0)
            rj->filters.success = mtev_true;
          xmlFreeDoc(doc);
        }
        if(!rj->filters.success) usleep(REPL_FAIL_WAIT_US);
      }

      /* Second pull checks */
      if(rj->checks.end) {
        snprintf(url, sizeof(url),
                 "https://%s:43191/checks/updates?peer=%s&prev=%"PRId64"&end=%"PRId64,
                 cn, my_id_str, rj->checks.prev, rj->checks.end);
        xmlDocPtr doc = fetch_xml_from_noit(curl, url, connect_to);
        if(doc) {
          rj->checks.batch_size = noit_check_process_repl(doc);
          if(rj->checks.batch_size >= 0)
            rj->checks.success = mtev_true;
          xmlFreeDoc(doc);
        }
        if(!rj->checks.success) usleep(REPL_FAIL_WAIT_US);
      }

      curl_easy_setopt(curl, CURLOPT_CONNECT_TO, NULL);
      curl_slist_free_all(connect_to);
    }
    mtev_memory_end();

  }
  else if(mask == EVENTER_ASYNCH) {
    pthread_mutex_lock(&noit_peer_lock);
    if(mtev_hash_retrieve(&peers, (const char *)rj->peerid, UUID_SIZE, &vp)) {
      noit_peer_t *peer = vp;
      peer->job_inflight = mtev_false;
      peer->filters.last_batch = rj->filters.batch_size;
      if(rj->filters.success && rj->filters.prev) {
        peer->filters.prev_fetched = rj->filters.prev;
      }
      if(rj->filters.success && rj->filters.end) {
        peer->filters.fetched = rj->filters.end;
      }
      peer->checks.last_batch = rj->checks.batch_size;
      if(rj->checks.success && rj->checks.prev) {
        peer->checks.prev_fetched = rj->checks.prev;
      }
      if(rj->checks.success && rj->checks.end) {
        peer->checks.fetched = rj->checks.end;
      }
      mtevL(cldeb, "REPL_JOB finish [%s] F[%"PRId64",%"PRId64"] %d C:[%"PRId64",%"PRId64"] %d\n",
        ((noit_peer_t *)vp)->cn,
        peer->filters.fetched, peer->filters.available, peer->filters.last_batch,
        peer->checks.fetched, peer->checks.available, peer->checks.last_batch);
      possibly_start_job(peer);
    }
    pthread_mutex_unlock(&noit_peer_lock);
    free(closure);
  }
  return 0;
}
static void
possibly_start_job(noit_peer_t *peer) {
  if(peer->job_inflight) {
    mtevL(cldeb, "REPL_JOB job already inflight\n");
    return;
  }
  if(mtev_cluster_get_node(my_cluster, peer->id) == NULL) {
    mtevL(cldeb, "REPL_JOB peer %s no longer in cluster\n", peer->cn);
    return;
  }
  if(peer->checks.last_batch || peer->filters.last_batch ||
     peer->checks.available != peer->checks.prev_fetched ||
     peer->filters.available != peer->filters.prev_fetched) {
    /* We have work to do */
    repl_job_t *rj = calloc(1, sizeof(*rj));
    mtev_uuid_copy(rj->peerid, peer->id);
    rj->checks.prev = peer->checks.fetched;
    rj->checks.end = peer->checks.available;
    if (batch_size > 0 && (rj->checks.end - rj->checks.prev > batch_size)) {
      rj->checks.end = rj->checks.prev + batch_size;
    }
    rj->filters.prev = peer->filters.fetched;
    rj->filters.end = peer->filters.available;
    if (batch_size > 0 && (rj->filters.end - rj->filters.prev > batch_size)) {
      rj->filters.end = rj->filters.prev + batch_size;
    }
    if(peer->checks.prev_fetched == rj->checks.end && peer->checks.last_batch == 0) {
      rj->checks.prev = rj->checks.end = 0;
    }
    if(peer->filters.prev_fetched == rj->filters.end && peer->filters.last_batch == 0) {
      rj->filters.prev = rj->filters.end = 0;
    }
    peer->job_inflight = true;
    eventer_add_asynch(repl_jobq, eventer_alloc_asynch(repl_work, rj));
  } else {
    mtevL(cldeb, "REPL_JOB no more work to do\n");
  }
}

mtev_boolean
noit_cluster_checkid_replication_pending(uuid_t checkid) {
  if(!my_cluster) return mtev_false;

  pthread_mutex_lock(&noit_peer_lock);
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  while(mtev_hash_adv(&peers, &iter)) {
    struct check_changes *n;
    noit_peer_t *peer = iter.value.ptr;
    for(n = peer->checks.head; n; n = n->next) {
      if(mtev_uuid_compare(n->checkid, checkid) == 0) {
        pthread_mutex_unlock(&noit_peer_lock);
        return mtev_true;
      }
    }
  }
  pthread_mutex_unlock(&noit_peer_lock);
  return mtev_false;
}

static void
update_peer(mtev_cluster_node_t *node) {
  void *vp;
  int64_t *check_seqnet = NULL, *filter_seqnet = NULL;
  noit_peer_t *peer;
  uuid_t nodeid;
  struct sockaddr *addr;
  socklen_t addrlen;
  struct timeval boot;
  const char *cn = mtev_cluster_node_get_cn(node);
  (void)mtev_cluster_node_get_addr(node, &addr, &addrlen);
  boot = mtev_cluster_node_get_boot_time(node);

  if(mtev_cluster_is_that_me(node)) return;

  /* must have lock here */
  mtev_cluster_node_get_id(node, nodeid);
  char nodeidstr[UUID_STR_LEN+1];
  mtev_uuid_unparse_lower(nodeid, nodeidstr);
  if(mtev_hash_retrieve(&peers, (const char *)nodeid, UUID_SIZE, &vp)) {
    peer = vp;
    mtevL(cldeb, "updating peer %s/%s\n", cn, nodeidstr);
  } else {
    peer = calloc(1, sizeof(*peer));
    mtev_uuid_copy(peer->id, nodeid);
    mtev_hash_store(&peers, (const char *)peer->id, UUID_SIZE, peer);
    mtevL(cldeb, "adding peer %s/%s\n", cn, nodeidstr);
  }

  peer->generation = generation;

  if(!peer->cn || strcmp(peer->cn, cn)) {
    free(peer->cn);
    peer->cn = strdup(cn);
  }
  if(!peer->addr || (peer->addrlen != addrlen) ||
     memcmp(peer->addr, addr, addrlen)) {
    free(peer->addr);
    peer->addrlen = addrlen;
    peer->addr = malloc(peer->addrlen);
    memcpy(peer->addr, addr, peer->addrlen);
  }
  if(memcmp(&peer->boot, &boot, sizeof(boot))) {
    /* boot time changes, we know nothing now */
    memcpy(&peer->boot, &boot, sizeof(boot));
    peer->checks.fetched = 0;
    peer->filters.fetched = 0;
    peer->checks.prev_fetched = 0;
    peer->filters.prev_fetched = 0;
    noit_peer_rebuild_changelog(peer);
  }
  if(mtev_cluster_get_heartbeat_payload(node,
        NOIT_MTEV_CLUSTER_APP_ID, NOIT_MTEV_CLUSTER_CHECK_SEQ_KEY,
        (void **)&check_seqnet) > 0) {
    peer->checks.available = ntohll(*check_seqnet);
  }
  if(mtev_cluster_get_heartbeat_payload(node,
        NOIT_MTEV_CLUSTER_APP_ID, NOIT_MTEV_CLUSTER_FILTER_SEQ_KEY,
        (void **)&filter_seqnet) > 0) {
    peer->filters.available = ntohll(*filter_seqnet);
  }

  possibly_start_job(peer);

  if(check_seqnet) {
    mtevL(cldeb, "    node %s -> check:[%"PRId64" -> %"PRId64"]\n",
          mtev_cluster_node_get_cn(node), peer->checks.fetched,
          peer->checks.available);
  }
  if(filter_seqnet) {
    mtevL(cldeb, "    node %s -> filter:[%"PRId64" -> %"PRId64"]\n",
          mtev_cluster_node_get_cn(node), peer->filters.fetched,
          peer->filters.available);
  }
}
static void
attach_to_cluster(mtev_cluster_t *nc) {
  int i, n;
  if(nc == my_cluster) return;
  my_cluster = nc;
  pthread_mutex_lock(&noit_peer_lock);
  generation++;
  if(!my_cluster) {
    mtev_hash_delete_all(&peers, NULL, noit_peer_free);
    pthread_mutex_unlock(&noit_peer_lock);
    return;
  }
  mtev_cluster_node_t *nodeset[MAX_CLUSTER_NODES];
  n = mtev_cluster_get_nodes(my_cluster, nodeset, MAX_CLUSTER_NODES, mtev_false);
  pthread_mutex_unlock(&noit_peer_lock);
  for(i=0; i<n; i++) {
    update_peer(nodeset[i]);
  }
  pthread_mutex_lock(&noit_peer_lock);
  clear_old_peers();
  pthread_mutex_unlock(&noit_peer_lock);
  eventer_jobq_set_min_max(repl_jobq, 0, i);

  mtev_cluster_set_heartbeat_payload(my_cluster,
    NOIT_MTEV_CLUSTER_APP_ID,
    NOIT_MTEV_CLUSTER_CHECK_SEQ_KEY,
    &checks_produced_netseq,
    sizeof(int64_t));
  mtev_cluster_set_heartbeat_payload(my_cluster,
    NOIT_MTEV_CLUSTER_APP_ID,
    NOIT_MTEV_CLUSTER_FILTER_SEQ_KEY,
    &filters_produced_netseq,
    sizeof(int64_t));
}
static const char *
cluster_change_nice_name(mtev_cluster_node_changes_t c, long tdiff)  {
  switch(c) {
    case MTEV_CLUSTER_NODE_DIED: return "died";
    case MTEV_CLUSTER_NODE_REBOOTED: return tdiff < 0 ? "is present" : "(re)booted";
    case MTEV_CLUSTER_NODE_CHANGED_SEQ: return "reconfigured";
    case MTEV_CLUSTER_NODE_CHANGED_PAYLOAD: return "has changes";
  }
  return "unknown";
}
static int my_cluster_id = -1;
int noit_cluster_self_index(void) {
  if(my_cluster_id >= 0) return my_cluster_id;

  mtev_cluster_t *cluster = mtev_cluster_by_name(NOIT_MTEV_CLUSTER_NAME);
  if(!cluster) return -1;

  uuid_t me;
  mtev_cluster_get_self(me);
  mtev_cluster_node_t *self = mtev_cluster_get_node(cluster, me);
  if(!self) return -1;

  my_cluster_id = mtev_cluster_node_get_idx(self);
  return my_cluster_id;
}
static mtev_hook_return_t
cluster_topo_cb(void *closure,
                mtev_cluster_node_changes_t node_changes,
                mtev_cluster_node_t *updated_node,
                mtev_cluster_t *cluster,
                struct timeval old_boot_time) {
  uuid_t id, me;
  char id_str[UUID_STR_LEN+1];
  mtev_cluster_get_self(me);
  mtev_cluster_node_t *self = mtev_cluster_get_node(cluster, me);
  struct timeval my_boot_time = mtev_cluster_node_get_boot_time(self);

  mtev_cluster_node_get_id(updated_node, id);
  mtev_uuid_unparse_lower(id, id_str);
  struct timeval boot_time = mtev_cluster_node_get_boot_time(updated_node);
  struct timeval diff;
  sub_timeval(boot_time, my_boot_time, &diff);
  mtevL(node_changes == MTEV_CLUSTER_NODE_CHANGED_PAYLOAD ? cldeb : mtev_notice,
        "cluster %s:%s [%s%s] -> %s\n",
        mtev_cluster_get_name(cluster),
        mtev_cluster_node_get_cn(updated_node), mtev_uuid_compare(me,id) ? "" : "me:",
        id_str, cluster_change_nice_name(node_changes, diff.tv_sec));
  if(!strcmp(mtev_cluster_get_name(cluster), NOIT_MTEV_CLUSTER_NAME)) {
    my_cluster_id = -1;
    attach_to_cluster(cluster);
    if(!mtev_cluster_is_that_me(updated_node)) update_peer(updated_node);
    else {
      struct sockaddr *addr;
      int port = 43191;
      switch(mtev_cluster_node_get_addr(updated_node, &addr, NULL)) {
        case AF_INET:
          port = ntohs(((struct sockaddr_in *)addr)->sin_port);
          break;
        case AF_INET6:
          port = ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
          break;
        default: break;
      }
      noit_cluster_setup_ssl(port);
    }
  }
  return MTEV_HOOK_CONTINUE;
}

static mtev_boolean
alive_nodes(mtev_cluster_node_t *node, mtev_boolean me, void *closure) {
  return !mtev_cluster_node_is_dead(node);
}
mtev_boolean
noit_should_run_check(noit_check_t *check, mtev_cluster_node_t **node) {
  mtev_boolean i_own = mtev_true;
  /* No clustering means I own everything */
  if(!my_cluster) return i_own;

  if(!strcmp(check->module, "selfcheck")) return i_own;

  if(noit_should_run_check_hook_invoke(check, my_cluster, &i_own, node) == MTEV_HOOK_DONE) {
    return i_own;
  }

  mtev_cluster_node_t *nodeset[MAX_CLUSTER_NODES];
  int w = MAX_CLUSTER_NODES;

  i_own = mtev_cluster_filter_owners(my_cluster, check->checkid, UUID_SIZE,
                                     nodeset, &w, alive_nodes, NULL);

  /* something is very wrong, we better run the check */
  if(w < 1) return mtev_true;

  /* Fill in the address of the node */
  if(w > 0 && node) *node = nodeset[0];
  return i_own;
}

static int
noit_clustering_show(mtev_console_closure_t ncct,
                     int argc, char **argv,
                     mtev_console_state_t *state, void *closure) {
  nc_printf(ncct, "my_cluster_id: %d\n", noit_cluster_self_index());
  if(!my_cluster) {
    nc_printf(ncct, "clustering not configured.\n");
    return 0;
  }

  pthread_mutex_lock(&noit_peer_lock);
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  while(mtev_hash_adv(&peers, &iter)) {
    struct check_changes *n;
    noit_peer_t *peer = iter.value.ptr;
    char idstr[UUID_STR_LEN+1];
    mtev_uuid_unparse_lower(peer->id, idstr);
    nc_printf(ncct, "-- %s (%s) --\n", idstr, peer->cn);
    nc_printf(ncct, "  checks_produced: %" PRId64 "\n", checks_produced);
    nc_printf(ncct, "  queued checks:\n");
    for(n = peer->checks.head; n; n = n->next) {
      mtev_uuid_unparse_lower(n->checkid, idstr);
      nc_printf(ncct, "    %s : %" PRId64 "\n", idstr, n->seq);
    }
  }
  pthread_mutex_unlock(&noit_peer_lock);

  return 0;
}

void noit_mtev_cluster_init() {
  mtev_console_state_t *tl;
  cmd_info_t *showcmd;
  pthread_key_create(&curl_tls, curl_tls_free);
  cldeb = mtev_log_stream_find("debug/noit/cluster");
  clerr = mtev_log_stream_find("error/noit/cluster");
  mtev_hash_init(&peers);

  tl = mtev_console_state_initial();
  showcmd = mtev_console_state_get_cmd(tl, "show");
  mtevAssert(showcmd && showcmd->dstate);

  mtev_conf_get_uint32(MTEV_CONF_ROOT, "//clusters/cluster[@name=\"noit\"]/@batch_size", &batch_size);

  mtev_console_state_add_cmd(showcmd->dstate,
  NCSCMD("noit-cluster", noit_clustering_show, NULL, NULL, NULL));

  repl_jobq = eventer_jobq_create_ms("noit_cluster", EVENTER_JOBQ_MS_GC);
  mtevAssert(repl_jobq);
  eventer_jobq_set_min_max(repl_jobq, 0, 1);

  mtev_cluster_init();
  mtev_cluster_handle_node_update_hook_register("noit-cluster", cluster_topo_cb, NULL);
  attach_to_cluster(mtev_cluster_by_name(NOIT_MTEV_CLUSTER_NAME));

}
