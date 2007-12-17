#include "noit_defines.h"
#include "eventer/eventer.h"

#include <stdio.h>
#include <stdlib.h>

int stdin_handler(eventer_t e, int mask, void *closure, struct timeval *now) {
  fprintf(stderr, "in stdin_handler:\n");
  return EVENTER_READ;
}
void stdin_sample() {
  eventer_t e;
  e = eventer_alloc();
  e->fd = 0;
  e->mask = EVENTER_READ;
  e->callback = stdin_handler;
  eventer_add(e);
}
int main(int argc, char **argv) {
  if(eventer_choose("kqueue") == -1) {
    fprintf(stderr, "Cannot choose kqueue\n");
    exit(-1);
  }
  if(eventer_init() == -1) {
    fprintf(stderr, "Cannot init kqueue\n");
    exit(-1);
  }

  stdin_sample();
  eventer_loop();
  return 0;
}
