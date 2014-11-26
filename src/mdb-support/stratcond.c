#include "mdb_help.h"
#include "noitutils.c"
#include "eventer.c"

const mdb_modinfo_t *_mdb_init() {
  _mdb_accum(&noitutils_linkage);
  return _mdb_accum(&eventer_linkage);
}
