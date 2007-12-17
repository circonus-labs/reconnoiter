/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef UTILS_NOIT_ATOMIC_H
#define UTILS_NOIT_ATOMIC_H

#include "noit_config.h"

typedef int32_t noit_atomic_t;
typedef int64_t noit_atomic64_t;

#ifdef HAVE_LIBKERN_OSATOMIC_H
/*
 * This secion is for Darwin.
 * And we simply don't run on 32bit PPC.  Life's a bitch.
 */
#include <libkern/OSAtomic.h>
typedef OSSpinLock noit_spinlock_t;
#define noit_atomic_cas32 OSAtomicCompareAndSwap32
#define noit_atomic_cas64 OSAtomicCompareAndSwap64
#define noit_atomic_inc32 OSAtomicIncrement32
#define noit_atomic_inc64 OSAtomicIncrement64
#define noit_atomic_dec32 OSAtomicDecrement32
#define noit_atomic_dec64 OSAtomicDecrement64
#define noit_spinlock_lock OSSpinLockLock
#define noit_spinlock_unlock OSSpinLockUnlock
#define noit_spinlock_trylock OSSpinLockTry
#elif defined(__GNUC__)

typedef u_int32_t noit_atomic32_t
typedef u_int64_t noit_atomic64_t

#if (SIZEOF_VOID_P == 4)
#define noit_atomic_casptr(a,b,c) noit_atomic_cas32((a),(void *)(b),(void *)(c))
#elif (SIZEOF_VOID_P == 8)
#define noit_atomic_casptr(a,b,c) noit_atomic_cas64((a),(void *)(b),(void *)(c))
#else
#error unsupported pointer width
#endif

static inline noit_atomic32_t
noit_atomic_cas32(volatile noit_atomic32_t *ptr
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
noit_atomic_cas64(volatile noit_atomic64_t *ptr
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
noit_atomic_cas64(volatile noit_atomic64_t *ptr
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
    : "%ebx", "%ecx", "memory");
  return prev;
};
#endif

#else
#error Please stub out the atomics section for your platform
#endif

#endif
