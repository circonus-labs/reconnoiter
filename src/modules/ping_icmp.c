/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2015-2017, Circonus, Inc. All rights reserved.
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

#include <mtev_defines.h>
#include <mtev_uuid.h>
#include <mtev_rand.h>

#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <openssl/rand.h>
#include <math.h>
#ifndef MAXFLOAT
#include <float.h>
#define MAXFLOAT FLT_MAX
#endif

#include <eventer/eventer.h>

#include "noit_mtev_bridge.h"
#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "check_test.h"

#define PING_INTERVAL 2000 /* 2000ms = 2s */
#define PING_COUNT    5

struct check_info {
  uint16_t check_no;
  uint8_t check_seq_no;
  uint8_t seq;
  int8_t expected_count;
  float *turnaround;
  eventer_t timeout_event;
};
struct ping_session_key {
  uintptr_t addr_of_check; /* ticket #288 */
  uuid_t checkid;
};
struct ping_payload {
  uintptr_t addr_of_check; /* ticket #288 */
  uuid_t checkid;
  uint64_t tv_sec;
  uint32_t tv_usec;
  uint16_t generation;    
  uint16_t check_no;
  uint8_t  check_pack_no;
  uint8_t  check_pack_cnt;
  uint8_t  size_bookend;
};
#define PING_PAYLOAD_LEN offsetof(struct ping_payload, size_bookend)
struct ping_closure {
  noit_module_t *self;
  noit_check_t *check;
  void *payload;
  int payload_len;
  int icp_len;
};
static mtev_log_stream_t nlerr = NULL;
static mtev_log_stream_t nldeb = NULL;
static int in_cksum(u_short *addr, int len);
static uintptr_t random_num;


typedef struct  {
  int ipv4_fd;
  int ipv6_fd;
  mtev_hash_table *in_flight;
  ck_spinlock_t in_flight_lock;
} ping_icmp_data_t;

