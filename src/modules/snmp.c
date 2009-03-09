/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <math.h>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;
static noit_hash_table target_sessions = NOIT_HASH_EMPTY;

struct target_session {
  void *sess_handle;
  eventer_t timeoutevent;
  int fd;
  int in_table;
  int refcnt;
};

struct snmp_check_closure {
  noit_module_t *self;
  noit_check_t *check;
};

struct check_info {
  int reqid;
  int timedout;
  struct {
     char *confname;
     char *oidname;
     oid oid[MAX_OID_LEN];
     size_t oidlen;
  } *oids;
  int noids;
  eventer_t timeoutevent;
  noit_module_t *self;
  noit_check_t *check;
};

/* We hold struct check_info's in there key's by their reqid.
 *   If they timeout, we remove them.
 *
 *   When SNMP queries complete, we look them up, if we find them
 *   then we know we can remove the timeout and  complete the check. 
 *   If we don't find them, the timeout fired and removed the check.
 */
noit_hash_table active_checks = NOIT_HASH_EMPTY;
static void add_check(struct check_info *c) {
  noit_hash_store(&active_checks, (char *)&c->reqid, sizeof(c->reqid), c);
}
static struct check_info *get_check(int reqid) {
  struct check_info *c;
  if(noit_hash_retrieve(&active_checks, (char *)&reqid, sizeof(reqid),
                        (void **)&c))
    return c;
  return NULL;
}
static void remove_check(struct check_info *c) {
  noit_hash_delete(&active_checks, (char *)&c->reqid, sizeof(c->reqid),
                   NULL, NULL);
}

static int noit_snmp_init(noit_module_t *self) {
  register_mib_handlers();
  read_premib_configs();
  read_configs();
  init_snmp("noitd");
  return 0;
}

/* Handling of results */
static void noit_snmp_log_results(noit_module_t *self, noit_check_t *check,
                                  struct snmp_pdu *pdu) {
  struct check_info *info = check->closure;
  struct variable_list *vars;
  struct timeval duration;
  char buff[128];
  stats_t current;
  int nresults = 0;

  noit_check_stats_clear(&current);

  if(pdu)
    for(vars = pdu->variables; vars; vars = vars->next_variable)
      nresults++;

  gettimeofday(&current.whence, NULL);
  sub_timeval(current.whence, check->last_fire_time, &duration);
  current.duration = duration.tv_sec * 1000 + duration.tv_usec / 1000;
  current.available = pdu ? NP_AVAILABLE : NP_UNAVAILABLE;
  current.state = (nresults == info->noids) ? NP_GOOD : NP_BAD;
  snprintf(buff, sizeof(buff), "%d/%d gets", nresults, info->noids);
  current.status = buff;

  /* We have no results over which to iterate. */
  if(!pdu) {
    noit_check_set_stats(self, check, &current);
    return;
  }

  /* manipulate the information ourselves */
  nresults = 0;
  for(vars = pdu->variables; vars; vars = vars->next_variable) {
    char *sp;
    int oid_idx;
    double float_conv;
    u_int64_t u64;
    int64_t i64;
    char *endptr;
    char varbuff[256];

    /* find the oid to which this is the response */
    oid_idx = nresults; /* our current idx is the most likely */
    if(info->oids[oid_idx].oidlen != vars->name_length ||
       memcmp(info->oids[oid_idx].oid, vars->name,
              vars->name_length * sizeof(oid))) {
      /* Not the most obvious guess */
      for(oid_idx = info->noids - 1; oid_idx >= 0; oid_idx--) {
        if(info->oids[oid_idx].oidlen == vars->name_length &&
           memcmp(info->oids[oid_idx].oid, vars->name,
                  vars->name_length * sizeof(oid))) break;
      }
    }
    if(oid_idx < 0) {
      snprint_variable(varbuff, sizeof(varbuff),
                       vars->name, vars->name_length, vars);
      noitL(nlerr, "Unexpected oid results to %s`%s`%s: %s\n",
            check->target, check->module, check->name, varbuff);
      nresults++;
      continue;
    }
    
#define SETM(a,b) noit_stats_set_metric(&current, \
                                        info->oids[oid_idx].confname, a, b)
    switch(vars->type) {
      case ASN_OCTET_STR:
        sp = malloc(1 + vars->val_len);
        memcpy(sp, vars->val.string, vars->val_len);
        sp[vars->val_len] = '\0';
        SETM(METRIC_STRING, sp);
        free(sp);
        break;
      case ASN_INTEGER:
      case ASN_GAUGE:
        SETM(METRIC_INT32, vars->val.integer);
        break;
      case ASN_TIMETICKS:
      case ASN_COUNTER:
        SETM(METRIC_UINT32, vars->val.integer);
        break;
      case ASN_INTEGER64:
        printI64(varbuff, vars->val.counter64);
        i64 = strtoll(varbuff, &endptr, 10);
        SETM(METRIC_INT64, (varbuff == endptr) ? NULL : &i64);
        break;
      case ASN_COUNTER64:
        printU64(varbuff, vars->val.counter64);
        u64 = strtoull(varbuff, &endptr, 10);
        SETM(METRIC_UINT64, (varbuff == endptr) ? NULL : &u64);
        break;
      case ASN_FLOAT:
        if(vars->val.floatVal) float_conv = *(vars->val.floatVal);
        SETM(METRIC_DOUBLE, vars->val.floatVal ? &float_conv : NULL);
        break;
      case ASN_DOUBLE:
        SETM(METRIC_DOUBLE, vars->val.doubleVal);
        break;
      default:
        snprint_variable(varbuff, sizeof(varbuff), vars->name, vars->name_length, vars);
        printf("%s!\n", varbuff);
        /* Advance passed the first space and use that unless there
         * is no space or we have no more string left.
         */
        sp = strchr(varbuff, ' ');
        if(sp) sp++;
        SETM(METRIC_STRING, (sp && *sp) ? sp : NULL);
    }
    nresults++;
  }
  noit_check_set_stats(self, check, &current);
}

