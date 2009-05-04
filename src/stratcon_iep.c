/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "utils/noit_b64.h"
#include "stratcon_datastore.h"
#include "noit_conf.h"
#include "noit_check.h"

#define SWEEP_DELAY { 0L, 10000L } /* 10ms */
eventer_jobq_t iep_jobq;

struct noit_line_list {
  char *line;
  struct noit_line_list *next;
};
struct iep_batch {
  /* This lock only needs to be used for inserting... (in the pivot)
   * Once the batch is "done" it is submitted to a thread and has
   * no more concurrent access.
   */
  pthread_mutex_t lock;
  int batch_size;
  struct noit_line_list *head;
  struct noit_line_list *tail;
};
/* We safely insert into the pivot... then lock and flip the batch
 * into a self-contained iep_batch which is the closure to the asynch
 * function that inserts it into OpenESB.
 */
static struct iep_batch pivot_batch;

static int
stratcon_iep_batch_add_line(const char *data) {
  int previous_size;
  struct noit_line_list *nnode;
  nnode = malloc(sizeof(*nnode));
  nnode->line = strdup(data);
  nnode->next = NULL;
  pthread_mutex_lock(&pivot_batch.lock);
  if(!pivot_batch.tail) pivot_batch.head = pivot_batch.tail = nnode;
  else {
    pivot_batch.tail->next = nnode;
    pivot_batch.tail = nnode;
  }
  previous_size = pivot_batch.batch_size;
  pivot_batch.batch_size++;
  pthread_mutex_unlock(&pivot_batch.lock);
  return previous_size;
}

static struct iep_batch *
stratcon_iep_batch_copytrunc() {
  struct iep_batch *nbatch;
  nbatch = calloc(1, sizeof(*nbatch));
  /* Lock */
  pthread_mutex_lock(&pivot_batch.lock);
  /* Copy */
  nbatch->batch_size = pivot_batch.batch_size;
  nbatch->head = pivot_batch.head;
  nbatch->tail = pivot_batch.tail;
  /* Trunc */
  pivot_batch.batch_size = 0;
  pivot_batch.head = pivot_batch.tail = NULL;
  /* Lock */
  pthread_mutex_unlock(&pivot_batch.lock);
  return nbatch;
}

static int
stratcon_iep_submitter(eventer_t e, int mask, void *closure,
                       struct timeval *now) {
  struct iep_batch *batch = closure;
  /* We only play when it is an asynch event */
  if(!(mask & EVENTER_ASYNCH_WORK)) return 0;

  if(mask & EVENTER_ASYNCH_CLEANUP) {
    /* free all the memory associated with the batch */
    while(batch->head) {
      struct noit_line_list *l;
      l = batch->head;
      batch->head = l->next;
      free(l->line);
      free(l);
    }
    free(batch);
    return 0;
  }

  /* pull from batch and submit */
  noitL(noit_error, "Firing stratcon_iep_submitter on a batch of %d events\n",
        batch->batch_size);

  return 0;
}

static int
stratcon_iep_batch_sweep(eventer_t e, int mask, void *closure,
                         struct timeval *now) {
  struct iep_batch *nbatch;
  struct timeval iep_timeout = { 5L, 0L };
  eventer_t newe;

  nbatch = stratcon_iep_batch_copytrunc();
  if(nbatch->batch_size == 0) {
    /* misfire */
    free(nbatch);
    return 0;
  }

  newe = eventer_alloc();
  newe->mask = EVENTER_ASYNCH;
  add_timeval(*now, iep_timeout, &newe->whence);
  newe->callback = stratcon_iep_submitter;
  newe->closure = nbatch;

  eventer_add(newe);
  return 0;
}

static void
stratcon_iep_datastore_onlooker(stratcon_datastore_op_t op,
                                struct sockaddr *remote, void *operand) {
  /* We only care about inserts */
  if(op != DS_OP_INSERT) return;

  /* process operand and push onto queue */
  if(stratcon_iep_batch_add_line((char *)operand) == 0) {
    /* If this is the first in the queue, then we need to schedule a
     * sweeper to submit the queue.
     */
    eventer_t e;
    struct timeval __now, sweep_delay = SWEEP_DELAY;

    gettimeofday(&__now, NULL);

    e = eventer_alloc();
    e->callback = stratcon_iep_batch_sweep;
    add_timeval(__now, sweep_delay, &e->whence);
    e->closure = NULL; /* we can only do one thing */
    e->mask = EVENTER_TIMER;
    eventer_add(e);
  }
}

void
stratcon_iep_init() {
  eventer_name_callback("stratcon_iep_batch_sweep", stratcon_iep_batch_sweep);
  eventer_name_callback("stratcon_iep_submitter", stratcon_iep_submitter);

  /* start up a thread pool of one */
  memset(&iep_jobq, 0, sizeof(iep_jobq));
  eventer_jobq_init(&iep_jobq, "iep_submitter");
  iep_jobq.backq = eventer_default_backq();
  eventer_jobq_increase_concurrency(&iep_jobq);

  /* Setup our pivot batch */
  memset(&pivot_batch, 0, sizeof(pivot_batch));
  pthread_mutex_init(&pivot_batch.lock, NULL);

  /* Add our onlooker */
  stratcon_datastore_register_onlooker(stratcon_iep_datastore_onlooker);
}