static int ping_icmp_config(noit_module_t *self, mtev_hash_table *options) {
  return 0;
}
static int ping_icmp_is_complete(noit_module_t *self, noit_check_t *check) {
  int i;
  struct check_info *data;
  data = (struct check_info *)check->closure;
  for(i=0; i<data->expected_count; i++)
    if(data->turnaround[i] < 0.0) {
      mtevL(nldeb, "ping_icmp: %s %d is still outstanding.\n",
            check->target_ip, i);
      return 0;
    }
  return 1;
}
static void ping_icmp_log_results(noit_module_t *self, noit_check_t *check) {
  struct check_info *data;
  double avail = 0.0, min = MAXFLOAT, max = 0.0, avg = 0.0, cnt;
  int avail_needed = 100;
  const char *config_val = NULL;
  int i, points = 0;
  char human_buffer[256];
  struct timeval now, duration;

  data = (struct check_info *)check->closure;
  for(i=0; i<data->expected_count; i++) {
    if(data->turnaround[i] >= 0.0) {
      points++;
      avg += data->turnaround[i];
      if(data->turnaround[i] > max) max = data->turnaround[i];
      if(data->turnaround[i] < min) min = data->turnaround[i];
    }
  }
  cnt = data->expected_count;
  if(points == 0) {
    min = 0.0 / 0.0;
    max = 0.0 / 0.0;
    avg = 0.0 / 0.0;
  }
  else {
    avail = (float)points /cnt;
    avg /= (float)points;
  }

  if(mtev_hash_retr_str(check->config, "avail_needed", strlen("avail_needed"),
                        &config_val))
    avail_needed = atoi(config_val);

  snprintf(human_buffer, sizeof(human_buffer),
           "cnt=%d,avail=%0.0f,min=%0.4f,max=%0.4f,avg=%0.4f",
           (int)cnt, 100.0*avail, min, max, avg);
  mtevL(nldeb, "ping_icmp(%s) [%s]\n", check->target_ip, human_buffer);

  mtev_gettimeofday(&now, NULL);
  sub_timeval(now, check->last_fire_time, &duration);
  noit_stats_set_whence(check, &now);
  noit_stats_set_duration(check, duration.tv_sec * 1000 + duration.tv_usec / 1000);
  noit_stats_set_available(check, (avail > 0.0) ? NP_AVAILABLE : NP_UNAVAILABLE);
  noit_stats_set_state(check, (avail < ((float)avail_needed / 100.0)) ? NP_BAD : NP_GOOD);
  noit_stats_set_status(check, human_buffer);
  noit_stats_set_metric(check, "count",
                        METRIC_INT32, &data->expected_count);
  avail *= 100.0;
  noit_stats_set_metric(check, "available", METRIC_DOUBLE, &avail);
  noit_stats_set_metric(check, "minimum",
                        METRIC_DOUBLE, avail > 0.0 ? &min : NULL);
  noit_stats_set_metric(check, "maximum",
                        METRIC_DOUBLE, avail > 0.0 ? &max : NULL);
  noit_stats_set_metric(check, "average",
                        METRIC_DOUBLE, avail > 0.0 ? &avg : NULL);
  noit_check_set_stats(check);
}
static int ping_icmp_timeout(eventer_t e, int mask,
                             void *closure, struct timeval *now) {
  struct ping_closure *pcl = (struct ping_closure *)closure;
  struct ping_session_key k;
  struct check_info *data;
  ping_icmp_data_t *ping_data;

  if(!NOIT_CHECK_KILLED(pcl->check) && !NOIT_CHECK_DISABLED(pcl->check)) {
    ping_icmp_log_results(pcl->self, pcl->check);
  }
  data = (struct check_info *)pcl->check->closure;
  data->timeout_event = NULL;
  noit_check_end(pcl->check);
  ping_data = noit_module_get_userdata(pcl->self);
  k.addr_of_check = (uintptr_t)pcl->check ^ random_num;
  mtev_uuid_copy(k.checkid, pcl->check->checkid);
  ck_spinlock_lock(&ping_data->in_flight_lock);
  mtev_boolean should_free = 
    mtev_hash_delete(ping_data->in_flight, (const char *)&k, sizeof(k),
                     free, NULL);
  ck_spinlock_unlock(&ping_data->in_flight_lock);
  if(should_free) free(pcl);
  return 0;
}