struct target_session *
_get_target_session(char *target) {
  struct target_session *ts;
  if(!noit_hash_retrieve(&target_sessions,
                         target, strlen(target), (void **)&ts)) {
    ts = calloc(1, sizeof(*ts));
    ts->fd = -1;
    ts->refcnt = 0;
    ts->in_table = 1;
    noit_hash_store(&target_sessions,
                    strdup(target), strlen(target), ts);
  }
  return ts;
}

static int noit_snmp_session_cleanse(struct target_session *ts) {
  if(ts->refcnt == 0 && ts->sess_handle) {
    eventer_remove_fd(ts->fd);
    if(ts->timeoutevent) eventer_remove(ts->timeoutevent);
    ts->timeoutevent = NULL;
    snmp_sess_close(ts->sess_handle);
    ts->sess_handle = NULL;
    if(!ts->in_table) {
      free(ts);
    }
    return 1;
  }
  return 0;
}

static int noit_snmp_session_timeout(eventer_t e, int mask, void *closure,
                                     struct timeval *now) {
  struct target_session *ts = closure;
  snmp_sess_timeout(ts->sess_handle);
  noit_snmp_session_cleanse(ts);
  return 0;
}

static int noit_snmp_check_timeout(eventer_t e, int mask, void *closure,
                                   struct timeval *now) {
  struct check_info *info = closure;
  info->timedout = 1;
  remove_check(info);
  /* Log our findings */
  noit_snmp_log_results(info->self, info->check, NULL);
  info->check->flags &= ~NP_RUNNING;
  return 0;
}

static void _set_ts_timeout(struct target_session *ts, struct timeval *t) {
  struct timeval now;
  eventer_t e = NULL;
  if(ts->timeoutevent) e = eventer_remove(ts->timeoutevent);
  ts->timeoutevent = NULL;
  if(!t) return;

  gettimeofday(&now, NULL);
  if(!e) e = eventer_alloc();
  e->callback = noit_snmp_session_timeout;
  e->closure = ts;
  e->mask = EVENTER_TIMER;
  add_timeval(now, *t, &e->whence);
  ts->timeoutevent = e;
  eventer_add(e);
}

