#include "noit_defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "noit_listener.h"
#include "noit_console.h"

int stdin_handler(eventer_t e, int mask, void *closure, struct timeval *now) {
  fprintf(stderr, "in stdin_handler:\n");
  while(1) {
    int len;
    char buffer[1024];
    len = e->opset->read(e->fd, buffer, sizeof(buffer)-1, &mask, NULL);
    printf("read() => %d\n", len);
    if(len < 0 && errno == EINTR) continue;
    if(len < 0 && errno == EAGAIN) break;
    if(len > 0) {
      buffer[len] = '\0';
      printf("read: '%s'\n", buffer);
    }
  }
  return mask;
}
void stdin_sample() {
  socklen_t salen;
  eventer_t e;

  salen = 1;
  if(ioctl(0, FIONBIO, &salen)) {
    fprintf(stderr, "Cannot unlock stdin\n");
  }
  e = eventer_alloc();
  e->fd = 0;
  e->mask = EVENTER_READ;
  e->callback = stdin_handler;
  eventer_add(e);
}
int main(int argc, char **argv) {
  noit_log_init();
  if(eventer_choose("kqueue") == -1) {
    fprintf(stderr, "Cannot choose kqueue\n");
    exit(-1);
  }
  if(eventer_init() == -1) {
    fprintf(stderr, "Cannot init kqueue\n");
    exit(-1);
  }

  stdin_sample();
  noit_listener("127.0.0.1", 23123, SOCK_STREAM, 5, noit_console_handler, NULL);
  eventer_loop();
  return 0;
}
