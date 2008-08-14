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

#ifndef _UTILS_NOIT_SEM_H
#define _UTILS_NOIT_SEM_H

#ifdef BROKEN_SEM_INIT

#include <pthread.h>

typedef struct {
  unsigned int    value;
  pthread_mutex_t lock;
  pthread_cond_t  cond;
} noit_sem_t;

/*! \fn int noit_sem_init(noit_sem_t *s, int unused, int value)
    \brief initializes a counting semaphore for first time use.
    \param s the semaphore to be initialized
    \param unused is unused (keeps API combatibility with sem_init()
    \param value sets the initial value of the semaphore
    \return 0 on success or -1 on failure
 */
API_EXPORT(int)  noit_sem_init(noit_sem_t *, int, int);
/*! \fn int noit_sem_wait(noit_sem_t *s)
    \brief decrements the value of the semaphore waiting if required.
    \param s the semaphore on which to wait
    \return 0 on success or -1 on failure
 */
API_EXPORT(int)  noit_sem_wait(noit_sem_t *);
/*! \fn int noit_sem_post(noit_sem_t *s)
    \brief increments the value of the semaphore releasing any waiters.
    \param s the semaphore on which to wait
    \return 0 on success or -1 on failure
 */
API_EXPORT(void) noit_sem_post(noit_sem_t *);
/*! \fn int noit_sem_trywait(noit_sem_t *s)
    \brief decrements the value of the semaphore if greater than 0 or fails
    \param s the semaphore on which to wait
    \return 0 on success or -1 on failure
 */
API_EXPORT(int)  noit_sem_trywait(noit_sem_t *);
/*! \fn int noit_sem_getvalue(noit_sem_t *s, int *value)
    \brief retrieves the current value of a semaphore, placing it in *value
    \param s the semaphore on which to operate
    \param value a pointer an integer that will be populated with the current value of the semaphore
    \return 0 on success or -1 on failure
 */
API_EXPORT(int)  noit_sem_getvalue(noit_sem_t *, int *);
/*! \fn int noit_sem_destroy(noit_sem_t *s)
    \brief releases all resources related to a semaphore
    \param s the semaphore to destroy
    \return 0 on success or -1 on failure
 */
API_EXPORT(void) noit_sem_destroy(noit_sem_t *);

#define sem_t noit_sem_t
#define sem_init noit_sem_init
#define sem_wait noit_sem_wait
#define sem_trywait noit_sem_trywait
#define sem_post noit_sem_post
#define sem_getvalue noit_sem_getvalue
#define sem_destroy noit_sem_destroy

#else /* BROKEN_SEM_INIT */

#ifdef HAVE_SEMAPHORE_H
#include <semaphore.h>
#endif

#endif

#endif
