/* ======================================================================
 * Copyright (c) 2000,2006 Theo Schlossnagle
 * All rights reserved.
 * The following code was written by Theo Schlossnagle for use in the
 * Backhand project at The Center for Networking and Distributed Systems
 * at The Johns Hopkins University.
 *
 * This is a skiplist implementation to be used for abstract structures
 * and is release under the LGPL license version 2.1 or later.  A copy
 * of this license can be found file LGPL.
 *
 * Alternatively, this file may be licensed under the new BSD license.
 * A copy of this license can be found file BSD.
 * 
 * ======================================================================
*/

#include "noit_defines.h"
#include "noit_skiplist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#ifndef MIN
#define MIN(a,b) ((a<b)?(a):(b))
#endif

static int noit_skiplisti_find_compare(noit_skiplist *sl, const void *data,
                                       noit_skiplist_node **ret,
                                       noit_skiplist_node **prev,
                                       noit_skiplist_node **next,
                                       noit_skiplist_comparator_t comp);

int noit_compare_voidptr(const void *a, const void *b) {
  if(a < b) return -1;
  if(a == b) return 0;
  return 1;
}

static int get_b_rand(void) {
  static int ph=32; /* More bits than we will ever use */
  static unsigned long randseq;
  if(ph > 31) { /* Num bits in return of lrand48() */
    ph=0;
    randseq = lrand48();
  }
  ph++;
  return ((randseq & (1 << (ph-1))) >> (ph-1));
}

void noit_skiplisti_init(noit_skiplist *sl) {
  memset(sl, 0, sizeof(*sl));
}

static int indexing_comp(const void *av, const void *bv) {
  const noit_skiplist *a = av;
  const noit_skiplist *b = bv;
  return (void *)(a->compare)>(void *)(b->compare);
}
static int indexing_compk(const void *a, const void *bv) {
  const noit_skiplist *b = bv;
  return a>(void *)(b->compare);
}

void noit_skiplist_init(noit_skiplist *sl) {
  noit_skiplisti_init(sl);
  sl->index = (noit_skiplist *)malloc(sizeof(noit_skiplist));
  noit_skiplisti_init(sl->index);
  noit_skiplist_set_compare(sl->index, indexing_comp, indexing_compk);
}

void noit_skiplist_set_compare(noit_skiplist *sl,
                               noit_skiplist_comparator_t comp,
                              noit_skiplist_comparator_t compk) {
  if(sl->compare && sl->comparek) {
    noit_skiplist_add_index(sl, comp, compk);
  } else {
    sl->compare = comp;
    sl->comparek = compk;
  }
}

void noit_skiplist_add_index(noit_skiplist *sl,
                             noit_skiplist_comparator_t comp,
                             noit_skiplist_comparator_t compk) {
  noit_skiplist_node *m;
  noit_skiplist *ni;
  int icount=0;
  noit_skiplist_find(sl->index, (void *)comp, &m);
  if(m) return; /* Index already there! */
  ni = (noit_skiplist *)malloc(sizeof(noit_skiplist));
  noit_skiplisti_init(ni);
  noit_skiplist_set_compare(ni, comp, compk);
  /* Build the new index... This can be expensive! */
  m = noit_skiplist_insert(sl->index, ni);
  while(m->prev) m=m->prev, icount++;
  for(m=noit_skiplist_getlist(sl); m; noit_skiplist_next(sl, &m)) {
    int j=icount-1;
    noit_skiplist_node *nsln;
    nsln = noit_skiplist_insert(ni, m->data);
    /* skip from main index down list */
    while(j>0) m=m->nextindex, j--;
    /* insert this node in the indexlist after m */
    nsln->nextindex = m->nextindex;
    if(m->nextindex) m->nextindex->previndex = nsln;
    nsln->previndex = m;
    m->nextindex = nsln;
  } 
}

noit_skiplist_node *noit_skiplist_getlist(noit_skiplist *sl) {
  if(!sl->bottom) return NULL;
  return sl->bottom->next;
}

void *noit_skiplist_find(noit_skiplist *sl,
                         const void *data,
                         noit_skiplist_node **iter) {
  return noit_skiplist_find_neighbors(sl, data, iter, NULL, NULL);
}
void *noit_skiplist_find_neighbors(noit_skiplist *sl,
                                   const void *data,
                                   noit_skiplist_node **iter,
                                   noit_skiplist_node **prev,
                                   noit_skiplist_node **next) {
  void *ret;
  noit_skiplist_node *aiter;
  if(!sl->compare) return 0;
  if(iter)
    ret = noit_skiplist_find_neighbors_compare(sl, data, iter,
                                               prev, next, sl->compare);
  else
    ret = noit_skiplist_find_neighbors_compare(sl, data, &aiter,
                                               prev, next, sl->compare);
  return ret;
}

