/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
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
 *     * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
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

#include "noit_defines.h"
#include "utils/noit_log.h"
#include "utils/noit_atomic.h"
#include "eventer/eventer.h"
#include <errno.h>
#include <setjmp.h>
#include <assert.h>
#include <signal.h>

#ifndef JOBQ_SIGNAL
#define JOBQ_SIGNAL SIGALRM
#endif

static noit_atomic32_t threads_jobq_inited = 0;
static pthread_key_t threads_jobq;
static sigset_t alarm_mask;
static noit_hash_table all_queues = NOIT_HASH_EMPTY;
pthread_mutex_t all_queues_lock;

static void
eventer_jobq_handler(int signo)
{
  eventer_jobq_t *jobq;
  eventer_job_t *job;
  sigjmp_buf *env;

  jobq = pthread_getspecific(threads_jobq);
  assert(jobq);
  env = pthread_getspecific(jobq->threadenv);
  job = pthread_getspecific(jobq->activejob);
  if(env && job)
    if(noit_atomic_cas32(&job->inflight, 0, 1) == 1)
       siglongjmp(*env, 1);
}

int
eventer_jobq_init(eventer_jobq_t *jobq, const char *queue_name) {
  pthread_mutexattr_t mutexattr;

  if(noit_atomic_cas32(&threads_jobq_inited, 1, 0) == 0) {
    struct sigaction act;

    sigemptyset(&alarm_mask);
    sigaddset(&alarm_mask, JOBQ_SIGNAL);
    act.sa_handler = eventer_jobq_handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);

    if(sigaction(JOBQ_SIGNAL, &act, NULL) < 0) {
      noitL(noit_error, "Cannot initialize signal handler: %s\n",
            strerror(errno));
      return -1;
    }

    if(pthread_key_create(&threads_jobq, NULL)) {
      noitL(noit_error, "Cannot initialize thread-specific jobq: %s\n",
            strerror(errno));
      return -1;
    }
    if(pthread_mutex_init(&all_queues_lock, NULL)) {
      noitL(noit_error, "Cannot initialize all_queues mutex: %s\n",
            strerror(errno));
      return -1;
    }
  }

  memset(jobq, 0, sizeof(*jobq));
  jobq->queue_name = strdup(queue_name);
  if(pthread_mutexattr_init(&mutexattr) != 0) {
    noitL(noit_error, "Cannot initialize lock attributes\n");
    return -1;
  }
  if(pthread_mutex_init(&jobq->lock, &mutexattr) != 0) {
    noitL(noit_error, "Cannot initialize lock\n");
    return -1;
  }
  if(sem_init(&jobq->semaphore, 0, 0) != 0) {
    noitL(noit_error, "Cannot initialize semaphore: %s\n",
          strerror(errno));
    return -1;
  }
  if(pthread_key_create(&jobq->activejob, NULL)) {
    noitL(noit_error, "Cannot initialize thread-specific activejob: %s\n",
          strerror(errno));
    return -1;
  }
  if(pthread_key_create(&jobq->threadenv, NULL)) {
    noitL(noit_error, "Cannot initialize thread-specific sigsetjmp env: %s\n",
          strerror(errno));
    return -1;
  }
  pthread_mutex_lock(&all_queues_lock);
  if(noit_hash_store(&all_queues, jobq->queue_name, strlen(jobq->queue_name),
                     jobq) == 0) {
    noitL(noit_error, "Duplicate queue name!\n");
    pthread_mutex_unlock(&all_queues_lock);
    return -1;
  }
  pthread_mutex_unlock(&all_queues_lock);
  return 0;
}

eventer_jobq_t *
eventer_jobq_retrieve(const char *name) {
  void *vjq = NULL;
  pthread_mutex_lock(&all_queues_lock);
  noit_hash_retrieve(&all_queues, name, strlen(name), &vjq);
  pthread_mutex_unlock(&all_queues_lock);
  return vjq;
}

