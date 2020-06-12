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
#include <mtev_fq.h>
#include <mtev_hash.h>
#include <mtev_memory.h>
#include <mtev_str.h>
#include <mtev_log.h>
#include <ck_fifo.h>
#include <mtev_fq.h>
#include <mtev_hooks.h>
#include <mtev_uuid.h>
#include <mtev_stats.h>
#include <mtev_time.h>
#include <mtev_rand.h>

#include <openssl/md5.h>
#include <ck_hs.h>

#include <noit_metric_director.h>
#include <noit_metric_tag_search.h>
#include <noit_check_log_helpers.h>
#include <noit_message_decoder.h>

/* pointers enqueue with this flag set, are flush pointers.
 * the flag should be removed and used and the pointer will
 * point to a dmflush_t
 */
#define FLUSHFLAG 0x1
typedef struct {
  eventer_t e;
  uint32_t refcnt;
} dmflush_t;

#define DMFLUSH_FLAG(a) ((dmflush_t *)((uintptr_t)(a) | FLUSHFLAG))
#define DMFLUSH_UNFLAG(a) ((dmflush_t *)((uintptr_t)(a) & ~(uintptr_t)FLUSHFLAG))

MTEV_HOOK_IMPL(metric_director_want, (noit_metric_message_t *m, int *wants, int wants_len),
               void *, closure, (void *closure, noit_metric_message_t *m, int *wants, int wants_len),
               (closure, m, wants, wants_len));

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
static thread_queue_t *queues;
static struct {
  ck_spinlock_t lock;
  ck_hs_t hs;
} *search_asts;
static mtev_hash_table id_level;
static pthread_mutex_t id_level_lock;
static mtev_hash_table dedupe_hashes;
static interest_cnt_t *check_interests;
static pthread_mutex_t check_interests_lock;
static mtev_boolean dedupe = mtev_true;
static uint32_t director_in_use = 0;
static uint64_t drop_before_threshold_ms = 0;
static uint32_t drop_backlog_over = 0;

static stats_ns_t *stats_ns;
static stats_handle_t *stats_msg_delay;
static stats_handle_t *stats_msg_seen;
static stats_handle_t *stats_msg_dropped_threshold;
static stats_handle_t *stats_msg_dropped_backlogged;
static stats_handle_t *stats_msg_distributed;
static stats_handle_t *stats_msg_queued;
static stats_handle_t *stats_msg_delivered;

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
noit_metric_director_adjust_checks_interest(short cnt) {
  return noit_metric_director_adjust_checks_interest_on_thread(get_my_lane(), cnt);
}
interest_cnt_t
noit_metric_director_adjust_checks_interest_on_thread(int thread_id, short cnt) {
  interest_cnt_t *src, *dst;
  int icnt, bytes;

  if(thread_id < 0) thread_id ^= thread_id;
  thread_id = thread_id % nthreads;

  mtev_memory_begin();
  pthread_mutex_lock(&check_interests_lock);
  src = ck_pr_load_ptr(&check_interests);
  bytes = sizeof(interest_cnt_t) * nthreads;
  dst = mtev_memory_safe_malloc(bytes);
  memcpy(dst, src, bytes);
  icnt = dst[thread_id];
  icnt += cnt;
  if(icnt < 0) icnt = 0;
  mtevAssert(icnt <= 0xffff);
  dst[thread_id] = icnt;
  ck_pr_store_ptr(&check_interests, dst);
  mtev_memory_safe_free(src);
  pthread_mutex_unlock(&check_interests_lock);
  mtev_memory_end();
  return icnt;
}

/*
 * the ASTs... each lane can register a set of ASTs
 * We are doing the concurrently, so we make a copy of the ASTs.
 *
 * [ thread_id, ck_hs [ ID: AST ] ]
 *
 */

static uint32_t ast_counter = 1;
struct search2 {
  uint32_t    ast_id; /* unique identifier for removal */
  int64_t account_id; /* >= 0 -> exact match, < 0 -> allow all */
  uuid_t  check_uuid; /* UUID_ZERO -> any, otherwise exact match */
  noit_metric_tag_search_ast_t *ast;
};

