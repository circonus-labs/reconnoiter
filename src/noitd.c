#include "noit_defines.h"
#include "eventer/eventer.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if(eventer_choose("kqueue") == -1) {
    fprintf(stderr, "Cannot choose kqueue\n");
    exit(-1);
  }
  if(eventer_init() == -1) {
    fprintf(stderr, "Cannot init kqueue\n");
    exit(-1);
  }
  return 0;
}
