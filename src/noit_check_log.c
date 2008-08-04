/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include <uuid/uuid.h>
#include <netinet/in.h>

#include "noit_check.h"
#include "noit_filters.h"
#include "utils/noit_log.h"
#include "jlog/jlog.h"

/* Log format is tab delimited:
 * NOIT CONFIG (implemented in noit_conf.c):
 *  'n' TIMESTAMP strlen(xmlconfig) base64(gzip(xmlconfig))
 *
 * CHECK:
 *  'C' TIMESTAMP UUID TARGET MODULE NAME
 *
 * STATUS:
 *  'S' TIMESTAMP UUID STATE AVAILABILITY DURATION STATUS_MESSAGE
 *
 * METRICS:
 *  'M' TIMESTAMP UUID NAME TYPE VALUE
 */

static noit_log_stream_t check_log = NULL;
static noit_log_stream_t status_log = NULL;
static noit_log_stream_t metrics_log = NULL;
#define SECPART(a) ((unsigned long)(a)->tv_sec)
#define MSECPART(a) ((unsigned long)((a)->tv_usec / 1000))

static void
handle_extra_feeds(noit_check_t *check,
                   void (*log_f)(noit_log_stream_t ls, noit_check_t *check)) {
  noit_log_stream_t ls;
  noit_skiplist_node *curr, *next;
  const char *feed_name;

  if(!check->feeds) return;
  curr = next = noit_skiplist_getlist(check->feeds);
  while(curr) {
    /* We advance next here (before we try to use curr).
     * We may need to remove the node we're looking at and that would
     * disturb the iterator, so advance in advance. */
    noit_skiplist_next(check->feeds, &next);
    feed_name = (char *)curr->data;
    ls = noit_log_stream_find(feed_name);
    if(ls) log_f(ls, check);
    else {
      noit_skiplisti_remove(check->feeds, curr, free);
    }
    curr = next;
  }
  /* We're done... we may have destroyed the last feed.
   * that combined with transience means we should kill the check */
  noit_check_transient_remove_feed(check, NULL);
}

static void
_noit_check_log_check(noit_log_stream_t ls,
                      noit_check_t *check) {
  struct timeval __now;
  char uuid_str[37];

  uuid_unparse_lower(check->checkid, uuid_str);
  gettimeofday(&__now, NULL);
  noitL(ls, "C\t%lu.%03lu\t%s\t%s\t%s\t%s\n",
        SECPART(&__now), MSECPART(&__now),
        uuid_str, check->target, check->module, check->name);
}

void
noit_check_log_check(noit_check_t *check) {
  handle_extra_feeds(check, _noit_check_log_check);
  if(!(check->flags & NP_TRANSIENT)) {
    SETUP_LOG(check, return);
    _noit_check_log_check(check_log, check);
  }
}

