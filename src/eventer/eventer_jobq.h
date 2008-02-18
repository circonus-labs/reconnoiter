/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _NOIT_JOBQUEUE_H
#define _NOIT_JOBQUEUE_H

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "utils/noit_atomic.h"
#include "utils/noit_sem.h"

#include <pthread.h>
#include <setjmp.h>

/*
 * This is for jobs that would block and need more forceful timeouts.
 */

typedef struct _eventer_job_t {
  pthread_mutex_t         lock;
  struct timeval          create_time;
  struct timeval          start_time;
  struct timeval          finish_time;
  pthread_t               executor;
  eventer_t               timeout_event;
  eventer_t               fd_event;
  int                     timeout_triggered; /* set, if it expires in-flight */
  noit_atomic32_t         inflight;
  noit_atomic32_t         has_cleanedup;
  void                  (*cleanup)(struct _eventer_job_t *);
  struct _eventer_job_t  *next;
} eventer_job_t;

typedef struct _eventer_jobq_t {
  pthread_mutex_t         lock;
  sem_t                   semaphore;
  noit_atomic32_t         concurrency;
  eventer_job_t          *headq;
  eventer_job_t          *tailq;
  pthread_key_t           threadenv;
  pthread_key_t           activejob;
  struct _eventer_jobq_t *backq;
} eventer_jobq_t;

int eventer_jobq_init(eventer_jobq_t *jobq);
void eventer_jobq_enqueue(eventer_jobq_t *jobq, eventer_job_t *job);
eventer_job_t *eventer_jobq_dequeue(eventer_jobq_t *jobq);
eventer_job_t *eventer_jobq_dequeue_nowait(eventer_jobq_t *jobq);
void eventer_jobq_destroy(eventer_jobq_t *jobq);
int eventer_jobq_execute_timeout(eventer_t e, int mask, void *closure,
                                 struct timeval *now);
int eventer_jobq_consume_available(eventer_t e, int mask, void *closure,
                                   struct timeval *now);
void eventer_jobq_increase_concurrency(eventer_jobq_t *jobq);
void eventer_jobq_decrease_concurrency(eventer_jobq_t *jobq);
void *eventer_jobq_consumer(eventer_jobq_t *jobq);

#endif
