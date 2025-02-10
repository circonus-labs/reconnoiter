/*
 * Copyright (c) 2016-2020, Circonus, Inc. All rights reserved.
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

#include <mtev_defines.h>
#include <mtev_conf.h>
#include <mtev_dso.h>
#include <mtev_fq.h>
#include <mtev_hash.h>
#include <mtev_kafka.h>
#include <mtev_memory.h>
#include <mtev_perftimer.h>
#include <mtev_str.h>
#include <mtev_log.h>
#include <ck_fifo.h>
#include <mtev_fq.h>
#include <mtev_hooks.h>
#include <mtev_uuid.h>
#include <mtev_stats.h>
#include <mtev_time.h>
#include <mtev_rand.h>
#include <mtev_frrh.h>

#include <openssl/md5.h>
#include <ck_hs.h>

#include <noit_metric_director.h>
#include <noit_metric_tag_search.h>
#include <noit_check_log_helpers.h>
#include <noit_message_decoder.h>
#include "noit_prometheus_translation.h"
#include "noit_ssl10_compat.h"

/* This is a hot mess designed to optimize metric selection on a variety of vectors.
 *
 * The director will look at messages and determine which "lanes" to push them down.
 * The default eventer loop threads are "lanes."
 *
 * All interests are account-qualified. Each of these lanes can express interest
 * in different ways:
 *
 * 1) Lanes can ask for an "exact" metric by name...
 *    This is not so exact as it can either match the exact metric without tags or
 *    the exact metric with tags.  If a lane expresses interest in `foo|ST[]`, they
 *    mean exactly `foo` with no tags.  If a lane expresses interest in `foo`, they
 *    mean any `foo|ST[...]` metric regardless of the tags.
 *    As metric names also include a UUID, the interest can be expressed with a
 *    specific check's UUID or the interest can be registered with the NULL UUID
 *    which means any check will match.
 *
 *    To summarize there are 4 possible "exact" match scenarios when receiving a metric
 *    like: `0e1aef26-d508-479a-a43c-db8cb1c25492 foo|ST[a:b,c:d]`
 *
 *    Anyone interested in the following should recieve these messages:
 *    1) `0e1aef26-d508-479a-a43c-db8cb1c25492 foo|ST[a:b,c:d]`
 *    2) `0e1aef26-d508-479a-a43c-db8cb1c25492 foo`
 *    3) `00000000-0000-0000-0000-000000000000 foo|ST[a:b,c:d]`
 *    4) `00000000-0000-0000-0000-000000000000 foo`
 *
 *    IMPLEMENTATION:
 *
 *    This is a simple two-level hash table:
 *       id_level[uuid]->[metricname] -> array of interest_cnt_t
 *
 *    As it is all O(1) lookups, it is not partitioned by account
 *
 * 2) Lanes can ask for a particular search expression (noit_metric_tag_search_ast_t)
 *    either on a specific check or on any check.
 *
 *    As a note, when a metric arrives, evaluating the tag search against that metric
 *    may not always return the same result because the tags in the inbound metric
 *    can be modified externally via hook (adding check tags).
 *
 *    IMPLEMENTATION:
 *
 *    This is more complicated for a variety of reasons including maintaining a cache.
 *
 *    Each account has an `account_search_t` structure that has a generational counter
 *    that is modified any time a search expression is added. The structure has a
 *    hash table of check_search_t structures indexed by UUID (where the NULL UUID means
 *    any check matches) that contain a hash of name_search_t by name (where NULL/"" 
 *    means it applies to any name), each of which contains a table per lane of all registered
 *    searches that apply.
 *
 *    To evaluate a given inbound metric, we first check the miss cache and if the
 *    item is present in the miss cache, then we skip all work.  Otherwise, 
 *    the account_search_t is found for the appropriate account and
 *      foreach uuid in ( metric.uuid , NULL uuid )
 *        foreach name ( metric.name, NULL) 
 *          foreach lane ( lanes )
 *            foreach search in ( account_search[uuid][name][lane] )
 *              if search is true mark interested and go to next lane
 *
 *    CACHE:
 *
 *    The most expensive metrics are those that match no searches as it is impossible
 *    to short circuit the evaluations.  As such we maintain a cache of known misses.
 *    Because the outcome of the full evaluation can change if (1) check tags are
 *    changed or (2) a new search is registered, we encode the generational counters
 *    for both the check and the account into the cache key:
 *        [ acct, acct_gen, check_uuid, check_gen, metric name w/ tags ]
 *
 *    If we perform a full evaluation and find no interests, we store that key in
 *    the miss cache.  If check tags change or a search is added, the key changes.
 *    We use a fair random replacement cache tweaked to a 100% replacement
 *    probability if the entry to be possibly replaced has any outdated
 *    generational counters.  This has the effect of force-replacing outdated keys
 *    and probabilistically replacing still-current keys.
 *
 *    And LRU is tough here b/c almost all metrics are seen in one very large
 *    ordered cycle.
 *
 */

/* pointers enqueue with this flag set, are flush pointers.
 * the flag should be removed and used and the pointer will
 * point to a dmflush_t
 */
#define FLUSHFLAG 0x1
typedef struct {
  eventer_t e;
  uint32_t refcnt;
} dmflush_t;

static void *__c_allocator_alloc(void *d, size_t size) {
  return malloc(size);
}
static void __c_allocator_free(void *d, void *p) {
  free(p);
}
static ProtobufCAllocator __c_allocator = {
  .alloc = __c_allocator_alloc,
  .free = __c_allocator_free,
  .allocator_data = NULL
};
#define protobuf_c_system_allocator __c_allocator

#define DMFLUSH_FLAG(a) ((dmflush_t *)((uintptr_t)(a) | FLUSHFLAG))
#define DMFLUSH_UNFLAG(a) ((dmflush_t *)((uintptr_t)(a) & ~(uintptr_t)FLUSHFLAG))

MTEV_HOOK_IMPL(metric_director_want, (noit_metric_message_t *m, int *wants, int wants_len),
               void *, closure, (void *closure, noit_metric_message_t *m, int *wants, int wants_len),
               (closure, m, wants, wants_len));

MTEV_HOOK_IMPL(metric_director_revise, (noit_metric_message_t *m, interest_cnt_t *interests, int interests_len),
               void *, closure, (void *closure, noit_metric_message_t *m, interest_cnt_t *interests, int interests_len),
               (closure, m, interests, interests_len));

struct fq_conn_s;
struct fq_msg;

struct hash_and_time {
  mtev_hash_table hash;
  uint32_t last_touched_s;
};

static __thread struct {
  int id;
  uint32_t *backlog;
  ck_fifo_spsc_t *fifo;
} my_lane;

typedef union {
  struct { void *queue; uint32_t backlog; } thread;
  uint8_t pad[CK_MD_CACHELINE];
} thread_queue_t;

static int nthreads;

static inline int safe_thread_id(int thread_id) {
  if(thread_id < 0) thread_id ^= thread_id;
  thread_id = thread_id % nthreads;
  return thread_id;
}

