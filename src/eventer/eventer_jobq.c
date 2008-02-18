/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "utils/noit_log.h"
#include "utils/noit_atomic.h"
#include "eventer/eventer_jobq.h"
#include <errno.h>
#include <setjmp.h>
#include <assert.h>
#include <signal.h>

static noit_atomic32_t threads_jobq_inited = 0;
static pthread_key_t threads_jobq;
static sigset_t alarm_mask;

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
  if(env && job && job->inflight) siglongjmp(*env, 1);
}

int
eventer_jobq_init(eventer_jobq_t *jobq) {
  pthread_mutexattr_t mutexattr;

  if(noit_atomic_cas32(&threads_jobq_inited, 1, 0) == 0) {
    struct sigaction act;

    sigemptyset(&alarm_mask);
    sigaddset(&alarm_mask, SIGALRM);
    act.sa_handler = eventer_jobq_handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);

    if(sigaction(SIGALRM, &act, NULL) < 0) {
      noitL(noit_error, "Cannot initialize signal handler: %s\n",
            strerror(errno));
      return -1;
    }

    if(pthread_key_create(&threads_jobq, NULL)) {
      noitL(noit_error, "Cannot initialize thread-specific jmp environment: %s\n",
            strerror(errno));
      return -1;
    }
  }

  memset(jobq, 0, sizeof(*jobq));
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
  return 0;
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

  job->next = NULL; /* To reduce any confusion */
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
  if(job->inflight) pthread_kill(job->executor, SIGALRM);
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
    else eventer_add(job->fd_event);
    job->fd_event = NULL;
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

    /* Mark our commencement */
    gettimeofday(&job->start_time, NULL);

    /* Safely check and handle if we've timed out while in queue */
    pthread_mutex_lock(&job->lock);
    if(job->timeout_triggered) {
      /* This happens if the timeout occurred before we even had the change
       * to pull the job off the queue.  We must be in bad shape here.
       */
      gettimeofday(&job->finish_time, NULL); /* We're done */
      pthread_mutex_unlock(&job->lock);
      job->fd_event->callback(job->fd_event, EVENTER_ASYNCH_CLEANUP,
                              job->fd_event->closure, &job->finish_time);
      eventer_jobq_enqueue(jobq->backq, job);
    }
    pthread_mutex_unlock(&job->lock);

    /* Run the job, if we timeout, will be killed with an ALRM from the
     * master thread.  We handle the alarm by longjmp'd out back here.
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
          job->fd_event->callback(job->fd_event, EVENTER_ASYNCH_WORK,
                                  job->fd_event->closure, &job->start_time);
        }
      }
    }
    if(noit_atomic_cas32(&job->inflight, 0, 1) != 1) {
      /* We were alredy terminated?!  Wicked race.  That's fine, just means
       * that we longjmp'd here.
       */
      gettimeofday(&job->finish_time, NULL);
      if(eventer_remove(job->timeout_event)) {
        eventer_free(job->timeout_event);
        job->timeout_event = NULL;
      }
    }
    if(noit_atomic_cas32(&job->has_cleanedup, 1, 0) == 0) {
      /* We need to cleanup... we haven't done it yet. */
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

