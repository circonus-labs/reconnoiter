/*
 * Copyright (c) 2014, Circonus Inc.
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

#include "noit_config.h"
#include "noit_hash.h"
#include <time.h>
#include <assert.h>
#include <ck_epoch.h>

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

static int rand_init;
void noit_hash_init(noit_hash_table *h) {
  return noit_hash_init_size(h, NoitHASH_INITIAL_SIZE);
}

static void *
ht_malloc(size_t r)
{

  return malloc(r);
}

static void
ht_free(void *p, size_t b, bool r)
{

  (void)b;
  (void)r;
  free(p);
  return;
}

static struct ck_malloc my_allocator = {
  .malloc = ht_malloc,
  .free = ht_free
};

void noit_hash_init_size(noit_hash_table *h, int size) {
  if(!rand_init) {
    srand48((long int)time(NULL));
    rand_init = 1;
  }
  if(size < 2) size = 2;
  assert(ck_ht_init(&h->ht, CK_HT_MODE_BYTESTRING, NULL, &my_allocator,
                    size, lrand48()));
}
int noit_hash_size(noit_hash_table *h) {
  if(h->ht.h == NULL) noit_hash_init(h);
  return ck_ht_count(&h->ht);
}
int noit_hash_replace(noit_hash_table *h, const char *k, int klen, void *data,
                      NoitHashFreeFunc keyfree, NoitHashFreeFunc datafree) {
  ck_ht_entry_t entry;
  ck_ht_hash_t hv;
  void *tofree;

  if(h->ht.h == NULL) noit_hash_init(h);
  ck_ht_hash(&hv, &h->ht, k, klen);
  ck_ht_entry_set(&entry, hv, k, klen, data);
  if(ck_ht_set_spmc(&h->ht, hv, &entry) && !ck_ht_entry_empty(&entry)) {
    if(keyfree && (tofree = ck_ht_entry_key(&entry)) != k) keyfree(tofree);
    if(datafree && (tofree = ck_ht_entry_value(&entry)) != data) datafree(tofree);
  }
  return 1;
}
int noit_hash_store(noit_hash_table *h, const char *k, int klen, void *data) {
  ck_ht_entry_t entry;
  ck_ht_hash_t hv;

  if(h->ht.h == NULL) noit_hash_init(h);
  ck_ht_hash(&hv, &h->ht, k, klen);
  ck_ht_entry_set(&entry, hv, k, klen, data);
  return ck_ht_put_spmc(&h->ht, hv, &entry);
}
int noit_hash_retrieve(noit_hash_table *h, const char *k, int klen, void **data) {
  ck_ht_entry_t entry;
  ck_ht_hash_t hv;

  if(!h) return 0;
  if(h->ht.h == NULL) noit_hash_init(h);
  ck_ht_hash(&hv, &h->ht, k, klen);
  ck_ht_entry_set(&entry, hv, k, klen, NULL);
  if(ck_ht_get_spmc(&h->ht, hv, &entry)) {
    if(data) *data = ck_ht_entry_value(&entry);
    return 1;
  }
  return 0;
}
int noit_hash_retr_str(noit_hash_table *h, const char *k, int klen, const char **dstr) {
  void *data;
  if(!h) return 0;
  if(noit_hash_retrieve(h, k, klen, &data)) {
    if(dstr) *dstr = data;
    return 1;
  }
  return 0;
}
int noit_hash_delete(noit_hash_table *h, const char *k, int klen,
                     NoitHashFreeFunc keyfree, NoitHashFreeFunc datafree) {
  ck_ht_entry_t entry;
  ck_ht_hash_t hv;

  if(!h) return 0;
  if(h->ht.h == NULL) noit_hash_init(h);
  ck_ht_hash(&hv, &h->ht, k, klen);
  ck_ht_entry_set(&entry, hv, k, klen, NULL);
  if(ck_ht_remove_spmc(&h->ht, hv, &entry)) {
    void *tofree;
    if(keyfree && (tofree = ck_ht_entry_key(&entry)) != NULL) keyfree(tofree);
    if(datafree && (tofree = ck_ht_entry_value(&entry)) != NULL) datafree(tofree);
    return 1;
  }
  return 0;
}

void noit_hash_delete_all(noit_hash_table *h, NoitHashFreeFunc keyfree, NoitHashFreeFunc datafree) {
  ck_ht_entry_t *cursor;
  ck_ht_iterator_t iterator = CK_HT_ITERATOR_INITIALIZER;
  if(!h) return;
  if(!keyfree && !datafree) return;
  if(h->ht.h == NULL) noit_hash_init(h);
  while(ck_ht_next(&h->ht, &iterator, &cursor)) {
    if(keyfree) keyfree(ck_ht_entry_key(cursor));
    if(datafree) datafree(ck_ht_entry_value(cursor));
  }
}

void noit_hash_destroy(noit_hash_table *h, NoitHashFreeFunc keyfree, NoitHashFreeFunc datafree) {
  if(!h) return;
  if(h->ht.h == NULL) noit_hash_init(h);
  noit_hash_delete_all(h, keyfree, datafree);
  ck_ht_destroy(&h->ht);
}

void noit_hash_merge_as_dict(noit_hash_table *dst, noit_hash_table *src) {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *k;
  int klen;
  void *data;
  if(src == NULL || dst == NULL) return;
  while(noit_hash_next(src, &iter, &k, &klen, &data))
    noit_hash_replace(dst, strdup(k), klen, strdup((char *)data), free, free);
}

int noit_hash_next(noit_hash_table *h, noit_hash_iter *iter,
                const char **k, int *klen, void **data) {
  ck_ht_entry_t *cursor;
  if(h->ht.h == NULL) noit_hash_init(h);
  if(!ck_ht_next(&h->ht, iter, &cursor)) return 0;
  *k = ck_ht_entry_key(cursor);
  *klen = ck_ht_entry_key_length(cursor);
  *data = ck_ht_entry_value(cursor);
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

/* vim: se sw=2 ts=2 et: */
