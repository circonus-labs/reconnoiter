#include "noit_defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "noit_listener.h"
#include "noit_console.h"
#include "noit_module.h"
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
  char conf_str[1024];
  parse_clargs(argc, argv);

  /* First initialize logging, so we can log errors */
  noit_log_init();
  if(debug)
    noit_log_stream_add_stream(noit_debug, noit_stderr);
  noit_log_stream_add_stream(noit_error, noit_stderr);

  /* Next load the configs */
  noit_conf_init();
  if(noit_conf_load(config_file) == -1) {
    fprintf(stderr, "Cannot load config: '%s'\n", config_file);
  }

  /* Lastly, run through all other system inits */
  if(!noit_conf_get_stringbuf(NULL, "/noit/eventer/implementation",
                              conf_str, sizeof(conf_str))) {
    noit_log(noit_stderr, NULL, "Cannot find '%s' in configuration\n",
             "/noit/eventer/implementation");
    exit(-1);
  }
  if(eventer_choose(conf_str) == -1) {
    noit_log(noit_stderr, NULL, "Cannot choose eventer %s\n", conf_str);
    exit(-1);
  }
  if(eventer_init() == -1) {
    noit_log(noit_stderr, NULL, "Cannot init eventer %s\n", conf_str);
    exit(-1);
  }
  noit_console_init();
  noit_module_init();
  noit_poller_init();
  noit_listener_init();

  eventer_loop();
  return 0;
}
