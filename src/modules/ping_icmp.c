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
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <math.h>
#ifndef MAXFLOAT
#include <float.h>
#define MAXFLOAT FLT_MAX
#endif

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "utils/noit_log.h"

#define PING_INTERVAL 2000 /* 2000ms = 2s */
#define PING_COUNT    5

struct check_info {
  int check_no;
  int check_seq_no;
  int seq;
  int32_t expected_count;
  float *turnaround;
  eventer_t timeout_event;
};
struct ping_payload {
  uuid_t checkid;
  u_int32_t generation;    
  struct timeval whence;
  int    check_no;
  int    check_pack_no;
  int    check_pack_cnt;
};
struct ping_closure {
  noit_module_t *self;
  noit_check_t *check;
  void *payload;
  int payload_len;
};
static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;
static int in_cksum(u_short *addr, int len);

typedef struct  {
  int ipv4_fd;
  int ipv6_fd;
} ping_icmp_data_t;

static int ping_icmp_config(noit_module_t *self, noit_hash_table *options) {
  return 0;
}
static int ping_icmp_is_complete(noit_module_t *self, noit_check_t *check) {
  int i;
  struct check_info *data;
  data = (struct check_info *)check->closure;
  for(i=0; i<data->expected_count; i++)
    if(data->turnaround[i] < 0.0) {
      noitL(nldeb, "ping_icmp: %s %d is still outstanding.\n",
            check->target, i);
      return 0;
    }
  return 1;
}
static void ping_icmp_log_results(noit_module_t *self, noit_check_t *check) {
  struct check_info *data;
  double avail, min = MAXFLOAT, max = 0.0, avg = 0.0, cnt;
  int avail_needed = 100;
  const char *config_val = NULL;
  int i, points = 0;
  char human_buffer[256];
  stats_t current;
  struct timeval duration;

  noit_check_stats_clear(&current);

  data = (struct check_info *)check->closure;
  for(i=0; i<data->expected_count; i++) {
    if(data->turnaround[i] >= 0.0) {
      points++;
      avg += data->turnaround[i];
      if(data->turnaround[i] > max) max = data->turnaround[i];
      if(data->turnaround[i] < min) min = data->turnaround[i];
    }
  }
  if(points == 0) {
    min = 0.0 / 0.0;
    max = 0.0 / 0.0;
  }
  cnt = data->expected_count;
  avail = (float)points /cnt;
  avg /= (float)points;

  if(noit_hash_retr_str(check->config, "avail_needed", strlen("avail_needed"),
                        &config_val))
    avail_needed = atoi(config_val);

  snprintf(human_buffer, sizeof(human_buffer),
           "cnt=%d,avail=%0.0f,min=%0.4f,max=%0.4f,avg=%0.4f",
           (int)cnt, 100.0*avail, min, max, avg);
  noitL(nldeb, "ping_icmp(%s) [%s]\n", check->target, human_buffer);

  gettimeofday(&current.whence, NULL);
  sub_timeval(current.whence, check->last_fire_time, &duration);
  current.duration = duration.tv_sec * 1000 + duration.tv_usec / 1000;
  current.available = (avail > 0.0) ? NP_AVAILABLE : NP_UNAVAILABLE;
  current.state = (avail < ((float)avail_needed / 100.0)) ? NP_BAD : NP_GOOD;
  current.status = human_buffer;
  noit_stats_set_metric(&current, "count",
                        METRIC_INT32, &data->expected_count);
  avail *= 100.0;
  noit_stats_set_metric(&current, "available", METRIC_DOUBLE, &avail);
  noit_stats_set_metric(&current, "minimum",
                        METRIC_DOUBLE, avail > 0.0 ? &min : NULL);
  noit_stats_set_metric(&current, "maximum",
                        METRIC_DOUBLE, avail > 0.0 ? &max : NULL);
  noit_stats_set_metric(&current, "average",
                        METRIC_DOUBLE, avail > 0.0 ? &avg : NULL);
  noit_check_set_stats(self, check, &current);
}
static int ping_icmp_timeout(eventer_t e, int mask,
                             void *closure, struct timeval *now) {
  struct ping_closure *pcl = (struct ping_closure *)closure;
  struct check_info *data;
  if(!NOIT_CHECK_KILLED(pcl->check) && !NOIT_CHECK_DISABLED(pcl->check)) {
    ping_icmp_log_results(pcl->self, pcl->check);
    data = (struct check_info *)pcl->check->closure;
    data->timeout_event = NULL;
  }
  pcl->check->flags &= ~NP_RUNNING;
  free(pcl);
  return 0;
}
static int ping_icmp_handler(eventer_t e, int mask,
                             void *closure, struct timeval *now) {
  noit_module_t *self = (noit_module_t *)closure;
  struct check_info *data;
  char packet[1500];
  int packet_len = sizeof(packet);
  union {
   struct sockaddr_in  in4;
   struct sockaddr_in6 in6;
  } from;
  unsigned int from_len;
  struct ip *ip = (struct ip *)packet;
  struct icmp *icp;
  struct ping_payload *payload;

  while(1) {
    int inlen, iphlen;
    noit_check_t *check;
    struct timeval tt;

    from_len = sizeof(from);

    inlen = recvfrom(e->fd, packet, packet_len, 0,
                     (struct sockaddr *)&from, &from_len);
    gettimeofday(now, NULL); /* set it, as we care about accuracy */

    if(inlen < 0) {
      if(errno == EAGAIN || errno == EINTR) break;
      noitLT(nlerr, now, "ping_icmp recvfrom: %s\n", strerror(errno));
      break;
    }
    iphlen = ip->ip_hl << 2;
    if((inlen-iphlen) != (sizeof(struct icmp)+sizeof(struct ping_payload))) {
      noitLT(nldeb, now,
             "ping_icmp bad size: %d+%d\n", iphlen, inlen-iphlen); 
      continue;
    }
    icp = (struct icmp *)(packet + iphlen);
    payload = (struct ping_payload *)(icp + 1);
    if(icp->icmp_type != ICMP_ECHOREPLY) {
      continue;
    }
    if(icp->icmp_id != (((vpsized_uint)self) & 0xffff)) {
      noitLT(nlerr, now,
               "ping_icmp not sent from this instance (%d:%d) vs. %lu\n",
               icp->icmp_id, ntohs(icp->icmp_seq),
               (unsigned long)(((vpsized_uint)self) & 0xffff));
      continue;
    }
    check = noit_poller_lookup(payload->checkid);
    /* make sure this check is from this generation! */
    if(!check) {
      char uuid_str[37];
      uuid_unparse_lower(payload->checkid, uuid_str);
      noitLT(nlerr, now,
             "ping_icmp response for unknown check '%s'\n", uuid_str);
      continue;
    }
    if(check->generation != payload->generation) {
      noitLT(nldeb, now,
             "ping_icmp response in generation gap\n");
      continue;
    }
    data = (struct check_info *)check->closure;

    /* If there is no timeout_event, the check must have completed.
     * We have nothing to do. */
    if(!data->timeout_event) continue;

    /* Sanity check the payload */
    if(payload->check_no != data->check_no) continue;
    if(payload->check_pack_cnt != data->expected_count) continue;
    if(payload->check_pack_no < 0 ||
       payload->check_pack_no >= data->expected_count) continue;

    sub_timeval(*now, payload->whence, &tt);
    data->turnaround[payload->check_pack_no] =
      (float)tt.tv_sec + (float)tt.tv_usec / 1000000.0;
    if(ping_icmp_is_complete(self, check)) {
      ping_icmp_log_results(self, check);
      eventer_remove(data->timeout_event);
      free(data->timeout_event->closure);
      eventer_free(data->timeout_event);
      data->timeout_event = NULL;
      check->flags &= ~NP_RUNNING;
    }
  }
  return EVENTER_READ;
}

