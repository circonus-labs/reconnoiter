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

static inline void add_timeval(struct timeval a, struct timeval b,
                               struct timeval *out)
{
  out->tv_usec = a.tv_usec + b.tv_usec;
  if (out->tv_usec > 1000000L) {
    a.tv_sec++;
    out->tv_usec -= 1000000L;
  }
  out->tv_sec = a.tv_sec + b.tv_sec;
}

#ifndef HAVE_UUID_UNPARSE_LOWER
/* Sigh, need to implement out own */
#include <uuid/uuid.h>
#include <ctype.h>
static inline void uuid_unparse_lower(uuid_t in, char *out) {
  int i;
  uuid_unparse(in, out);
  for(i=0;i<36;i++) out[i] = tolower(out[i]);
}
#endif

#ifdef HAVE_TERMIO_H
#define USE_TERMIO
#endif

#ifndef MIN
#define MIN(x,y)  ((x) < (y) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x,y)  ((x) > (y) ? (x) : (y))
#endif
#ifndef SUN_LEN
#define SUN_LEN(ptr) (sizeof(*(ptr)) - sizeof((ptr)->sun_path) + strlen((ptr)->sun_path))
#endif

#endif