static void
_noit_check_log_status(noit_log_stream_t ls,
                       noit_check_t *check) {
  stats_t *c;
  char uuid_str[37];

  uuid_unparse_lower(check->checkid, uuid_str);
  c = &check->stats.current;
  noitL(ls, "S\t%lu.%03lu\t%s\t%c\t%c\t%d\t%s\n",
        SECPART(&c->whence), MSECPART(&c->whence), uuid_str,
        (char)c->state, (char)c->available, c->duration, c->status);
}
void
noit_check_log_status(noit_check_t *check) {
  handle_extra_feeds(check, _noit_check_log_status);
  if(!(check->flags & NP_TRANSIENT)) {
    SETUP_LOG(status, return);
    _noit_check_log_status(status_log, check);
  }
}
static void
_noit_check_log_metrics(noit_log_stream_t ls, noit_check_t *check) {
  char uuid_str[37];
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *key;
  int klen;
  metric_t *m;
  stats_t *c;

  uuid_unparse_lower(check->checkid, uuid_str);
  c = &check->stats.current;
  while(noit_hash_next(&c->metrics, &iter, &key, &klen, (void **)&m)) {
    /* If we apply the filter set and it returns false, we don't log */
    if(!noit_apply_filterset(check->filterset, check, m)) continue;

    if(!m->metric_value.s) { /* they are all null */
      noitL(ls, "M\t%lu.%03lu\t%s\t%s\t%c\t[[null]]\n",
            SECPART(&c->whence), MSECPART(&c->whence), uuid_str,
            m->metric_name, m->metric_type);
    }
    else {
      switch(m->metric_type) {
        case METRIC_INT32:
          noitL(ls, "M\t%lu.%03lu\t%s\t%s\t%c\t%d\n",
                SECPART(&c->whence), MSECPART(&c->whence), uuid_str,
                m->metric_name, m->metric_type, *(m->metric_value.i));
          break;
        case METRIC_UINT32:
          noitL(ls, "M\t%lu.%03lu\t%s\t%s\t%c\t%u\n",
                SECPART(&c->whence), MSECPART(&c->whence), uuid_str,
                m->metric_name, m->metric_type, *(m->metric_value.I));
          break;
        case METRIC_INT64:
          noitL(ls, "M\t%lu.%03lu\t%s\t%s\t%c\t%lld\n",
                SECPART(&c->whence), MSECPART(&c->whence), uuid_str,
                m->metric_name, m->metric_type, *(m->metric_value.l));
          break;
        case METRIC_UINT64:
          noitL(ls, "M\t%lu.%03lu\t%s\t%s\t%c\t%llu\n",
                SECPART(&c->whence), MSECPART(&c->whence), uuid_str,
                m->metric_name, m->metric_type, *(m->metric_value.L));
          break;
        case METRIC_DOUBLE:
          noitL(ls, "M\t%lu.%03lu\t%s\t%s\t%c\t%.12e\n",
                SECPART(&c->whence), MSECPART(&c->whence), uuid_str,
                m->metric_name, m->metric_type, *(m->metric_value.n));
          break;
        case METRIC_STRING:
          noitL(ls, "M\t%lu.%03lu\t%s\t%s\t%c\t%s\n",
                SECPART(&c->whence), MSECPART(&c->whence), uuid_str,
                m->metric_name, m->metric_type, m->metric_value.s);
          break;
        default:
          noitL(noit_error, "Unknown metric type '%c' 0x%x\n",
                m->metric_type, m->metric_type);
      }
    }
  }
}
void
noit_check_log_metrics(noit_check_t *check) {
  handle_extra_feeds(check, _noit_check_log_metrics);
  if(!(check->flags & NP_TRANSIENT)) {
    SETUP_LOG(metrics, return);
    _noit_check_log_metrics(metrics_log, check);
  }
}

int
noit_stats_snprint_metric(char *b, int l, metric_t *m) {
  int rv;
  if(!m->metric_value.s) { /* they are all null */
    rv = snprintf(b, l, "%s[%c] = [[null]]", m->metric_name, m->metric_type);
  }
  else {
    switch(m->metric_type) {
      case METRIC_INT32:
        rv = snprintf(b, l, "%s[%c] = %d",
                      m->metric_name, m->metric_type, *(m->metric_value.i));
        break;
      case METRIC_UINT32:
        rv = snprintf(b, l, "%s[%c] = %u",
                      m->metric_name, m->metric_type, *(m->metric_value.I));
        break;
      case METRIC_INT64:
        rv = snprintf(b, l, "%s[%c] = %lld",
                      m->metric_name, m->metric_type, *(m->metric_value.l));
        break;
      case METRIC_UINT64:
        rv = snprintf(b, l, "%s[%c] = %llu",
                      m->metric_name, m->metric_type, *(m->metric_value.L));
        break;
      case METRIC_DOUBLE:
        rv = snprintf(b, l, "%s[%c] = %.12e",
                      m->metric_name, m->metric_type, *(m->metric_value.n));
        break;
      case METRIC_STRING:
        rv = snprintf(b, l, "%s[%c] = %s",
                      m->metric_name, m->metric_type, m->metric_value.s);
        break;
      default:
        rv = snprintf(b, l, "%s has unknown metric type 0%02x",
                      m->metric_name, m->metric_type);
    }
  }
  return rv;
}
