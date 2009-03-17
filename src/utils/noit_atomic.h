/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef UTILS_NOIT_ATOMIC_H
#define UTILS_NOIT_ATOMIC_H

#include "noit_config.h"

typedef int32_t noit_atomic32_t;
typedef int64_t noit_atomic64_t;

#ifdef HAVE_LIBKERN_OSATOMIC_H
/*
 * This secion is for Darwin.
 * And we simply don't run on 32bit PPC.  Life's a bitch.
 */
#include <libkern/OSAtomic.h>
typedef OSSpinLock noit_spinlock_t;
#define noit_atomic_cas32(ref,new,old) (OSAtomicCompareAndSwap32(old,new,ref) ? old : new)
#define noit_atomic_cas64(ref,new,old) (OSAtomicCompareAndSwap64(old,new,ref) ? old : new)
#define noit_atomic_add32(ref,diff) OSAtomicAdd32(ref,diff)
#define noit_atomic_add64(ref,diff) OSAtomicAdd64(ref,diff)
#define noit_atomic_sub32(ref,diff) OSAtomicAdd32(ref,0-(diff))
#define noit_atomic_sub64(ref,diff) OSAtomicAdd64(ref,0-(diff))
#define noit_atomic_inc32(ref) OSAtomicIncrement32(ref)
#define noit_atomic_inc64(ref) OSAtomicIncrement64(ref)
#define noit_atomic_dec32(ref) OSAtomicDecrement32(ref)
#define noit_atomic_dec64(ref) OSAtomicDecrement64(ref)
#define noit_spinlock_lock OSSpinLockLock
#define noit_spinlock_unlock OSSpinLockUnlock
#define noit_spinlock_trylock OSSpinLockTry
#elif defined(__GNUC__)

#if (SIZEOF_VOID_P == 4)
#define noit_atomic_casptr(a,b,c) noit_atomic_cas32((a),(void *)(b),(void *)(c))
#elif (SIZEOF_VOID_P == 8)
#define noit_atomic_casptr(a,b,c) noit_atomic_cas64((a),(void *)(b),(void *)(c))
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
    : "%ecx", "memory");
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
