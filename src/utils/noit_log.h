/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#ifndef _UTILS_NOIT_LOG_H
#define _UTILS_NOIT_LOG_H

#include "noit_defines.h"
#include <pthread.h>
#include <stdarg.h>

struct _noit_log_stream_outlet_list {
  struct _noit_log_stream *outlet;
  struct _noit_log_stream_outlet_list *next;
};
typedef struct _noit_log_stream {
  char *name;
  int enabled;
  int fd;
  char *path;
  struct _noit_log_stream_outlet_list *outlets;
} * noit_log_stream_t;

extern noit_log_stream_t noit_stderr;
extern noit_log_stream_t noit_debug;
extern noit_log_stream_t noit_error;

API_EXPORT(void) noit_log_init();
API_EXPORT(noit_log_stream_t) noit_log_stream_new();
API_EXPORT(noit_log_stream_t) noit_log_stream_new_on_fd(const char *name, int fd);
API_EXPORT(noit_log_stream_t) noit_log_stream_new_on_file(const char *path);
API_EXPORT(noit_log_stream_t) noit_log_stream_find(const char *name);
API_EXPORT(void) noit_log_stream_add_stream(noit_log_stream_t ls,
                                            noit_log_stream_t outlet);
API_EXPORT(noit_log_stream_t)
                 noit_log_stream_remove_stream(noit_log_stream_t ls,
                                               const char *name);
API_EXPORT(void) noit_log_stream_reopen(noit_log_stream_t ls);
API_EXPORT(void) noit_log_stream_close(noit_log_stream_t ls);
API_EXPORT(void) noit_log_stream_free(noit_log_stream_t ls);
API_EXPORT(void) noit_vlog(noit_log_stream_t ls, struct timeval *,
                           const char *format, va_list arg);
API_EXPORT(void) noit_log(noit_log_stream_t ls, struct timeval *,
                          const char *format, ...)
#ifdef __GNUC__
  __attribute__ ((format (printf, 3, 4)))
#endif
  ;

#endif