static int ping_icmp_handler(eventer_t e, int mask,
                             void *closure, struct timeval *now,
                             uint8_t family) {
  noit_module_t *self = (noit_module_t *)closure;
  ping_icmp_data_t *ping_data;
  struct check_info *data;
  char packet[1500];
  int packet_len = sizeof(packet);
  union {
   struct sockaddr_in  in4;
   struct sockaddr_in6 in6;
  } from;
  unsigned int from_len;
  struct ping_payload *payload;

  if(family != AF_INET && family != AF_INET6) return EVENTER_READ;

  ping_data = noit_module_get_userdata(self);
  while(1) {
    struct ping_session_key k;
    int inlen;
    uint8_t iphlen = 0;
    void *vcheck;
    noit_check_t *check;
    struct timeval tt, whence;

    from_len = sizeof(from);

    inlen = recvfrom(eventer_get_fd(e), packet, packet_len, 0,
                     (struct sockaddr *)&from, &from_len);
    mtev_gettimeofday(now, NULL); /* set it, as we care about accuracy */

    if(inlen < 0) {
      if(errno == EAGAIN || errno == EINTR) break;
      mtevLT(nldeb, now, "ping_icmp recvfrom: %s\n", strerror(errno));
      break;
    }

    if(family == AF_INET) {
      struct icmp *icp4;
      iphlen = ((struct ip *)packet)->ip_hl << 2;
      if((inlen-iphlen) != sizeof(struct icmp)+PING_PAYLOAD_LEN) {
        mtevLT(nldeb, now,
               "ping_icmp bad size: %d+%d\n", iphlen, inlen-iphlen); 
        continue;
      }
      icp4 = (struct icmp *)(packet + iphlen);
      payload = (struct ping_payload *)(icp4 + 1);
      if(icp4->icmp_type != ICMP_ECHOREPLY) {
        mtevLT(nldeb, now, "ping_icmp bad type: %d\n", icp4->icmp_type);
        continue;
      }
      if(icp4->icmp_id != (((uintptr_t)self) & 0xffff)) {
        mtevLT(nldeb, now,
                 "ping_icmp not sent from this instance (%d:%d) vs. %lu\n",
                 icp4->icmp_id, ntohs(icp4->icmp_seq),
                 (unsigned long)(((uintptr_t)self) & 0xffff));
        continue;
      }
    }
    else if(family == AF_INET6) {
      struct icmp6_hdr *icp6 = (struct icmp6_hdr *)packet;
      if((inlen) != sizeof(struct icmp6_hdr)+PING_PAYLOAD_LEN) {
        mtevLT(nldeb, now,
               "ping_icmp bad size: %d+%d\n", iphlen, inlen-iphlen); 
        continue;
      }
      payload = (struct ping_payload *)(icp6+1);
      if(icp6->icmp6_type != ICMP6_ECHO_REPLY) {
        mtevLT(nldeb, now, "ping_icmp bad type: %d\n", icp6->icmp6_type);
        continue;
      }
      if(icp6->icmp6_id != (((uintptr_t)self) & 0xffff)) {
        mtevLT(nldeb, now,
                 "ping_icmp not sent from this instance (%d:%d) vs. %lu\n",
                 icp6->icmp6_id, ntohs(icp6->icmp6_seq),
                 (unsigned long)(((uintptr_t)self) & 0xffff));
        continue;
      }
    }
    else {
      /* This should be unreachable */
      continue;
    }

    char uuid_str[37];
    mtev_uuid_unparse_lower(payload->checkid, uuid_str);

    check = NULL;
    k.addr_of_check = payload->addr_of_check;
    mtev_uuid_copy(k.checkid, payload->checkid);
    vcheck = NULL;
    ck_spinlock_lock(&ping_data->in_flight_lock);
    mtev_hash_retrieve(ping_data->in_flight,
                       (const char *)&k, sizeof(k),
                       &vcheck);
    ck_spinlock_unlock(&ping_data->in_flight_lock);
    check = noit_poller_lookup(k.checkid);
    // if we don't get a match, need to also scan test checks if that module is loaded
    if(!check) {
      check = NOIT_TESTCHECK_LOOKUP(k.checkid);
    }
    if(!check) {
      mtevLT(nldeb, now,
             "ping_icmp response for missing check '%s'\n", uuid_str);
      continue;
    }
    if(check != vcheck) {
      mtevLT(nldeb, now,
             "ping_icmp response for mismatched check '%s'\n", uuid_str);
      continue;
    }

    /* make sure this check is from this generation! */
    if(!check) {
      mtevLT(nldeb, now,
             "ping_icmp response for unknown check '%s'\n", uuid_str);
      continue;
    }
    if((check->generation & 0xffff) != payload->generation) {
      mtevLT(nldeb, now,
             "ping_icmp response in generation gap\n");
      continue;
    }
    data = (struct check_info *)check->closure;

    /* If there is no timeout_event, the check must have completed.
     * We have nothing to do. */
    if(!data->timeout_event) {
      mtevLT(nldeb, now,
             "ping_icmp response timeout/completion for check '%s'\n", uuid_str);
      continue;
    }

    /* Sanity check the payload */
    if(payload->check_no != data->check_no) {
      mtevLT(nldeb, now,
             "ping_icmp response check number mismatch for check '%s'\n", uuid_str);
      continue;
    }
    if(payload->check_pack_cnt != data->expected_count) {
      mtevLT(nldeb, now,
             "ping_icmp response check packet count mismatch for check '%s'\n", uuid_str);
      continue;
    }
    if(payload->check_pack_no >= data->expected_count) { 
      mtevLT(nldeb, now,
             "ping_icmp response check packet number mismatch for check '%s'\n", uuid_str);
      continue;
    }

    mtevLT(nldeb, now,
           "ping_icmp response received for check '%s'\n", uuid_str);

    whence.tv_sec = payload->tv_sec;
    whence.tv_usec = payload->tv_usec;
    sub_timeval(*now, whence, &tt);
    data->turnaround[payload->check_pack_no] =
      (float)tt.tv_sec + (float)tt.tv_usec / 1000000.0;
    if(ping_icmp_is_complete(self, check)) {
      k.addr_of_check = (uintptr_t)check ^ random_num;
      mtev_uuid_copy(k.checkid, check->checkid);
      ck_spinlock_lock(&ping_data->in_flight_lock);
      mtev_boolean first_finisher =
        mtev_hash_delete(ping_data->in_flight, (const char *)&k,
                         sizeof(k), free, NULL);
      ck_spinlock_unlock(&ping_data->in_flight_lock);
      if(first_finisher) {
        noit_check_end(check);
        ping_icmp_log_results(self, check);
        eventer_t olde = eventer_remove(data->timeout_event);
        if(olde) {
          eventer_remove(olde);
          free(eventer_get_closure(olde));
          eventer_free(olde);
          data->timeout_event = NULL;
        }
      }
    }
  }
  return EVENTER_READ;
}
static int ping_icmp4_handler(eventer_t e, int mask,
                              void *closure, struct timeval *now) {
  return ping_icmp_handler(e, mask, closure, now, AF_INET);
}
static int ping_icmp6_handler(eventer_t e, int mask,
                              void *closure, struct timeval *now) {
  return ping_icmp_handler(e, mask, closure, now, AF_INET6);
}

