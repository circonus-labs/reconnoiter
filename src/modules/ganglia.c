#include "noit_defines.h"
#include "utils/noit_str.h"

#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>

#include "noit_module.h"
#include "noit_check.h"
#include "noit_check_tools.h"
#include "noit_rest.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"
#include "utils/noit_b64.h"

#define GANGLIA_DEFAULT_MCAST_ADDR "239.2.11.71"
#define GANGLIA_DEFAULT_MCAST_PORT 8649

static noit_module_t *global_ganglia = NULL;

typedef struct _mod_config {
  noit_hash_table *options;
  noit_hash_table target_sessions; //??
  noit_boolean asynch_metrics;
  int ipv4_fd;
  int ipv6_fd;
} ganglia_mod_config_t;

typedef struct ganglia_closure_s {
  stats_t current;
  int stats_count;
  int ntfy_count;
} ganglia_closure_t;

//based on formats from \\ganglia/monitor-core/lib/gm_protocol.x
//taken from version 3.2, 2009-12-13 15:38:58 -0500
enum Ganglia_msg_formats {
   gmetadata_full = 128, //this one refers to metadataref
   gmetric_ushort,
   gmetric_short,
   gmetric_int,
   gmetric_uint,
   gmetric_string,
   gmetric_float,
   gmetric_double,
   gmetadata_request
};

struct ganglia_dgram {
  noit_module_t *self;
  void *payload;
  int len;
  int type;
  char *name;
};

union ifd { 
  int i;
  float f;
  double d;
};

static noit_boolean
noit_collects_check_aynsch(noit_module_t *self,
                           noit_check_t *check) {
  const char *config_val;
  ganglia_mod_config_t *conf = noit_module_get_userdata(self);
  noit_boolean is_asynch = conf->asynch_metrics;
  if(noit_hash_retr_str(check->config,
                        "asynch_metrics", strlen("asynch_metrics"),
                        (const char **)&config_val)) {
    if(!strcasecmp(config_val, "false") || !strcasecmp(config_val, "off"))
      is_asynch = noit_false;
  }

  if(is_asynch) check->flags |= NP_SUPPRESS_METRICS;
  else check->flags &= ~NP_SUPPRESS_METRICS;
  return is_asynch;
}

static void clear_closure(noit_check_t *check, ganglia_closure_t *gcl) {
  gcl->stats_count = 0;
  gcl->ntfy_count = 0;
  noit_check_stats_clear(check, &gcl->current);
}

static int
ganglia_process_dgram(noit_check_t *check, void *closure) {
  struct ganglia_dgram *pkt = closure;
  noit_boolean immediate;
  ganglia_closure_t *gcl;

  if (!check || strcmp(check->module, "ganglia")) return 0;

  immediate = noit_collects_check_aynsch(pkt->self, check);
  gcl = check->closure ? (ganglia_closure_t *)check->closure : (ganglia_closure_t *)(check->closure = calloc(1, sizeof(ganglia_closure_t)));

  switch(pkt->type) {
    case gmetadata_full:
      //nothing of value to get from the metadata
      break;
    case gmetadata_request:
      break;
    case gmetric_short:
    case gmetric_int:{
      int *val = pkt->payload;
      *val = ntohl(*val);
      noit_stats_set_metric(check, &gcl->current, pkt->name, METRIC_INT32, val);
      if(immediate) noit_stats_log_immediate_metric(check, pkt->name, METRIC_INT32, val);
      break;
                     }
    case gmetric_ushort:
    case gmetric_uint:{
      uint32_t *val = pkt->payload;
      *val = ntohl(*val);
      noit_stats_set_metric(check, &gcl->current, pkt->name, METRIC_UINT32, val);
      if(immediate) noit_stats_log_immediate_metric(check, pkt->name, METRIC_UINT32, val);
      break;
                      }
    case gmetric_string:{
      uint32_t *len = pkt->payload;
      *len = ntohl(*len);
      pkt->payload += 4;
      char *str = pkt->payload;
      noit_stats_set_metric(check, &gcl->current, pkt->name, METRIC_STRING, str);
      if(immediate) noit_stats_log_immediate_metric(check, pkt->name, METRIC_STRING, str);
      break;
                        }
    case gmetric_float:{
      double val = 0;
      union ifd *ptr = pkt->payload;
      ptr->i = ntohl(ptr->i);
      val = (double) ptr->f;
      noit_stats_set_metric(check, &gcl->current, pkt->name, METRIC_DOUBLE, &val);
      if(immediate) noit_stats_log_immediate_metric(check, pkt->name, METRIC_DOUBLE, &val);
      break;    
                       }
    case gmetric_double:{
      double val = 0;
      union ifd *ptr = pkt->payload;
      ptr->i = ntohl(ptr->i);
      (ptr+4)->i = ntohl((ptr+4)->i);
      val = ptr->d;
      noit_stats_set_metric(check, &gcl->current, pkt->name, METRIC_DOUBLE, &val);
      if(immediate) noit_stats_log_immediate_metric(check, pkt->name, METRIC_DOUBLE, &val);
      break;
                        }
    default:
      noitL(noit_error, "ganglia: unknown packet type received\n");
      return 0;
  }

  gcl->stats_count++;

  return 1;
}