static void
search2_cleanup(void *v) {
  struct search2 *s = (struct search2 *)v;
  noit_metric_tag_search_free(s->ast);
}

static unsigned long
search2_hash(const void *object, unsigned long seed) {
  struct search2 *s = (struct search2 *)object;
  return s->ast_id * seed;
}
static bool
search2_compare(const void *previous, const void *compare) {
  return ((struct search2 *)previous)->ast_id == ((struct search2 *)compare)->ast_id;
}

uint32_t
noit_metric_director_register_search_on_thread(int thread_id, int64_t account_id, uuid_t check_uuid,
                               noit_metric_tag_search_ast_t *ast) {
  /* Here we want to add the AST to a specific thread's interest. */
  if(thread_id < 0) thread_id ^= thread_id;
  thread_id = thread_id % nthreads;
  mtev_memory_begin();
  struct search2 *as = mtev_memory_safe_malloc_cleanup(sizeof(*as), search2_cleanup);
  memset(as, 0, sizeof(*as));
  as->ast_id = ck_pr_faa_32(&ast_counter, 1);
  as->account_id = account_id;
  mtev_uuid_copy(as->check_uuid, check_uuid);
  as->ast = noit_metric_tag_search_ref(ast);
  unsigned long hash = CK_HS_HASH(&search_asts[thread_id].hs, search2_hash, as);
  ck_spinlock_lock(&search_asts[thread_id].lock);
  ck_hs_put(&search_asts[thread_id].hs, hash, as);
  ck_spinlock_unlock(&search_asts[thread_id].lock);
  mtev_memory_end();
  return as->ast_id;
}

uint32_t
noit_metric_director_register_search(int64_t account_id, uuid_t check_uuid, noit_metric_tag_search_ast_t *ast) {
  return noit_metric_director_register_search_on_thread(get_my_lane(), account_id, check_uuid, ast);
}

mtev_boolean
noit_metric_director_deregister_search_on_thread(int thread_id, uint32_t ast_id) {
  if(thread_id < 0) thread_id ^= thread_id;
  thread_id = thread_id % nthreads;
  mtev_memory_begin();
  struct search2 *found, dummy = { .ast_id = ast_id };
  unsigned long hash = CK_HS_HASH(&search_asts[thread_id].hs, search2_hash, &dummy);
  ck_spinlock_lock(&search_asts[thread_id].lock);
  found = ck_hs_remove(&search_asts[thread_id].hs, hash, &dummy);
  ck_spinlock_unlock(&search_asts[thread_id].lock);
  if(found) mtev_memory_safe_free(found);
  mtev_memory_end();
  return found != NULL;
}

mtev_boolean
noit_metric_director_deregister_search(uint32_t ast_id) {
  return noit_metric_director_deregister_search_on_thread(get_my_lane(), ast_id);
}

