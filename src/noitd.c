#include "noit_defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "getopt_long.h"
#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "noit_listener.h"
#include "noit_console.h"
#include "noit_conf.h"

static char *config_file = ETC_DIR "/noit.conf";
static int debug = 0;

void parse_clargs(int argc, char **argv) {
  int c;
  while((c = getopt(argc, argv, "c:d")) != EOF) {
    switch(c) {
      case 'c':
        config_file = strdup(optarg);
        break;
      case 'd':
        debug++;
        break;
      default:
        break;
    }
  }
}

int main(int argc, char **argv) {
  parse_clargs(argc, argv);

  noit_log_init();
  if(debug)
    noit_log_stream_add_stream(noit_debug, noit_stderr);
  noit_log_stream_add_stream(noit_error, noit_stderr);

  noit_conf_init();
  if(noit_conf_load(config_file) == -1) {
    fprintf(stderr, "Cannot load config: '%s'\n", config_file);
  }
  if(eventer_choose("kqueue") == -1) {
    fprintf(stderr, "Cannot choose kqueue\n");
    exit(-1);
  }
  if(eventer_init() == -1) {
    fprintf(stderr, "Cannot init kqueue\n");
    exit(-1);
  }

  noit_listener("127.0.0.1", 23123, SOCK_STREAM, 5, noit_console_handler, NULL);
  eventer_loop();
  return 0;
}
