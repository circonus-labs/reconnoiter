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
#define noit_atomic_cas OSAtomicCompareAndSwap32
#define noit_atomic_cas64 OSAtomicCompareAndSwap64
#define noit_atomic_inc OSAtomicIncrement32
#define noit_atomic_inc64 OSAtomicIncrement64
#define noit_atomic_dec OSAtomicDecrement32
#define noit_atomic_dec64 OSAtomicDecrement64
#define noit_spinlock_lock OSSpinLockLock
#define noit_spinlock_unlock OSSpinLockUnlock
#define noit_spinlock_trylock OSSpinLockTry
#else
#error Please stub out the atomics section for your platform
#endif

#endif
