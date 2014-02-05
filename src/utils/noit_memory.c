/*
 * Copyright (c) 2014, Circonus, Inc.
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
 *    * Neither the name Circonus, Inc. nor the names of its contributors
 *      may be used to endorse or promote products derived from this
 *      software without specific prior written permission.
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

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <ck_epoch.h>
#include "utils/noit_log.h"

#define NOIT_EPOCH_SAFE_MAGIC 0x5afe5afe

static int initialized = 0;
static ck_epoch_t epoch_ht;
static __thread ck_epoch_record_t *epoch_rec;

void noit_memory_init_thread() {
  if(epoch_rec == NULL) {
    epoch_rec = malloc(sizeof(*epoch_rec));
    ck_epoch_register(&epoch_ht, epoch_rec);
  }
}
void noit_memory_init() {
  if(initialized) return;
  initialized = 1;
  ck_epoch_init(&epoch_ht);
  noit_memory_init_thread();
}
void noit_memory_maintenance() {
  ck_epoch_record_t epoch_temporary = *epoch_rec;
  if(ck_epoch_poll(&epoch_ht, epoch_rec)) {
    if(epoch_temporary.n_pending != epoch_rec->n_pending ||
       epoch_temporary.n_peak != epoch_rec->n_peak ||
       epoch_temporary.n_dispatch != epoch_rec->n_dispatch) {
      noitL(noit_debug,
            "summary: [%u/%u/%u] %u pending, %u peak, %lu reclamations -> "
              "[%u/%u/%u] %u pending, %u peak, %lu reclamations\n\n",
              epoch_temporary.state, epoch_temporary.epoch,epoch_temporary.active,
              epoch_temporary.n_pending, epoch_temporary.n_peak, epoch_temporary.n_dispatch,
              epoch_rec->state, epoch_rec->epoch,epoch_rec->active,
              epoch_rec->n_pending, epoch_rec->n_peak, epoch_rec->n_dispatch);
    }
  }
}
void noit_memory_begin() {
  ck_epoch_begin(&epoch_ht, epoch_rec);
}
void noit_memory_end() {
  ck_epoch_end(&epoch_ht, epoch_rec);
}

struct safe_epoch {
  ck_epoch_entry_t epoch_entry;
  uint32_t magic;
};

static void noit_memory_real_free(ck_epoch_entry_t *e) {
  free(e);
  return;
}

void *noit_memory_safe_malloc(size_t r) {
  struct safe_epoch *b;
  b = malloc(sizeof(*b) + r);
  b->magic = NOIT_EPOCH_SAFE_MAGIC;
  return b + 1;
}

void *noit_memory_ck_malloc(size_t r) {
  return noit_memory_safe_malloc(r);
}

void noit_memory_ck_free(void *p, size_t b, bool r) {
  struct safe_epoch *e = (p - sizeof(struct safe_epoch));

  if(p == NULL) return;
  (void)b;
  assert(e->magic == NOIT_EPOCH_SAFE_MAGIC);

  if (r == true) {
    /* Destruction requires safe memory reclamation. */
    ck_epoch_call(&epoch_ht, epoch_rec, &e->epoch_entry, noit_memory_real_free);
  } else {
    noit_memory_real_free(&e->epoch_entry);
  }

  return;
}

void noit_memory_safe_free(void *p) {
  noit_memory_ck_free(p, 0, true);
}