interest_cnt_t
noit_adjust_metric_interest(uuid_t id, const char *metric, short cnt) {
  return noit_metric_director_adjust_metric_interest(id, metric, cnt);
}
interest_cnt_t
noit_metric_director_adjust_metric_interest(uuid_t id, const char *metric, short cnt) {
  return noit_metric_director_adjust_metric_interest_on_thread(get_my_lane(), id, metric, cnt);
}
interest_cnt_t
noit_metric_director_adjust_metric_interest_on_thread(int thread_id, uuid_t id, const char *metric, short cnt) {
  int icnt;
  void *vhash, **vinterests;
  mtev_hash_table *level2;
  const interest_cnt_t *interests; /* once created, never modified (until freed) */

  if(thread_id < 0) thread_id ^= thread_id;
  thread_id = thread_id % nthreads;

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
    if(cnt < 0) {
      /* this would result in no interests */
      mtev_memory_end();
      return 0;
    }
    metric_copy = strdup(metric);
    vinterests = malloc(sizeof(*vinterests));
    interest_cnt_t *newinterests = mtev_memory_safe_calloc(nthreads, sizeof(*newinterests));
    newinterests[thread_id] = cnt;
    *vinterests = newinterests;
    if(!mtev_hash_store(level2, metric_copy, strlen(metric_copy), vinterests)) {
      free(metric_copy);
      mtev_memory_safe_free(*vinterests);
      free(vinterests);
    } else {
      /* we set the interests here... we're good. */
      mtev_memory_end();
      return cnt;
    }
    found = mtev_hash_retrieve(level2, metric, strlen(metric), (void **)&vinterests);
    mtevAssert(found);
  }
  /* We have vinterests which points to a constant set of interests.
   * to change it, we copy, update and replace. coping with a race
   * by retrying.
   */
  interest_cnt_t *newinterests = NULL;
  do {
    if(newinterests) mtev_memory_safe_free(newinterests);
    interests = ck_pr_load_ptr(vinterests);
    /* This is fine because thread_id is only ours */
    icnt = interests[thread_id];
    icnt += cnt;
    if(icnt < 0) icnt = 0;
    mtevAssert(icnt <= 0xffff);
    interest_cnt_t *newinterests = mtev_memory_safe_calloc(nthreads, sizeof(*newinterests));
    memcpy(newinterests, interests, nthreads * sizeof(*newinterests));
    /* update out copy */
    newinterests[thread_id] = icnt;
  } while(!ck_pr_cas_ptr(vinterests, (void *)interests, (void *)newinterests));
  /* We've replaced interests... safely free it */
  mtev_memory_safe_free((void *)interests);
  mtev_memory_end();
  return icnt;
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

