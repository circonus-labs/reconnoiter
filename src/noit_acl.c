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

static void
noit_acl_from_conf() {
  noit_conf_section_t *sets;
  int i, cnt;

  sets = noit_conf_get_sections(NULL, "/noit/acl//network", &cnt);
  for(i=0; i<cnt; i++) {
    noit_acl_add(sets[i]);
  }
  free(sets);
}
void
noit_acl_init() {
  noit_acl_from_conf();
}
void
noit_refresh_acl() {
}
void
noit_acl_add(noit_conf_section_t setinfo) {

}
noit_boolean
noit_acl_check_ip(const char *ip) {
  return noit_false;
}