static uint32_t miss_cache_size = 10000000; /* 10mm */
static thread_queue_t *queues;
static mtev_hash_table id_level;
static mtev_hash_table dedupe_hashes;
static interest_cnt_t *check_interests;
static pthread_mutex_t check_interests_lock;
static mtev_boolean dedupe = mtev_true;
static uint32_t director_in_use = 0;
static uint64_t drop_before_threshold_ms = 0;
static uint32_t drop_backlog_over = 0;

/*
 * the ASTs... each lane can register a set of ASTs
 * We are doing this concurrently, so we make a copy of the ASTs.
 *
 * [ thread_id, ck_hs [ ID: AST ] ]
 *
 */

static uint32_t ast_counter = 1;
typedef struct {
  uint32_t    ast_id; /* unique identifier for removal */
  int64_t account_id; /* >= 0 -> exact match, < 0 -> allow all */
  uuid_t  check_uuid; /* UUID_ZERO -> any, otherwise exact match */
  noit_metric_tag_search_ast_t *ast;
} tag_search_registration_t;

static void
tag_search_registration_cleanup(void *v) {
  tag_search_registration_t *s = (tag_search_registration_t *)v;
  noit_metric_tag_search_free(s->ast);
}

static unsigned long
tag_search_registration_hash(const void *object, unsigned long seed) {
  tag_search_registration_t *s = (tag_search_registration_t *)object;
  return s->ast_id * seed;
}
static bool
tag_search_registration_compare(const void *previous, const void *compare) {
  return ((tag_search_registration_t *)previous)->ast_id == ((tag_search_registration_t *)compare)->ast_id;
}

struct thread_asts {
  ck_spinlock_t lock;
  ck_hs_t hs;
} CK_CC_CACHELINE;

/* This gen gets update by dynamic hook when a check changes */
typedef struct {
  char *metric_name;
  size_t metric_name_len;
  struct thread_asts *search_asts;
} name_search_t;

static unsigned long
name_hash(const void *object, unsigned long seed) {
  name_search_t *ns = (name_search_t *)object;
  return mtev_hash__hash(ns->metric_name, ns->metric_name_len, seed);
}

static bool
name_compare(const void *previous, const void *compare) {
  name_search_t *p = (name_search_t *)previous;
  name_search_t *c = (name_search_t *)compare;
  if(p->metric_name_len != c->metric_name_len) return false;
  return !memcmp(p->metric_name, c->metric_name, p->metric_name_len);
}

/* This gen gets update by dynamic hook when a check changes */
typedef struct {
  uuid_t check_uuid;
  pthread_mutex_t name_searches_writes;
  ck_hs_t name_searches;
} check_search_t;

typedef struct {
  uuid_t check_uuid;
  uint64_t gen;
} check_generation_t;
static ck_hs_t check_generation;
static pthread_mutex_t check_generation_writes = PTHREAD_MUTEX_INITIALIZER;

/* This gets updated internally to this director when a search is added or removed */
typedef struct {
  uint64_t account_id;
  uint64_t gen;
  pthread_mutex_t check_searches_writes;
  ck_hs_t check_searches;
} account_search_t;
static pthread_mutex_t account_searches_writes = PTHREAD_MUTEX_INITIALIZER;
static ck_hs_t account_searches;

static unsigned long
check_hash(const void *object, unsigned long seed) {
  check_generation_t *s = (check_generation_t *)object;
  uint64_t *asu64 = (uint64_t *)s->check_uuid;
  return (asu64[0] ^ asu64[1]) * seed;
}
static bool
check_compare(const void *previous, const void *compare) {
  return mtev_uuid_compare(((check_generation_t *)previous)->check_uuid, ((check_generation_t *)compare)->check_uuid) == 0;
}

static struct thread_asts *thread_asts_alloc(int cnt) {
  struct thread_asts *asts = calloc(sizeof(*asts),nthreads);
  for(int i=0; i<nthreads; i++) {
    ck_spinlock_init(&asts[i].lock);
    ck_hs_init(&asts[i].hs, CK_HS_MODE_OBJECT | CK_HS_MODE_SPMC,
               tag_search_registration_hash, tag_search_registration_compare,
               &mtev_memory_safe_ck_malloc, cnt, mtev_rand());
  }
  return asts;
}

void thread_asts_free(struct thread_asts *asts) {
  for(int i=0; i<nthreads; i++) {
    ck_hs_destroy(&asts[i].hs);
  }
  free(asts);
}

static name_search_t *
name_get_search(check_search_t *cs, const char *metric_name, size_t metric_name_len, bool create) {
  name_search_t ns;
  ns.metric_name = (char *)metric_name;
  ns.metric_name_len = metric_name_len;
  unsigned long hash = CK_HS_HASH(&cs->name_searches, name_hash, &ns);
  name_search_t *found = (name_search_t *)ck_hs_get(&cs->name_searches, hash, &ns);
  if(found) return found;
  if(!create) return NULL;
  name_search_t *newns = calloc(1, sizeof(*newns));
  newns->metric_name = mtev_strndup(metric_name, metric_name_len);
  newns->metric_name_len = metric_name_len;

  newns->search_asts = thread_asts_alloc(8);
  pthread_mutex_lock(&cs->name_searches_writes);
  if(ck_hs_put(&cs->name_searches, hash, newns)) {
    pthread_mutex_unlock(&cs->name_searches_writes);
    return newns;
  }
  pthread_mutex_unlock(&cs->name_searches_writes);
  thread_asts_free(newns->search_asts);
  free(newns->metric_name);
  free(newns);
  return name_get_search(cs, metric_name, metric_name_len, false);
}

static check_search_t *
check_get_search(account_search_t *as, const uuid_t check_uuid, bool create) {
  check_search_t cs;
  mtev_uuid_copy(cs.check_uuid, check_uuid);
  unsigned long hash = CK_HS_HASH(&as->check_searches, check_hash, &cs);
  check_search_t *found = (check_search_t *)ck_hs_get(&as->check_searches, hash, &cs);
  if(found) return found;
  if(!create) return NULL;
  check_search_t *newcs = calloc(1, sizeof(*newcs));
  mtev_uuid_copy(newcs->check_uuid, check_uuid);

  ck_hs_init(&newcs->name_searches, CK_HS_MODE_OBJECT | CK_HS_MODE_SPMC,
             name_hash, name_compare,
             &mtev_memory_safe_ck_malloc, 8, mtev_rand());

  pthread_mutex_init(&newcs->name_searches_writes, NULL);
  pthread_mutex_lock(&as->check_searches_writes);
  if(ck_hs_put(&as->check_searches, hash, newcs)) {
    pthread_mutex_unlock(&as->check_searches_writes);
    return newcs;
  }
  pthread_mutex_unlock(&as->check_searches_writes);
  pthread_mutex_destroy(&newcs->name_searches_writes);
  ck_hs_destroy(&newcs->name_searches);
  free(newcs);
  return check_get_search(as, check_uuid, false);
}


static unsigned long
account_hash(const void *object, unsigned long seed) {
  account_search_t *s = (account_search_t *)object;
  return (s->account_id) * seed;
}
static bool
account_compare(const void *previous, const void *compare) {
  return ((account_search_t *)previous)->account_id == ((account_search_t *)compare)->account_id;
}

