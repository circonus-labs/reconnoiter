/*
 * Copyright (c) 2005-2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *    * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *      of its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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

/* This implementation is based directly on:
 * http://www.cs.wustl.edu/~schmidt/win32-cv-1.html
 * Many thanks for his thorough explanation of the problem.
 */

#include "noit_defines.h"
#include "utils/noit_sem.h"

#ifdef BROKEN_SEM_INIT
#include <pthread.h>
#include <errno.h>

int
noit_sem_init(noit_sem_t *s, int unused, int value) {
  pthread_mutexattr_t mutexattr;
  if(pthread_mutexattr_init(&mutexattr) != 0) return -1;
  if(pthread_mutex_init(&s->lock, &mutexattr) != 0) return -1;
  if(pthread_cond_init(&s->cond, NULL) != 0) return -1;
  s->value = value;
  return 0;
}

void noit_sem_post(noit_sem_t *s) {
  pthread_mutex_lock(&s->lock);
  pthread_cond_signal(&s->cond);
  s->value++;
  pthread_mutex_unlock(&s->lock);
}

int
noit_sem_wait(noit_sem_t *s) {
  pthread_mutex_lock(&s->lock);
 reattempt:
  if(s->value > 0) {
    s->value--;
    pthread_mutex_unlock(&s->lock);
  } else {
    pthread_cond_wait(&s->cond, &s->lock);
    goto reattempt;
  }
  return 0;
}

int
noit_sem_trywait(noit_sem_t *s) {
  pthread_mutex_lock(&s->lock);
  if(s->value > 0) {
    s->value--;
    pthread_mutex_unlock(&s->lock);
    return 0;
  }
  pthread_mutex_unlock(&s->lock);
  errno = EAGAIN;
  return -1;
}

int
noit_sem_getvalue(noit_sem_t *s, int *value) {
  *value = s->value;
  return 0;
}

void
noit_sem_destroy(noit_sem_t *s)
{
  pthread_mutex_destroy(&s->lock);
  pthread_cond_destroy(&s->cond);
}

#endif
