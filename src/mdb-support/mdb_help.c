#include <sys/mdb_modapi.h>

typedef struct mdb_modinfo_var {
        ushort_t mi_dvers;
        mdb_dcmd_t *mi_dcmds;
        mdb_walker_t *mi_walkers;
} mdb_modinfo_var_t;

#define MAX_MDB_STUFF 1024
static mdb_dcmd_t _static_mi_dcmds[MAX_MDB_STUFF];
static mdb_walker_t _static_mi_walkers[MAX_MDB_STUFF];

static mdb_modinfo_var_t _one_true_modinfo = {
  .mi_dvers = MDB_API_VERSION,
  .mi_dcmds = _static_mi_dcmds,
  .mi_walkers = _static_mi_walkers
};

const mdb_modinfo_t *_mdb_accum(const mdb_modinfo_t *toadd) {
  int i, dcmd_cnt = 0, new_dcmd_cnt = 0;
  int walker_cnt = 0, new_walker_cnt = 0;
 
  if(_one_true_modinfo.mi_dcmds) while(_one_true_modinfo.mi_dcmds[dcmd_cnt++].dc_name);
  if(dcmd_cnt>0) dcmd_cnt--;
  if(toadd->mi_dcmds) while(toadd->mi_dcmds[new_dcmd_cnt++].dc_name);
  if(new_dcmd_cnt>0) new_dcmd_cnt--;

  if(new_dcmd_cnt > 0) {
    for(i=0;i<new_dcmd_cnt;i++) {
      if((i+dcmd_cnt+1) >= MAX_MDB_STUFF) mdb_warn("too many dcmds");
      else {
        memcpy(&_one_true_modinfo.mi_dcmds[dcmd_cnt+i], &toadd->mi_dcmds[i], sizeof(mdb_dcmd_t));
      }
    }
  }

  if(_one_true_modinfo.mi_walkers) while(_one_true_modinfo.mi_walkers[walker_cnt++].walk_name);
  if(walker_cnt>0) walker_cnt--;
  if(toadd->mi_walkers) while(toadd->mi_walkers[new_walker_cnt++].walk_name);
  if(new_walker_cnt>0) new_walker_cnt--;

  if(new_walker_cnt > 0) {
    for(i=0;i<new_walker_cnt;i++) {
      if((i+walker_cnt+1) >= MAX_MDB_STUFF) mdb_warn("too many walkers");
      else {
        memcpy(&_one_true_modinfo.mi_walkers[walker_cnt+i], &toadd->mi_walkers[i], sizeof(mdb_walker_t));
      }
    }
  }

  return (mdb_modinfo_t *)&_one_true_modinfo;
}

int _print_addr_cb(uintptr_t addr, const void *u, void *data) {
  mdb_printf("%p\n", addr);
  return WALK_NEXT;
}