static int noit_ganglia_handler(eventer_t e, int mask, void *closure,
                             struct timeval *now) {
  char packet[1500]; //1500 is correct; see __GANGLIA_MTU
  int packet_len = sizeof(packet);
  noit_module_t *self = (noit_module_t *)closure;

  while(1) {
    struct ganglia_dgram pkt;
    int inlen;
    void *payload = &packet;
    char *host, *name;
    int *len;
    uint32_t *type;

    inlen = recvfrom(e->fd, packet, packet_len, 0, NULL, 0);

    if(inlen < 0) {
      if(errno == EAGAIN) break; //out of data to read, hand it back to eventer
                                 //and wait to be scheduled again
      noitLT(noit_error, now, "ganglia: recvfrom: %s\n", strerror(errno));
      break;
    }

    type = payload;
    *type = ntohl(*type);
    payload += 4;

    len = payload;
    *len = ntohl(*len);
    if(!*len) {
      noitL(noit_error, "ganglia: empty host\n");
      return -1;
    }
    payload += 4;
    host = calloc(1, *len + 1);
    memcpy(host, payload, *len);
    payload += (*len + 3) & ~0x03;

    len = payload;
    *len = ntohl(*len);
    if(!*len) {
      noitL(noit_error, "ganglia: empty name\n");
      return -1;
    }
    payload += 4;
    name = calloc(1, *len + 1);
    memcpy(name, payload, *len);
    payload += (*len + 3) & ~0x03;

    //skip the spoof boolean
    payload += 4;

    //skip the format string
    len = payload;
    *len = ntohl(*len);
    payload += 4;
    payload += (*len + 3) & ~0x03;

    pkt.self = self;
    pkt.payload = payload;
    pkt.len = inlen;
    pkt.type = *type;
    pkt.name = name;

    if(!noit_poller_target_do(host, ganglia_process_dgram, &pkt))
      noitL(noit_error, "ganglia: no checks from host: %s\n", host);

    free(host);
    free(name);
  }
  return EVENTER_READ | EVENTER_EXCEPTION;
}

static int
ganglia_submit(noit_module_t *self, noit_check_t *check,
                         noit_check_t *cause) {
  //almost entirely pulled from collectd.c:collectd_submit_internal()
  ganglia_closure_t *gcl;
  struct timeval now, duration, age;
  noit_boolean immediate;
  /* We are passive, so we don't do anything for transient checks */
  if(check->flags & NP_TRANSIENT) return 0;

  gettimeofday(&now, NULL);

  /* If we're immediately logging things and we've done so within the
   * check's period... we've no reason to passively log now.
   */
  immediate = noit_collects_check_aynsch(self, check);
  sub_timeval(now, check->stats.current.whence, &age);
  if(immediate && (age.tv_sec * 1000 + age.tv_usec / 1000) < check->period)
    return 0;

  if(!check->closure) {
    gcl = check->closure = (void *)calloc(1, sizeof(ganglia_closure_t)); 
    memset(gcl, 0, sizeof(ganglia_closure_t));
    memcpy(&gcl->current.whence, &now, sizeof(now));
  } else {
    // Don't count the first run
    char human_buffer[256];
    gcl = (ganglia_closure_t*)check->closure; 
    memcpy(&gcl->current.whence, &now, sizeof(now));
    sub_timeval(gcl->current.whence, check->last_fire_time, &duration);
    gcl->current.duration = duration.tv_sec; // + duration.tv_usec / (1000 * 1000);

    snprintf(human_buffer, sizeof(human_buffer),
             "dur=%d,run=%d,stats=%d,ntfy=%d", gcl->current.duration, 
             check->generation, gcl->stats_count, gcl->ntfy_count);
    noitL(noit_debug, "ganglia(%s) [%s]\n", check->target, human_buffer);

    gcl->current.available = (gcl->ntfy_count > 0 || gcl->stats_count > 0) ? 
        NP_AVAILABLE : NP_UNAVAILABLE;
    gcl->current.state = (gcl->ntfy_count > 0 || gcl->stats_count > 0) ? 
        NP_GOOD : NP_BAD;
    gcl->current.status = human_buffer;
    noit_check_passive_set_stats(check, &gcl->current);

    memcpy(&check->last_fire_time, &gcl->current.whence, sizeof(duration));
  }
  clear_closure(check, gcl);
  return 0;
}

