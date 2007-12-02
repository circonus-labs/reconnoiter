#include "eventer/eventer.h"
#include "eventer/eventer_impl.h"
#include "eventer/eventer_POSIX_fd_opset.h"

eventer_t eventer_alloc() {
  eventer_t e;
  e = calloc(1, sizeof(*e));
  e->opset = eventer_POSIX_fd_opset;
  return e;
}

void eventer_free(eventer_t e) {
  free(e);
}

int eventer_choose(const char *name) {
  int i;
  eventer_impl_t choice;
  for(choice = registered_eventers[0]; choice; choice++) {
    if(!strcmp(choice->name, name)) {
      __eventer = choice;
      return 0;
    }
  }
  return -1;
}