static account_search_t *
account_get_search(uint64_t account_id, bool create) {
  account_search_t as = { .account_id = account_id };
  unsigned long hash = CK_HS_HASH(&account_searches, account_hash, &as);
  account_search_t *found = (account_search_t *)ck_hs_get(&account_searches, hash, &as);
  if(found) return found;
  if(!create) return NULL;
  account_search_t *newas = calloc(1, sizeof(*newas));
  newas->account_id = account_id;

  ck_hs_init(&newas->check_searches, CK_HS_MODE_OBJECT | CK_HS_MODE_SPMC,
             check_hash, check_compare,
             &mtev_memory_safe_ck_malloc, 128, mtev_rand());
  pthread_mutex_init(&newas->check_searches_writes, NULL);

  pthread_mutex_lock(&account_searches_writes);
  if(ck_hs_put(&account_searches, hash, newas)) {
    pthread_mutex_unlock(&account_searches_writes);
    return newas;
  }
  pthread_mutex_unlock(&account_searches_writes);
  pthread_mutex_destroy(&newas->check_searches_writes);
  ck_hs_destroy(&newas->check_searches);
  free(newas);
  return account_get_search(account_id, false);
}


void metric_director_set_check_generation_dyn(uuid_t check_uuid, uint64_t gen) {
  unsigned long hash = CK_HS_HASH(&check_generation, check_hash, check_uuid);
  check_generation_t *newcg, *found = ck_hs_get(&check_generation, hash, check_uuid);
  if(found) {
    found->gen = gen;
    return;
  }
  newcg = calloc(1, sizeof(*newcg));
  mtev_uuid_copy(newcg->check_uuid, check_uuid);
  newcg->gen = gen;
  pthread_mutex_lock(&check_generation_writes);
  if(ck_hs_put(&check_generation, hash, newcg)) newcg = NULL;
  pthread_mutex_unlock(&check_generation_writes);
  if(newcg) free(newcg);
}

static stats_ns_t *stats_ns;
static stats_handle_t *stats_cache_size;
static stats_handle_t *stats_cache_lookups;
static stats_handle_t *stats_cache_hits;
static stats_handle_t *stats_cache_purge_rand;
static stats_handle_t *stats_cache_purge_version;
static stats_ns_t *lanes_stats_ns;
static stats_handle_t *stats_msg_delay;
static stats_handle_t *stats_msg_selection_latency;
static stats_handle_t *stats_msg_seen;
static stats_handle_t *stats_msg_dropped_threshold;
static stats_handle_t *stats_msg_dropped_backlogged;
static stats_handle_t *stats_msg_distributed;
static stats_handle_t *stats_msg_queued;
static stats_handle_t *stats_msg_delivered;

static inline interest_cnt_t adjust_interest(interest_cnt_t in, short adj) {
  int tmp = (int)in;
  /* If it is USHRT_MAX, then it gets stuck that way. */
  if(tmp == USHRT_MAX) return USHRT_MAX;
  tmp += adj;
  /* bounds cap it back to an unsigned short */
  if(tmp < 0) return 0;
  if(tmp > USHRT_MAX) return USHRT_MAX;
  return (interest_cnt_t)tmp;
}
void
noit_metric_director_message_ref(void *m) {
  noit_metric_message_t *message = (noit_metric_message_t *)m;
  ck_pr_inc_32(&message->refcnt);
}

void
noit_metric_director_message_deref(void *m) {
  noit_metric_message_t *message = (noit_metric_message_t *)m;
  bool zero;
  ck_pr_dec_32_zero(&message->refcnt, &zero);
  if (zero) {
    noit_metric_message_free(message);
  }
}

static int
get_my_lane() {
  director_in_use = 1;
  ck_pr_fence_store();

  if(my_lane.fifo == NULL) {
    int new_thread;
    my_lane.fifo = calloc(1, sizeof(ck_fifo_spsc_t));
    ck_fifo_spsc_init(my_lane.fifo, malloc(sizeof(ck_fifo_spsc_entry_t)));
    for(new_thread=0;new_thread<nthreads;new_thread++) {
      if(ck_pr_cas_ptr(&queues[new_thread].thread.queue, NULL, my_lane.fifo)) break;
    }
    mtevAssert(new_thread<nthreads);
    my_lane.id = new_thread;
    my_lane.backlog = &queues[new_thread].thread.backlog;
    char laneid_str[32];
    snprintf(laneid_str, sizeof(laneid_str), "%d", new_thread);
    stats_ns_t *ns = mtev_stats_ns(lanes_stats_ns, laneid_str);
    stats_handle_t *h = stats_rob_u32(ns, "backlog", my_lane.backlog);
    stats_handle_units(h, STATS_UNITS_MESSAGES);
    stats_handle_add_tag(h, "lane", laneid_str);
    mtevL(mtev_debug, "Assigning thread(%p) to %d\n", (void*)(uintptr_t)pthread_self(), my_lane.id);
  }
  return my_lane.id;
}

int
noit_metric_director_my_lane() {
  return get_my_lane();
}

interest_cnt_t
noit_adjust_checks_interest(short cnt) {
  return noit_metric_director_adjust_checks_interest(cnt);
}
interest_cnt_t
noit_metric_director_adjust_checks_interest(short adj) {
  return noit_metric_director_adjust_checks_interest_on_thread(get_my_lane(), adj);
}
interest_cnt_t
noit_metric_director_adjust_checks_interest_on_thread(int thread_id, short adj) {
  interest_cnt_t *src, *dst;
  int bytes;

  thread_id = safe_thread_id(thread_id);

  mtev_memory_begin();
  pthread_mutex_lock(&check_interests_lock);
  src = ck_pr_load_ptr(&check_interests);
  bytes = sizeof(interest_cnt_t) * nthreads;
  dst = mtev_memory_safe_malloc(bytes);
  memcpy(dst, src, bytes);
  dst[thread_id] = adjust_interest(dst[thread_id], adj);
  ck_pr_store_ptr(&check_interests, dst);
  mtev_memory_safe_free(src);
  pthread_mutex_unlock(&check_interests_lock);
  interest_cnt_t rv = dst[thread_id];
  mtev_memory_end();
  return rv;
}

uint32_t
noit_metric_director_register_search_on_thread(int thread_id, int64_t account_id, uuid_t check_uuid,
                               const char *metric_name, noit_metric_tag_search_ast_t *ast) {
  if(metric_name == NULL) metric_name = "";
  /* Here we want to add the AST to a specific thread's interest. */
  thread_id = safe_thread_id(thread_id);
  mtev_memory_begin();
  tag_search_registration_t *as = mtev_memory_safe_malloc_cleanup(sizeof(*as), tag_search_registration_cleanup);
  memset(as, 0, sizeof(*as));
  as->ast_id = ck_pr_faa_32(&ast_counter, 1);
  as->account_id = account_id;
  account_search_t *searches = account_get_search(account_id, true);
  mtev_uuid_copy(as->check_uuid, check_uuid);
  as->ast = noit_metric_tag_search_ref(ast);

  check_search_t *cs = check_get_search(searches, check_uuid, true);
  name_search_t *ns = name_get_search(cs, metric_name, strlen(metric_name), true);

  unsigned long hash = CK_HS_HASH(&ns->search_asts[thread_id].hs, tag_search_registration_hash, as);
  ck_spinlock_lock(&ns->search_asts[thread_id].lock);
  ck_hs_put(&ns->search_asts[thread_id].hs, hash, as);
  ck_spinlock_unlock(&ns->search_asts[thread_id].lock);

  mtev_memory_end();
  ck_pr_inc_64(&searches->gen);
  return as->ast_id;
}

