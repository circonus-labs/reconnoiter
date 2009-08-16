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

#define _EVENTER_C_
#include "eventer/eventer.h"
#include "utils/noit_hash.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

eventer_t eventer_alloc() {
  eventer_t e;
  e = calloc(1, sizeof(*e));
  e->opset = eventer_POSIX_fd_opset;
  return e;
}

int eventer_timecompare(const void *av, const void *bv) {
  /* Herein we avoid equality.  This function is only used as a comparator
   * for a heap of timed events.  If they are equal, b is considered less
   * just to maintain an order (despite it not being stable).
   */
  const eventer_t a = (eventer_t)av;
  const eventer_t b = (eventer_t)bv;
  if(a->whence.tv_sec < b->whence.tv_sec) return -1;
  if(a->whence.tv_sec == b->whence.tv_sec &&
     a->whence.tv_usec < b->whence.tv_usec) return -1;
  return 1;
}

void eventer_free(eventer_t e) {
  free(e);
}

int eventer_set_fd_nonblocking(int fd) {
  int flags;
  if(((flags = fcntl(fd, F_GETFL, 0)) == -1) ||
     (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1))
    return -1;
  return 0;
}
int eventer_set_fd_blocking(int fd) {
  int flags;
  if(((flags = fcntl(fd, F_GETFL, 0)) == -1) ||
     (fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1))
    return -1;
  return 0;
}

static noit_hash_table __name_to_func = NOIT_HASH_EMPTY;
static noit_hash_table __func_to_name = NOIT_HASH_EMPTY;
int eventer_name_callback(const char *name, eventer_func_t f) {
  void **fptr = malloc(sizeof(*fptr));
  *fptr = (void *)f;
  noit_hash_replace(&__name_to_func, strdup(name), strlen(name), (void *)f, free, NULL);
  noit_hash_replace(&__func_to_name, (char *)fptr, sizeof(*fptr), strdup(name),
                    free, free);
  return 0;
}
eventer_func_t eventer_callback_for_name(const char *name) {
  void *vf;
  if(noit_hash_retrieve(&__name_to_func, name, strlen(name), &vf))
    return (eventer_func_t)vf;
  return (eventer_func_t)NULL;
}
const char *eventer_name_for_callback(eventer_func_t f) {
  const char *name;
  if(noit_hash_retr_str(&__func_to_name, (char *)&f, sizeof(f), &name))
    return name;
  return NULL;
}

int eventer_choose(const char *name) {
  int i = 0;
  eventer_impl_t choice;
  for(choice = registered_eventers[i];
      choice;
      choice = registered_eventers[++i]) {
    if(!strcmp(choice->name, name)) {
      __eventer = choice;
      return 0;
    }
  }
  return -1;
}