void
eventer_jobq_enqueue(eventer_jobq_t *jobq, eventer_job_t *job) {
  job->next = NULL;

  pthread_mutex_lock(&jobq->lock);
  if(jobq->tailq) {
    /* If there is a tail (queue has items), just push it on the end. */
    jobq->tailq->next = job;
    jobq->tailq = job;
  }
  else {
    /* Otherwise, this is the first and only item on the list. */
    jobq->headq = jobq->tailq = job;
  }
  pthread_mutex_unlock(&jobq->lock);

  /* Signal consumers */
  sem_post(&jobq->semaphore);
}

static eventer_job_t *
__eventer_jobq_dequeue(eventer_jobq_t *jobq, int should_wait) {
  eventer_job_t *job = NULL;

  /* Wait for a job */
  if(should_wait) while(sem_wait(&jobq->semaphore) && errno == EINTR);
  /* Or Try-wait for a job */
  else if(sem_trywait(&jobq->semaphore)) return NULL;

  pthread_mutex_lock(&jobq->lock);
  if(jobq->headq) {
    /* If there are items, pop and advance the header pointer */
    job = jobq->headq;
    jobq->headq = jobq->headq->next;
    if(!jobq->headq) jobq->tailq = NULL;
  }
  pthread_mutex_unlock(&jobq->lock);

  if(job) job->next = NULL; /* To reduce any confusion */
  return job;
}

eventer_job_t *
eventer_jobq_dequeue(eventer_jobq_t *jobq) {
  return __eventer_jobq_dequeue(jobq, 1);
}

eventer_job_t *
eventer_jobq_dequeue_nowait(eventer_jobq_t *jobq) {
  return __eventer_jobq_dequeue(jobq, 0);
}