uint32_t
noit_metric_director_register_search(int64_t account_id, uuid_t check_uuid, const char *metric_name, noit_metric_tag_search_ast_t *ast) {
  return noit_metric_director_register_search_on_thread(get_my_lane(), account_id, check_uuid, metric_name, ast);
}

mtev_boolean
noit_metric_director_deregister_search_on_thread(int thread_id, int64_t account_id, uuid_t check_uuid, const char *metric_name, uint32_t ast_id) {
  if(metric_name == NULL) metric_name = "";
  (void)check_uuid;
  thread_id = safe_thread_id(thread_id);
  mtev_memory_begin();
  account_search_t *searches = account_get_search(account_id, false);
  if(!searches) {
    mtev_memory_end();
    return mtev_false;
  }
  tag_search_registration_t *found = NULL, dummy = { .ast_id = ast_id };
  check_search_t *cs = check_get_search(searches, check_uuid, false);
  if(cs)  {
    name_search_t *ns = name_get_search(cs, metric_name, strlen(metric_name), false);
    if(ns) {
      unsigned long hash = CK_HS_HASH(&ns->search_asts[thread_id].hs, tag_search_registration_hash, &dummy);
      ck_spinlock_lock(&ns->search_asts[thread_id].lock);
      found = ck_hs_remove(&ns->search_asts[thread_id].hs, hash, &dummy);
      ck_spinlock_unlock(&ns->search_asts[thread_id].lock);
    }
  }
  if(found) mtev_memory_safe_free(found);
  mtev_memory_end();
  return found != NULL;
}

mtev_boolean
noit_metric_director_deregister_search(int64_t account_id, uuid_t check_uuid, const char *metric_name, uint32_t ast_id) {
  return noit_metric_director_deregister_search_on_thread(get_my_lane(), account_id, check_uuid, metric_name, ast_id);
}

interest_cnt_t
noit_adjust_metric_interest(uuid_t id, const char *metric, short adj) {
  return noit_metric_director_adjust_metric_interest(id, metric, adj);
}
interest_cnt_t
noit_metric_director_adjust_metric_interest(uuid_t id, const char *metric, short adj) {
  return noit_metric_director_adjust_metric_interest_on_thread(get_my_lane(), id, metric, adj);
}
interest_cnt_t
noit_metric_director_adjust_metric_interest_on_thread(int thread_id, uuid_t id, const char *metric, short adj) {
  void *vhash, **vinterests;
  mtev_hash_table *level2;
  const interest_cnt_t *interests; /* once created, never modified (until freed) */
  interest_cnt_t *newinterests = NULL;

  thread_id = safe_thread_id(thread_id);

  /* Get us our UUID->HASH(METRICS) mapping */
  if(!mtev_hash_retrieve(&id_level, (const char *)id, UUID_SIZE, &vhash)) {
    int found;
    uuid_t *copy = malloc(UUID_SIZE);
    level2 = calloc(1,sizeof(*level2));
    mtev_hash_init_locks(level2, MTEV_HASH_DEFAULT_SIZE, MTEV_HASH_LOCK_MODE_MUTEX);
    mtev_uuid_copy(*copy, id);
    if(!mtev_hash_store(&id_level, (const char *)copy, UUID_SIZE, level2)) {
      mtev_hash_destroy(level2, NULL, NULL);
      free(level2);
      free(copy);
    }
    found = mtev_hash_retrieve(&id_level, (const char *)id, UUID_SIZE, &vhash);
    mtevAssert(found);
  }
  level2 = vhash;

  mtev_memory_begin();
  if(!mtev_hash_retrieve(level2, metric, strlen(metric), (void **)&vinterests)) {
    int found;
    char *metric_copy;
    if(adj < 0) {
      /* this would result in no interests */
      mtev_memory_end();
      return 0;
    }
    metric_copy = strdup(metric);
    vinterests = malloc(sizeof(*vinterests));
    newinterests = mtev_memory_safe_calloc(nthreads, sizeof(*newinterests));
    newinterests[thread_id] = adj;
    *vinterests = newinterests;
    if(!mtev_hash_store(level2, metric_copy, strlen(metric_copy), vinterests)) {
      free(metric_copy);
      free(vinterests);
      /* we leave newinterests allocated as we'll use it in our update path */
    } else {
      /* we set the interests here... we're good. */
      mtev_memory_end();
      return adj;
    }
    found = mtev_hash_retrieve(level2, metric, strlen(metric), (void **)&vinterests);
    mtevAssert(found);
  }
  /* We have vinterests which points to a constant set of interests.
   * to change it, we copy, update and replace. coping with a race
   * by retrying.
   */
  if(!newinterests) newinterests = mtev_memory_safe_calloc(nthreads, sizeof(*newinterests));
  do {
    interests = ck_pr_load_ptr(vinterests);
    memcpy(newinterests, interests, nthreads * sizeof(*newinterests));
    newinterests[thread_id] = adjust_interest(newinterests[thread_id], adj);
  } while(!ck_pr_cas_ptr(vinterests, (void *)interests, (void *)newinterests));
  /* We've replaced interests... safely free it */
  mtev_memory_safe_free((void *)interests);
  interest_cnt_t rv = newinterests[thread_id];
  mtev_memory_end();
  return rv;
}

static void
distribute_message_with_interests(interest_cnt_t *interests, noit_metric_message_t *message) {
  int i, msg_queued = 0, msg_dropped_backlogged = 0;
  mtev_boolean msg_distributed = mtev_false;
  for(i = 0; i < nthreads; i++) {
    if(interests[i] > 0) {
      if(drop_backlog_over && ck_pr_load_32(&queues[i].thread.backlog) > drop_backlog_over) {
        msg_dropped_backlogged++;
        continue;
      }
      msg_distributed = mtev_true;
      msg_queued++;
      ck_fifo_spsc_t *fifo = (ck_fifo_spsc_t *) queues[i].thread.queue;
      ck_fifo_spsc_entry_t *fifo_entry;
      ck_pr_inc_32(&queues[i].thread.backlog);
      ck_fifo_spsc_enqueue_lock(fifo);
      fifo_entry = ck_fifo_spsc_recycle(fifo);
      if(!fifo_entry) fifo_entry = malloc(sizeof(ck_fifo_spsc_entry_t));
      noit_metric_director_message_ref(message);
      ck_fifo_spsc_enqueue(fifo, fifo_entry, message);
      ck_fifo_spsc_enqueue_unlock(fifo);
    }
  }
  stats_add64(stats_msg_dropped_backlogged, msg_dropped_backlogged);
  stats_add64(stats_msg_queued, msg_queued);
  if (msg_distributed) {
    stats_add64(stats_msg_distributed, 1);
  }
}

