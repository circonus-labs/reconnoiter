#include "noit_defines.h"

#include "utils/noit_hash.h"
#include "utils/noit_atomic.h"
#include "noit_conf.h"
#include "noit_check.h"
#include "noit_conf_checks.h"
#include "noit_acl.h"

#include <pcre.h>
#include <assert.h>
#include <libxml/tree.h>

typedef struct _aclnetwork {
  char *ip_range;
  struct _aclnetwork *next;
} aclnetwork_t;

typedef struct {
  noit_atomic32_t ref_cnt;
  char *name;
  char *type;
  aclnetwork_t *networks;
  noit_hash_table *ip;
} aclset_t;

static noit_hash_table *aclsets = NULL;

static void
noit_aclnetwork_free(void *vp) {
  aclnetwork_t *n = vp;
  free(n->ip_range);
  free(n);
}

static aclnetwork_t*
noit_aclnetwork_create(const char *ip_range) {
  aclnetwork_t *net = calloc(1, sizeof(*net));
  net->ip_range = strdup(ip_range);
  return net;
}

static void
aclset_free(void *vp) {
  aclset_t *as = vp;
  aclnetwork_t *n;
  if(noit_atomic_dec32(&as->ref_cnt) != 0) return;
  noitL(noit_error, "Freeing acl [%d]: %s\n", as->ref_cnt, as->name);
  while(as->networks) {
    n = as->networks->next;
    noit_aclnetwork_free(as->networks);
    as->networks = n;
  }
  noit_hash_destroy(as->ip, NULL, NULL);
  if(as->name) free(as->name);
  if(as->type) free(as->type);
  free(as);
}

static aclset_t*
noit_aclset_create(const char *name, const char *type) {
  aclset_t *set = calloc(1, sizeof(*set));
  set->ref_cnt = 1;
  set->name = strdup(name);
  set->type = strdup(type);
  set->ip = calloc(1, sizeof(noit_hash_table*));
  noit_hash_init(set->ip);
  return set;
}

static void
noit_acl_from_conf() {
  noit_conf_section_t *sets;
  int i, cnt;

  sets = noit_conf_get_sections(NULL, "/noit/acl", &cnt);
  for(i=0; i<cnt; i++) {
    noit_acl_add(sets[i]);
  }
  free(sets);
}
void
noit_acl_add_network(aclset_t *set, aclnetwork_t *net) {
  net->next = set->networks;
  set->networks = net;
}
void
noit_acl_add_ip(aclset_t *set, const char *ip) {
  noit_hash_store(set->ip, ip, strlen(ip), NULL);
}
void
noit_acl_init() {
  aclsets = calloc(1, sizeof(*aclsets));
  noit_acl_from_conf();
}
void
noit_refresh_acl() {
}
void
noit_acl_add(noit_conf_section_t setinfo) {
  noit_conf_section_t *networks;
  char acl_name[256];
  char acl_type[256];
  aclset_t *set;
  int fcnt, j;

  if(!noit_conf_get_stringbuf(setinfo, "@name",
                              acl_name, sizeof(acl_name))) {
    noitL(noit_error,
          "acl with no name, skipping as it cannot be referenced.\n");
    return;
  }

  if(!noit_conf_get_stringbuf(setinfo, "@type",
                              acl_type, sizeof(acl_type))) {
    strcpy(acl_type, "deny");
  }

  noitL(noit_debug, "loaded ACL (name=%s, type=%s)\n", acl_name, acl_type);

  set = noit_aclset_create(acl_name, acl_type);
  networks = noit_conf_get_sections(setinfo, "network", &fcnt);

  for(j=fcnt-1; j>=0; j--) {
    char buffer[256];
    if(!noit_conf_get_stringbuf(networks[j], "@ip", buffer, sizeof(buffer))) {
      noitL(noit_error, "ip or ip range not specified\n");
      continue;
    }
    noitL(noit_debug, "  %s\n", buffer);
    if (strstr(buffer, "/")) {
      // network
      noit_acl_add_network(set, noit_aclnetwork_create(buffer));
    }
    else {
      // ip
      noit_acl_add_ip(set, buffer);
    }
  }

  noit_hash_replace(aclsets, set->name, strlen(set->name), (void *)set,
                    NULL, aclset_free);
}
noit_boolean
noit_acl_check_ip(const char *ip) {
  return noit_false;
}
