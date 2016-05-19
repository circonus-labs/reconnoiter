/*
 * Copyright (c) 2016, Circonus, Inc. All rights reserved.
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
#include <mtev_str.h>
#include <ck_fifo.h>

#include <noit_metric_director.h>
#include <noit_check_log_helpers.h>

struct metric {
  uuid_t id;
  const char *metric_name;
  int metric_name_len;
};
#define NEXT(in,c) memchr(in,c,line_len-(in-line))
static bool extract_metric(const char *line, int line_len, struct metric *m, bool has_ip) {
  const char *p = line;
  char uuid_copy[UUID_STR_LEN+1];
  if(line[0] != 'M' && line[0] != 'H') return false;
  if(NULL == (p = NEXT(p+1,'\t'))) return false;
  if(has_ip && NULL == (p = NEXT(p+1,'\t'))) return false;
  if(NULL == (p = NEXT(p+1,'\t'))) return false;
  if(NULL == (p = NEXT(p+1,'\t'))) return false;
  if(p - UUID_STR_LEN < line) return false;
  memcpy(uuid_copy,p-UUID_STR_LEN,UUID_STR_LEN);
  uuid_copy[UUID_STR_LEN] = '\0';
  if(uuid_parse(uuid_copy,m->id)) return false;
  m->metric_name = ++p;
  p = NEXT(p,'\t');
  if(!p) p = line + line_len;
  m->metric_name_len = p - m->metric_name;
  return true;
}

static __thread struct {
  int id;
  ck_fifo_spsc_t *fifo;
} my_lane;

static int nthreads;
static volatile void **thread_queues;
static mtev_hash_table id_level;
typedef unsigned short caql_cnt_t;
static caql_cnt_t *check_interests;

static int
get_my_lane() {
  if(my_lane.fifo == NULL) {
    int new_thread;
    my_lane.fifo = calloc(1, sizeof(ck_fifo_spsc_t));
    ck_fifo_spsc_init(my_lane.fifo, malloc(sizeof(ck_fifo_spsc_entry_t)));
    for(new_thread=0;new_thread<nthreads;new_thread++) {
      if(mtev_atomic_casptr(&thread_queues[new_thread], my_lane.fifo, NULL) == NULL) break;
    }
    assert(new_thread<nthreads);
    my_lane.id = new_thread;
    mtevL(mtev_debug, "Assigning thread(%p) to %d\n", pthread_self(), my_lane.id);
  }
  return my_lane.id;
}

void
noit_adjust_checks_interest(short cnt) {
  int thread_id, icnt;

  thread_id = get_my_lane();

  icnt = check_interests[thread_id];
  icnt += cnt;
  if(icnt < 0) icnt = 0;
  assert(icnt <= 0xffff);
  check_interests[thread_id] = icnt;
}

void
noit_adjust_metric_interest(uuid_t id, const char *metric, short cnt) {
  int thread_id, icnt;
  void *vhash, *vinterests;
  mtev_hash_table *level2;
  caql_cnt_t *interests;

  thread_id = get_my_lane();

  /* Get us our UUID->HASH(METRICS) mapping */
  if(!mtev_hash_retrieve(&id_level, (const char *)id, UUID_SIZE, &vhash)) {
    int found;
    uuid_t *copy = malloc(UUID_SIZE);
    level2 = calloc(1,sizeof(*level2));
    uuid_copy(*copy, id);
    if(!mtev_hash_store(&id_level, (const char *)copy, UUID_SIZE, level2)) {
      free(copy);
      free(level2);
    }
    found = mtev_hash_retrieve(&id_level, (const char *)id, UUID_SIZE, &vhash);
    assert(found);
  }
  level2 = vhash;

  if(!mtev_hash_retrieve(level2, metric, strlen(metric), &vinterests)) {
    int found;
    char *metric_copy;
    metric_copy = strdup(metric);
    vinterests = calloc(nthreads, sizeof(*interests));
    if(!mtev_hash_store(level2, metric_copy, strlen(metric_copy), vinterests)) {
      free(metric_copy);
      free(vinterests);
    }
    found = mtev_hash_retrieve(level2, metric, strlen(metric), &vinterests);
    assert(found);
  }
  interests = vinterests;
  /* This is fine because thread_id is only ours */
  icnt = interests[thread_id];
  icnt += cnt;
  if(icnt < 0) icnt = 0;
  assert(icnt <= 0xffff);
  interests[thread_id] = icnt;
}

