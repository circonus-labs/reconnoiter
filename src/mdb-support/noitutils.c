#include <sys/mdb_modapi.h>
#include "utils/noit_skiplist.h"
#include "utils/noit_hash.h"

static int noit_skiplist_walk_init(mdb_walk_state_t *s) {
  noit_skiplist l;
  noit_skiplist_node n;
  if(mdb_vread(&l, sizeof(l), s->walk_addr) == -1) return WALK_ERR;
  if(l.bottom == NULL) return WALK_DONE;
  if(mdb_vread(&n, sizeof(n), (uintptr_t)l.bottom) == -1) return WALK_ERR;
  s->walk_addr = (uintptr_t)n.data;
  s->walk_data = n.next;
  return WALK_NEXT;
}
static int noit_skiplist_walk_step(mdb_walk_state_t *s) {
  noit_skiplist_node n;
  void *dummy = NULL;
  if(s->walk_data == NULL) return WALK_DONE;
  if(mdb_vread(&n, sizeof(n), (uintptr_t)s->walk_data) == -1) return WALK_ERR;
  s->walk_addr = (uintptr_t)n.data;
  s->walk_callback(s->walk_addr, &dummy, s->walk_cbdata);
  s->walk_data = n.next;
  return WALK_NEXT;
}

static void noit_skiplist_walk_fini(mdb_walk_state_t *s) {
}

/* This section needs to be kept current with libck */
#if defined(CK_F_PR_LOAD_8) && defined(CK_F_PR_STORE_8)
#define CK_HT_WORD      uint8_t
#elif defined(CK_F_PR_LOAD_16) && defined(CK_F_PR_STORE_16)
#define CK_HT_WORD      uint16_t
#elif defined(CK_F_PR_LOAD_32) && defined(CK_F_PR_STORE_32)
#define CK_HT_WORD      uint32_t
#else
#error "ck_ht is not supported on your platform."
#endif
struct ck_ht_map {
  unsigned int mode;
  uint64_t deletions;
  uint64_t probe_maximum;
  uint64_t probe_length;
  uint64_t probe_limit;
  uint64_t size;
  uint64_t n_entries;
  uint64_t mask;
  uint64_t capacity;
  uint64_t step;
  CK_HT_WORD *probe_bound;
  struct ck_ht_entry *entries;
};
/* end libck sync section */

struct hash_helper {
  int size;
  int bucket;
  ck_ht_entry_t *buckets;
  ck_ht_entry_t *vmem;
};
static int noit_hash_walk_init(mdb_walk_state_t *s) {
  noit_hash_table l;
  struct ck_ht_map map;
  struct hash_helper *hh;
  void *dummy = NULL;
  if(mdb_vread(&l, sizeof(l), s->walk_addr) == -1) return WALK_ERR;
  if(mdb_vread(&map, sizeof(map), (uintptr_t)l.ht.map) == -1) return WALK_ERR;
  if(map.n_entries == 0) return WALK_DONE;
  hh = mdb_zalloc(sizeof(struct hash_helper), UM_GC);
  hh->size = map.capacity;
  hh->buckets = mdb_alloc(sizeof(ck_ht_entry_t) * map.capacity, UM_GC);
  s->walk_data = hh;
  hh->vmem = (ck_ht_entry_t *)map.entries;
  mdb_vread(hh->buckets, sizeof(ck_ht_entry_t) * map.capacity, (uintptr_t)hh->vmem);
  for(;hh->bucket<hh->size;hh->bucket++) {
    if(!ck_ht_entry_empty(&hh->buckets[hh->bucket]) && hh->buckets[hh->bucket].key != CK_HT_KEY_TOMBSTONE) {
      s->walk_addr = (uintptr_t)&hh->vmem[hh->bucket];
      s->walk_callback(s->walk_addr, &dummy, s->walk_cbdata);
      hh->bucket++;
      return WALK_NEXT;
    }
  }
  return WALK_DONE;
}
static int noit_hash_walk_step(mdb_walk_state_t *s) {
  void *dummy = NULL;
  struct hash_helper *hh = s->walk_data;
  if(s->walk_data == NULL) return WALK_DONE;
  for(;hh->bucket<hh->size;hh->bucket++) {
    if(!ck_ht_entry_empty(&hh->buckets[hh->bucket]) && hh->buckets[hh->bucket].key != CK_HT_KEY_TOMBSTONE) {
      s->walk_addr = (uintptr_t)&hh->vmem[hh->bucket];
      s->walk_callback(s->walk_addr, &dummy, s->walk_cbdata);
      hh->bucket++;
      return WALK_NEXT;
    }
  }
  return WALK_DONE;
}
static void noit_hash_walk_fini(mdb_walk_state_t *s) {
}