static void
dmflush_observe(dmflush_t *ptr) {
  bool zero;
  ck_pr_dec_32_zero(&ptr->refcnt, &zero);
  if(zero) {
    eventer_trigger(ptr->e, eventer_get_mask(ptr->e));
    free(ptr);
  }
}

void
noit_metric_director_flush(eventer_t e) {
  dmflush_t *ptr = calloc(1, sizeof(*ptr));
  ptr->e = e;
  ptr->refcnt = 1;
  for(int i=0;i<nthreads;i++) {
    ck_fifo_spsc_t *fifo = (ck_fifo_spsc_t *) queues[i].thread.queue;
    if(fifo != NULL) {
      ck_pr_inc_32(&ptr->refcnt);
      ck_fifo_spsc_entry_t *fifo_entry;
      ck_pr_inc_32(&queues[i].thread.backlog);
      ck_fifo_spsc_enqueue_lock(fifo);
      fifo_entry = ck_fifo_spsc_recycle(fifo);
      if(!fifo_entry) fifo_entry = malloc(sizeof(ck_fifo_spsc_entry_t));
      ck_fifo_spsc_enqueue(fifo, fifo_entry, DMFLUSH_FLAG(ptr));
      ck_fifo_spsc_enqueue_unlock(fifo);
    }
  }
  dmflush_observe(ptr);
}

static mtev_frrh_t *search_miss_cache;
typedef struct {
  uint64_t account_id;
  uint64_t account_gen;
  uuid_t   check_uuid;
  uint64_t check_gen;
  char     metric_name[0];
} search_miss_key;
static inline size_t make_search_miss_key(account_search_t *as, noit_metric_id_t *id, char *buf, size_t buflen) {
  if(id->name_len_with_tags + sizeof(search_miss_key) > buflen) return 0;
  search_miss_key *keyoverlay = (search_miss_key *)buf;
  keyoverlay->account_id = id->account_id;
  keyoverlay->account_gen = as->gen;
  mtev_uuid_copy(keyoverlay->check_uuid, id->id);
  unsigned long hash = CK_HS_HASH(&check_generation, check_hash, id->id);
  check_generation_t *cgen = ck_hs_get(&check_generation, hash, id->id);
  keyoverlay->check_gen = cgen ? cgen->gen : 0;
  memcpy(keyoverlay->metric_name, id->name, id->name_len_with_tags);
  return sizeof(search_miss_key) + id->name_len_with_tags;
}
static mtev_boolean generation_aware_displace(uint32_t prob, const char *key, uint32_t keylen, const void *data) {
  /* We look at the old key to see if the generational counters have changed... if they have we force a replace */
  if(keylen < sizeof(search_miss_key)) return mtev_true;
  search_miss_key *overlaykey = (search_miss_key *)key;
  account_search_t *as = account_get_search(overlaykey->account_id, false);
  if(!as || overlaykey->account_gen != as->gen) return mtev_true;
  unsigned long hash = CK_HS_HASH(&check_generation, check_hash, overlaykey->check_uuid);
  check_generation_t *cgen = ck_hs_get(&check_generation, hash, overlaykey->check_uuid);
  uint64_t expected_gen = cgen ? cgen->gen : 0;
  if(overlaykey->check_gen != expected_gen) {
    stats_add64(stats_cache_purge_version, 1);
    return mtev_true;
  }
  if(prob <= (uint32_t)mtev_rand()) {
    stats_add64(stats_cache_purge_rand, 1);
    return mtev_true;
  }
  return mtev_false;
}
static mtev_boolean check_search_miss_cache(account_search_t *as, noit_metric_id_t *id) {
  if(search_miss_cache == NULL) return mtev_false;
  char key[MAX_METRIC_TAGGED_NAME + sizeof(search_miss_key)];
  size_t keylen = make_search_miss_key(as, id, key, sizeof(key));
  if(keylen == 0) return mtev_false;
  stats_add64(stats_cache_lookups, 1);
  if(mtev_frrh_get(search_miss_cache, key, keylen) != NULL) {
    stats_add64(stats_cache_hits, 1);
    return mtev_true;
  }
  return mtev_false;
}
static void set_search_miss_cache(account_search_t *as, noit_metric_id_t *id) {
  if(search_miss_cache == NULL) return;
  char key[MAX_METRIC_TAGGED_NAME + sizeof(search_miss_key)];
  size_t keylen = make_search_miss_key(as, id, key, sizeof(key));
  if(keylen == 0) return;
  mtev_frrh_set(search_miss_cache, key, keylen, "");
}

static void
distribute_metric(noit_metric_message_t *message) {
  mtev_perftimer_t start;
  static const uuid_t uuid_zero = { };
  void *vhash, **vinterests;
  interest_cnt_t has_interests = 0;
  interest_cnt_t interests[nthreads];
  memset(interests, 0, sizeof(interests));

  mtev_perftimer_start(&start);
  mtev_memory_begin();
  /* First we process interests for specific metrics: uuid-metric|ST[tags...] */
  for(int check_idx=0; check_idx<2; check_idx++) {
    /* First time through we use the message's ID, second time we use the NULL UUID
     * which is used to "match any" check.
     */
    if(mtev_hash_retrieve(&id_level, check_idx ? (const char *)&uuid_zero : (const char *)&message->id.id, UUID_SIZE,
        &vhash)) {
      if(mtev_hash_retrieve((mtev_hash_table *) vhash, message->id.name,
                            message->id.name_len_with_tags, (void **)&vinterests)) {
        memcpy(interests, *vinterests, sizeof(interests)); /* This was SMR allocated and is safe */
        for(int i=0; i<nthreads; i++) has_interests |= interests[i];
      }
      if(mtev_hash_retrieve((mtev_hash_table *) vhash, message->id.name,
                            message->id.name_len, (void **)&vinterests)) {
        memcpy(interests, *vinterests, sizeof(interests)); /* This was SMR allocated and is safe */
        for(int i=0; i<nthreads; i++) has_interests |= interests[i];
      }
    }
  }

  /* Next we run search queries per lane to find matches */
  account_search_t *as = account_get_search(message->id.account_id, false);
  if(as) {
    if(!check_search_miss_cache(as, &message->id)) {
      check_search_t *css[2];
      css[0] = check_get_search(as, message->id.id, false);
      css[1] = check_get_search(as, uuid_zero, false);
      interest_cnt_t search_interest = 0;
      for(int cs_idx=0; cs_idx<2; cs_idx++) {
        if(css[cs_idx] == NULL) continue;
        name_search_t *nss[2];
        nss[0] = name_get_search(css[cs_idx], message->id.name, message->id.name_len, false);
        nss[1] = name_get_search(css[cs_idx], "", 0, false);
        for(int ns_idx=0; ns_idx<2; ns_idx++) {
          if(nss[ns_idx] == NULL) continue;
          name_search_t *ns = nss[ns_idx];
          for(int i=0; i<nthreads; i++) {
            tag_search_registration_t *search;
            ck_hs_iterator_t iter;
            if(interests[i] != 0) continue;

            /* First do the specific check */
            ck_hs_iterator_init(&iter);
            while(ck_hs_next_spmc(&ns->search_asts[i].hs, &iter, (void **)&search)) {
              if(noit_metric_tag_search_evaluate_against_metric_id(search->ast, &message->id)) {
                has_interests = interests[i] = search_interest = 1;
                break; /* no need to process additional searches in this lane */
              }
            }
          }
        }
      }
      if(search_interest == 0) set_search_miss_cache(as, &message->id);
    }
  }
  mtev_memory_end();

  /* Now call the hook... start with no interests and then
   * build out a interest_cnt_t* hook_interests with those that
   * where not in the list we used above.
   */
  if(metric_director_want_hook_exists()) {
    int wants[nthreads];
    memset(wants, 0, sizeof(wants));

    switch(metric_director_want_hook_invoke(message, wants, nthreads)) {
      case MTEV_HOOK_DONE:
      case MTEV_HOOK_CONTINUE:
        for(int i=0;i<nthreads;i++) {
          if(wants[i] && interests[i] == 0) {
            has_interests = interests[i] = 1;
          }
        }
      default: break;
    }
  }

  if(metric_director_revise_hook_exists()) {
    /* This revises the list, so we reset has_interests and recalculate */
    has_interests = 0;
    if(metric_director_revise_hook_invoke(message, interests, nthreads) != MTEV_HOOK_ABORT) {
      for(int i=0; i<nthreads; i++) has_interests |= interests[i];
    }
  }
  stats_set_hist_intscale(stats_msg_selection_latency, mtev_perftimer_elapsed(&start), -9, 1);
  if(has_interests) distribute_message_with_interests(interests, message);
}