static int noit_ganglia_initiate_check(noit_module_t *self,
                                        noit_check_t *check,
                                        int once, noit_check_t *cause) {
  check->flags |= NP_PASSIVE_COLLECTION;
  INITIATE_CHECK(ganglia_submit, self, check, cause);
  return 0;
}

static int noit_ganglia_config(noit_module_t *self, noit_hash_table *options) {
  ganglia_mod_config_t *conf = noit_module_get_userdata(self);
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

static int noit_ganglia_onload(noit_image_t *self) {
  eventer_name_callback("noit_ganglia/handler", noit_ganglia_handler);
  return 0;
}

static int noit_ganglia_init(noit_module_t *self) {
  const char *config_val;
  ganglia_mod_config_t *conf;
  conf = noit_module_get_userdata(self);
  struct ip_mreq mreq;
  struct ipv6_mreq mreqv6;
  struct sockaddr_in skaddr;
  struct sockaddr_in6 skaddr6;
  const char *multiaddr, *multiaddr6;
  int portint=0;
  unsigned short port;

  //if already initialized, fail
  if(global_ganglia) return -1;

  conf->asynch_metrics = noit_true;
  if(noit_hash_retr_str(conf->options,
                        "asynch_metrics", strlen("asynch_metrics"),
                        (const char **)&config_val)) {
    if(!strcasecmp(config_val, "false") || !strcasecmp(config_val, "off"))
      conf->asynch_metrics = noit_false;
  }

  /* Default Collectd port */
  portint = GANGLIA_DEFAULT_MCAST_PORT;
  if(noit_hash_retr_str(conf->options,
                         "ganglia_port", strlen("ganglia_port"),
                         (const char**)&config_val))
    portint = atoi(config_val);
  port = (unsigned short) portint;

  if(!noit_hash_retr_str(conf->options,
                         "ganglia_multiaddr", strlen("ganglia_multiaddr"),
                         (const char**)&multiaddr))
    multiaddr = GANGLIA_DEFAULT_MCAST_ADDR;


  conf->ipv4_fd = conf->ipv6_fd = -1;

  //ipv4 socket and binding
  conf->ipv4_fd = socket(PF_INET, NE_SOCK_CLOEXEC|SOCK_DGRAM, IPPROTO_UDP);
  if(conf->ipv4_fd < 0) {
    close(conf->ipv4_fd);
    noitL(noit_error, "ganglia: ipv4 socket failed: %s\n", strerror(errno));
    return -1;
  }

  if(eventer_set_fd_nonblocking(conf->ipv4_fd)) {
    close(conf->ipv4_fd);
    noitL(noit_error, "ganglia: could not set ipv4 socket non-blocking: %s\n", strerror(errno));
    return -1;
  }

  //ipv4 binding
  memset(&skaddr, 0, sizeof(skaddr));
  skaddr.sin_family = AF_INET;
  skaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  skaddr.sin_port = htons(port);

  if(bind(conf->ipv4_fd, (struct sockaddr *)&skaddr, sizeof(skaddr))) {
    close(conf->ipv4_fd);
    noitL(noit_error, "ganglia: ipv4 binding failed: %s\n", strerror(errno));
    return -1;
  }

  //join ipv4 multicast
  memset(&mreq, 0, sizeof(mreq));

  inet_pton(AF_INET, multiaddr, &mreq.imr_multiaddr.s_addr);

  mreq.imr_interface.s_addr = htonl(INADDR_ANY);

  if(setsockopt(conf->ipv4_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq))) {
    close(conf->ipv4_fd);
    noitL(noit_error, "ganglia: ipv4 multicast join failed: %s\n", strerror(errno));
    return -1;
  }

  eventer_t newe;
  newe = eventer_alloc();
  newe->fd = conf->ipv4_fd;
  newe->mask = EVENTER_READ | EVENTER_EXCEPTION;
  newe->callback = noit_ganglia_handler;
  newe->closure = self;
  eventer_add(newe);
  noitL(noit_debug, "Added ipv4 handler!\n");

  portint = GANGLIA_DEFAULT_MCAST_PORT;
  if(noit_hash_retr_str(conf->options,
                         "ganglia_port6", strlen("ganglia_port6"),
                         (const char**)&config_val))
    portint = atoi(config_val);
  port = (unsigned short) portint;

  if(!noit_hash_retr_str(conf->options,
                         "ganglia_multiaddr6", strlen("ganglia_multiaddr6"),
                         (const char**)&multiaddr6))
    //ganglia doesn't have a default ipv6 multicast
    multiaddr6 = NULL;

  //ipv6 socket, nonblocking
  if(multiaddr6) { 
    conf->ipv6_fd = socket(AF_INET6, NE_SOCK_CLOEXEC|SOCK_DGRAM, IPPROTO_UDP);
    if(conf->ipv6_fd < 0) {
      noitL(noit_error, "ganglia: IPv6 socket creation failed: %s\n", strerror(errno));
    }
    else if(eventer_set_fd_nonblocking(conf->ipv6_fd)) {
      noitL(noit_error, "ganglia: could not set socket non-blocking: %s\n", strerror(errno));
      close(conf->ipv6_fd);
      conf->ipv6_fd =  -1;
    }
  }

  //ipv6 binding
  if(conf->ipv6_fd > 0) {
    memset(&skaddr6, 0, sizeof(skaddr6));
    skaddr6.sin6_family = AF_INET6;
    skaddr6.sin6_addr = in6addr_any;
    skaddr6.sin6_port = htons(port);
    if(bind(conf->ipv6_fd, (struct sockaddr *)&skaddr6, sizeof(skaddr6))) {
      noitL(noit_error, "ganglia: ipv6 binding failed: %s\n", strerror(errno));
      close(conf->ipv6_fd);
      conf->ipv6_fd = -1;
    }
  }

  //join ipv6 multicast
  if(conf->ipv6_fd > 0) {
    memset(&mreqv6, 0, sizeof(mreqv6));

    inet_pton(AF_INET6, multiaddr6, &mreqv6.ipv6mr_multiaddr.s6_addr);

    mreqv6.ipv6mr_interface = 0;

    if(setsockopt(conf->ipv6_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreqv6, sizeof(mreqv6))) {
      noitL(noit_error, "ganglia: ipv6 multicast join failed: %s\n", strerror(errno));
      close(conf->ipv6_fd);
      conf->ipv6_fd = -1;
    }
  }

  if(conf->ipv6_fd > 0) {
    eventer_t newe;
    newe = eventer_alloc();
    newe->fd = conf->ipv6_fd;
    newe->mask = EVENTER_READ;
    newe->callback = noit_ganglia_handler;
    newe->closure = self;
    eventer_add(newe);
    noitL(noit_debug, "Added ipv4 handler!\n");
  }

  noit_module_set_userdata(self, conf);

  global_ganglia = self;
  return 0;
}

#include "ganglia.xmlh"
noit_module_t ganglia = {
  {
    NOIT_MODULE_MAGIC,
    NOIT_MODULE_ABI_VERSION,
    "ganglia",
    "ganglia collection",
    ganglia_xml_description,
    noit_ganglia_onload
  },
  noit_ganglia_config,
  noit_ganglia_init,
  noit_ganglia_initiate_check,
  NULL
};