static int
_print_hash_bucket_data_cb(uintptr_t addr, const void *u, void *data)
{
  ck_ht_entry_t b;
  if(mdb_vread(&b, sizeof(b), addr) == -1) return WALK_ERR;
  mdb_printf("%p\n", ck_ht_entry_value(&b));
  return WALK_NEXT;
}

static int
noit_log_dcmd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv) {
  noit_hash_table l;
  struct ck_ht_map map;
  ck_ht_entry_t *buckets;
  uintptr_t vmem;
  int bucket = 0;
  char logname[128];

  if(argv == 0) {
    GElf_Sym sym;
    int rv;
    if(mdb_lookup_by_name("noit_loggers", &sym) == -1) return DCMD_ERR;
    rv = mdb_pwalk("noit_hash", _print_hash_bucket_data_cb, NULL, sym.st_value);
    return (rv == WALK_DONE) ? DCMD_OK : DCMD_ERR;
  }
  if(argc != 1 || argv[0].a_type != MDB_TYPE_STRING) {
    return DCMD_USAGE;
  }
  if(mdb_readsym(&l, sizeof(l), "noit_loggers") == -1) return DCMD_ERR;
  if(mdb_vread(&map, sizeof(map), (uintptr_t)l.ht.map) == -1) return DCMD_ERR;
  if(map.n_entries == 0) return DCMD_OK;
  buckets = mdb_alloc(sizeof(*buckets) * map.capacity, UM_GC);
  vmem = (uintptr_t)map.entries;
  mdb_vread(buckets, sizeof(ck_ht_entry_t) * map.capacity, (uintptr_t)vmem);
  for(;bucket<map.capacity;bucket++) {
    if(!ck_ht_entry_empty(&buckets[bucket]) && buckets[bucket].key != CK_HT_KEY_TOMBSTONE) {
      void *key;
      uint16_t keylen;
      key = ck_ht_entry_key(&buckets[bucket]);
      keylen = ck_ht_entry_key_length(&buckets[bucket]);
      logname[0] = '\0';
      mdb_vread(logname, MIN(keylen, sizeof(logname)), (uintptr_t)key);
      logname[MIN(keylen,sizeof(logname)-1)] = '\0';
      if(!strcmp(logname, argv[0].a_un.a_str)) {
        mdb_printf("%p\n", ck_ht_entry_value(&buckets[bucket]));
        return DCMD_OK;
      }
    }
  }
  return DCMD_OK;
}

struct _noit_log_stream {
  unsigned flags;
  /* Above is exposed... 'do not change it... dragons' */
  char *type;
  char *name;
  int mode;
  char *path;
  void *ops;
  void *op_ctx;
  noit_hash_table *config;
  struct _noit_log_stream_outlet_list *outlets;
  pthread_rwlock_t *lock;
  int32_t written;
  unsigned deps_materialized:1;
  unsigned flags_below;
};

typedef struct {
  u_int64_t head;
  u_int64_t tail;
  int noffsets;
  int *offsets;
  int segmentsize;
  int segmentcut;
  char *segment;
} membuf_ctx_t;