static int ping_icmp_init(noit_module_t *self) {
  socklen_t on;
  struct protoent *proto;
  ping_icmp_data_t *data;

  RAND_pseudo_bytes((unsigned char *)&random_num, sizeof(uintptr_t));

  data = malloc(sizeof(*data));
  data->in_flight = calloc(1, sizeof(*data->in_flight));
  mtev_hash_init(data->in_flight);
  ck_spinlock_init(&data->in_flight_lock);
  data->ipv4_fd = data->ipv6_fd = -1;

  if ((proto = getprotobyname("icmp")) == NULL) {
    mtevL(noit_error, "Couldn't find 'icmp' protocol\n");
    free(data);
    return -1;
  }

  data->ipv4_fd = socket(AF_INET, NE_SOCK_CLOEXEC|SOCK_RAW, proto->p_proto);
  if(data->ipv4_fd < 0) {
    mtevL(noit_error, "ping_icmp: socket failed: %s\n",
          strerror(errno));
  }
  else {
    socklen_t slen = sizeof(on);
    if(getsockopt(data->ipv4_fd, SOL_SOCKET, SO_SNDBUF, &on, &slen) == 0) {
      if(on <= 0) on = 1024;
      while(on < (1 << 20)) {
        on <<= 1;
        if(setsockopt(data->ipv4_fd, SOL_SOCKET, SO_SNDBUF,
                      &on, sizeof(on)) != 0) {
          on >>= 1;
          break;
        }
      }
      mtevL(noit_debug, "ping_icmp: send buffer set to %d\n", on);
    }
    else
      mtevL(noit_error, "Cannot get sndbuf size: %s\n", strerror(errno));

    if(eventer_set_fd_nonblocking(data->ipv4_fd)) {
      close(data->ipv4_fd);
      data->ipv4_fd = -1;
      mtevL(noit_error,
            "ping_icmp: could not set socket non-blocking: %s\n",
            strerror(errno));
    }
  }
  if(data->ipv4_fd >= 0) {
    eventer_t newe;
    newe = eventer_alloc_fd(ping_icmp4_handler, self, data->ipv4_fd, EVENTER_READ);
    eventer_pool_t *dp = eventer_pool("noit_module_ping_icmp");
    if(dp) eventer_set_owner(newe, eventer_choose_owner_pool(dp, mtev_rand()));
    eventer_add(newe);
  }

  if ((proto = getprotobyname("ipv6-icmp")) != NULL) {
    data->ipv6_fd = socket(AF_INET6, NE_SOCK_CLOEXEC|SOCK_RAW, proto->p_proto);
    if(data->ipv6_fd < 0) {
      mtevL(noit_error, "ping_icmp: socket failed: %s\n",
            strerror(errno));
    }
    else {
      if(eventer_set_fd_nonblocking(data->ipv6_fd)) {
        close(data->ipv6_fd);
        data->ipv6_fd = -1;
        mtevL(noit_error,
              "ping_icmp: could not set socket non-blocking: %s\n",
                 strerror(errno));
      }
    }
    if(data->ipv6_fd >= 0) {
      eventer_t newe;
      newe = eventer_alloc_fd(ping_icmp6_handler, self, data->ipv6_fd, EVENTER_READ);
      eventer_pool_t *dp = eventer_pool("noit_module_ping_icmp");
      if(dp) eventer_set_owner(newe, eventer_choose_owner_pool(dp, mtev_rand()));
      eventer_add(newe);
    }
  }
  else
    mtevL(noit_error, "Couldn't find 'ipv6-icmp' protocol\n");

  noit_module_set_userdata(self, data);
  return 0;
}

