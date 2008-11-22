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
#include "utils/noit_security.h"
#include "noit_listener.h"
#include "noit_console.h"
#include "noit_module.h"
#include "noit_conf.h"
#include "stratcon_jlog_streamer.h"
#include "stratcon_datastore.h"

#define APPNAME "stratcon"
static char *config_file = ETC_DIR "/" APPNAME ".conf";
static const char *droptouser = NULL;
static const char *droptogroup = NULL;
static const char *chrootpath = NULL;
static int foreground = 0;
static int debug = 0;

#include "man/stratcond.usage.h"
static void usage(const char *progname) {
  printf("Usage for %s:\n", progname);
#ifdef STRATCOND_USAGE
  write(STDOUT_FILENO, STRATCOND_USAGE, sizeof(STRATCOND_USAGE)-1);
#else
  printf("\nError in usage, build problem.\n");
#endif
  return;
}

void parse_clargs(int argc, char **argv) {
  int c;
  while((c = getopt(argc, argv, "hc:dDu:g:t:")) != EOF) {
    switch(c) {
      case 'h':
        usage(argv[0]);
        exit(1);
        break;
      case 'u':
        droptouser = strdup(optarg);
        break;
      case 'g':
        droptogroup = strdup(optarg);
        break;
      case 't':
        chrootpath = strdup(optarg);
        break;
      case 'c':
        config_file = strdup(optarg);
        break;
      case 'd':
        debug++;
        break;
      case 'D':
        foreground = 1;
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
  table = noit_conf_get_hash(NULL, "/" APPNAME "/eventer/config");
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

  chdir("/");
  if(!foreground) {
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    if(fork()) exit(0);
    setsid();
    if(fork()) exit(0);
  }

  /* First initialize logging, so we can log errors */
  noit_log_init();
  noit_log_stream_add_stream(noit_debug, noit_stderr);
  noit_log_stream_add_stream(noit_error, noit_stderr);

  /* Next load the configs */
  noit_conf_init(APPNAME);
  if(noit_conf_load(config_file) == -1) {
    fprintf(stderr, "Cannot load config: '%s'\n", config_file);
    exit(2);
  }

  /* Reinitialize the logging system now that we have a config */
  noit_conf_log_init(APPNAME);
  if(debug)
    noit_debug->enabled = 1;

  /* Lastly, run through all other system inits */
  if(!noit_conf_get_stringbuf(NULL, "/" APPNAME "/eventer/@implementation",
                              conf_str, sizeof(conf_str))) {
    noitL(noit_stderr, "Cannot find '%s' in configuration\n",
          "/" APPNAME "/eventer/@implementation");
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
  noit_listener_init(APPNAME);

  /* Drop privileges */
  if(chrootpath && noit_security_chroot(chrootpath)) {
    noitL(noit_stderr, "Failed to chroot(), exiting.\n");
    exit(-1);
  }
  if(noit_security_usergroup(droptouser, droptogroup)) {
    noitL(noit_stderr, "Failed to drop privileges, exiting.\n");
    exit(-1);
  }

  stratcon_jlog_streamer_init(APPNAME);

  /* Write our log out, and setup a watchdog to write it out on change. */
  stratcon_datastore_saveconfig(NULL);
  noit_conf_coalesce_changes(10); /* 10 seconds of no changes before we write */
  noit_conf_watch_and_journal_watchdog(stratcon_datastore_saveconfig, NULL);

  eventer_loop();
  return 0;
}