static int
membuf_print_dmcd(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv) {
  uint_t opt_v = FALSE;
  int rv = DCMD_OK;
  struct _noit_log_stream ls;
  char logtype[128];
  membuf_ctx_t mb, *membuf;
  int log_lines = 0, idx, nmsg;
  int *offsets;
  uint64_t opt_n = 0;

  if(mdb_getopts(argc, argv,
     'v', MDB_OPT_SETBITS, TRUE, &opt_v,
     'n', MDB_OPT_UINT64, &opt_n, NULL) != argc)
                return (DCMD_USAGE);

  log_lines = (int)opt_n;
  if(mdb_vread(&ls, sizeof(ls), addr) == -1) return DCMD_ERR;
  if(mdb_readstr(logtype, sizeof(logtype), (uintptr_t)ls.type) == -1) return DCMD_ERR;
  if(strcmp(logtype, "memory")) {
    mdb_warn("log_stream not of type 'memory'\n");
    return DCMD_ERR;
  }
  if(mdb_vread(&mb, sizeof(mb), (uintptr_t)ls.op_ctx) == -1) return DCMD_ERR;
  membuf = &mb;

  /* Find out how many lines we have */
  nmsg = ((membuf->tail % membuf->noffsets) >= (membuf->head % membuf->noffsets)) ?
           ((membuf->tail % membuf->noffsets) - (membuf->head % membuf->noffsets)) :
           ((membuf->tail % membuf->noffsets) + membuf->noffsets - (membuf->head % membuf->noffsets));
  if(nmsg >= membuf->noffsets) return DCMD_ERR;
  if(log_lines == 0) log_lines = nmsg;
  log_lines = MIN(log_lines,nmsg);
  idx = (membuf->tail >= log_lines) ?
          (membuf->tail - log_lines) : 0;
 
  mdb_printf("Displaying %d of %d logs\n", log_lines, nmsg);
  mdb_printf("==================================\n");

  if(idx == membuf->tail) return 0;

  /* If we're asked for a starting index outside our range, then we should set it to head. */
  if((membuf->head > membuf->tail && idx < membuf->head && idx >= membuf->tail) ||
     (membuf->head < membuf->tail && (idx >= membuf->tail || idx < membuf->head)))
    idx = membuf->head;

  offsets = mdb_alloc(sizeof(*offsets) * membuf->noffsets, UM_SLEEP);
  if(mdb_vread(offsets, sizeof(*offsets) * membuf->noffsets, (uintptr_t)membuf->offsets) == -1) {
    mdb_warn("error reading offsets\n");
    return DCMD_ERR;
  }
  while(idx != membuf->tail) {
    char line[65536];
    struct timeval copy;
    uintptr_t logline;
    uint64_t nidx;
    size_t len;
    nidx = idx + 1;
    len = (offsets[idx % membuf->noffsets] < offsets[nidx % membuf->noffsets]) ?
            offsets[nidx % membuf->noffsets] - offsets[idx % membuf->noffsets] :
            membuf->segmentcut - offsets[idx % membuf->noffsets];
    if(mdb_vread(&copy, sizeof(copy), (uintptr_t)membuf->segment + offsets[idx % membuf->noffsets]) == -1) {
      mdb_warn("error reading timeval from log line\n");
      rv = DCMD_ERR;
      break;
    }
    logline = (uintptr_t)membuf->segment + offsets[idx % membuf->noffsets] + sizeof(copy);
    len -= sizeof(copy);
    if(len > sizeof(line)-1) {
      mdb_warn("logline too long\n");
    }
    else if(mdb_vread(line, len, logline) == -1) {
      mdb_warn("error reading log line\n");
      break;
    }
    else {
      line[len]='\0';
      if(opt_v) {
        mdb_printf("[%u] [%u.%u]\n", idx, copy.tv_sec, copy.tv_usec);
        mdb_inc_indent(4);
      }
      mdb_printf("%s", line);
      if(opt_v) {
        mdb_dec_indent(4);
      }
    }
    idx = nidx;
  }
  mdb_free(offsets, sizeof(*offsets) * membuf->noffsets);
  return rv;
}

static mdb_walker_t _utils_walkers[] = {
  {
  .walk_name = "noit_skiplist",
  .walk_descr = "walk a noit_skiplist along it's ordered bottom row",
  .walk_init = noit_skiplist_walk_init,
  .walk_step = noit_skiplist_walk_step,
  .walk_fini = noit_skiplist_walk_fini,
  .walk_init_arg = NULL
  },
  {
  .walk_name = "noit_hash",
  .walk_descr = "walk a noit_hash",
  .walk_init = noit_hash_walk_init,
  .walk_step = noit_hash_walk_step,
  .walk_fini = noit_hash_walk_fini,
  .walk_init_arg = NULL
  },
  { NULL }
};
static mdb_dcmd_t _utils_dcmds[] = {
  {
    "noit_log",
    "[logname]",
    "returns the noit_log_stream_t for the named log",
    noit_log_dcmd,
    NULL,
    NULL
  },
  {
    "noit_print_membuf_log",
    "[-v] [-n nlines]",
    "prints the at most [n] log lines",
    membuf_print_dmcd,
    NULL,
    NULL
  },
  { NULL }
};

static mdb_modinfo_t noitutils_linkage = {
  .mi_dvers = MDB_API_VERSION,
  .mi_dcmds = _utils_dcmds,
  .mi_walkers = _utils_walkers
};