static void
distribute_line(caql_cnt_t *interests,
                const char *line, int line_len) {
  int i;
  for(i=0;i<nthreads;i++) {
    if(interests[i] > 0) {
      char *copy = mtev__strndup(line, line_len);
      ck_fifo_spsc_t *fifo = (ck_fifo_spsc_t *)thread_queues[i];
      ck_fifo_spsc_entry_t *fifo_entry;
      fifo_entry = ck_fifo_spsc_recycle(fifo);
      if(!fifo_entry) fifo_entry = malloc(sizeof(ck_fifo_spsc_entry_t));
      ck_fifo_spsc_enqueue_lock(fifo);
      ck_fifo_spsc_enqueue(fifo, fifo_entry, copy);
      ck_fifo_spsc_enqueue_unlock(fifo);
    }
  }
}

static void
distribute_metric(struct metric *metric, 
                  const char *line, int line_len) {
  void *vhash, *vinterests;
  char uuid_str[UUID_STR_LEN+1];
  uuid_unparse_lower(metric->id, uuid_str);
  if(mtev_hash_retrieve(&id_level, (const char *)&metric->id, UUID_SIZE, &vhash)) {
    if(mtev_hash_retrieve((mtev_hash_table *)vhash,
                          metric->metric_name, metric->metric_name_len,
                          &vinterests)) {
      caql_cnt_t *interests = vinterests;
      distribute_line(interests, line, line_len);
    }
  }
}

static void
distribute_check(const char *line, int line_len) {
  distribute_line(check_interests, line, line_len);
}

char *noit_metric_director_lane_next() {
  char *line = NULL;
  if(my_lane.fifo == NULL) return NULL;
  ck_fifo_spsc_dequeue_lock(my_lane.fifo);
  if(ck_fifo_spsc_dequeue(my_lane.fifo, &line) == false) {
    line = NULL;
  }
  ck_fifo_spsc_dequeue_unlock(my_lane.fifo);
  return line;
}
static void
handle_metric_buffer(const char *payload, int payload_len, int has_noit) {
  switch(payload[0]) {
    case 'C':
    case 'S':
      distribute_check(payload, payload_len);
      break;
    case 'H':
    case 'M':
      {
        struct metric m;
        if(extract_metric((const char *)payload, payload_len, &m, has_noit)) {
          distribute_metric(&m, (const char *)payload, payload_len);
        }
      }
      break;
    case 'B':
      {
        int n_metrics, i;
        char **metrics = NULL;
        n_metrics = noit_check_log_b_to_sm((const char *)payload, payload_len, &metrics, has_noit);
        for(i=0;i<n_metrics;i++) {
          handle_metric_buffer(metrics[i], strlen(metrics[i]), false);
          free(metrics[i]);
        }
        free(metrics);
      }
      break;
    default: ;
      /* ignored */
  }
}
static mtev_hook_return_t
handle_fq_message(void *closure, fq_client client, int idx, fq_msg *m) {
  handle_metric_buffer((const char *)m->payload, m->payload_len, 1);
  return MTEV_HOOK_CONTINUE;
}
static mtev_hook_return_t
handle_log_line(void *closure, mtev_log_stream_t ls, struct timeval *whence,
                const char *timebuf, int timebuflen,
                const char *debugbuf, int debugbuflen,
                const char *line, size_t len) {
  if(!ls) return MTEV_HOOK_CONTINUE;
  const char *name = mtev_log_stream_get_name(ls);
  if(!name ||
     (strcmp(name,"metrics") && strcmp(name,"bundle") &&
      strcmp(name,"check") && strcmp(name,"status")))
    return MTEV_HOOK_CONTINUE;
  handle_metric_buffer(line, len, 0);
  return MTEV_HOOK_CONTINUE;
}

void noit_metric_director_init() {
  nthreads = eventer_loop_concurrency();
  assert(nthreads > 0);
  thread_queues = calloc(sizeof(*thread_queues),nthreads);
  check_interests = calloc(sizeof(*check_interests),nthreads);
  if(mtev_fq_handle_message_hook_register_available())
    mtev_fq_handle_message_hook_register("metric-director", handle_fq_message, NULL);
  mtev_log_line_hook_register("metric-director", handle_log_line, NULL);
}