void *noit_skiplist_find_compare(noit_skiplist *sli,
                                 const void *data,
                                 noit_skiplist_node **iter,
                                 noit_skiplist_comparator_t comp) {
  return noit_skiplist_find_neighbors_compare(sli, data, iter,
                                              NULL, NULL, comp);
}
void *noit_skiplist_find_neighbors_compare(noit_skiplist *sli,
                                           const void *data,
                                           noit_skiplist_node **iter,
                                           noit_skiplist_node **prev,
                                           noit_skiplist_node **next,
                                           noit_skiplist_comparator_t comp) {
  noit_skiplist_node *m = NULL;
  noit_skiplist *sl;
  if(iter) *iter = NULL;
  if(prev) *prev = NULL;
  if(next) *next = NULL;
  if(comp==sli->compare || !sli->index) {
    sl = sli;
  } else {
    noit_skiplist_find(sli->index, (void *)comp, &m);
    assert(m);
    sl= (noit_skiplist *) m->data;
  }
  noit_skiplisti_find_compare(sl, data, iter, prev, next, sl->comparek);
  return (iter && *iter)?((*iter)->data):NULL;
}
static int noit_skiplisti_find_compare(noit_skiplist *sl,
                                       const void *data,
                                       noit_skiplist_node **ret,
                                       noit_skiplist_node **prev,
                                       noit_skiplist_node **next,
                                       noit_skiplist_comparator_t comp) {
  noit_skiplist_node *m = NULL;
  int count=0;
  if(ret) *ret = NULL;
  if(prev) *prev = NULL;
  if(next) *next = NULL;
  m = sl->top;
  while(m) {
    int compared;
    compared = (m->next) ? comp(data, m->next->data) : -1;
    if(compared == 0) { /* Found */
      m=m->next; /* m->next is the match */
      while(m->down) m=m->down; /* proceed to the bottom-most */
      if(ret) *ret = m;
      if(prev) *prev = m->prev;
      if(next) *next = m->next;
      return count;
    }
    if((m->next == NULL) || (compared<0)) {
      if(m->down == NULL) {
        /* This is... we're about to bail, figure out our neighbors */
        if(prev) *prev = (m == sl->bottom) ? NULL : m;
        if(next) *next = m->next;
      }
      m = m->down;
      count++;
    }
    else
      m = m->next, count++;
  }
  if(ret) *ret = NULL;
  return count;
}
void *noit_skiplist_next(noit_skiplist *sl, noit_skiplist_node **iter) {
  if(!*iter) return NULL;
  *iter = (*iter)->next;
  return (*iter)?((*iter)->data):NULL;
}
void *noit_skiplist_previous(noit_skiplist *sl, noit_skiplist_node **iter) {
  if(!*iter) return NULL;
  *iter = (*iter)->prev;
  return (*iter)?((*iter)->data):NULL;
}
noit_skiplist_node *noit_skiplist_insert(noit_skiplist *sl,
                                         const void *data) {
  if(!sl->compare) return 0;
  return noit_skiplist_insert_compare(sl, data, sl->compare);
}

