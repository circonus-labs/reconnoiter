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

/* This is for udns */
#ifdef HAVE_INET_PTON
#ifdef HAVE_INET_NTOP
#define HAVE_INET_PTON_NTOP 1
#endif
#endif
/* udns checks for IPv6, noit doesn't work without it */
#define HAVE_IPv6

#include <stdio.h>

static inline int noit_build_version(char *buff, int len) {
  const char *v = NOIT_HEADURL;
  const char *start, *end, *ns;
  if(NULL != (start = strstr(v, "reconnoiter/"))) {
    start += strlen("reconnoiter/");
    if(NULL != (end = strstr(start, "/src"))) {
      ns = strchr(start, '/'); /* necessarily non-NULL */
      ns++;
      if(!strncmp(start, "trunk/", 6))
        return snprintf(buff, len, "trunk.%s", NOIT_SVNVERSION);
      if(!strncmp(start, "branches/", 9))
        return snprintf(buff, len, "b_%.*s.%s", (int)(end - ns), ns, NOIT_SVNVERSION);
      if(!strncmp(start, "tags/", 5))
        return snprintf(buff, len, "%.*s.%s", (int)(end - ns), ns, NOIT_SVNVERSION);
    }
  }
  return snprintf(buff, len, "unknown.%s", NOIT_SVNVERSION);
}

#endif