static int ping_icmp_init(noit_module_t *self) {
  socklen_t on;
  struct protoent *proto;
  ping_icmp_data_t *data;

  data = malloc(sizeof(*data));
  data->ipv4_fd = data->ipv6_fd = -1;

  if ((proto = getprotobyname("icmp")) == NULL) {
    noitL(noit_error, "Couldn't find 'icmp' protocol\n");
    return -1;
  }

  data->ipv4_fd = socket(AF_INET, SOCK_RAW, proto->p_proto);
  if(data->ipv4_fd < 0) {
    noitL(noit_error, "ping_icmp: socket failed: %s\n",
          strerror(errno));
  }
  else {
    socklen_t slen = sizeof(on);
    if(getsockopt(data->ipv4_fd, SOL_SOCKET, SO_SNDBUF, &on, &slen) == 0) {
      while(on < (1 << 20)) {
        on <<= 1;
        if(setsockopt(data->ipv4_fd, SOL_SOCKET, SO_SNDBUF,
                      &on, sizeof(on)) != 0) {
          on >>= 1;
          break;
        }
      }
      noitL(noit_debug, "ping_icmp: send buffer set to %d\n", on);
    }
    else
      noitL(noit_error, "Cannot get sndbuf size: %s\n", strerror(errno));

    on = 1;
    if(ioctl(data->ipv4_fd, FIONBIO, &on)) {
      close(data->ipv4_fd);
      data->ipv4_fd = -1;
      noitL(noit_error,
            "ping_icmp: could not set socket non-blocking: %s\n",
            strerror(errno));
    }
  }
  if(data->ipv4_fd >= 0) {
    eventer_t newe;
    newe = eventer_alloc();
    newe->fd = data->ipv4_fd;
    newe->mask = EVENTER_READ;
    newe->callback = ping_icmp_handler;
    newe->closure = self;
    eventer_add(newe);
  }

  data->ipv6_fd = socket(AF_INET6, SOCK_RAW, proto->p_proto);
  if(data->ipv6_fd < 0) {
    noitL(noit_error, "ping_icmp: socket failed: %s\n",
          strerror(errno));
  }
  else {
    on = 1;
    if(ioctl(data->ipv6_fd, FIONBIO, &on)) {
      close(data->ipv6_fd);
      data->ipv6_fd = -1;
      noitL(noit_error,
            "ping_icmp: could not set socket non-blocking: %s\n",
               strerror(errno));
    }
  }
  if(data->ipv6_fd >= 0) {
    eventer_t newe;
    newe = eventer_alloc();
    newe->fd = data->ipv6_fd;
    newe->mask = EVENTER_READ;
    newe->callback = ping_icmp_handler;
    newe->closure = self;
    eventer_add(newe);
  }

  noit_module_set_userdata(self, data);
  return 0;
}

