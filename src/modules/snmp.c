/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "noit_defines.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <math.h>
#include <ctype.h>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"

static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;
static int __snmp_initialize_once = 0;

#define SNMPV2_TRAPS_PREFIX     SNMP_OID_SNMPMODULES,1,1,5
oid trap_prefix[]    = { SNMPV2_TRAPS_PREFIX };
oid cold_start_oid[] = { SNMPV2_TRAPS_PREFIX, 1 };  /* SNMPv2-MIB */
oid warm_start_oid[] = { SNMPV2_TRAPS_PREFIX, 2 };  /* SNMPv2-MIB */
oid link_down_oid[]  = { SNMPV2_TRAPS_PREFIX, 3 };  /* IF-MIB */
oid link_up_oid[]    = { SNMPV2_TRAPS_PREFIX, 4 };  /* IF-MIB */
oid auth_fail_oid[]  = { SNMPV2_TRAPS_PREFIX, 5 };  /* SNMPv2-MIB */
oid egp_xxx_oid[]    = { SNMPV2_TRAPS_PREFIX, 99 }; /* ??? */

#define SNMPV2_TRAP_OBJS_PREFIX SNMP_OID_SNMPMODULES,1,1,4
oid snmptrap_oid[] = { SNMPV2_TRAP_OBJS_PREFIX, 1, 0 };
size_t snmptrap_oid_len = OID_LENGTH(snmptrap_oid);
oid snmptrapenterprise_oid[] = { SNMPV2_TRAP_OBJS_PREFIX, 3, 0 };
size_t snmptrapenterprise_oid_len = OID_LENGTH(snmptrapenterprise_oid);
oid sysuptime_oid[] = { SNMP_OID_MIB2, 1, 3, 0 };
size_t sysuptime_oid_len = OID_LENGTH(sysuptime_oid);

#define SNMPV2_COMM_OBJS_PREFIX SNMP_OID_SNMPMODULES,18,1
oid agentaddr_oid[] = { SNMPV2_COMM_OBJS_PREFIX, 3, 0 };
size_t agentaddr_oid_len = OID_LENGTH(agentaddr_oid);
oid community_oid[] = { SNMPV2_COMM_OBJS_PREFIX, 4, 0 };
size_t community_oid_len = OID_LENGTH(community_oid);

#define RECONNOITER_PREFIX     SNMP_OID_ENTERPRISES,32863,1
oid reconnoiter_oid[] = { RECONNOITER_PREFIX };
size_t reconnoiter_oid_len = OID_LENGTH(reconnoiter_oid);
oid reconnoiter_check_prefix_oid[] = { RECONNOITER_PREFIX,1,1 };
size_t reconnoiter_check_prefix_oid_len =
  OID_LENGTH(reconnoiter_check_prefix_oid);
size_t reconnoiter_check_oid_len = OID_LENGTH(reconnoiter_check_prefix_oid) + 8;
oid reconnoiter_metric_prefix_oid[] = { RECONNOITER_PREFIX,1,2 };
size_t reconnoiter_metric_prefix_oid_len =
  OID_LENGTH(reconnoiter_metric_prefix_oid);

oid reconnoiter_check_status_oid[] = { RECONNOITER_PREFIX,1,3};
size_t reconnoiter_check_status_oid_len =
  OID_LENGTH(reconnoiter_check_status_oid);
oid reconnoiter_check_state_oid[] = { RECONNOITER_PREFIX,1,3,1};
size_t reconnoiter_check_state_oid_len =
  OID_LENGTH(reconnoiter_check_state_oid);
oid reconnoiter_check_state_unknown_oid[] = { RECONNOITER_PREFIX,1,3,1,0};
oid reconnoiter_check_state_good_oid[] = { RECONNOITER_PREFIX,1,3,1,1};
oid reconnoiter_check_state_bad_oid[] = { RECONNOITER_PREFIX,1,3,1,2};
size_t reconnoiter_check_state_val_len =
  OID_LENGTH(reconnoiter_check_state_unknown_oid);
