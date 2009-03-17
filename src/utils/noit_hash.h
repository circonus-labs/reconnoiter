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

/* this is just a copy (with search and replace) from jlog_hash */

#ifndef _NOIT_HASH_H
#define _NOIT_HASH_H

#include "noit_config.h"

typedef void (*NoitHashFreeFunc)(void *);

typedef struct _noit_hash_bucket {
  const char *k;
  int klen;
  void *data;
  struct _noit_hash_bucket *next;
} noit_hash_bucket;

typedef struct {
  noit_hash_bucket **buckets;
  u_int32_t table_size;
  u_int32_t initval;
  u_int32_t num_used_buckets;
  u_int32_t size;
  unsigned dont_rebucket:1;
  unsigned _spare:31;
} noit_hash_table;

#define NOIT_HASH_EMPTY { NULL, 0, 0, 0, 0, 0, 0 }

typedef struct {
  void *p2;
  int p1;
} noit_hash_iter;

#define NOIT_HASH_ITER_ZERO { NULL, 0 }

void noit_hash_init(noit_hash_table *h);
/* NOTE! "k" and "data" MUST NOT be transient buffers, as the hash table
 * implementation does not duplicate them.  You provide a pair of
 * NoitHashFreeFunc functions to free up their storage when you call
 * noit_hash_delete(), noit_hash_delete_all() or noit_hash_destroy().
 * */
int noit_hash_store(noit_hash_table *h, const char *k, int klen, void *data);
int noit_hash_replace(noit_hash_table *h, const char *k, int klen, void *data,
                      NoitHashFreeFunc keyfree, NoitHashFreeFunc datafree);
int noit_hash_retrieve(noit_hash_table *h, const char *k, int klen, void **data);
int noit_hash_retr_str(noit_hash_table *h, const char *k, int klen, const char **data);
int noit_hash_delete(noit_hash_table *h, const char *k, int klen,
                     NoitHashFreeFunc keyfree, NoitHashFreeFunc datafree);
void noit_hash_delete_all(noit_hash_table *h, NoitHashFreeFunc keyfree,
                          NoitHashFreeFunc datafree);
void noit_hash_destroy(noit_hash_table *h, NoitHashFreeFunc keyfree,
                       NoitHashFreeFunc datafree);

/* This is a convenience function only.  It assumes that all keys and values
 * in the destination hash are strings and allocated with malloc() and
 * assumes that the source contains only keys and values that can be
 * suitably duplicated by strdup().
 */
void noit_hash_merge_as_dict(noit_hash_table *dst, noit_hash_table *src);

/* This is an iterator and requires the hash to not be written to during the
   iteration process.
   To use:
     noit_hash_iter iter = NOIT_HASH_ITER_ZERO;

     const char *k;
     int klen;
     void *data;

     while(noit_hash_next(h, &iter, &k, &klen, &data)) {
       .... use k, klen and data ....
     }
*/
int noit_hash_next(noit_hash_table *h, noit_hash_iter *iter,
                   const char **k, int *klen, void **data);
int noit_hash_firstkey(noit_hash_table *h, const char **k, int *klen);
int noit_hash_nextkey(noit_hash_table *h, const char **k, int *klen, const char *lk, int lklen);

/* This function serves no real API use sans calculating expected buckets
   for keys (or extending the hash... which is unsupported) */
u_int32_t noit_hash__hash(const char *k, u_int32_t length, u_int32_t initval);
noit_hash_bucket *noit_hash__new_bucket(const char *k, int klen, void *data);
void noit_hash__rebucket(noit_hash_table *h, int newsize);
#endif