static int noit_snmp_handler(eventer_t e, int mask, void *closure,
                             struct timeval *now) {
  fd_set fdset;
  int fds, block = 0;
  struct timeval timeout = { 0, 0 };
  struct target_session *ts = closure;
  FD_ZERO(&fdset);
  FD_SET(e->fd, &fdset);
  fds = e->fd + 1;
  snmp_sess_read(ts->sess_handle, &fdset);
  if(noit_snmp_session_cleanse(ts))
    return 0;
  snmp_sess_select_info(ts->sess_handle, &fds, &fdset, &timeout, &block);
  _set_ts_timeout(ts, block ? &timeout : NULL);
  return EVENTER_READ | EVENTER_EXCEPTION;
}
static int noit_snmp_asynch_response(int operation, struct snmp_session *sp,
                                     int reqid, struct snmp_pdu *pdu,
                                     void *magic) {
  struct check_info *info;
  struct target_session *ts = magic;

  /* We don't deal with refcnt hitting zero here.  We could only be hit from
   * the snmp read/timeout stuff.  Handle it there.
   */
  ts->refcnt--; 

  info = get_check(reqid);
  if(!info) return 1;
  remove_check(info);
  if(info->timeoutevent) {
    eventer_remove(info->timeoutevent);
    eventer_free(info->timeoutevent);
    info->timeoutevent = NULL;
  }

  /* Log our findings */
  noit_snmp_log_results(info->self, info->check, pdu);
  info->check->flags &= ~NP_RUNNING;
  return 1;
}

static void noit_snmp_sess_open(struct target_session *ts,
                                noit_check_t *check) {
  const char *community;
  struct snmp_session sess;
  snmp_sess_init(&sess);
  sess.version = SNMP_VERSION_2c;
  sess.peername = check->target;
  if(!noit_hash_retrieve(check->config, "community", strlen("community"),
                         (void **)&community)) {
    community = "public";
  }
  sess.community = (unsigned char *)community;
  sess.community_len = strlen(community);
  sess.callback = noit_snmp_asynch_response;
  sess.callback_magic = ts;
  ts->sess_handle = snmp_sess_open(&sess);
}

