/*
 * $Id: printbuf.h,v 1.4 2006/01/26 02:16:28 mclark Exp $
 *
 * Copyright (c) 2004, 2005 Metaparadigm Pte. Ltd.
 * Michael Clark <michael@metaparadigm.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See COPYING for details.
 *
 *
 * Copyright (c) 2008-2009 Yahoo! Inc.  All rights reserved.
 * The copyrights to the contents of this file are licensed under the MIT License
 * (http://www.opensource.org/licenses/mit-license.php)
 */

#ifndef _printbuf_h_
#define _printbuf_h_

#ifdef __cplusplus
extern "C" {
#endif

#undef PRINTBUF_DEBUG

struct jl_printbuf {
  char *buf;
  int bpos;
  int size;
};

extern struct jl_printbuf*
jl_printbuf_new(void);

/* As an optimization, jl_printbuf_memappend is defined as a macro that
 * handles copying data if the buffer is large enough; otherwise it
 * invokes jl_printbuf_memappend_real() which performs the heavy lifting
 * of realloc()ing the buffer and copying data.
 */
extern int
jl_printbuf_memappend(struct jl_printbuf *p, const char *buf, int size);

#define jl_printbuf_memappend_fast(p, bufptr, bufsize)          \
do {                                                         \
  if ((p->size - p->bpos) > bufsize) {                       \
    memcpy(p->buf + p->bpos, (bufptr), bufsize);             \
    p->bpos += bufsize;                                      \
    p->buf[p->bpos]= '\0';                                   \
  } else {  jl_printbuf_memappend(p, (bufptr), bufsize); }      \
} while (0)

extern int
jl_sprintbuf(struct jl_printbuf *p, const char *msg, ...);

extern void
jl_printbuf_reset(struct jl_printbuf *p);

extern void
jl_printbuf_free(struct jl_printbuf *p);

#ifdef __cplusplus
}
#endif

#endif