static void
distribute_check(noit_metric_message_t *message) {
  mtev_memory_begin();
  distribute_message_with_interests(ck_pr_load_ptr(&check_interests), message);
  mtev_memory_end();
}

static mtev_hash_table *
get_dedupe_hash(uint64_t whence)
{
  struct hash_and_time *hash_with_time;
  mtev_hash_table *hash;

  if (mtev_hash_retrieve(&dedupe_hashes, (const char *)&whence, sizeof(whence), (void **)&hash_with_time) == 1) {
    hash = &hash_with_time->hash;
  } else {
    hash_with_time = calloc(1, sizeof(struct hash_and_time));

    mtev_hash_init_locks(&hash_with_time->hash, MTEV_HASH_DEFAULT_SIZE, MTEV_HASH_LOCK_MODE_MUTEX);
    uint64_t *stored_ts = calloc(1, sizeof(uint64_t));
    *stored_ts = whence;
    if (mtev_hash_store(&dedupe_hashes, (const char *)stored_ts, sizeof(*stored_ts), hash_with_time) == 0) {
      /* ugh, someone beat us */
      free(stored_ts);
      mtev_hash_destroy(&hash_with_time->hash, NULL, NULL);
      free(hash_with_time);
      if (mtev_hash_retrieve(&dedupe_hashes, (const char *)&whence, sizeof(whence), (void **)&hash_with_time) == 0) {
        return NULL;
      }
    }
    hash = &hash_with_time->hash;
  }

  hash_with_time->last_touched_s = mtev_gethrtime() / 1000000000;
  return hash;
}

static int
noit_metric_director_prune_dedup(eventer_t e, int mask, void *unused,
    struct timeval *now) {
  struct timeval etime;
  mtev_hash_iter iter = MTEV_HASH_ITER_ZERO;
  uint64_t now_hrtime = mtev_gethrtime() / 1000000000;
  const char *k;
  int klen;
  void *data;
  struct hash_and_time *hash_with_time;

  struct removable_hashes {
    uint64_t key;
    struct hash_and_time *data;
    struct removable_hashes *next;
  };

  struct removable_hashes *head = NULL;
  struct removable_hashes *tail = NULL;

  /* build a list of expirable items */
  while(mtev_hash_next(&dedupe_hashes, &iter, &k, &klen, &data)) {
    hash_with_time = data;
    if (now_hrtime > hash_with_time->last_touched_s && now_hrtime - hash_with_time->last_touched_s > 10) {
      struct removable_hashes *h = calloc(1, sizeof(struct removable_hashes));
      h->key = *(uint64_t *)k;
      h->data = hash_with_time;
      if (tail != NULL) {
        tail->next = h;
      }
      tail = h;

      if (head == NULL) {
        head = tail;
      }
    }
  }

  /* expire them */
  while (head != NULL) {
    mtev_hash_delete(&dedupe_hashes, (const char *)&head->key, sizeof(head->key), free, NULL);
    mtev_hash_destroy(&head->data->hash, free, NULL);
    free(head->data);
    struct removable_hashes *prev = head;
    head = head->next;
    free(prev);
  }

  etime = eventer_get_whence(e);
  etime.tv_sec = now->tv_sec + 5;
  eventer_add_at(noit_metric_director_prune_dedup, unused, etime);
  return 0;
}

static void
distribute_message(noit_metric_message_t *message) {
  if(message->type == 'H' || message->type == 'M') {
    distribute_metric(message);
  } else {
    distribute_check(message);
  }
}

noit_metric_message_t *noit_metric_director_lane_next_backlog(uint32_t *backlog) {
  noit_metric_message_t *msg = NULL;
  if(my_lane.fifo == NULL)
    return NULL;
 again:
  ck_fifo_spsc_dequeue_lock(my_lane.fifo);
  if(ck_fifo_spsc_dequeue(my_lane.fifo, &msg) == false) {
    msg = NULL;
  }
  ck_fifo_spsc_dequeue_unlock(my_lane.fifo);
  if((uintptr_t)msg & FLUSHFLAG) {
    dmflush_observe(DMFLUSH_UNFLAG((dmflush_t *)msg));
    goto again;
  }
  if (msg) {
    ck_pr_dec_32(my_lane.backlog);
    stats_add64(stats_msg_delivered, 1);
  }
  if(backlog) *backlog = ck_pr_load_32(my_lane.backlog);
  return msg;
}

noit_metric_message_t *noit_metric_director_lane_next() {
  return noit_metric_director_lane_next_backlog(NULL);
}

