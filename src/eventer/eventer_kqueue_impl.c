/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "eventer/eventer.h"
#include "utils/noit_atomic.h"

#include <sys/event.h>
#include <pthread.h>

int maxfds;
struct {
  eventer_t e;
  pthread_t executor;
  noit_spinlock_t lock;
} **master_fds;

int kqueue_fd;
typedef struct kqueue_setup {
  struct kevent *__ke_vec;
  unsigned int __ke_vec_a;
  unsigned int __ke_vec_used;
} * kqs_t;

static pthread_mutex_t kqs_lock;
static kqs_t master_kqs = NULL;
static pthread_key_t kqueue_setup_key;
#define KQUEUE_DECL kqs_t kqs
#define KQUEUE_SETUP kqs = (kqs_t) pthread_getspecific(kqueue_setup_key)
#define ke_vec kqs->__ke_vec
#define ke_vec_a kqs->__ke_vec_a
#define ke_vec_used kqs->__ke_vec_used

static void
ke_change (register int const ident,
           register int const filter,
           register int const flags,
           register void *const udata) {
  enum { initial_alloc = 64 };
  register struct kevent *kep;
  KQUEUE_DECL;

  KQUEUE_SETUP;
  if (!ke_vec_a) {
    ke_vec_a = initial_alloc;
    ke_vec = (struct kevent *) malloc(ke_vec_a * sizeof (struct kevent));
  }
  else if (ke_vec_used == ke_vec_a) {
    ke_vec_a <<= 1;
    ke_vec = (struct kevent *) realloc(ke_vec,
                                       ke_vec_a * sizeof (struct kevent));
  }
  kep = &ke_vec[ke_vec_used++];

  EV_SET(kep, ident, filter, flags, 0, 0, udata);
}

static int eventer_kqueue_impl_init() {
  struct rlimit rlim;
  kqueue_fd = kqueue();
  if(kqueue_fd == -1) {
    return -1;
  }
  pthread_mutex_init(&kqs_lock, NULL);
  master_kqs = calloc(1, sizeof(*master_kqs));
  getrlimit(RLIMIT_NOFILE, &rlim);
  maxfds = rlim.rlim_cur;
  master_fds = calloc(maxfds, sizeof(*master_fds));
  return 0;
}
static int eventer_kqueue_impl_propset(const char *key, const char *value) {
  return -1;
}
static void eventer_kqueue_impl_add(eventer_t e) {
}
static void eventer_kqueue_impl_remove(eventer_t e) {
}
static void eventer_kqueue_impl_update(eventer_t e) {
}
static eventer_t eventer_kqueue_impl_remove_fd(int fd) {
}
static void eventer_kqueue_impl_loop() {
}

struct _eventer_impl eventer_kqueue_impl = {
  "kqueue",
  eventer_kqueue_impl_init,
  eventer_kqueue_impl_propset,
  eventer_kqueue_impl_add,
  eventer_kqueue_impl_remove,
  eventer_kqueue_impl_update,
  eventer_kqueue_impl_remove_fd,
  eventer_kqueue_impl_loop
};
