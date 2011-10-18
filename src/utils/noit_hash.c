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

#include "noit_config.h"
#include "noit_hash.h"

/* This is from http://burtleburtle.net/bob/hash/doobs.html */

#define NoitHASH_INITIAL_SIZE (1<<7)

#define mix(a,b,c) \
{ \
  a -= b; a -= c; a ^= (c>>13); \
  b -= c; b -= a; b ^= (a<<8); \
  c -= a; c -= b; c ^= (b>>13); \
  a -= b; a -= c; a ^= (c>>12);  \
  b -= c; b -= a; b ^= (a<<16); \
  c -= a; c -= b; c ^= (b>>5); \
  a -= b; a -= c; a ^= (c>>3);  \
  b -= c; b -= a; b ^= (a<<10); \
  c -= a; c -= b; c ^= (b>>15); \
}

static inline
u_int32_t __hash(const char *k, u_int32_t length, u_int32_t initval)
{
   register u_int32_t a,b,c,len;

   /* Set up the internal state */
   len = length;
   a = b = 0x9e3779b9;  /* the golden ratio; an arbitrary value */
   c = initval;         /* the previous hash value */

   /*---------------------------------------- handle most of the key */
   while (len >= 12)
   {
      a += (k[0] +((u_int32_t)k[1]<<8) +((u_int32_t)k[2]<<16) +((u_int32_t)k[3]<<24));
      b += (k[4] +((u_int32_t)k[5]<<8) +((u_int32_t)k[6]<<16) +((u_int32_t)k[7]<<24));
      c += (k[8] +((u_int32_t)k[9]<<8) +((u_int32_t)k[10]<<16)+((u_int32_t)k[11]<<24));
      mix(a,b,c);
      k += 12; len -= 12;
   }

   /*------------------------------------- handle the last 11 bytes */
   c += length;
   switch(len)              /* all the case statements fall through */
   {
   case 11: c+=((u_int32_t)k[10]<<24);
   case 10: c+=((u_int32_t)k[9]<<16);
   case 9 : c+=((u_int32_t)k[8]<<8);
      /* the first byte of c is reserved for the length */
   case 8 : b+=((u_int32_t)k[7]<<24);
   case 7 : b+=((u_int32_t)k[6]<<16);
   case 6 : b+=((u_int32_t)k[5]<<8);
   case 5 : b+=k[4];
   case 4 : a+=((u_int32_t)k[3]<<24);
   case 3 : a+=((u_int32_t)k[2]<<16);
   case 2 : a+=((u_int32_t)k[1]<<8);
   case 1 : a+=k[0];
     /* case 0: nothing left to add */
   }
   mix(a,b,c);
   /*-------------------------------------------- report the result */
   return c;
}

u_int32_t noit_hash__hash(const char *k, u_int32_t length, u_int32_t initval) {
  return __hash(k,length,initval);
}

void noit_hash_init(noit_hash_table *h) {
  memset(h, 0, sizeof(noit_hash_table));
  h->initval = lrand48();
  h->table_size = NoitHASH_INITIAL_SIZE;
  h->buckets = calloc(h->table_size, sizeof(noit_hash_bucket *));
}

void noit_hash_init_size(noit_hash_table *h, int size) {
  memset(h, 0, sizeof(noit_hash_table));
  h->initval = lrand48();
  h->table_size = size;
  h->buckets = calloc(h->table_size, sizeof(noit_hash_bucket *));
}

