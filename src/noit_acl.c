#include "noit_defines.h"

#include "utils/noit_hash.h"
#include "utils/noit_atomic.h"
#include "noit_conf.h"
#include "noit_check.h"
#include "noit_conf_checks.h"
#include "noit_acl.h"
#include "libcidr.h"

#include <pcre.h>
#include <assert.h>
#include <libxml/tree.h>

typedef enum {
  ACL_ALLOW = 0,
  ACL_DENY
} acltype_t;

typedef struct _aclcidr_t {
  CIDR *cidr;
  struct _aclcidr_t *next;
} aclcidr_t;

typedef struct {
  noit_atomic32_t ref_cnt;
  char *name;
  acltype_t type;
  aclcidr_t *cidrs;
} aclset_t;

static noit_hash_table *aclsets = NULL;

static void
noit_aclcidr_free(void *vp) {
  aclcidr_t *n = vp;
  cidr_free(n->cidr);
  free(n);
}

static aclcidr_t*
noit_aclcidr_create(const char *range) {
  aclcidr_t *c = calloc(1, sizeof(*c));
  c->cidr = cidr_from_str(range);
  return c;
}

static void
aclset_free(void *vp) {
  aclset_t *as = vp;
  aclcidr_t *n;
  if(noit_atomic_dec32(&as->ref_cnt) != 0) return;
  noitL(noit_error, "Freeing acl [%d]: %s\n", as->ref_cnt, as->name);
  while(as->cidrs) {
    n = as->cidrs->next;
    noit_aclcidr_free(as->cidrs);
    as->cidrs = n;
  }
  if(as->name) free(as->name);
  free(as);
}

static aclset_t*
noit_acl_create(const char *name, const char *type) {
  aclset_t *set = calloc(1, sizeof(*set));
  set->ref_cnt = 1;
  set->name = strdup(name);
  set->type = strcasecmp(type, "deny") == 0 ? ACL_DENY : ACL_ALLOW;
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
noit_acl_add_cidr(aclset_t *set, const char *range) {
  aclcidr_t *cidr = noit_aclcidr_create(range);
  cidr->next = set->cidrs;
  set->cidrs = cidr;
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
    noitL(noit_error,
          "acl with no type, skipping as it cannot be referenced.\n");
    return;
  }

  noitL(noit_debug, "loaded ACL (name=%s, type=%s)\n", acl_name, acl_type);

  set = noit_acl_create(acl_name, acl_type);
  networks = noit_conf_get_sections(setinfo, "network", &fcnt);

  for(j=fcnt-1; j>=0; j--) {
    char buffer[256];
    if(!noit_conf_get_stringbuf(networks[j], "@ip", buffer, sizeof(buffer))) {
      noitL(noit_error, "ip or ip range not specified\n");
      continue;
    }
    noitL(noit_debug, "  %s\n", buffer);
    noit_acl_add_cidr(set, buffer);
  }

  noit_hash_replace(aclsets, set->name, strlen(set->name), (void *)set,
                    NULL, aclset_free);
}
noit_boolean
noit_acl_check_ip(const char *ip) {
  // check networks
  return noit_false;
}
