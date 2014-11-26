#include <sys/mdb_modapi.h>
#include "eventer/eventer.h"

struct fds_data {
  void *readonce;
  int idx;
  int maxfds;
};
static int noit_fds_walk_init(mdb_walk_state_t *s) {
  struct fds_data *fds;
  struct _eventer_impl l;
  if(mdb_readsym(&l, sizeof(l), "eventer_ports_impl") == -1) return WALK_ERR;
  fds = mdb_alloc(sizeof(*fds), UM_GC);
  if(!fds) {
    mdb_warn("allocation failure\n");
    return WALK_ERR;
  }
  fds->idx = 0;
  fds->maxfds = l.maxfds;
  fds->readonce = mdb_alloc(sizeof(l.master_fds[0])*l.maxfds, UM_GC);
  if(!fds->readonce) {
    mdb_warn("allocation failure\n");
    return WALK_ERR;
  }
  if(mdb_vread(fds->readonce, sizeof(l.master_fds[0])*l.maxfds, (uintptr_t)l.master_fds) == -1) {
    mdb_warn("invalid read of master_fds\n");
    return WALK_ERR;
  }
  s->walk_data = fds;
  return WALK_NEXT;
}
static int noit_fds_walk_step(mdb_walk_state_t *s) {
  struct fds_data *fds = s->walk_data;
  struct _eventer_impl l;
  struct _event e;
  eventer_t *eptr;
  void *dummy = NULL;

  for(; fds->idx < fds->maxfds; fds->idx++) {
    eptr = fds->readonce + sizeof(l.master_fds[0]) * fds->idx;
    if(*eptr == NULL) continue;
    if(mdb_vread(&e, sizeof(e), (uintptr_t)*eptr) == -1) return WALK_ERR;
    if(e.fd != fds->idx) continue;
    s->walk_addr = (uintptr_t)*eptr;
    s->walk_callback(s->walk_addr, &dummy, s->walk_cbdata);
    fds->idx++;
    return WALK_NEXT;
  }
  return WALK_DONE;
}

static void noit_fds_walk_fini(mdb_walk_state_t *s) {
}

static mdb_walker_t _eventer_walkers[] = {
  {
  .walk_name = "eventer_fds",
  .walk_descr = "walk all struct _event for fds registered with the eventer",
  .walk_init = noit_fds_walk_init,
  .walk_step = noit_fds_walk_step,
  .walk_fini = noit_fds_walk_fini,
  .walk_init_arg = NULL
  },
  { NULL }
};

struct jobq_crutch {
  int flags;
  const char *name;
};

static int _jobq_cb(uintptr_t addr, const void *u, void *data) {
  char queue_name[128];
  struct jobq_crutch *c = data;
  eventer_jobq_t jq;
  if(mdb_vread(&jq, sizeof(jq), (uintptr_t)addr) == -1) return WALK_ERR;
  if(mdb_readstr(queue_name, sizeof(queue_name), (uintptr_t)jq.queue_name) == -1) return WALK_ERR;
  if(!c->name || !strcmp(c->name, queue_name))
    mdb_printf("%p\n", addr);
  return WALK_NEXT;
}

static int
noit_jobq_dcmds(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv) {
  struct jobq_crutch c = { .flags = flags, .name = NULL };
  GElf_Sym sym;
  int rv;
  if(argc == 1 && argv[0].a_type == MDB_TYPE_STRING) c.name = argv[0].a_un.a_str;
  else if(argc != 0) return DCMD_USAGE;
  if(mdb_lookup_by_name("all_queues", &sym) == -1) return DCMD_ERR;
  rv = mdb_pwalk("noit_hash", _jobq_cb, &c, sym.st_value);
  return (rv == WALK_DONE) ? DCMD_OK : DCMD_ERR;
}

static int
noit_timed_dcmds(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv) {
  GElf_Sym sym;
  uintptr_t deref_addr;
  int rv;
  /* this is a pointer to a skiplist, deref */
  if(mdb_lookup_by_name("timed_events", &sym) == -1) return DCMD_ERR;
  if(mdb_vread(&deref_addr, sizeof(deref_addr), sym.st_value) == -1) return DCMD_ERR;
  rv = mdb_pwalk("noit_skiplist", _print_addr_cb, NULL, deref_addr);
  return (rv == WALK_DONE) ? DCMD_OK : DCMD_ERR;
}