noit_hash_bucket *noit_hash__new_bucket(const char *k, int klen, void *data) {
  noit_hash_bucket *b;
  b = calloc(1, sizeof(noit_hash_bucket));
  b->k = k;
  b->klen = klen;
  b->data = data;
  return b;
}
void noit_hash__rebucket(noit_hash_table *h, int newsize) {
  int i, newoff;
  noit_hash_bucket **newbuckets, *b, *n;

  if (h->dont_rebucket) return;

  i = newsize;
  while(i) {
    if(i & 1) break;
    i >>= 1;
  }
  if(i & ~1) {
    return;
  }
  newbuckets = calloc(newsize, sizeof(noit_hash_bucket *));
  h->num_used_buckets = 0;
  for(i = 0; i < h->table_size; i++) {
    b = h->buckets[i];
    while(b) {
      n = b->next;
      newoff = __hash(b->k, b->klen, h->initval) & (newsize-1);
      if(newbuckets[newoff] == NULL) h->num_used_buckets++;
      b->next = newbuckets[newoff];
      newbuckets[newoff] = b;
      b = n;
    }
  }
  free(h->buckets);
  h->table_size = newsize;
  h->buckets = newbuckets;
}
int noit_hash_replace(noit_hash_table *h, const char *k, int klen, void *data,
                   NoitHashFreeFunc keyfree, NoitHashFreeFunc datafree) {
  int off;
  int replaced = 0;
  noit_hash_bucket __b, *tr, *b = &__b;

  if(h->table_size == 0) noit_hash_init(h);
  off = __hash(k, klen, h->initval) & (h->table_size-1);
  __b.next = h->buckets[off];
  if(!b->next) h->num_used_buckets++;
  while(b->next) {
    if(b->next->klen == klen && !memcmp(b->next->k, k, klen)) {
      tr = b->next;
      if(keyfree) keyfree((void *)tr->k);
      if(datafree && tr->data) datafree((void *)tr->data);
      b->next = tr->next;
      if(tr == h->buckets[off]) h->buckets[off] = tr->next;
      free(tr);
      replaced = 1;
      break;
    } else {
      b = b->next;
    }
  }
  b = noit_hash__new_bucket(k, klen, data);
  b->next = h->buckets[off]; 
  h->buckets[off] = b;
  if(!replaced) h->size++;

  if(h->size > h->table_size - (h->table_size >> 3)) {
    noit_hash__rebucket(h, h->table_size << 1);
  }
  return 1;
}
int noit_hash_store(noit_hash_table *h, const char *k, int klen, void *data) {
  int off;
  noit_hash_bucket *b;

  if(h->table_size == 0) noit_hash_init(h);
  off = __hash(k, klen, h->initval) & (h->table_size-1);
  b = h->buckets[off];
  if(!b) h->num_used_buckets++;
  while(b) {
    if(b->klen == klen && !memcmp(b->k, k, klen)) return 0;
    b = b->next;
  }
  b = noit_hash__new_bucket(k, klen, data);
  b->next = h->buckets[off]; 
  h->buckets[off] = b;
  h->size++;

  if(h->size > h->table_size - (h->table_size >> 3)) {
    noit_hash__rebucket(h, h->table_size << 1);
  }
  return 1;
}
int noit_hash_retrieve(noit_hash_table *h, const char *k, int klen, void **data) {
  int off;
  noit_hash_bucket *b;

  if(!h) return 0;
  if(h->table_size == 0) noit_hash_init(h);
  off = __hash(k, klen, h->initval) & (h->table_size-1);
  b = h->buckets[off];
  while(b) {
    if(b->klen == klen && !memcmp(b->k, k, klen)) break;
    b = b->next;
  }
  if(b) {
    if(data) *data = b->data;
    return 1;
  }
  return 0;
}
int noit_hash_retr_str(noit_hash_table *h, const char *k, int klen, const char **dstr) {
  int rv;
  void *vptr = NULL;
  rv = noit_hash_retrieve(h,k,klen,&vptr);
  *dstr = vptr;
  return rv;
}
int noit_hash_delete(noit_hash_table *h, const char *k, int klen,
                  NoitHashFreeFunc keyfree, NoitHashFreeFunc datafree) {
  int off;
  noit_hash_bucket *b, *prev = NULL;

  if(h->table_size == 0) noit_hash_init(h);
  off = __hash(k, klen, h->initval) & (h->table_size-1);
  b = h->buckets[off];
  while(b) {
    if(b->klen == klen && !memcmp(b->k, k, klen)) break;
    prev = b;
    b = b->next;
  }
  if(!b) return 0; /* No match */
  if(!prev) h->buckets[off] = h->buckets[off]->next;
  else prev->next = b->next;
  if(keyfree) keyfree((void *)b->k);
  if(datafree && b->data) datafree(b->data);
  free(b);
  h->size--;
  if(h->buckets[off] == NULL) h->num_used_buckets--;
  if(h->table_size > NoitHASH_INITIAL_SIZE &&
     h->size < h->table_size >> 2) 
    noit_hash__rebucket(h, h->table_size >> 1);
  return 1;
}

