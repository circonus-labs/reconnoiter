/*
 * $Id: json_object_private.h,v 1.4 2006/01/26 02:16:28 mclark Exp $
 *
 * Copyright (c) 2004, 2005 Metaparadigm Pte. Ltd.
 * Michael Clark <michael@metaparadigm.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See COPYING for details.
 *
 */

#ifndef _json_object_private_h_
#define _json_object_private_h_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef void (json_object_delete_fn)(struct json_object *o);
typedef int (json_object_to_json_string_fn)(struct json_object *o,
					    struct printbuf *pb);

struct json_object
{
  enum json_type o_type;
  enum json_int_overflow o_ioverflow;
  json_object_delete_fn *_delete;
  json_object_to_json_string_fn *_to_json_string;
  int _ref_count;
  struct printbuf *_pb;
  union data {
    boolean c_boolean;
    double c_double;
    int c_int;
    struct lh_table *c_object;
    struct array_list *c_array;
    char *c_string;
  } o;
  union {
    u_int64_t c_uint64;
    int64_t c_int64;
  } overflow;
};

/* CAW: added for ANSI C iteration correctness */
struct json_object_iter
{
	char *key;
	struct json_object *val;
	struct lh_entry *entry;
};

#ifdef __cplusplus
}
#endif

#endif