/* Boolean */
oid reconnoiter_check_available_oid[] = { RECONNOITER_PREFIX,1,3,2};
size_t reconnoiter_check_available_oid_len =
  OID_LENGTH(reconnoiter_check_available_oid);
oid reconnoiter_check_available_unknown_oid[] = { RECONNOITER_PREFIX,1,3,2,0};
oid reconnoiter_check_available_yes_oid[] = { RECONNOITER_PREFIX,1,3,2,1};
oid reconnoiter_check_available_no_oid[] = { RECONNOITER_PREFIX,1,3,2,2};
size_t reconnoiter_check_available_val_len =
  OID_LENGTH(reconnoiter_check_available_unknown_oid);
/* timeticks? gauge/unsigned? */
oid reconnoiter_check_duration_oid[] = { RECONNOITER_PREFIX,1,3,3};
size_t reconnoiter_check_duration_oid_len =
  OID_LENGTH(reconnoiter_check_duration_oid);
/* string */
oid reconnoiter_check_status_msg_oid[] = { RECONNOITER_PREFIX,1,3,4};
size_t reconnoiter_check_status_msg_oid_len =
  OID_LENGTH(reconnoiter_check_status_msg_oid);

typedef struct _mod_config {
  noit_hash_table *options;
  noit_hash_table target_sessions;
} snmp_mod_config_t;

struct target_session {
  void *sess_handle;
  noit_module_t *self;
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
  void *vc;
  if(noit_hash_retrieve(&active_checks, (char *)&reqid, sizeof(reqid), &vc))
    return (struct check_info *)vc;
  return NULL;
}
static void remove_check(struct check_info *c) {
  noit_hash_delete(&active_checks, (char *)&c->reqid, sizeof(c->reqid),
                   NULL, NULL);
}