void noit_hash_delete_all(noit_hash_table *h, NoitHashFreeFunc keyfree, NoitHashFreeFunc datafree) {
  int i;
  noit_hash_bucket *b, *tofree;
  for(i=0; i<h->table_size; i++) {
    b = h->buckets[i];
    while(b) {
      tofree = b;
      b = b->next;
      if(keyfree) keyfree((void *)tofree->k);
      if(datafree && tofree->data) datafree(tofree->data);
      free(tofree);
    }
    h->buckets[i] = NULL;
  }
  h->num_used_buckets = 0;
  h->size = 0;
  noit_hash__rebucket(h, NoitHASH_INITIAL_SIZE);
}

void noit_hash_destroy(noit_hash_table *h, NoitHashFreeFunc keyfree, NoitHashFreeFunc datafree) {
  noit_hash_delete_all(h, keyfree, datafree);
  if(h->buckets) free(h->buckets);
}

void noit_hash_merge_as_dict(noit_hash_table *dst, noit_hash_table *src) {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *k;
  int klen;
  void *data;
  if(src == NULL || dst == NULL) return;
  while(noit_hash_next(src, &iter, &k, &klen, &data))
    noit_hash_store(dst, strdup(k), klen, strdup((char *)data));
}

int noit_hash_next(noit_hash_table *h, noit_hash_iter *iter,
                const char **k, int *klen, void **data) {
  noit_hash_bucket *b;
 next_row:
  if(iter->p1 < 0 || iter->p1 >= h->table_size) return 0;
  if(iter->p2 == NULL) iter->p2 = (void *)h->buckets[iter->p1];
  if(iter->p2 == NULL) {
    iter->p1++;
    goto next_row;
  }
  b = (noit_hash_bucket *)(iter->p2);
  *k = b->k; *klen = b->klen; 
  if(data) *data = b->data;
  b = b->next;
  if(!b) iter->p1++;
  iter->p2 = b;
  return 1;
}

int noit_hash_next_str(noit_hash_table *h, noit_hash_iter *iter,
                       const char **k, int *klen, const char **dstr) {
  void *data = NULL;
  int rv;
  rv = noit_hash_next(h,iter,k,klen,&data);
  *dstr = data;
  return rv;
}

int noit_hash_firstkey(noit_hash_table *h, const char **k, int *klen) {
  int i;
  for(i=0;i<h->table_size;i++) {
    if(h->buckets[i]) {
      *k = h->buckets[i]->k;
      *klen = h->buckets[i]->klen;
      return 1;
    }
  }
  return 0;
}
int noit_hash_nextkey(noit_hash_table *h, const char **k, int *klen, const char *lk, int lklen) {
  int off;
  noit_hash_bucket *b;

  if(h->table_size == 0) return 0;
  off = __hash(lk, lklen, h->initval) & (h->table_size-1);
  b = h->buckets[off];
  while(b) {
    if(b->klen == lklen && !memcmp(b->k, lk, lklen)) break;
    b = b->next;
  }
  if(b) {
    if(b->next) {
      *k = b->next->k;
      *klen = b->next->klen;
      return 1;
    } else {
      off++;
      for(;off < h->table_size; off++) {
        if(h->buckets[off]) {
          *k = h->buckets[off]->k;
          *klen = h->buckets[off]->klen;
          return 1;
        }
      }
    }
  }
  return 0;
}
/* vim: se sw=2 ts=2 et: */
