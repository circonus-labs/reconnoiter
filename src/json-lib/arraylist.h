/*
 * $Id: arraylist.h,v 1.4 2006/01/26 02:16:28 mclark Exp $
 *
 * Copyright (c) 2004, 2005 Metaparadigm Pte. Ltd.
 * Michael Clark <michael@metaparadigm.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See COPYING for details.
 *
 */

#ifndef _arraylist_h_
#define _arraylist_h_

#ifdef __cplusplus
extern "C" {
#endif

#define ARRAY_LIST_DEFAULT_SIZE 32

typedef void (jl_array_list_free_fn) (void *data);

struct jl_array_list
{
  void **array;
  int length;
  int size;
  jl_array_list_free_fn *free_fn;
};

extern struct jl_array_list*
jl_array_list_new(jl_array_list_free_fn *free_fn);

extern void
jl_array_list_free(struct jl_array_list *al);

extern void*
jl_array_list_get_idx(struct jl_array_list *al, int i);

extern int
jl_array_list_put_idx(struct jl_array_list *al, int i, void *data);

extern int
jl_array_list_add(struct jl_array_list *al, void *data);

extern int
jl_array_list_length(struct jl_array_list *al);

#ifdef __cplusplus
}
#endif

#endif