static int
noit_fds_dcmds(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv) {
  int fd;
  struct _eventer_impl l;
  struct _event e;
  eventer_t eptr;

  if(argc == 0) {
    int rv = mdb_walk("eventer_fds", _print_addr_cb, NULL);
    return (rv == WALK_DONE) ? DCMD_OK : DCMD_ERR;
  }

  if(argc != 1 || argv[0].a_type != MDB_TYPE_STRING)
    return DCMD_USAGE;
  fd = (int)mdb_strtoull(argv[0].a_un.a_str);
  
  if(mdb_readsym(&l, sizeof(l), "eventer_ports_impl") == -1) return DCMD_ERR;
  if(fd < 0 || fd > l.maxfds) {
    mdb_warn("fd overflow\n");
    return DCMD_ERR;
  }
  if(mdb_vread(&eptr, sizeof(eptr), (uintptr_t)&l.master_fds[fd]) == -1) return DCMD_ERR;
  if(eptr == NULL) return DCMD_OK;
  if(mdb_vread(&e, sizeof(e), (uintptr_t)eptr) == -1) return DCMD_ERR;
  if(e.fd != fd) {
    mdb_warn("fd in slot doesn't match. possible corruption.\n");
    return DCMD_ERR;
  }
  mdb_printf("%p", eptr);
  return DCMD_OK;
}

static const mdb_bitmask_t mask_bits[] = {
  { "READ", EVENTER_READ, EVENTER_READ},
  { "WRITE", EVENTER_WRITE, EVENTER_WRITE},
  { "EXCEPTION", EVENTER_EXCEPTION, EVENTER_EXCEPTION},
  { "TIMER", EVENTER_TIMER, EVENTER_TIMER},
  { "RECURRENT", EVENTER_RECURRENT, EVENTER_RECURRENT},
  { "ASYNCH", EVENTER_ASYNCH, EVENTER_ASYNCH},
  { "ASYNCH_WORK", EVENTER_ASYNCH, EVENTER_ASYNCH_WORK},
  { "ASYNCH_CLEANUP", EVENTER_ASYNCH, EVENTER_ASYNCH_CLEANUP},
  { "EVIL_BRUTAL", EVENTER_EVIL_BRUTAL, EVENTER_EVIL_BRUTAL},
  { "CANCEL_DEFERRED", EVENTER_CANCEL, EVENTER_CANCEL_DEFERRED},
  { "CANCEL_ASYNCH", EVENTER_CANCEL, EVENTER_CANCEL_ASYNCH},
  { "CANCEL", EVENTER_CANCEL, EVENTER_CANCEL},
  { NULL, 0, 0 }
};

static int
noit_print_event_dcmds(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv) {
  struct _event e;
  if(mdb_vread(&e, sizeof(e), addr) == -1) {
    mdb_warn("invalid read\n");
    return DCMD_ERR;
  }
  mdb_printf("%a = {\n", addr);
  mdb_inc_indent(4);
  mdb_printf("callback = %a\n", e.callback);
  mdb_printf("closure  = %a\n", e.closure);
  mdb_printf("fd       = %x (%dt)\n", e.fd, e.fd);
  mdb_printf("opset    = %a (%x)\n", e.opset, e.opset_ctx);
  mdb_printf("mask     = %hb\n", e.mask, mask_bits);
  if(e.whence.tv_sec)
    mdb_printf("whence   = %Y.%06d\n", e.whence.tv_sec, e.whence.tv_usec);
  mdb_dec_indent(4);
  mdb_printf("}\n");
  return DCMD_OK;
}

static mdb_dcmd_t _eventer_dcmds[] = {
  {
    "print_event",
    "",
    "prints the event",
    noit_print_event_dcmds,
    NULL,
    NULL
  },
  {
    "eventer_fd",
    "<fd>",
    "returns the struct _event for the specified fd (or all)",
    noit_fds_dcmds,
    NULL,
    NULL
  },
  {
    "eventer_jobq",
    "[name]",
    "returns the event_jobq_t (all or just name)",
    noit_jobq_dcmds,
    NULL,
    NULL
  },
  {
    "eventer_timed",
    "",
    "returns all timed struct _event",
    noit_timed_dcmds,
    NULL,
    NULL
  },
  { NULL }
};

static mdb_modinfo_t eventer_linkage = {
  .mi_dvers = MDB_API_VERSION,
  .mi_dcmds = _eventer_dcmds,
  .mi_walkers = _eventer_walkers
};
