#include "noit_defines.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"
#include "noit_listener.h"
#include "noit_console.h"
#include "noit_module.h"
#include "noit_conf.h"
#include "noit_conf_checks.h"

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

static
int configure_eventer() {
  int rv = 0;
  noit_hash_table *table;
  table = noit_conf_get_hash(NULL, "/noit/eventer/config");
  if(table) {
    noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
    const char *key, *value;
    int klen;
    while(noit_hash_next(table, &iter, &key, &klen, (void **)&value)) {
      int subrv;
      if((subrv = eventer_propset(key, value)) != 0)
        rv = subrv;
    }
    noit_hash_destroy(table, free, free);
    free(table);
  }
  return rv;
}

int main(int argc, char **argv) {
  char conf_str[1024];

  parse_clargs(argc, argv);

  /* First initialize logging, so we can log errors */
  noit_log_init();
  noit_log_stream_add_stream(noit_debug, noit_stderr);
  noit_log_stream_add_stream(noit_error, noit_stderr);

  /* Next load the configs */
  noit_conf_init("noit");
  noit_conf_checks_init("noit");
  if(noit_conf_load(config_file) == -1) {
    fprintf(stderr, "Cannot load config: '%s'\n", config_file);
  }

  /* Reinitialize the logging system now that we have a config */
  noit_conf_log_init("noit");
  if(debug)
    noit_debug->enabled = 1;

  /* Lastly, run through all other system inits */
  if(!noit_conf_get_stringbuf(NULL, "/noit/eventer/@implementation",
                              conf_str, sizeof(conf_str))) {
    noitL(noit_stderr, "Cannot find '%s' in configuration\n",
          "/noit/eventer/@implementation");
    exit(-1);
  }
  if(eventer_choose(conf_str) == -1) {
    noitL(noit_stderr, "Cannot choose eventer %s\n", conf_str);
    exit(-1);
  }
  if(configure_eventer() != 0) {
    noitL(noit_stderr, "Cannot configure eventer\n");
    exit(-1);
  }
  if(eventer_init() == -1) {
    noitL(noit_stderr, "Cannot init eventer %s\n", conf_str);
    exit(-1);
  }
  noit_console_init();
  noit_module_init();
  noit_poller_init();
  noit_listener_init();

  eventer_loop();
  return 0;
}