void
eventer_jobq_destroy(eventer_jobq_t *jobq) {
  pthread_mutex_destroy(&jobq->lock);
  sem_destroy(&jobq->semaphore);
}
int
eventer_jobq_execute_timeout(eventer_t e, int mask, void *closure,
                             struct timeval *now) {
  eventer_job_t *job = closure;
  job->timeout_triggered = 1;
  job->timeout_event = NULL;
  noitL(eventer_deb, "%p jobq -> timeout job [%p]\n", pthread_self(), job);
  if(job->inflight) pthread_kill(job->executor, JOBQ_SIGNAL);
  return 0;
}
int
eventer_jobq_consume_available(eventer_t e, int mask, void *closure,
                               struct timeval *now) {
  eventer_jobq_t *jobq = closure;
  eventer_job_t *job;
  /* This can only be called with a backq jobq
   * (a standalone queue with no backq itself)
   */
  assert(jobq && !jobq->backq);
  while((job = eventer_jobq_dequeue_nowait(jobq)) != NULL) {
    int newmask;
    newmask = job->fd_event->callback(job->fd_event, job->fd_event->mask,
                                      job->fd_event->closure, now);
    if(!newmask) eventer_free(job->fd_event);
    else {
      job->fd_event->mask = newmask;
      eventer_add(job->fd_event);
    }
    job->fd_event = NULL;
    assert(job->timeout_event == NULL);
    free(job);
  }
  return EVENTER_RECURRENT;
}
static void *
eventer_jobq_consumer_pthreadentry(void *vp) {
  return eventer_jobq_consumer((eventer_jobq_t *)vp);
}
void *
eventer_jobq_consumer(eventer_jobq_t *jobq) {
  eventer_job_t *job;
  sigjmp_buf env;

  assert(jobq->backq);
  noit_atomic_inc32(&jobq->concurrency);
  /* Each thread can consume from only one queue */
  pthread_setspecific(threads_jobq, jobq);
  pthread_setspecific(jobq->threadenv, &env);

  while(1) {
    pthread_setspecific(jobq->activejob, NULL);
    job = eventer_jobq_dequeue(jobq);
    if(!job) continue;
    if(!job->fd_event) {
      free(job);
      break;
    }
    pthread_setspecific(jobq->activejob, job);
    noitL(eventer_deb, "%p jobq[%s] -> running job [%p]\n", pthread_self(), jobq->queue_name, job);

    /* Mark our commencement */
    gettimeofday(&job->start_time, NULL);

    /* Safely check and handle if we've timed out while in queue */
    pthread_mutex_lock(&job->lock);
    if(job->timeout_triggered) {
      struct timeval diff, diff2;
      /* This happens if the timeout occurred before we even had the change
       * to pull the job off the queue.  We must be in bad shape here.
       */
      noitL(eventer_deb, "%p jobq[%s] -> timeout before start [%p]\n", pthread_self(), jobq->queue_name, job);
      gettimeofday(&job->finish_time, NULL); /* We're done */
      sub_timeval(job->finish_time, job->fd_event->whence, &diff);
      sub_timeval(job->finish_time, job->create_time, &diff2);
      noitL(eventer_deb, "%p jobq[%s] -> timeout before start [%p] -%0.6f (%0.6f)\n",
            pthread_self(), jobq->queue_name, job,
            (float)diff.tv_sec + (float)diff.tv_usec/1000000.0,
            (float)diff2.tv_sec + (float)diff2.tv_usec/1000000.0);
      pthread_mutex_unlock(&job->lock);
      job->fd_event->callback(job->fd_event, EVENTER_ASYNCH_CLEANUP,
                              job->fd_event->closure, &job->finish_time);
      eventer_jobq_enqueue(jobq->backq, job);
      continue;
    }
    pthread_mutex_unlock(&job->lock);
    
    /* Run the job, if we timeout, will be killed with a JOBQ_SIGNAL from
     * the master thread.  We handle the alarm by longjmp'd out back here.
     */
    job->executor = pthread_self();
    if(sigsetjmp(env, 1) == 0) {
      /* We could get hit right here... (timeout and terminated from
       * another thread.  inflight isn't yet set (next line), so it
       * won't longjmp.  But timeout_triggered will be set... so we
       * should recheck that after we mark ourselves inflight.
       */
      if(noit_atomic_cas32(&job->inflight, 1, 0) == 0) {
        if(!job->timeout_triggered) {
          noitL(eventer_deb, "%p jobq[%s] -> executing [%p]\n", pthread_self(), jobq->queue_name, job);
          job->fd_event->callback(job->fd_event, EVENTER_ASYNCH_WORK,
                                  job->fd_event->closure, &job->start_time);
        }
      }
    }

    job->inflight = 0;
    noitL(eventer_deb, "%p jobq[%s] -> finished [%p]\n", pthread_self(), jobq->queue_name, job);
    /* No we know we won't have siglongjmp called on us */

    gettimeofday(&job->finish_time, NULL);
    if(job->timeout_event &&
       eventer_remove(job->timeout_event)) {
      eventer_free(job->timeout_event);
    }
    job->timeout_event = NULL;

    if(noit_atomic_cas32(&job->has_cleanedup, 1, 0) == 0) {
      /* We need to cleanup... we haven't done it yet. */
      noitL(eventer_deb, "%p jobq[%s] -> cleanup [%p]\n", pthread_self(), jobq->queue_name, job);
      job->fd_event->callback(job->fd_event, EVENTER_ASYNCH_CLEANUP,
                              job->fd_event->closure, &job->finish_time);
    }
    eventer_jobq_enqueue(jobq->backq, job);
  }
  noit_atomic_dec32(&jobq->concurrency);
  pthread_exit(NULL);
  return NULL;
}

void eventer_jobq_increase_concurrency(eventer_jobq_t *jobq) {
  pthread_t tid;
  pthread_create(&tid, NULL, eventer_jobq_consumer_pthreadentry, jobq);
}
void eventer_jobq_decrease_concurrency(eventer_jobq_t *jobq) {
  eventer_job_t *job;
  job = calloc(1, sizeof(*job));
  eventer_jobq_enqueue(jobq, job);
}
void eventer_jobq_process_each(void (*func)(eventer_jobq_t *, void *),
                               void *closure) {
  const char *key;
  int klen;
  void *vjobq;
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;

  pthread_mutex_lock(&all_queues_lock);
  while(noit_hash_next(&all_queues, &iter, &key, &klen, &vjobq)) {
    func((eventer_jobq_t *)vjobq, closure);
  }
  pthread_mutex_unlock(&all_queues_lock);
}