static int ping_icmp_real_send(eventer_t e, int mask,
                               void *closure, struct timeval *now) {
  struct ping_closure *pcl = (struct ping_closure *)closure;
  struct icmp *icp;
  struct ping_payload *payload;
  ping_icmp_data_t *data;
  int i;

  noitLT(nldeb, now, "ping_icmp_real_send(%s)\n", pcl->check->target);
  data = noit_module_get_userdata(pcl->self);
  icp = (struct icmp *)pcl->payload;
  payload = (struct ping_payload *)(icp + 1);
  gettimeofday(&payload->whence, NULL); /* now isn't accurate enough */
  icp->icmp_cksum = in_cksum(pcl->payload, pcl->payload_len);
  if(pcl->check->target_family == AF_INET) {
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    memcpy(&sin.sin_addr,
           &pcl->check->target_addr.addr, sizeof(sin.sin_addr));
    i = sendto(data->ipv4_fd,
               pcl->payload, pcl->payload_len, 0,
               (struct sockaddr *)&sin, sizeof(sin));
  }
  else {
    struct sockaddr_in6 sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin6_family = AF_INET6;
    memcpy(&sin.sin6_addr,
           &pcl->check->target_addr.addr6, sizeof(sin.sin6_addr));
    i = sendto(data->ipv6_fd,
               pcl->payload, pcl->payload_len, 0,
               (struct sockaddr *)&sin, sizeof(sin));
  }
  if(i != pcl->payload_len) {
    noitLT(nlerr, now, "Error sending ICMP packet to %s: %s\n",
             pcl->check->target, strerror(errno));
  }
  free(pcl->payload);
  free(pcl);
  return 0;
}
static void ping_check_cleanup(noit_module_t *self, noit_check_t *check) {
  struct check_info *ci = (struct check_info *)check->closure;
  if(ci) {
    if(ci->timeout_event) {
      eventer_remove(ci->timeout_event);
      free(ci->timeout_event->closure);
      eventer_free(ci->timeout_event);
      ci->timeout_event = NULL;
    }
    if(ci->turnaround) free(ci->turnaround);
  }
}
static int ping_icmp_send(noit_module_t *self, noit_check_t *check) {
  struct timeval when, p_int;
  struct icmp *icp;
  struct ping_payload *payload;
  struct ping_closure *pcl;
  struct check_info *ci = (struct check_info *)check->closure;
  int packet_len, i;
  eventer_t newe;
  const char *config_val;

  int interval = PING_INTERVAL;
  int count = PING_COUNT;
  if(noit_hash_retr_str(check->config, "interval", strlen("interval"),
                        &config_val))
    interval = atoi(config_val);
  if(noit_hash_retr_str(check->config, "count", strlen("count"),
                        &config_val))
    count = atoi(config_val);

  check->flags |= NP_RUNNING;
  noitL(nldeb, "ping_icmp_send(%p,%s,%d,%d)\n",
        self, check->target, interval, count);

  /* remove a timeout if we still have one -- we should unless someone
   * has set a lower timeout than the period.
   */
  if(ci->timeout_event) {
    eventer_remove(ci->timeout_event);
    free(ci->timeout_event->closure);
    eventer_free(ci->timeout_event);
    ci->timeout_event = NULL;
  }

  gettimeofday(&when, NULL);
  memcpy(&check->last_fire_time, &when, sizeof(when));

  /* Setup some stuff used in the loop */
  p_int.tv_sec = interval / 1000;
  p_int.tv_usec = (interval % 1000) * 1000;
  packet_len = sizeof(*icp) + sizeof(*payload);

  /* Prep holding spots for return info */
  ci->expected_count = count;
  if(ci->turnaround) free(ci->turnaround);
  ci->turnaround = malloc(count * sizeof(*ci->turnaround));

  ++ci->check_no;
  for(i=0; i<count; i++) {
    /* Negative means we've not received a response */
    ci->turnaround[i] = -1.0;

    newe = eventer_alloc();
    newe->callback = ping_icmp_real_send;
    newe->mask = EVENTER_TIMER;
    memcpy(&newe->whence, &when, sizeof(when));
    add_timeval(when, p_int, &when); /* Next one is a bit later */

    icp = calloc(1,packet_len);
    payload = (struct ping_payload *)(icp + 1);

    icp->icmp_type = ICMP_ECHO;
    icp->icmp_code = 0;
    icp->icmp_cksum = 0;
    icp->icmp_seq = htons(ci->seq++);
    icp->icmp_id = (((vpsized_uint)self) & 0xffff);

    uuid_copy(payload->checkid, check->checkid);
    payload->generation = check->generation;
    payload->check_no = ci->check_no;
    payload->check_pack_no = i;
    payload->check_pack_cnt = count;

    pcl = calloc(1, sizeof(*pcl));
    pcl->self = self;
    pcl->check = check;
    pcl->payload = icp;
    pcl->payload_len = packet_len;

    newe->closure = pcl;
    eventer_add(newe);
  }
  newe = eventer_alloc();
  newe->mask = EVENTER_TIMER;
  gettimeofday(&when, NULL);
  p_int.tv_sec = check->timeout / 1000;
  p_int.tv_usec = (check->timeout % 1000) * 1000;
  add_timeval(when, p_int, &newe->whence);
  pcl = calloc(1, sizeof(*pcl));
  pcl->self = self;
  pcl->check = check;
  newe->closure = pcl;
  newe->callback = ping_icmp_timeout;
  eventer_add(newe);
  ci->timeout_event = newe;

  return 0;
}
static int ping_icmp_initiate_check(noit_module_t *self, noit_check_t *check,
                                    int once, noit_check_t *cause) {
  if(!check->closure) check->closure = calloc(1, sizeof(struct check_info));
  INITIATE_CHECK(ping_icmp_send, self, check);
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

static int ping_icmp_onload(noit_image_t *self) {
  nlerr = noit_log_stream_find("error/ping_icmp");
  nldeb = noit_log_stream_find("debug/ping_icmp");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;
  eventer_name_callback("ping_icmp/timeout", ping_icmp_timeout);
  eventer_name_callback("ping_icmp/handler", ping_icmp_handler);
  return 0;
}
#include "ping_icmp.xmlh"
noit_module_t ping_icmp = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "ping_icmp",
    "ICMP based host availability detection",
    ping_icmp_xml_description,
    ping_icmp_onload
  },
  ping_icmp_config,
  ping_icmp_init,
  ping_icmp_initiate_check,
  ping_check_cleanup
};