static void
distribute_metric(noit_metric_message_t *message) {
  int i;
  static uuid_t uuid_zero = { 0 };
  void *vhash, **vinterests;
  interest_cnt_t has_interests = 0;
  interest_cnt_t interests[nthreads];
  memset(interests, 0, sizeof(interests));


  mtev_memory_begin();
  /* First we process interests for specific metrics: uuid-metric|ST[tags...] */
  for(i=0; i<2; i++) {
    /* First time through we use the message's ID, second time we use the NULL UUID
     * which is used to "match any" check.
     */
    if(mtev_hash_retrieve(&id_level, i ? (const char *)&uuid_zero : (const char *)&message->id.id, UUID_SIZE,
        &vhash)) {
      if(mtev_hash_retrieve((mtev_hash_table *) vhash, message->id.name,
                            message->id.name_len_with_tags, (void **)&vinterests)) {
        memcpy(interests, *vinterests, sizeof(interests)); /*  we access this exactly once, no need to ck load or tmp variable */
        for(i=0; i<nthreads; i++) has_interests |= interests[i];
      }
      if(mtev_hash_retrieve((mtev_hash_table *) vhash, message->id.name,
                            message->id.name_len, (void **)&vinterests)) {
        memcpy(interests, *vinterests, sizeof(interests)); /*  we access this exactly once, no need to ck load or tmp variable */
        for(i=0; i<nthreads; i++) has_interests |= interests[i];
      }
    }
  }

  /* Next we run search queries per lane to find matches */
  for(i=0; i<nthreads; i++) {
    if(interests[i] != 0) continue; /* already interest, skip any work */
    struct search2 *search;
    ck_hs_iterator_t iter = CK_HS_ITERATOR_INITIALIZER;
    while(ck_hs_next_spmc(&search_asts[i].hs, &iter, (void **)&search)) {
      if((search->account_id < 0 || (uint64_t)search->account_id == message->id.account_id) &&
         noit_metric_tag_search_evaluate_against_metric_id(search->ast, &message->id)) {
        has_interests = interests[i] = 1;
        break; /* no need to process additional searches in this lane */
      }
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
        for(i=0;i<nthreads;i++) {
          if(wants[i] && interests[i] == 0) {
            has_interests = interests[i] = 1;
          }
        }
      default: break;
    }
  }
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
check_duplicate(char *payload, size_t payload_len) {
  if (dedupe) {
    unsigned char *digest = malloc(MD5_DIGEST_LENGTH);
    MD5((unsigned char*)payload, payload_len, digest);

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
  stats_ns_add_tag(stats_ns, "subsystem", "metric_director");
  stats_ns_add_tag(stats_ns, "units", "messages");
  /* total count of messages read from fq */
  stats_msg_seen = stats_register_fanout(stats_ns, "seen", STATS_TYPE_COUNTER, 16);
  /* count of messages dropped due to drop_threshold */
  stats_ns_t *stats_ns_dropped = mtev_stats_ns(stats_ns, "dropped");
  stats_msg_dropped_threshold = stats_register_fanout(stats_ns_dropped, "too_late", STATS_TYPE_COUNTER, 16);
  stats_handle_tagged_name(stats_msg_dropped_threshold, "dropped");
  stats_handle_add_tag(stats_msg_dropped_threshold, "reason", "too_late");
  /* count of messages dropped due to drop_backlogged */
  stats_msg_dropped_backlogged = stats_register_fanout(stats_ns_dropped, "too_full", STATS_TYPE_COUNTER, 16);
  stats_handle_tagged_name(stats_msg_dropped_backlogged, "dropped");
  stats_handle_add_tag(stats_msg_dropped_backlogged, "reason", "too_full");
  /* count of messages distributed to at least one lane */
  stats_msg_distributed = stats_register_fanout(stats_ns, "distributed", STATS_TYPE_COUNTER, 16);
  /* count of messages queued for delivery */
  stats_msg_queued = stats_register_fanout(stats_ns, "queued", STATS_TYPE_COUNTER, 16);
  /* count of messages delivered */
  stats_msg_delivered = stats_register_fanout(stats_ns, "delivered", STATS_TYPE_COUNTER, 16);
  /* histogram of delay timings */
  stats_msg_delay = stats_register_fanout(stats_ns, "delay", STATS_TYPE_HISTOGRAM, 16);

  nthreads = eventer_loop_concurrency();
  mtevAssert(nthreads > 0);
  queues = calloc(sizeof(*queues),nthreads);
  search_asts = calloc(sizeof(*search_asts),nthreads);
  for(int i=0; i<nthreads; i++) {
    ck_spinlock_init(&search_asts[i].lock);
    ck_hs_init(&search_asts[i].hs, CK_HS_MODE_OBJECT | CK_HS_MODE_SPMC,
               search2_hash, search2_compare,
               &mtev_memory_safe_ck_malloc, 128, mtev_rand());
  }

  mtev_memory_begin();
  check_interests = mtev_memory_safe_calloc(sizeof(*check_interests),nthreads);
  mtev_memory_end();
  pthread_mutex_init(&check_interests_lock, NULL);
  pthread_mutex_init(&id_level_lock, NULL);
  /* subscribe to metric messages submitted via fq */
  if(mtev_fq_handle_message_hook_register_available())
    mtev_fq_handle_message_hook_register("metric-director", handle_fq_message, NULL);

  /* metrics can be injected into the metric director via the "metrics" log channel */
  mtev_log_line_hook_register("metric-director", handle_log_line, NULL);

  eventer_add_in_s_us(noit_metric_director_prune_dedup, NULL, 2, 0);
}

void noit_metric_director_init_globals(void) {
  mtev_hash_init_locks(&id_level, MTEV_HASH_DEFAULT_SIZE, MTEV_HASH_LOCK_MODE_MUTEX);
  mtev_hash_init_locks(&dedupe_hashes, MTEV_HASH_DEFAULT_SIZE, MTEV_HASH_LOCK_MODE_MUTEX);
  eventer_name_callback("noit_metric_director_prune_dedup",
                        noit_metric_director_prune_dedup);
}

void
noit_metric_director_drop_backlogged(uint32_t limit) {
  drop_backlog_over = limit;
}

void
noit_metric_director_drop_before(double t) {
  drop_before_threshold_ms = t * 1000;
}