noit_skiplist_node *noit_skiplist_insert_compare(noit_skiplist *sl,
                                                 const void *data,
                                                 noit_skiplist_comparator_t comp) {
  noit_skiplist_node *m, *p, *tmp, *ret = NULL, **stack;
  int nh=1, ch, stacki;
  if(!sl->top) {
    sl->height = 1;
    sl->top = sl->bottom = 
      calloc(1, sizeof(noit_skiplist_node));
    sl->top->sl = sl;
  }
  if(sl->preheight) {
    while(nh < sl->preheight && get_b_rand()) nh++;
  } else {
    while(nh <= sl->height && get_b_rand()) nh++;
  }
  /* Now we have the new height at which we wish to insert our new node */
  /* Let us make sure that our tree is a least that tall (grow if necessary)*/
  for(;sl->height<nh;sl->height++) {
    sl->top->up = (noit_skiplist_node *)calloc(1, sizeof(noit_skiplist_node));
    sl->top->up->down = sl->top;
    sl->top = sl->top->up;
    sl->top->sl = sl;
  }
  ch = sl->height;
  /* Find the node (or node after which we would insert) */
  /* Keep a stack to pop back through for insertion */
  m = sl->top;
  stack = (noit_skiplist_node **)alloca(sizeof(noit_skiplist_node *)*(nh));
  stacki=0;
  while(m) {
    int compared=-1;
    if(m->next) compared=comp(data, m->next->data);
    if(compared == 0) {
      return 0;
    }
    if(compared<0) {
      if(ch<=nh) {
	/* push on stack */
	stack[stacki++] = m;
      }
      m = m->down;
      ch--;
    } else {
      m = m->next;
    }
  }
  /* Pop the stack and insert nodes */
  p = NULL;
  for(;stacki>0;stacki--) {
    m = stack[stacki-1];
    tmp = calloc(1, sizeof(*tmp));
    tmp->next = m->next;
    if(m->next) m->next->prev=tmp;
    tmp->prev = m;
    tmp->down = p;
    if(p) p->up=tmp;
    tmp->data = (void *)data;
    tmp->sl = sl;
    m->next = tmp;
    /* This sets ret to the bottom-most node we are inserting */
    if(!p) ret=tmp;
    p = tmp;
  }
  if(sl->index != NULL) {
    /* this is a external insertion, we must insert into each index as well */
    noit_skiplist_node *p, *ni, *li;
    assert(ret);
    li=ret;
    for(p = noit_skiplist_getlist(sl->index); p; noit_skiplist_next(sl->index, &p)) {
      ni = noit_skiplist_insert((noit_skiplist *)p->data, ret->data);
      assert(ni);
      li->nextindex = ni;
      ni->previndex = li;
      li = ni;
    }
  }
  sl->size++;
  return ret;
}
int noit_skiplist_remove(noit_skiplist *sl,
                         const void *data, noit_freefunc_t myfree) {
  if(!sl->compare) return 0;
  return noit_skiplist_remove_compare(sl, data, myfree, sl->comparek);
}
int noit_skiplisti_remove(noit_skiplist *sl, noit_skiplist_node *m, noit_freefunc_t myfree) {
  noit_skiplist_node *p;
  if(!m) return 0;
  if(m->nextindex) noit_skiplisti_remove(m->nextindex->sl, m->nextindex, NULL);
  while(m->up) m=m->up;
  while(m) {
    p=m;
    p->prev->next = p->next; /* take me out of the list */
    if(p->next) p->next->prev = p->prev; /* take me out of the list */
    m=m->down;
    /* This only frees the actual data in the bottom one */
    if(!m && myfree && p->data) myfree(p->data);
    free(p);
  }
  sl->size--;
  while(sl->top && sl->top->next == NULL) {
    /* While the row is empty and we are not on the bottom row */
    p = sl->top;
    sl->top = sl->top->down; /* Move top down one */
    if(sl->top) sl->top->up = NULL; /* Make it think its the top */
    free(p);
    sl->height--;
  }
  if(!sl->top) sl->bottom = NULL;
  return 1;
}
int noit_skiplist_remove_compare(noit_skiplist *sli,
                                 const void *data,
                                 noit_freefunc_t myfree,
                                 noit_skiplist_comparator_t comp) {
  noit_skiplist_node *m;
  noit_skiplist *sl;
  if(comp==sli->comparek || !sli->index) {
    sl = sli;
  } else {
    noit_skiplist_find(sli->index, (void *)comp, &m);
    assert(m);
    sl= (noit_skiplist *) m->data;
  }
  noit_skiplisti_find_compare(sl, data, &m, NULL, NULL, comp);
  if(!m) return 0;
  while(m->previndex) m=m->previndex;
  return noit_skiplisti_remove(sl, m, myfree);
}
void noit_skiplist_remove_all(noit_skiplist *sl, noit_freefunc_t myfree) {
  noit_skiplist_node *m, *p, *u;
  m=sl->bottom;
  while(m) {
    p = m->next;
    if(p && myfree && p->data) myfree(p->data);
    while(m) {
      u = m->up;
      free(m);
      m=u;
    }
    m = p;
  }
  sl->top = sl->bottom = NULL;
  sl->height = 0;
  sl->size = 0;
}
static void noit_skiplisti_destroy(void *vsl) {
  noit_skiplist_destroy((noit_skiplist *)vsl, NULL);
  free(vsl);
}
void noit_skiplist_destroy(noit_skiplist *sl, noit_freefunc_t myfree) {
  while(noit_skiplist_pop(sl->index, noit_skiplisti_destroy) != NULL);
  noit_skiplist_remove_all(sl, myfree);
}
void *noit_skiplist_pop(noit_skiplist * a, noit_freefunc_t myfree)
{
  noit_skiplist_node *sln;
  void *data = NULL;
  sln = noit_skiplist_getlist(a);
  if (sln) {
    data = sln->data;
    noit_skiplisti_remove(a, sln, myfree);
  }
  return data;
}
void *noit_skiplist_peek(noit_skiplist * a)
{
  noit_skiplist_node *sln;
  sln = noit_skiplist_getlist(a);
  if (sln) {
    return sln->data;
  }
  return NULL;
}