static int ping_icmp_real_send(eventer_t e, int mask,
                               void *closure, struct timeval *now) {
  struct ping_closure *pcl = (struct ping_closure *)closure;
  struct ping_session_key k;
  struct ping_payload *payload;
  struct timeval whence;
  ping_icmp_data_t *data;
  void *vcheck;
  int i = 0;

  data = noit_module_get_userdata(pcl->self);
  payload = (struct ping_payload *)((char *)pcl->payload + pcl->icp_len);
  k.addr_of_check = payload->addr_of_check;
  mtev_uuid_copy(k.checkid, payload->checkid);

  ck_spinlock_lock(&data->in_flight_lock);
  if(!mtev_hash_retrieve(data->in_flight, (const char *)&k, sizeof(k),
                         &vcheck)) {
    ck_spinlock_unlock(&data->in_flight_lock);
    mtevLT(nldeb, now, "ping check no longer active, bailing\n");
    goto cleanup;
  }
  ck_spinlock_unlock(&data->in_flight_lock);

  if(pcl->check->target_ip[0] == '\0') goto cleanup;

  mtevLT(nldeb, now, "ping_icmp_real_send(%s)\n", pcl->check->target_ip);
  mtev_gettimeofday(&whence, NULL); /* now isn't accurate enough */
  payload->tv_sec = whence.tv_sec;
  payload->tv_usec = whence.tv_usec;
  if(pcl->check->target_family == AF_INET) {
    struct sockaddr_in sin;
    struct icmp *icp4 = (struct icmp *)pcl->payload;
    if(data->ipv4_fd < 0) {
      mtevLT(nldeb, now, "IPv4 ping unavailable\n");
      goto cleanup;
    }
    icp4->icmp_cksum = in_cksum(pcl->payload, pcl->payload_len);
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    memcpy(&sin.sin_addr,
           &pcl->check->target_addr.addr, sizeof(sin.sin_addr));
    i = sendto(data->ipv4_fd,
               pcl->payload, pcl->payload_len, 0,
               (struct sockaddr *)&sin, sizeof(sin));
  }
  else if(pcl->check->target_family == AF_INET6) {
    struct sockaddr_in6 sin;
    struct icmp6_hdr *icp6 = (struct icmp6_hdr *)pcl->payload;
    if(data->ipv6_fd < 0) {
      mtevLT(nldeb, now, "IPv6 ping unavailable\n");
      goto cleanup;
    }
    icp6->icmp6_cksum = in_cksum(pcl->payload, pcl->payload_len);
    memset(&sin, 0, sizeof(sin));
    sin.sin6_family = AF_INET6;
    memcpy(&sin.sin6_addr,
           &pcl->check->target_addr.addr6, sizeof(sin.sin6_addr));
    i = sendto(data->ipv6_fd,
               pcl->payload, pcl->payload_len, 0,
               (struct sockaddr *)&sin, sizeof(sin));
  }
  if(i != pcl->payload_len) {
    mtevLT(nlerr, now, "Error sending ICMP packet to %s(%s): %s\n",
             pcl->check->target, pcl->check->target_ip, strerror(errno));
  }
 cleanup:
  free(pcl->payload);
  free(pcl);
  return 0;
}
static void ping_check_cleanup(noit_module_t *self, noit_check_t *check) {
  struct check_info *ci = (struct check_info *)check->closure;
  if(ci) {
    if(ci->timeout_event) {
      eventer_t e = eventer_remove(ci->timeout_event);
      if(e) {
        free(eventer_get_closure(e));
        eventer_free(e);
      }
    }
    if(ci->turnaround) free(ci->turnaround);
    free(ci);
  }
  check->closure = NULL;
}
static int ping_icmp_send(noit_module_t *self, noit_check_t *check,
                          noit_check_t *cause) {
  struct timeval when, p_int;
  struct ping_payload *payload;
  struct ping_closure *pcl;
  struct check_info *ci = (struct check_info *)check->closure;
  int packet_len, icp_len, i;
  eventer_t newe;
  const char *config_val;
  ping_icmp_data_t *ping_data;
  struct ping_session_key *k;
  void *icp;

  int interval = PING_INTERVAL;
  int count = PING_COUNT;

  BAIL_ON_RUNNING_CHECK(check);

  if(mtev_hash_retr_str(check->config, "interval", strlen("interval"),
                        &config_val))
    interval = atoi(config_val);
  if(mtev_hash_retr_str(check->config, "count", strlen("count"),
                        &config_val))
    count = atoi(config_val);

  noit_check_begin(check);
  ping_data = noit_module_get_userdata(self);
  k = calloc(1, sizeof(*k));
  k->addr_of_check = (uintptr_t)check ^ random_num;
  mtev_uuid_copy(k->checkid, check->checkid);
  if(!mtev_hash_store(ping_data->in_flight, (const char *)k, sizeof(*k),
                      check)) {
    free(k);
  }
  mtevL(nldeb, "ping_icmp_send(%p,%s,%d,%d)\n",
        self, check->target_ip, interval, count);

  /* remove a timeout if we still have one -- we should unless someone
   * has set a lower timeout than the period.
   */
  if(ci->timeout_event) {
    eventer_t olde = eventer_remove(ci->timeout_event);
    if(olde) {
      free(eventer_get_closure(olde));
      eventer_free(olde);
    }
    ci->timeout_event = NULL;
  }

  mtev_gettimeofday(&when, NULL);
  memcpy(&check->last_fire_time, &when, sizeof(when));

  /* Setup some stuff used in the loop */
  p_int.tv_sec = interval / 1000;
  p_int.tv_usec = (interval % 1000) * 1000;
  icp_len = (check->target_family == AF_INET6) ?
              sizeof(struct icmp6_hdr) : sizeof(struct icmp);
  packet_len = icp_len + PING_PAYLOAD_LEN;

  /* Prep holding spots for return info */
  ci->expected_count = count;
  if(ci->turnaround) free(ci->turnaround);
  ci->turnaround = malloc(count * sizeof(*ci->turnaround));

  ++ci->check_no;
  for(i=0; i<count; i++) {
    /* Negative means we've not received a response */
    ci->turnaround[i] = -1.0;

    icp = calloc(1,packet_len);
    payload = (struct ping_payload *)((char *)icp + icp_len);

    if(check->target_family == AF_INET) {
      struct icmp *icp4 = icp;
      icp4->icmp_type = ICMP_ECHO;
      icp4->icmp_code = 0;
      icp4->icmp_cksum = 0;
      icp4->icmp_seq = htons(ci->seq++);
      icp4->icmp_id = (((uintptr_t)self) & 0xffff);
    }
    else if(check->target_family == AF_INET6) {
      struct icmp6_hdr *icp6 = icp;
      icp6->icmp6_type = ICMP6_ECHO_REQUEST;
      icp6->icmp6_code = 0;
      icp6->icmp6_cksum = 0;
      icp6->icmp6_seq = htons(ci->seq++);
      icp6->icmp6_id = (((uintptr_t)self) & 0xffff);
    }

    payload->addr_of_check = (uintptr_t)check ^ random_num;
    mtev_uuid_copy(payload->checkid, check->checkid);
    payload->generation = check->generation & 0xffff;
    payload->check_no = ci->check_no;
    payload->check_pack_no = i;
    payload->check_pack_cnt = count;

    pcl = calloc(1, sizeof(*pcl));
    pcl->self = self;
    pcl->check = check;
    pcl->payload = icp;
    pcl->payload_len = packet_len;
    pcl->icp_len = icp_len;

    newe = eventer_in(ping_icmp_real_send, pcl, p_int);
    eventer_add(newe);
  }
  p_int.tv_sec = check->timeout / 1000;
  p_int.tv_usec = (check->timeout % 1000) * 1000;
  pcl = calloc(1, sizeof(*pcl));
  pcl->self = self;
  pcl->check = check;
  newe = eventer_in(ping_icmp_timeout, pcl, p_int);
  ci->timeout_event = newe;
  eventer_add(newe);

  return 0;
}
static int ping_icmp_initiate_check(noit_module_t *self, noit_check_t *check,
                                    int once, noit_check_t *cause) {
  if(!check->closure) check->closure = calloc(1, sizeof(struct check_info));
  INITIATE_CHECK(ping_icmp_send, self, check, cause);
  return 0;
}

