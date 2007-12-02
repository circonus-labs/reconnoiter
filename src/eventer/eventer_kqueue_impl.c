/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include "eventer/eventer.h"

#include <sys/event.h>

static int eventer_kqueue_impl_init() {
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
