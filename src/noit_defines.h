#ifndef _NOIT_DEFINES_H
#define _NOIT_DEFINES_H

#include "noit_config.h"

#define API_EXPORT(type) extern type

static inline int compare_timeval(struct timeval a, struct timeval b) {
  if (a.tv_sec < b.tv_sec) return -1;
  if (a.tv_sec > b.tv_sec) return 1;
  if (a.tv_usec < b.tv_usec) return -1;
  if (a.tv_usec > b.tv_usec) return 1;
  return 0;
}

static inline void sub_timeval(struct timeval a, struct timeval b,
                               struct timeval *out)
{
  out->tv_usec = a.tv_usec - b.tv_usec;
  if (out->tv_usec < 0L) {
    a.tv_sec--;
    out->tv_usec += 1000000L;
  }
  out->tv_sec = a.tv_sec - b.tv_sec;
  if (out->tv_sec < 0L) {
    out->tv_sec++;
    out->tv_usec -= 1000000L;
  }
}

#endif
