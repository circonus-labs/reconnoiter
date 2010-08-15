/*
 * Copyright (c) 2005-2009, OmniTI Computer Consulting, Inc.
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

#ifndef UTILS_NOIT_ATOMIC_H
#define UTILS_NOIT_ATOMIC_H

#include "noit_config.h"

typedef int32_t noit_atomic32_t;
typedef int64_t noit_atomic64_t;

#if defined(__GNUC__)

#if (SIZEOF_VOID_P == 4)
#define noit_atomic_casptr(a,b,c) ((void *)noit_atomic_cas32((vpsized_int *)(a),(vpsized_int)(void *)(b),(vpsized_int)(void *)(c)))
#elif (SIZEOF_VOID_P == 8)
#define noit_atomic_casptr(a,b,c) ((void *)noit_atomic_cas64((vpsized_int *)(a),(vpsized_int)(void *)(b),(vpsized_int)(void *)(c)))
#else
#error unsupported pointer width
#endif


typedef noit_atomic32_t noit_spinlock_t;

static inline noit_atomic32_t
noit_atomic_cas32(volatile noit_atomic32_t *ptr,
                  volatile noit_atomic32_t rpl,
                  volatile noit_atomic32_t curr) {
  noit_atomic32_t prev;
  __asm__ volatile (
      "lock; cmpxchgl %1, %2"
    : "=a" (prev)
    : "r"  (rpl), "m" (*(ptr)), "0" (curr)
    : "memory");
  return prev;
}

#ifdef __x86_64__
static inline noit_atomic64_t
noit_atomic_cas64(volatile noit_atomic64_t *ptr,
                  volatile noit_atomic64_t rpl,
                  volatile noit_atomic64_t curr) {
  noit_atomic64_t prev;
  __asm__ volatile (
      "lock; cmpxchgq %1, %2"
    : "=a" (prev)
    : "r"  (rpl), "m" (*(ptr)), "0" (curr)
    : "memory");
  return prev;
}
#else
static inline noit_atomic64_t
noit_atomic_cas64(volatile noit_atomic64_t *ptr,
                  volatile noit_atomic64_t rpl,
                  volatile noit_atomic64_t curr) {
  noit_atomic64_t prev;
  __asm__ volatile (
      "pushl %%ebx;"
      "mov 4+%1,%%ecx;"
      "mov %1,%%ebx;"
      "lock;"
      "cmpxchg8b (%3);"
      "popl %%ebx"
    : "=A" (prev)
    : "m" (rpl), "A" (curr), "r" (ptr)
    : "%ecx", "memory", "cc");
  return prev;
};
#endif

static inline void noit_spinlock_lock(volatile noit_spinlock_t *lock) {
  while(noit_atomic_cas32(lock, 1, 0) != 0);
}
static inline void noit_spinlock_unlock(volatile noit_spinlock_t *lock) {
  while(noit_atomic_cas32(lock, 0, 1) != 1);
}
static inline int noit_spinlock_trylock(volatile noit_spinlock_t *lock) {
  return (noit_atomic_cas32(lock, 1, 0) == 0);
}

#elif (defined(__amd64) || defined(__i386)) && (defined(__SUNPRO_C) || defined(__SUNPRO_CC))

typedef noit_atomic32_t noit_spinlock_t;

extern noit_atomic32_t noit_atomic_cas32(volatile noit_atomic32_t *mem,
        volatile noit_atomic32_t newval, volatile noit_atomic32_t cmpval);
extern noit_atomic64_t noit_atomic_cas64(volatile noit_atomic64_t *mem,
        volatile noit_atomic64_t newval, volatile noit_atomic64_t cmpval);
extern void *noit_atomic_casptr(volatile void **mem,
        volatile void *newval, volatile void *cmpval);

static inline void noit_spinlock_lock(volatile noit_spinlock_t *lock) {
  while(noit_atomic_cas32(lock, 1, 0) != 0);
}
static inline void noit_spinlock_unlock(volatile noit_spinlock_t *lock) {
  while(noit_atomic_cas32(lock, 0, 1) != 1);
}
static inline int noit_spinlock_trylock(volatile noit_spinlock_t *lock) {
  return (noit_atomic_cas32(lock, 1, 0) == 0);
}

#else
#error Please stub out the atomics section for your platform
#endif

#ifndef noit_atomic_add32
static inline noit_atomic32_t noit_atomic_add32(volatile noit_atomic32_t *loc,
                                                volatile noit_atomic32_t diff) {
  register noit_atomic32_t current;
  do {
    current = *(loc);
  } while(noit_atomic_cas32(loc, current + diff, current) != current);
  return current + diff;
}
#endif

#ifndef noit_atomic_add64
static inline noit_atomic64_t noit_atomic_add64(volatile noit_atomic64_t *loc,
                                                volatile noit_atomic64_t diff) {
  register noit_atomic64_t current;
  do {
    current = *(loc);
  } while(noit_atomic_cas64(loc, current + diff, current) != current);
  return current + diff;
}
#endif

#ifndef noit_atomic_sub32
static inline noit_atomic32_t noit_atomic_sub32(volatile noit_atomic32_t *loc,
                                                volatile noit_atomic32_t diff) {
  register noit_atomic32_t current;
  do {
    current = *(loc);
  } while(noit_atomic_cas32(loc, current - diff, current) != current);
  return current - diff;
}
#endif

#ifndef noit_atomic_sub64
static inline noit_atomic64_t noit_atomic_sub64(volatile noit_atomic64_t *loc,
                                                volatile noit_atomic64_t diff) {
  register noit_atomic64_t current;
  do {
    current = *(loc);
  } while(noit_atomic_cas64(loc, current - diff, current) != current);
  return current - diff;
}
#endif

#ifndef noit_atomic_inc32
#define noit_atomic_inc32(a) noit_atomic_add32(a, 1)
#endif

#ifndef noit_atomic_inc64
#define noit_atomic_inc64(a) noit_atomic_add64(a, 1)
#endif

#ifndef noit_atomic_dec32
#define noit_atomic_dec32(a) noit_atomic_add32(a, -1)
#endif

#ifndef noit_atomic_dec64
#define noit_atomic_dec64(a) noit_atomic_add64(a, -1)
#endif

#endif