static noit_noit_t *
get_noit(const char *payload, int payload_len, noit_noit_t *data) {
  const char *cp = payload, *end = payload + payload_len;
  while(cp < end && *cp != '\t') cp++;
  if(++cp >= end) return NULL;
  data->name = cp;
  while(cp < end && *cp != '\t') cp++;
  if(cp >= end) return NULL;
  data->name_len = cp - data->name;
  return data;
}
static void
handle_metric_buffer(const char *payload, int payload_len,
    int has_noit, noit_noit_t *noit) {

  if (payload_len <= 0) {
    return;
  }

  switch (payload[0]) {
    case 'C':
    case 'D':
    case 'S':
    case 'H':
    case 'M':
      {
        // mtev_fq will free the fq_msg -> copy the payload
        int nlen = payload_len;
        if(noit) nlen += noit->name_len+2;
        char *copy = calloc(1, nlen+1);
        memcpy(copy, payload, payload_len);
        if(noit) memcpy(copy + payload_len + 1, noit->name, noit->name_len);
        noit_metric_message_t *message = calloc(1, sizeof(noit_metric_message_t));

        message->type = copy[0];
        message->original_allocated = mtev_true;
        message->original_message = copy;
        message->original_message_len = payload_len;
        noit_metric_director_message_ref(message);

        stats_add64(stats_msg_seen, 1);
        int rv = noit_message_decoder_parse_line(message, has_noit);

        if(message->value.whence_ms) // ignore screwy messages
          stats_set_hist_intscale(stats_msg_delay, mtev_now_ms() - message->value.whence_ms, -3, 1);
        if(drop_before_threshold_ms && message->value.whence_ms < drop_before_threshold_ms) {
          stats_add64(stats_msg_dropped_threshold, 1);
          goto bail;
        }

        if(message->noit.name == NULL && noit) {
          message->noit.name_len = noit->name_len;
          message->noit.name = copy + payload_len + 1;
        }

        if(rv == 1) {
          distribute_message(message);
        }

      bail:
        noit_metric_director_message_deref(message);
      }
      break;
    case 'B':
      {
        int n_metrics, i;
        char **metrics = NULL;
        noit_noit_t src_noit_impl, *src_noit = NULL;
        src_noit = get_noit(payload, payload_len, &src_noit_impl);
        n_metrics = noit_check_log_b_to_sm((const char *)payload, payload_len,
            &metrics, has_noit);
        for(i = 0; i < n_metrics; i++) {
          if(metrics[i] == NULL) continue;
          handle_metric_buffer(metrics[i], strlen(metrics[i]), false, src_noit);
          free(metrics[i]);
        }
        free(metrics);
      }
      break;
    default: ;
      /* ignored */
  }
}

static uint64_t
get_message_time(const char* msg, int msg_length) {
  const int minimum_bytes_before_second_tab = sizeof("M\t1.2.3.4");
  if (msg_length <= minimum_bytes_before_second_tab) {
    mtevL(mtev_error, "Unable to retrieve timestamp from message: %s\n", msg);
    return 0;
  }

  const char* time_str = (char*) memchr(msg + minimum_bytes_before_second_tab,
      '\t', msg_length - minimum_bytes_before_second_tab);
  if (time_str) {
    ++time_str;
  }

  if (time_str == NULL) {
    mtevL(mtev_error, "Unable to retrieve timestamp from message: %s\n", msg);
    return 0;
  }

  return atol(time_str);
}

static mtev_boolean
check_duplicate(const char *payload, const size_t payload_len) {
  if (dedupe) {
    unsigned char *digest = malloc(MD5_DIGEST_LENGTH);
    const EVP_MD *md = EVP_get_digestbyname("MD5");
    mtevAssert(md);
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, md, NULL);
    EVP_DigestUpdate(ctx, payload, payload_len);
    EVP_DigestFinal(ctx, digest, NULL);
    EVP_MD_CTX_free(ctx);

    uint64_t whence = get_message_time(payload, payload_len);
    if(whence > 0) {
      mtev_hash_table *hash = get_dedupe_hash(whence);
      if (hash) {
        int x = mtev_hash_store(hash, (const char *)digest, MD5_DIGEST_LENGTH, (void *)0x1);
        if (x == 0) {
          /* this is a dupe */
          free(digest);
          return mtev_true;
        }
      } else {
        free(digest);
      }
    } else {
      free(digest);
    }
  }
  return mtev_false;
}

static void
handle_prometheus_message(const void *data, size_t data_len) {
  mtev_dyn_buffer_t uncompressed;
  mtev_dyn_buffer_init(&uncompressed);
  size_t uncompressed_size = 0;

  if (!noit_prometheus_snappy_uncompress(&uncompressed, &uncompressed_size,
                                         data, data_len)) {
    mtevL(mtev_error, "ERROR: Cannot snappy decompress incoming prometheus\n");
    return;
  }
  mtev_dyn_buffer_advance(&uncompressed, uncompressed_size);
  Prometheus__WriteRequest *write = prometheus__write_request__unpack(&protobuf_c_system_allocator,
                                                                      mtev_dyn_buffer_used(&uncompressed),
                                                                      mtev_dyn_buffer_data(&uncompressed));
  if(!write) {
    mtev_dyn_buffer_destroy(&uncompressed);
    mtevL(mtev_error, "Prometheus__WriteRequest decode: protobuf invalid\n");
    return;
  }

  for (size_t i = 0; i < write->n_timeseries; i++) {
    Prometheus__TimeSeries *ts = write->timeseries[i];
    /* each timeseries has a list of labels (Tags) and a list of samples */
    prometheus_coercion_t coercion = noit_prometheus_metric_name_coerce(ts->labels, ts->n_labels,
                                                                        true, true, NULL);
    char *metric_name = noit_prometheus_metric_name_from_labels(ts->labels, ts->n_labels, coercion.units,
                                                            coercion.is_histogram);
    // TODO: More handling
    free(metric_name);
  }
  mtev_dyn_buffer_destroy(&uncompressed);
}

static mtev_hook_return_t
handle_fq_message(void *closure, struct fq_conn_s *client, int idx, struct fq_msg *m,
                  void *payload, size_t payload_len) {
  if(ck_pr_load_32(&director_in_use) == 0) return MTEV_HOOK_CONTINUE;
  if(check_duplicate(payload, payload_len) == mtev_false) {
    handle_metric_buffer(payload, payload_len, 1, NULL);
  }
  return MTEV_HOOK_CONTINUE;
}
static mtev_hook_return_t
handle_kafka_message(void *closure, mtev_rd_kafka_message_t *msg) {
  if(ck_pr_load_32(&director_in_use) == 0) {
    return MTEV_HOOK_CONTINUE;
  }
  mtev_rd_kafka_message_ref(msg);
  if (!strcasecmp(msg->protocol, "prometheus")) {
    handle_prometheus_message(msg->payload, msg->payload_len);
  }
  else {
    if(check_duplicate(msg->payload, msg->payload_len) == mtev_false) {
      handle_metric_buffer(msg->payload, msg->payload_len, 1, NULL);
    }
  }
  mtev_rd_kafka_message_deref(msg);
  return MTEV_HOOK_CONTINUE;
}
static mtev_hook_return_t
handle_log_line(void *closure, mtev_log_stream_t ls, const struct timeval *whence,
                const char *timebuf, int timebuflen,
                const char *debugbuf, int debugbuflen,
                const char *line, size_t len) {
  if(!ls) return MTEV_HOOK_CONTINUE;
  if(ck_pr_load_32(&director_in_use) == 0) return MTEV_HOOK_CONTINUE;
  const char *name = mtev_log_stream_get_name(ls);
  if(!name ||
     (strcmp(name,"metrics") && strcmp(name,"bundle") &&
      strcmp(name,"check") && strcmp(name,"status")))
    return MTEV_HOOK_CONTINUE;
  handle_metric_buffer(line, len, -1, NULL);
  return MTEV_HOOK_CONTINUE;
}

void
noit_metric_director_dedupe(mtev_boolean d)
{
  dedupe = d;
}