static int noit_snmp_fill_req(struct snmp_pdu *req, noit_check_t *check) {
  int i, klen;
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *name, *value;
  struct check_info *info = check->closure;
  noit_hash_table check_attrs_hash = NOIT_HASH_EMPTY;

  /* Toss the old set and bail if we have zero */
  if(info->oids) {
    for(i=0; i<info->noids;i++) {
      if(info->oids[i].confname) free(info->oids[i].confname);
      if(info->oids[i].oidname) free(info->oids[i].oidname);
    }
    free(info->oids);
  }
  info->noids = 0;
  info->oids = NULL;

  /* Figure our how many. */
  while(noit_hash_next(check->config, &iter, &name, &klen, (void **)&value)) {
    if(!strncasecmp(name, "oid_", 4)) {
      info->noids++;
    }
  }

  if(info->noids == 0) return 0;

  /* Create a hash of important check attributes */
  noit_check_make_attrs(check, &check_attrs_hash);

  /* Fill out the new set of required oids */
  info->oids = calloc(info->noids, sizeof(*info->oids));
  memset(&iter, 0, sizeof(iter));
  i = 0;
  while(noit_hash_next(check->config, &iter, &name, &klen, (void **)&value)) {
    if(!strncasecmp(name, "oid_", 4)) {
      char oidbuff[128];
      name += 4;
      info->oids[i].confname = strdup(name);
      noit_check_interpolate(oidbuff, sizeof(oidbuff), value,
                             &check_attrs_hash, check->config);
      info->oids[i].oidname = strdup(oidbuff);
      info->oids[i].oidlen = MAX_OID_LEN;
      get_node(oidbuff, info->oids[i].oid, &info->oids[i].oidlen);
      snmp_add_null_var(req, info->oids[i].oid, info->oids[i].oidlen);
      i++;
    }
  }
  assert(info->noids == i);
  noit_hash_destroy(&check_attrs_hash, NULL, NULL);
  return info->noids;
}
static int noit_snmp_send(noit_module_t *self, noit_check_t *check) {
  struct snmp_pdu *req;
  struct target_session *ts;
  struct check_info *info = check->closure;

  info->self = self;
  info->check = check;
  info->timedout = 0;

  check->flags |= NP_RUNNING;
  ts = _get_target_session(check->target);
  gettimeofday(&check->last_fire_time, NULL);
  if(!ts->refcnt) {
    eventer_t newe;
    int fds, block;
    struct timeval timeout;
    fd_set fdset;
    noit_snmp_sess_open(ts, check);
    block = 0;
    fds = 0;
    FD_ZERO(&fdset);
    snmp_sess_select_info(ts->sess_handle, &fds, &fdset, &timeout, &block);
    assert(fds > 0);
    ts->fd = fds-1;
    newe = eventer_alloc();
    newe->fd = ts->fd;
    newe->callback = noit_snmp_handler;
    newe->closure = ts;
    newe->mask = EVENTER_READ | EVENTER_EXCEPTION;
    eventer_add(newe);
  }
  if(!ts->sess_handle) {
    /* Error */
    /* No need to do anything, this will be handled in the else below */
  }
  ts->refcnt++; /* Increment here, decrement when this check completes */

  req = snmp_pdu_create(SNMP_MSG_GET);
  if(req) noit_snmp_fill_req(req, check);
  /* Setup out snmp requests */
  if(ts->sess_handle && req &&
     (info->reqid = snmp_sess_send(ts->sess_handle, req)) != 0) {
    struct timeval when, to;
    info->timeoutevent = eventer_alloc();
    info->timeoutevent->callback = noit_snmp_check_timeout;
    info->timeoutevent->closure = info;
    info->timeoutevent->mask = EVENTER_TIMER;

    gettimeofday(&when, NULL);
    to.tv_sec = check->timeout / 1000;
    to.tv_usec = (check->timeout % 1000) * 1000;
    add_timeval(when, to, &info->timeoutevent->whence);
    eventer_add(info->timeoutevent);
    add_check(info);
  }
  else {
    ts->refcnt--;
     noit_snmp_session_cleanse(ts);
    /* Error */
    if(req) snmp_free_pdu(req);
    /* Log our findings */
    noit_snmp_log_results(self, check, NULL);
    check->flags &= ~NP_RUNNING;
  }
  return 0;
}

static int noit_snmp_initiate_check(noit_module_t *self, noit_check_t *check,
                                    int once, noit_check_t *cause) {
  if(!check->closure) check->closure = calloc(1, sizeof(struct check_info));
  INITIATE_CHECK(noit_snmp_send, self, check);
  return 0;
}

static int noit_snmp_config(noit_module_t *self, noit_hash_table *config) {
  return 0;
}
static int noit_snmp_onload(noit_image_t *self) {
  nlerr = noit_log_stream_find("error/snmp");
  nldeb = noit_log_stream_find("debug/snmp");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;
  eventer_name_callback("noit_snmp/check_timeout", noit_snmp_check_timeout);
  eventer_name_callback("noit_snmp/session_timeout", noit_snmp_session_timeout);
  eventer_name_callback("noit_snmp/handler", noit_snmp_handler);
  return 0;
}

#include "snmp.xmlh"
noit_module_t snmp = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "snmp",
    "SNMP collection",
    snmp_xml_description,
    noit_snmp_onload
  },
  noit_snmp_config,
  noit_snmp_init,
  noit_snmp_initiate_check,
  NULL /* noit_snmp_cleanup */
};

