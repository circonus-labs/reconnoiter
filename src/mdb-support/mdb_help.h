#ifndef MDB_HELP_H
#define MDB_HELP_H

#include <sys/mdb_modapi.h>
extern const mdb_modinfo_t *_mdb_accum(const mdb_modinfo_t *toadd);
extern int _print_addr_cb(uintptr_t addr, const void *u, void *data);

#endif
