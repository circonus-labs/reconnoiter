/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _UTILS_NOIT_LOG_H
#define _UTILS_NOIT_LOG_H

#include "noit_defines.h"
#include <pthread.h>
#include <stdarg.h>
#include "utils/noit_hash.h"

struct _noit_log_stream_outlet_list {
  struct _noit_log_stream *outlet;
  struct _noit_log_stream_outlet_list *next;
};

typedef struct {
  int (*openop)(struct _noit_log_stream *);
  int (*reopenop)(struct _noit_log_stream *);
  int (*writeop)(struct _noit_log_stream *, const void *, size_t);
  int (*closeop)(struct _noit_log_stream *);
} logops_t;

typedef struct _noit_log_stream {
  char *type;
  char *name;
  int enabled;
  char *path;
  logops_t *ops;
  void *op_ctx;
  noit_hash_table *config;
  struct _noit_log_stream_outlet_list *outlets;
} * noit_log_stream_t;

extern noit_log_stream_t noit_stderr;
extern noit_log_stream_t noit_debug;
extern noit_log_stream_t noit_error;

API_EXPORT(void) noit_log_init();
API_EXPORT(void) noit_register_logops(const char *name, logops_t *ops);
API_EXPORT(noit_log_stream_t)
  noit_log_stream_new(const char *, const char *, const char *,
                      void *, noit_hash_table *);
API_EXPORT(noit_log_stream_t)
  noit_log_stream_new_on_fd(const char *, int, noit_hash_table *);
API_EXPORT(noit_log_stream_t)
  noit_log_stream_new_on_file(const char *, noit_hash_table *);
API_EXPORT(noit_log_stream_t) noit_log_stream_find(const char *);
API_EXPORT(void) noit_log_stream_remove(const char *name);
API_EXPORT(void) noit_log_stream_add_stream(noit_log_stream_t ls,
                                            noit_log_stream_t outlet);
API_EXPORT(noit_log_stream_t)
                 noit_log_stream_remove_stream(noit_log_stream_t ls,
                                               const char *name);
API_EXPORT(void) noit_log_stream_reopen(noit_log_stream_t ls);
API_EXPORT(void) noit_log_stream_close(noit_log_stream_t ls);
API_EXPORT(void) noit_log_stream_free(noit_log_stream_t ls);
API_EXPORT(void) noit_vlog(noit_log_stream_t ls, struct timeval *,
                           const char *file, int line,
                           const char *format, va_list arg);
API_EXPORT(void) noit_log(noit_log_stream_t ls, struct timeval *,
                          const char *file, int line,
                          const char *format, ...)
#ifdef __GNUC__
  __attribute__ ((format (printf, 5, 6)))
#endif
  ;

#define noitLT(ls, t, args...) \
  if(ls && ls->enabled) noit_log(ls, t, __FILE__, __LINE__, args)
#define noitL(ls, args...) do { \
  if(ls && ls->enabled) { \
    struct timeval __noitL_now; \
    gettimeofday(&__noitL_now, NULL); \
    noit_log(ls, &__noitL_now, __FILE__, __LINE__, args); \
  } \
} while(0)

#define SETUP_LOG(a, b) do { if(!a##_log) a##_log = noit_log_stream_find(#a); \
                             if(!a##_log) { b; } } while(0)

#endif
