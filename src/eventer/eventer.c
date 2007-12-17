#include "eventer/eventer.h"
#include "eventer/eventer_impl.h"
#include "eventer/eventer_POSIX_fd_opset.h"

eventer_t eventer_alloc() {
  eventer_t e;
  e = calloc(1, sizeof(*e));
  e->opset = eventer_POSIX_fd_opset;
  return e;
}

int eventer_timecompare(void *av, void *bv) {
  /* Herein we avoid equality.  This function is only used as a comparator
   * for a heap of timed events.  If they are equal, b is considered less
   * just to maintain an order (despite it not being stable).
   */
  eventer_t a = (eventer_t)av;
  eventer_t b = (eventer_t)bv;
  if(a->whence.tv_sec < b->whence.tv_sec) return -1;
  if(a->whence.tv_sec == b->whence.tv_sec &&
     a->whence.tv_usec < b->whence.tv_usec) return -1;
  return 1;
}

void eventer_free(eventer_t e) {
  free(e);
}

int eventer_choose(const char *name) {
  eventer_impl_t choice;
  for(choice = registered_eventers[0]; choice; choice++) {
    if(!strcmp(choice->name, name)) {
      __eventer = choice;
      return 0;
    }
  }
  return -1;
}