struct target_session *
_get_target_session(noit_module_t *self, char *target) {
  void *vts;
  struct target_session *ts;
  snmp_mod_config_t *conf;
  conf = noit_module_get_userdata(self);
  if(!noit_hash_retrieve(&conf->target_sessions,
                         target, strlen(target), &vts)) {
    ts = calloc(1, sizeof(*ts));
    ts->self = self;
    ts->fd = -1;
    ts->refcnt = 0;
    ts->in_table = 1;
    noit_hash_store(&conf->target_sessions,
                    strdup(target), strlen(target), ts);
    vts = ts;
  }
  return (struct target_session *)vts;
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
      case SNMP_NOSUCHOBJECT:
      case SNMP_NOSUCHINSTANCE:
        SETM(METRIC_STRING, NULL);
        break;
      default:
        snprint_variable(varbuff, sizeof(varbuff), vars->name, vars->name_length, vars);
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

static int noit_snmp_session_cleanse(struct target_session *ts) {
  if(ts->refcnt == 0 && ts->sess_handle) {
    eventer_remove_fd(ts->fd);
    if(ts->timeoutevent) {
      eventer_remove(ts->timeoutevent);
      ts->timeoutevent = NULL;
    }
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
  if(ts->timeoutevent == e)
    ts->timeoutevent = NULL; /* this will be freed on return */
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
  if(ts->timeoutevent) {
    e = eventer_remove(ts->timeoutevent);
    ts->timeoutevent = NULL;
  }
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

/* This 'convert_v1pdu_to_v2' was cribbed directly from netsnmp */
static netsnmp_pdu *
convert_v1pdu_to_v2( netsnmp_pdu* template_v1pdu ) {
  netsnmp_pdu *template_v2pdu;
  netsnmp_variable_list *first_vb;
  netsnmp_variable_list *var;
  oid enterprise[MAX_OID_LEN];
  size_t enterprise_len;

  /*
   * Make a copy of the v1 Trap PDU
   *   before starting to convert this
   *   into a v2 Trap PDU.
   */
  template_v2pdu = snmp_clone_pdu( template_v1pdu);
  if(!template_v2pdu) {
    snmp_log(LOG_WARNING,
             "send_trap: failed to copy v2 template PDU\n");
    return NULL;
  }
  template_v2pdu->command = SNMP_MSG_TRAP2;
  first_vb = template_v2pdu->variables;

  /*
   * Insert an snmpTrapOID varbind before the original v1 varbind list
   *   either using one of the standard defined trap OIDs,
   *   or constructing this from the PDU enterprise & specific trap fields
   */
  if(template_v1pdu->trap_type == SNMP_TRAP_ENTERPRISESPECIFIC) {
    memcpy(enterprise, template_v1pdu->enterprise,
           template_v1pdu->enterprise_length*sizeof(oid));
    enterprise_len = template_v1pdu->enterprise_length;
    enterprise[enterprise_len++] = 0;
    enterprise[enterprise_len++] = template_v1pdu->specific_type;
  } else {
    memcpy(enterprise, cold_start_oid, sizeof(cold_start_oid));
    enterprise[9]  = template_v1pdu->trap_type+1;
    enterprise_len = sizeof(cold_start_oid)/sizeof(oid);
  }

  var = NULL;
  if(!snmp_varlist_add_variable(&var,
                                snmptrap_oid, snmptrap_oid_len,
                                ASN_OBJECT_ID,
                                (u_char*)enterprise,
                                enterprise_len*sizeof(oid))) {
    noitL(nlerr, "send_trap: failed to insert copied snmpTrapOID varbind\n");
    snmp_free_pdu(template_v2pdu);
    return NULL;
  }
  var->next_variable        = template_v2pdu->variables;
  template_v2pdu->variables = var;

  /*
   * Insert a sysUptime varbind at the head of the v2 varbind list
   */
  var = NULL;
  if(!snmp_varlist_add_variable(&var,
                                sysuptime_oid, sysuptime_oid_len,
                                ASN_TIMETICKS,
                                (u_char*)&(template_v1pdu->time), 
                                sizeof(template_v1pdu->time))) {
    noitL(nlerr, "send_trap: failed to insert copied sysUptime varbind\n");
    snmp_free_pdu(template_v2pdu);
    return NULL;
  }
  var->next_variable = template_v2pdu->variables;
  template_v2pdu->variables = var;

  /*
   * Append the other three conversion varbinds,
   *  (snmpTrapAgentAddr, snmpTrapCommunity & snmpTrapEnterprise)
   *  if they're not already present.
   *  But don't bomb out completely if there are problems.
   */
  var = find_varbind_in_list(template_v2pdu->variables,
                             agentaddr_oid, agentaddr_oid_len);
  if(!var && (template_v1pdu->agent_addr[0]
              || template_v1pdu->agent_addr[1]
              || template_v1pdu->agent_addr[2]
              || template_v1pdu->agent_addr[3])) {
    if(!snmp_varlist_add_variable(&(template_v2pdu->variables),
                                  agentaddr_oid, agentaddr_oid_len,
                                  ASN_IPADDRESS,
                                  (u_char*)&(template_v1pdu->agent_addr), 
                                  sizeof(template_v1pdu->agent_addr)))
      noitL(nlerr, "send_trap: failed to append snmpTrapAddr varbind\n");
  }
  var = find_varbind_in_list(template_v2pdu->variables,
                             community_oid, community_oid_len);
  if(!var && template_v1pdu->community) {
    if(!snmp_varlist_add_variable(&(template_v2pdu->variables),
                                  community_oid, community_oid_len,
                                  ASN_OCTET_STR,
                                  template_v1pdu->community, 
                                  template_v1pdu->community_len))
      noitL(nlerr, "send_trap: failed to append snmpTrapCommunity varbind\n");
  }
  var = find_varbind_in_list(template_v2pdu->variables,
                             snmptrapenterprise_oid,
                             snmptrapenterprise_oid_len);
  if(!var && 
     template_v1pdu->trap_type != SNMP_TRAP_ENTERPRISESPECIFIC) {
    if(!snmp_varlist_add_variable(&(template_v2pdu->variables),
                                  snmptrapenterprise_oid,
                                  snmptrapenterprise_oid_len,
                                  ASN_OBJECT_ID,
                                  (u_char*)template_v1pdu->enterprise, 
                                  template_v1pdu->enterprise_length*sizeof(oid)))
      noitL(nlerr, "send_trap: failed to append snmpEnterprise varbind\n");
  }
  return template_v2pdu;
}

static int noit_snmp_oid_to_checkid(oid *o, int l, uuid_t checkid, char *out) {
  int i;
  char _uuid_str[UUID_STR_LEN+1], *cp, *uuid_str;

  uuid_str = out ? out : _uuid_str;
  if(l != reconnoiter_check_oid_len) {
    noitL(nlerr, "unsupported (length) trap recieved\n");
    return -1;
  }
  if(netsnmp_oid_equals(o,
                        reconnoiter_check_prefix_oid_len,
                        reconnoiter_check_prefix_oid,
                        reconnoiter_check_prefix_oid_len) != 0) {
    noitL(nlerr, "unsupported (wrong namespace) trap recieved\n");
    return -1;
  }
  /* encode this as a uuid */
  cp = uuid_str;
  for(i=0;
      i < reconnoiter_check_oid_len - reconnoiter_check_prefix_oid_len;
      i++) {
    oid v = o[i + reconnoiter_check_prefix_oid_len];
    if(v < 0 || v > 0xffff) {
      noitL(nlerr, "trap target oid [%ld] out of range\n", v);
      return -1;
    }
    snprintf(cp, 5, "%04x", (unsigned short)(v & 0xffff));
    cp += 4;
    /* hyphens after index 1,2,3,4 */
    if(i > 0 && i < 5) *cp++ = '-';
  }
  if(uuid_parse(uuid_str, checkid) != 0) {
    noitL(nlerr, "unexpected error decoding trap uuid '%s'\n", uuid_str);
    return -1;
  }
  return 0;
}

#define isoid(a,b,c,d) (netsnmp_oid_equals(a,b,c,d) == 0)
#define isoidprefix(a,b,c,d) (netsnmp_oid_equals(a,MIN(b,d),c,d) == 0)
#define setstatus(st,soid,sv) \
  if(isoid(o,l,soid,reconnoiter_check_state_val_len)) current->st = sv

static int
noit_snmp_trapvars_to_stats(stats_t *current, netsnmp_variable_list *var) {
  if(isoidprefix(var->name, var->name_length, reconnoiter_check_status_oid,
                 reconnoiter_check_status_oid_len)) {
    if(var->type == ASN_OBJECT_ID) {
      if(isoid(var->name, var->name_length,
               reconnoiter_check_state_oid, reconnoiter_check_state_oid_len)) {
        oid *o = var->val.objid;
        size_t l = var->val_len / sizeof(*o);
        setstatus(state, reconnoiter_check_state_unknown_oid, NP_UNKNOWN);
        else setstatus(state, reconnoiter_check_state_good_oid, NP_GOOD);
        else setstatus(state, reconnoiter_check_state_bad_oid, NP_BAD);
        else return -1;
      }
      else if(isoid(var->name, var->name_length,
                    reconnoiter_check_available_oid,
                    reconnoiter_check_available_oid_len)) {
        oid *o = var->val.objid;
        size_t l = var->val_len / sizeof(*o);
        setstatus(available, reconnoiter_check_available_unknown_oid, NP_UNKNOWN);
        else setstatus(available, reconnoiter_check_available_yes_oid, NP_AVAILABLE);
        else setstatus(available, reconnoiter_check_available_no_oid, NP_UNAVAILABLE);
        else return -1;
      }
      else {
        /* We don't unerstand any other OBJECT_ID types */
        return -1;
      }
    }
    else if(var->type == ASN_UNSIGNED) {
      /* This is only for the duration (in ms) */
      if(isoid(var->name, var->name_length,
               reconnoiter_check_duration_oid,
               reconnoiter_check_duration_oid_len)) {
        current->duration = *(var->val.integer);
      }
      else
        return -1;
    }
    else if(var->type == ASN_OCTET_STR) {
      /* This is only for the status message */
      if(isoid(var->name, var->name_length,
               reconnoiter_check_status_msg_oid,
               reconnoiter_check_status_msg_oid_len)) {
        current->status = malloc(var->val_len + 1);
        memcpy(current->status, var->val.string, var->val_len);
        current->status[var->val_len] = '\0';
      }
      else
        return -1;
    }
    else {
      /* I don't understand any other type of status message */
      return -1;
    }
  }
  else if(isoidprefix(var->name, var->name_length,
                      reconnoiter_metric_prefix_oid,
                      reconnoiter_metric_prefix_oid_len)) {
    /* decode the metric and store the value */
    int i, len;
    u_int64_t u64;
    double doubleVal;
    char metric_name[128], buff[128], *cp;
    if(var->name_length <= reconnoiter_metric_prefix_oid_len) return -1;
    len = var->name[reconnoiter_metric_prefix_oid_len];
    if(var->name_length != (reconnoiter_metric_prefix_oid_len + 1 + len) ||
       len > sizeof(metric_name) - 1) {
      noitL(nlerr, "snmp trap, malformed metric name\n");
      return -1;
    }
    for(i=0;i<len;i++) {
      ((unsigned char *)metric_name)[i] =
        (unsigned char)var->name[reconnoiter_metric_prefix_oid_len + 1 + i];
      if(!isprint(metric_name[i])) {
        noitL(nlerr, "metric_name contains unprintable characters\n");
        return -1;
      }
    }
    metric_name[i] = '\0';
    switch(var->type) {
      case ASN_INTEGER:
      case ASN_UINTEGER:
      case ASN_TIMETICKS:
      case ASN_INTEGER64:
        noit_stats_set_metric(current, metric_name,
                              METRIC_INT64, var->val.integer);
        break;
      case ASN_COUNTER64:
        u64 = ((u_int64_t)var->val.counter64->high) << 32;
        u64 |= var->val.counter64->low;
        noit_stats_set_metric(current, metric_name,
                              METRIC_UINT64, &u64);
        break;
      case ASN_OPAQUE_FLOAT:
        doubleVal = (double)*var->val.floatVal;
        noit_stats_set_metric(current, metric_name,
                              METRIC_DOUBLE, &doubleVal);
        break;
      case ASN_OPAQUE_DOUBLE:
        noit_stats_set_metric(current, metric_name,
                              METRIC_DOUBLE, var->val.doubleVal);
        break;
      case ASN_OCTET_STR:
        snprint_value(buff, sizeof(buff), var->name, var->name_length, var);
        /* Advance passed the first space and use that unless there
         * is no space or we have no more string left.
         */
        cp = strchr(buff, ' ');
        if(cp) {
          char *ecp;
          cp++;
          if(*cp == '"') {
            ecp = cp + strlen(cp) - 1;
            if(*ecp == '"') {
              cp++; *ecp = '\0';
            }
          }
        }
        noit_stats_set_metric(current, metric_name,
                              METRIC_STRING, (cp && *cp) ? cp : NULL);
        break;
      default:
        noitL(nlerr, "snmp trap unsupport data type %d\n", var->type);
    }
    noitL(nldeb, "metric_name -> '%s'\n", metric_name);
  }
  else {
    /* No idea what this is */
    return -1;
  }
  return 0;
}
static int noit_snmp_trapd_response(int operation, struct snmp_session *sp,
                                    int reqid, struct snmp_pdu *pdu,
                                    void *magic) {
  /* the noit pieces */
  noit_check_t *check;
  struct target_session *ts = magic;
  snmp_mod_config_t *conf;
  const char *community = NULL;
  stats_t current;
  int success = 0;

  /* parsing destination */
  char uuid_str[UUID_STR_LEN + 1];
  uuid_t checkid;

  /* snmp oid parsing helper vars */
  netsnmp_pdu *newpdu = pdu;
  netsnmp_variable_list *var;

  conf = noit_module_get_userdata(ts->self);

  if(pdu->version == SNMP_VERSION_1)
    newpdu = convert_v1pdu_to_v2(pdu);
  if(!newpdu || newpdu->version != SNMP_VERSION_2c) goto cleanup;

  for(var = newpdu->variables; var != NULL; var = var->next_variable) {
    if(netsnmp_oid_equals(var->name, var->name_length,
                          snmptrap_oid, snmptrap_oid_len) == 0)
      break;
  }

  if (!var || var->type != ASN_OBJECT_ID) {
    noitL(nlerr, "unsupport trap (not a trap?) received\n");
    goto cleanup;
  }

  /* var is the oid on which we are trapping.
   * It should be in the reconnoiter check prefix.
   */
  if(noit_snmp_oid_to_checkid(var->val.objid, var->val_len/sizeof(oid),
                              checkid, uuid_str)) {
    goto cleanup;
  }
  noitL(nldeb, "recieved trap for %s\n", uuid_str);
  check = noit_poller_lookup(checkid);
  if(!check) {
    noitL(nlerr, "trap received for non-existent check '%s'\n", uuid_str);
    goto cleanup;
  }
  if(!noit_hash_retr_str(check->config, "community", strlen("community"),
                         &community) &&
     !noit_hash_retr_str(conf->options, "community", strlen("community"),
                         &community)) {
    noitL(nlerr, "No community defined for check, dropping trap\n");
    goto cleanup;
  }

  if(strlen(community) != newpdu->community_len ||
     memcmp(community, newpdu->community, newpdu->community_len)) {
    noitL(nlerr, "trap attempt with wrong community string\n");
    goto cleanup;
  }

  /* We have a check. The trap is authorized. Now, extract everything. */
  memset(&current, 0, sizeof(current));
  gettimeofday(&current.whence, NULL);
  current.available = NP_AVAILABLE;

  for(; var != NULL; var = var->next_variable)
    if(noit_snmp_trapvars_to_stats(&current, var) == 0) success++;
  if(success) {
    char buff[24];
    snprintf(buff, sizeof(buff), "%d datum", success);
    current.state = NP_GOOD;
    current.status = strdup(buff);
  }
  else {
    current.state = NP_BAD;
    current.status = strdup("no data");
  }
  noit_check_set_stats(ts->self, check, &current);

 cleanup:
  if(newpdu != pdu)
    snmp_free_pdu(newpdu);
  return 0;
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
  if(!noit_hash_retr_str(check->config, "community", strlen("community"),
                         &community)) {
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
  while(noit_hash_next_str(check->config, &iter, &name, &klen, &value)) {
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
  while(noit_hash_next_str(check->config, &iter, &name, &klen, &value)) {
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
  ts = _get_target_session(self, check->target);
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

static int noit_snmptrap_initiate_check(noit_module_t *self,
                                        noit_check_t *check,
                                        int once, noit_check_t *cause) {
  /* We don't do anything for snmptrap checks.  Not intuitive... but they
   * never "run."  We accept input out-of-band via snmp traps.
   */
  return 0;
}

static int noit_snmp_config(noit_module_t *self, noit_hash_table *options) {
  snmp_mod_config_t *conf;
  conf = noit_module_get_userdata(self);
  if(conf) {
    if(conf->options) {
      noit_hash_destroy(conf->options, free, free);
      free(conf->options);
    }
  }
  else
    conf = calloc(1, sizeof(*conf));
  conf->options = options;
  noit_module_set_userdata(self, conf);
  return 1;
}
static int noit_snmp_onload(noit_image_t *self) {
  if(!nlerr) nlerr = noit_log_stream_find("error/snmp");
  if(!nldeb) nldeb = noit_log_stream_find("debug/snmp");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;
  eventer_name_callback("noit_snmp/check_timeout", noit_snmp_check_timeout);
  eventer_name_callback("noit_snmp/session_timeout", noit_snmp_session_timeout);
  eventer_name_callback("noit_snmp/handler", noit_snmp_handler);
  return 0;
}

static int noit_snmptrap_onload(noit_image_t *self) {
  if(!nlerr) nlerr = noit_log_stream_find("error/snmp");
  if(!nldeb) nldeb = noit_log_stream_find("debug/snmp");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;
  eventer_name_callback("noit_snmp/session_timeout", noit_snmp_session_timeout);
  eventer_name_callback("noit_snmp/handler", noit_snmp_handler);
  return 0;
}

static int noit_snmp_init(noit_module_t *self) {
  const char *opt;
  snmp_mod_config_t *conf;

  conf = noit_module_get_userdata(self);

  if(!__snmp_initialize_once) {
    register_mib_handlers();
    read_premib_configs();
    read_configs();
    init_snmp("noitd");
    __snmp_initialize_once = 1;
  }

  if(strcmp(self->hdr.name, "snmptrap") == 0) {
    eventer_t newe;
    int i, block = 0, fds = 0;
    fd_set fdset;
    struct timeval timeout = { 0, 0 };
    struct target_session *ts;
    netsnmp_transport *transport;
    netsnmp_session sess, *session = &sess;

    if(!noit_hash_retrieve(conf->options,
                           "snmptrapd_port", strlen("snmptrapd_port"),
                           (void **)&opt))
      opt = "162";

    transport = netsnmp_transport_open_server("snmptrap", opt);
    if(!transport) {
      noitL(nlerr, "cannot open netsnmp transport for trap daemon\n");
      return -1;
    }
    ts = _get_target_session(self, "snmptrapd");
    snmp_sess_init(session);
    session->peername = SNMP_DEFAULT_PEERNAME;
    session->version = SNMP_DEFAULT_VERSION;
    session->community_len = SNMP_DEFAULT_COMMUNITY_LEN;
    session->retries = SNMP_DEFAULT_RETRIES;
    session->timeout = SNMP_DEFAULT_TIMEOUT;
    session->callback = noit_snmp_trapd_response;
    session->callback_magic = (void *) ts;
    session->authenticator = NULL;
    session->isAuthoritative = SNMP_SESS_UNKNOWNAUTH;
    ts->sess_handle = snmp_sess_add(session, transport, NULL, NULL);

    FD_ZERO(&fdset);
    snmp_sess_select_info(ts->sess_handle, &fds, &fdset, &timeout, &block);
    assert(fds > 0);
    for(i=0; i<fds; i++) {
      if(FD_ISSET(i, &fdset)) {
        ts->refcnt++;
        ts->fd = i;
        newe = eventer_alloc();
        newe->fd = ts->fd;
        newe->callback = noit_snmp_handler;
        newe->closure = ts;
        newe->mask = EVENTER_READ | EVENTER_EXCEPTION;
        eventer_add(newe);
      }
    }
  }
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

#include "snmptrap.xmlh"
noit_module_t snmptrap = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "snmptrap",
    "SNMP trap collection",
    snmptrap_xml_description,
    noit_snmptrap_onload
  },
  noit_snmp_config,
  noit_snmp_init,
  noit_snmptrap_initiate_check,
  NULL /* noit_snmp_cleanup */
};
