/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <math.h>

#include "noit_module.h"
#include "noit_poller.h"
#include "utils/noit_log.h"

#define PING_INTERVAL 2000 /* 2000ms = 2s */
#define PING_COUNT    5

struct check_info {
  int check_no;
  int check_seq_no;
  int seq;
  int expected_count;
  float *turnaround;
  eventer_t timeout_event;
};
struct ping_payload {
  uuid_t checkid;
  struct timeval whence;
  int    check_no;
  int    check_pack_no;
  int    check_pack_cnt;
};
struct ping_closure {
  noit_module_t *self;
  noit_check_t check;
  void *payload;
  int payload_len;
};
static noit_log_stream_t nlerr = NULL;
static noit_log_stream_t nldeb = NULL;
static int ping_icmp_recur_handler(eventer_t e, int mask, void *closure,
                                   struct timeval *now);
static int in_cksum(u_short *addr, int len);

typedef struct  {
  int ipv4_fd;
  int ipv6_fd;
} ping_icmp_data_t;

static int ping_icmp_onload(noit_module_t *self) {
  nlerr = noit_log_stream_find("error/ping_icmp");
  nldeb = noit_log_stream_find("debug/ping_icmp");
  if(!nlerr) nlerr = noit_stderr;
  if(!nldeb) nldeb = noit_debug;
  return 0;
}
static int ping_icmp_config(noit_module_t *self, noit_hash_table *options) {
  return 0;
}
static int ping_icmp_is_complete(noit_module_t *self, noit_check_t check) {
  int i;
  struct check_info *data;
  data = (struct check_info *)check->closure;
  for(i=0; i<data->expected_count; i++)
    if(data->turnaround[i] == 0.0) {
      noit_log(nldeb, NULL, "ping_icmp: %s %d is still outstanding.\n",
               check->target, i);
      return 0;
    }
  return 1;
}
static void ping_icmp_log_results(noit_module_t *self, noit_check_t check) {
  struct check_info *data;
  float avail, min = MAXFLOAT, max = 0.0, avg = 0.0, cnt;
  int i, points = 0;

  data = (struct check_info *)check->closure;
  for(i=0; i<data->expected_count; i++) {
    if(data->turnaround[i] != 0) {
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
  noit_log(nldeb, NULL, "ping_icmp(%s) [cnt=%d,avail=%0.0f,min=%0.4f,max=%0.4f,avg=%0.4f]\n", check->target, (int)cnt, 100.0*avail, min, max, avg);
}
static int ping_icmp_timeout(eventer_t e, int mask,
                             void *closure, struct timeval *now) {
  struct ping_closure *pcl = (struct ping_closure *)closure;
  struct check_info *data;
  ping_icmp_log_results(pcl->self, pcl->check);
  data = (struct check_info *)pcl->check->closure;
  data->timeout_event = NULL;
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
  struct ip *ip = (struct ip *)packet;;
  struct icmp *icp;
  struct ping_payload *payload;

  while(1) {
    float t1, t2;
    int inlen, iphlen;
    noit_check_t check;
    struct timeval tt;

    from_len = sizeof(from);

    inlen = recvfrom(e->fd, packet, packet_len, 0,
                     (struct sockaddr *)&from, &from_len);
    gettimeofday(now, NULL); /* set it, as we care about accuracy */

    if(inlen < 0) {
      if(errno == EAGAIN || errno == EINTR) break;
      noit_log(nlerr, now, "ping_icmp recvfrom: %s\n", strerror(errno));
      break;
    }
    iphlen = ip->ip_hl << 2;
    if((inlen-iphlen) != (sizeof(struct icmp)+sizeof(struct ping_payload))) {
      noit_log(nlerr, now,
               "ping_icmp bad size: %d+%d\n", iphlen, inlen-iphlen); 
      continue;
    }
    icp = (struct icmp *)(packet + iphlen);
    payload = (struct ping_payload *)(icp + 1);
    if(icp->icmp_type != ICMP_ECHOREPLY) {
      continue;
    }
    if(icp->icmp_id != (unsigned short)self) {
      noit_log(nlerr, now,
               "ping_icmp not sent from this instance (%d:%d) vs. %d\n",
               icp->icmp_id, ntohs(icp->icmp_seq), (unsigned short)self);
      continue;
    }
    check = noit_poller_lookup(payload->checkid);
    if(!check) {
      char uuid_str[37];
      uuid_unparse_lower(payload->checkid, uuid_str);
      noit_log(nlerr, now,
               "ping_icmp response for unknown check '%s'\n", uuid_str);
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
    t1 = (float)tt.tv_sec + (float)tt.tv_usec / 1000000.0;
    data->turnaround[payload->check_pack_no] = t1;
    if(ping_icmp_is_complete(self, check)) {
      ping_icmp_log_results(self, check);
      eventer_remove(data->timeout_event);
      free(data->timeout_event->closure);
      eventer_free(data->timeout_event);
      data->timeout_event = NULL;
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
    noit_log(nlerr, NULL, "Couldn't find 'icmp' protocol\n");
    return -1;
  }

  data->ipv4_fd = socket(AF_INET, SOCK_RAW, proto->p_proto);
  if(data->ipv4_fd < 0) {
    noit_log(nlerr, NULL, "ping_icmp: socket failed: %s\n",
             strerror(errno));
  }
  else {
    on = 1;
    if(ioctl(data->ipv4_fd, FIONBIO, &on)) {
      close(data->ipv4_fd);
      data->ipv4_fd = -1;
      noit_log(nlerr, NULL,
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
    noit_log(nlerr, NULL, "ping_icmp: socket failed: %s\n",
             strerror(errno));
  }
  else {
    on = 1;
    if(ioctl(data->ipv6_fd, FIONBIO, &on)) {
      close(data->ipv6_fd);
      data->ipv6_fd = -1;
      noit_log(nlerr, NULL,
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

  noit_log(nldeb, NULL, "ping_icmp_real_send(%s)\n", pcl->check->target);
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
    noit_log(nlerr, now, "Error sending ICMP packet to %s: %s\n",
             pcl->check->target, strerror(errno));
  }
  free(pcl->payload);
  free(pcl);
  return 0;
}
static int ping_icmp_send(noit_module_t *self, noit_check_t check,
                          int interval, int count) {
  struct timeval when, p_int;
  struct icmp *icp;
  struct ping_payload *payload;
  struct ping_closure *pcl;
  struct check_info *ci = (struct check_info *)check->closure;
  int packet_len, i;
  eventer_t newe;

  noit_log(nldeb, NULL, "ping_icmp_send(%p,%s,%d,%d)\n",
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
  ci->turnaround = calloc(count, sizeof(*ci->turnaround));

  ++ci->check_no;
  for(i=0; i<count; i++) {
    newe = eventer_alloc();
    newe->callback = ping_icmp_real_send;
    newe->mask = EVENTER_TIMER;
    memcpy(&newe->whence, &when, sizeof(when));
    add_timeval(when, p_int, &when); /* Next one is a bit later */

    icp = malloc(packet_len);
    payload = (struct ping_payload *)(icp + 1);

    icp->icmp_type = ICMP_ECHO;
    icp->icmp_code = 0;
    icp->icmp_cksum = 0;
    icp->icmp_seq = htons(ci->seq++);
    icp->icmp_id = (unsigned short)self;

    uuid_copy(payload->checkid, check->checkid);
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
static int ping_icmp_schedule_next(noit_module_t *self,
                                   eventer_t e, noit_check_t check,
                                   struct timeval *now) {
  eventer_t newe;
  struct timeval last_check = { 0L, 0L };
  struct timeval period, earliest;
  struct ping_closure *pcl;

  /* If we have an event, we know when we intended it to fire.  This means
   * we should schedule that point + period.
   */
  if(now)
    memcpy(&earliest, now, sizeof(earliest));
  else
    gettimeofday(&earliest, NULL);
  if(e) memcpy(&last_check, &e->whence, sizeof(last_check));
  period.tv_sec = check->period / 1000;
  period.tv_usec = (check->period % 1000) * 1000;

  newe = eventer_alloc();
  memcpy(&newe->whence, &last_check, sizeof(last_check));
  add_timeval(newe->whence, period, &newe->whence);
  if(compare_timeval(newe->whence, earliest) < 0)
    memcpy(&newe->whence, &earliest, sizeof(earliest));
  newe->mask = EVENTER_TIMER;
  newe->callback = ping_icmp_recur_handler;
  pcl = calloc(1, sizeof(*pcl));
  pcl->self = self;
  pcl->check = check;
  newe->closure = pcl;

  eventer_add(newe);
  check->fire_event = newe;
  return 0;
}
static int ping_icmp_recur_handler(eventer_t e, int mask, void *closure,
                                   struct timeval *now) {
  struct ping_closure *cl = (struct ping_closure *)closure;
  ping_icmp_schedule_next(cl->self, e, cl->check, now);
  ping_icmp_send(cl->self, cl->check, PING_INTERVAL, PING_COUNT);
  free(cl);
  return 0;
}
static int ping_icmp_initiate_check(noit_module_t *self, noit_check_t check) {
  check->closure = calloc(1, sizeof(struct check_info));
  ping_icmp_schedule_next(self, NULL, check, NULL);
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

noit_module_t ping_icmp = {
  NOIT_MODULE_MAGIC,
  NOIT_MODULE_ABI_VERSION,
  "ping_icmp",
  "ICMP based host availability detection",
  ping_icmp_onload,
  ping_icmp_config,
  ping_icmp_init,
  ping_icmp_initiate_check
};