/*
 *      I N _ C K S U M
 *          This is from Mike Muuss's Public Domain code.
 * Checksum routine for Internet Protocol family headers (C Version)
 *
 */
static int in_cksum(u_short *addr, int len)
{
  register int nleft = len;
  register u_short *w = addr;
  register u_short answer;
  register int sum = 0;

  /*
   *  Our algorithm is simple, using a 32 bit accumulator (sum),
   *  we add sequential 16 bit words to it, and at the end, fold
   *  back all the carry bits from the top 16 bits into the lower
   *  16 bits.
   */
  while( nleft > 1 )  {
    sum += *w++;
    nleft -= 2;
  }

  /* mop up an odd byte, if necessary */
  if( nleft == 1 ) {
    u_short  u = 0;

    *(u_char *)(&u) = *(u_char *)w ;
    sum += u;
  }

  /*
   * add back carry outs from top 16 bits to low 16 bits
   */
  sum = (sum >> 16) + (sum & 0xffff);  /* add hi 16 to low 16 */
  sum += (sum >> 16);      /* add carry */
  answer = ~sum;        /* truncate to 16 bits */
  return (answer);
}

static int ping_icmp_onload(mtev_image_t *self) {
  nlerr = mtev_log_stream_find("error/ping_icmp");
  nldeb = mtev_log_stream_find("debug/ping_icmp");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;
  eventer_name_callback("ping_icmp/timeout", ping_icmp_timeout);
  eventer_name_callback("ping_icmp/handler", ping_icmp4_handler);
  eventer_name_callback("ping_icmp6/handler", ping_icmp6_handler);
  eventer_name_callback("ping_icmp/send", ping_icmp_real_send);
  return 0;
}
#include "ping_icmp.xmlh"
noit_module_t ping_icmp = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "ping_icmp",
    .description = "ICMP based host availability detection",
    .xml_description = ping_icmp_xml_description,
    .onload = ping_icmp_onload
  },
  ping_icmp_config,
  ping_icmp_init,
  ping_icmp_initiate_check,
  ping_check_cleanup,
  /* all ping checks share a single socket, so we're operationally
   * mostly single-threaded already. no benefit to having checks
   * run in multiple threads. */
  .thread_unsafe = 1
};