void noit_metric_director_init() {
  mtev_stats_init();
  stats_ns = mtev_stats_ns(mtev_stats_ns(NULL, "noit"), "metric_director");
  lanes_stats_ns = mtev_stats_ns(stats_ns, "lanes");
  stats_ns_add_tag(stats_ns, "subsystem", "metric_director");

  stats_ns_t *cache_ns = mtev_stats_ns(stats_ns, "cache");
  stats_cache_size = stats_rob_u32(cache_ns, "size", &miss_cache_size);
  stats_handle_tagged_name(stats_cache_size, "cache_size");
  stats_cache_lookups = stats_register_fanout(cache_ns, "lookups", STATS_TYPE_COUNTER, 16);
  stats_handle_tagged_name(stats_cache_lookups, "cache_lookups");
  stats_cache_hits = stats_register_fanout(cache_ns, "hits", STATS_TYPE_COUNTER, 16);
  stats_handle_tagged_name(stats_cache_hits, "cache_hits");
  stats_cache_purge_rand = stats_register_fanout(cache_ns, "cache_purges_rand", STATS_TYPE_COUNTER, 16);
  stats_handle_tagged_name(stats_cache_purge_rand, "cache_purges");
  stats_handle_add_tag(stats_cache_purge_rand, "reason", "displacement");
  stats_cache_purge_version = stats_register_fanout(cache_ns, "cache_purges_version", STATS_TYPE_COUNTER, 16);
  stats_handle_tagged_name(stats_cache_purge_version, "cache_purges");
  stats_handle_add_tag(stats_cache_purge_version, "reason", "stale");
  /* total count of messages read from fq */
  stats_msg_seen = stats_register_fanout(stats_ns, "seen", STATS_TYPE_COUNTER, 16);
  stats_handle_units(stats_msg_seen, STATS_UNITS_MESSAGES);
  /* count of messages dropped due to drop_threshold */
  stats_ns_t *stats_ns_dropped = mtev_stats_ns(stats_ns, "dropped");
  stats_ns_add_tag(stats_ns_dropped, "units", STATS_UNITS_MESSAGES);
  stats_msg_dropped_threshold = stats_register_fanout(stats_ns_dropped, "too_late", STATS_TYPE_COUNTER, 16);
  stats_handle_tagged_name(stats_msg_dropped_threshold, "dropped");
  stats_handle_add_tag(stats_msg_dropped_threshold, "reason", "too_late");
  /* count of messages dropped due to drop_backlogged */
  stats_msg_dropped_backlogged = stats_register_fanout(stats_ns_dropped, "too_full", STATS_TYPE_COUNTER, 16);
  stats_handle_tagged_name(stats_msg_dropped_backlogged, "dropped");
  stats_handle_add_tag(stats_msg_dropped_backlogged, "reason", "too_full");
  /* count of messages distributed to at least one lane */
  stats_msg_distributed = stats_register_fanout(stats_ns, "distributed", STATS_TYPE_COUNTER, 16);
  stats_handle_units(stats_msg_distributed, STATS_UNITS_MESSAGES);
  /* count of messages queued for delivery */
  stats_msg_queued = stats_register_fanout(stats_ns, "queued", STATS_TYPE_COUNTER, 16);
  stats_handle_units(stats_msg_queued, STATS_UNITS_MESSAGES);
  /* count of messages delivered */
  stats_msg_delivered = stats_register_fanout(stats_ns, "delivered", STATS_TYPE_COUNTER, 16);
  stats_handle_units(stats_msg_delivered, STATS_UNITS_MESSAGES);
  /* histogram of delay timings */
  stats_msg_delay = stats_register_fanout(stats_ns, "delay", STATS_TYPE_HISTOGRAM, 16);
  stats_handle_units(stats_msg_delay, STATS_UNITS_SECONDS);
  stats_msg_selection_latency = stats_register_fanout(stats_ns, "selection_latency", STATS_TYPE_HISTOGRAM, 16);
  stats_handle_units(stats_msg_selection_latency, STATS_UNITS_SECONDS);

  nthreads = eventer_loop_concurrency();
  mtevAssert(nthreads > 0);

  queues = calloc(sizeof(*queues),nthreads);

  mtev_conf_get_uint32(MTEV_CONF_ROOT, "//metric_director/@miss_cache_size", &miss_cache_size);
  double miss_cache_replacement_probability = 0.1;
  mtev_conf_get_double(MTEV_CONF_ROOT, "//metric_director/@replacement_rate", &miss_cache_replacement_probability);
  if(miss_cache_replacement_probability < 0) miss_cache_replacement_probability = 0;
  if(miss_cache_replacement_probability > 0.999) miss_cache_replacement_probability = 0.999;
  if(miss_cache_size) {
    search_miss_cache =
      mtev_frrh_alloc(miss_cache_size, 0,
                      UINT32_MAX * miss_cache_replacement_probability,
                      NULL, NULL, NULL);
    mtev_frrh_set_prob_function(search_miss_cache, generation_aware_displace);
  }

  mtev_memory_begin();

  check_interests = mtev_memory_safe_calloc(sizeof(*check_interests),nthreads);
  ck_hs_init(&account_searches, CK_HS_MODE_OBJECT | CK_HS_MODE_SPMC,
             account_hash, account_compare,
             &mtev_memory_safe_ck_malloc, 1024, mtev_rand());
  ck_hs_init(&check_generation, CK_HS_MODE_OBJECT | CK_HS_MODE_SPMC,
             check_hash, check_compare,
             &mtev_memory_safe_ck_malloc, 1024*128, mtev_rand());

  mtev_memory_end();

  pthread_mutex_init(&check_interests_lock, NULL);
  eventer_add_in_s_us(noit_metric_director_prune_dedup, NULL, 2, 0);
}

static mtev_hook_return_t
noit_director_hooks_register(void *closure) {
  (void)closure;
  
  /* subscribe to metric messages submitted via fq */
  if(mtev_fq_handle_message_hook_register_available())
    mtev_fq_handle_message_hook_register("metric-director", handle_fq_message, NULL);

  if(mtev_kafka_handle_message_hook_register_available())
    mtev_kafka_handle_message_hook_register("metric-director", handle_kafka_message, NULL);

  /* metrics can be injected into the metric director via the "metrics" log channel */
  mtev_log_line_hook_register("metric-director", handle_log_line, NULL);
  return MTEV_HOOK_CONTINUE;
}

void noit_metric_director_init_globals(void) {
  mtev_hash_init_locks(&id_level, MTEV_HASH_DEFAULT_SIZE, MTEV_HASH_LOCK_MODE_MUTEX);
  mtev_hash_init_locks(&dedupe_hashes, MTEV_HASH_DEFAULT_SIZE, MTEV_HASH_LOCK_MODE_MUTEX);
  eventer_name_callback("noit_metric_director_prune_dedup",
                        noit_metric_director_prune_dedup);
  dso_post_init_hook_register("noit_director_hooks_register", noit_director_hooks_register, NULL);
}

void
noit_metric_director_drop_backlogged(uint32_t limit) {
  drop_backlog_over = limit;
}

void
noit_metric_director_drop_before(double t) {
  drop_before_threshold_ms = t * 1000;
}
