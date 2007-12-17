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

#ifndef _NOIT_SKIPLIST_P_H
#define _NOIT_SKIPLIST_P_H

/* This is a skiplist implementation to be used for abstract structures
   within the Spread multicast and group communication toolkit

   This portion written by -- Theo Schlossnagle <jesus@cnds.jhu.eu>
*/

/* This is the function type that must be implemented per object type
   that is used in a skiplist for comparisons to maintain order */
typedef int (*noit_skiplist_comparator_t)(void *, void *);
typedef void (*noit_freefunc_t)(void *);

struct _noit_skiplist_node;

typedef struct _iskiplist {
  noit_skiplist_comparator_t compare;
  noit_skiplist_comparator_t comparek;
  int height;
  int preheight;
  int size;
  struct _noit_skiplist_node *top;
  struct _noit_skiplist_node *bottom;
  /* These two are needed for appending */
  struct _noit_skiplist_node *topend;
  struct _noit_skiplist_node *bottomend;
  struct _iskiplist *index;
} noit_skiplist;

typedef struct _noit_skiplist_node {
  void *data;
  struct _noit_skiplist_node *next;
  struct _noit_skiplist_node *prev;
  struct _noit_skiplist_node *down;
  struct _noit_skiplist_node *up;
  struct _noit_skiplist_node *previndex;
  struct _noit_skiplist_node *nextindex;
  noit_skiplist *sl;
} noit_skiplist_node;


void noit_skiplist_init(noit_skiplist *sl);
void noit_skiplist_set_compare(noit_skiplist *sl, noit_skiplist_comparator_t,
                               noit_skiplist_comparator_t);
void noit_skiplist_add_index(noit_skiplist *sl, noit_skiplist_comparator_t,
                             noit_skiplist_comparator_t);
noit_skiplist_node *noit_skiplist_getlist(noit_skiplist *sl);
void *noit_skiplist_find_compare(noit_skiplist *sl, void *data,
                                 noit_skiplist_node **iter,
		                 noit_skiplist_comparator_t func);
void *noit_skiplist_find(noit_skiplist *sl, void *data,
                         noit_skiplist_node **iter);
void *noit_skiplist_next(noit_skiplist *sl, noit_skiplist_node **);
void *noit_skiplist_previous(noit_skiplist *sl, noit_skiplist_node **);

noit_skiplist_node *noit_skiplist_insert_compare(noit_skiplist *sl,
                                                 void *data,
                                                 noit_skiplist_comparator_t comp);
noit_skiplist_node *noit_skiplist_insert(noit_skiplist *sl, void *data);
int noit_skiplist_remove_compare(noit_skiplist *sl, void *data,
                                 noit_freefunc_t myfree,
                                 noit_skiplist_comparator_t comp);
int noit_skiplist_remove(noit_skiplist *sl, void *data, noit_freefunc_t myfree);
int noit_skiplisti_remove(noit_skiplist *sl, noit_skiplist_node *m,
                          noit_freefunc_t myfree);
void noit_skiplist_remove_all(noit_skiplist *sl, noit_freefunc_t myfree);

int noit_skiplisti_find_compare(noit_skiplist *sl, void *data,
                                noit_skiplist_node **ret,
                                noit_skiplist_comparator_t comp);

void *noit_skiplist_pop(noit_skiplist * a, noit_freefunc_t myfree);
void *noit_skiplist_peek(noit_skiplist * a);

int noit_compare_voidptr(void *, void *);

#endif
