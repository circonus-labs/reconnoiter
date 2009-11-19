/*
 * Copyright (c) 2007-2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "noit_defines.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"
#include "utils/noit_security.h"
#include "utils/noit_watchdog.h"
#include "noit_listener.h"
#include "noit_console.h"
#include "noit_jlog_listener.h"
#include "noit_rest.h"
#include "noit_check_rest.h"
#include "noit_livestream_listener.h"
#include "noit_capabilities_listener.h"
#include "noit_module.h"
#include "noit_conf.h"
#include "noit_conf_checks.h"
#include "noit_filters.h"

#define APPNAME "noit"
#define CHILD_WATCHDOG_TIMEOUT 5 /*seconds*/

static char *config_file = ETC_DIR "/" APPNAME ".conf";
static const char *droptouser = NULL;
static const char *droptogroup = NULL;
static const char *chrootpath = NULL;
static int foreground = 0;
static int debug = 0;
static char **enable_logs;
static int enable_logs_cnt = 0;
static char **disable_logs;
static int disable_logs_cnt = 0;

#include "man/noitd.usage.h"
static void usage(const char *progname) {
  printf("Usage for %s:\n", progname);
#ifdef NOITD_USAGE
  assert(write(STDOUT_FILENO,
               NOITD_USAGE,
               sizeof(NOITD_USAGE)-1) == sizeof(NOITD_USAGE)-1);
#else
  printf("\nError in usage, build problem.\n");
#endif
  return;
}

void cli_log_switches() {
  int i;
  noit_log_stream_t ls;
  for(i=0; i<enable_logs_cnt; i++) {
    ls = noit_log_stream_find(enable_logs[i]);
    if(!ls) noitL(noit_error, "No such log: '%s'\n", enable_logs[i]);
    if(ls && !ls->enabled) {
      noitL(noit_error, "Enabling %s\n", enable_logs[i]);
      ls->enabled = 1;
    }
  }
  for(i=0; i<disable_logs_cnt; i++) {
    ls = noit_log_stream_find(disable_logs[i]);
    if(!ls) noitL(noit_error, "No such log: '%s'\n", enable_logs[i]);
    if(ls && ls->enabled) {
      noitL(noit_error, "Disabling %s\n", disable_logs[i]);
      ls->enabled = 0;
    }
  }
}
void parse_clargs(int argc, char **argv) {
  int c;
  enable_logs = calloc(argc, sizeof(*enable_logs));
  disable_logs = calloc(argc, sizeof(*disable_logs));
  while((c = getopt(argc, argv, "hc:dDu:g:t:l:L:")) != EOF) {
    switch(c) {
      case 'h':
        usage(argv[0]);
        exit(1);
        break;
      case 'l':
        enable_logs[enable_logs_cnt++] = strdup(optarg);
        break;
      case 'L':
        disable_logs[disable_logs_cnt++] = strdup(optarg);
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
      case 'D':
        foreground = 1;
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
  table = noit_conf_get_hash(NULL, "/" APPNAME "/eventer/config");
  if(table) {
    noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
    const char *key, *value;
    int klen;
    while(noit_hash_next_str(table, &iter, &key, &klen, &value)) {
      int subrv;
      if((subrv = eventer_propset(key, value)) != 0)
        rv = subrv;
    }
    noit_hash_destroy(table, free, free);
    free(table);
  }
  return rv;
}

static int __reload_needed = 0;
static void request_conf_reload(int sig) {
  if(sig == SIGHUP) {
    __reload_needed = 1;
  }
}
static int noitice_hup(eventer_t e, int mask, void *unused, struct timeval *now) {
  if(__reload_needed) {
    noitL(noit_error, "SIGHUP received, performing reload\n");
    if(noit_conf_load(config_file) == -1) {
      noitL(noit_error, "Cannot load config: '%s'\n", config_file);
      exit(-1);
    }
    noit_poller_reload(NULL);
    __reload_needed = 0;
  }
  return 0;
}
static int child_main() {
  eventer_t e;

  /* Load our config...
   * to ensure it is current w.r.t. to this child starting */
  if(noit_conf_load(config_file) == -1) {
    noitL(noit_error, "Cannot load config: '%s'\n", config_file);
    exit(-1);
  }

  noit_log_reopen_all();

  signal(SIGHUP, request_conf_reload);

  /* initialize the eventer */
  if(eventer_init() == -1) {
    noitL(noit_stderr, "Cannot initialize eventer\n");
    exit(-1);
  }

  /* Setup our heartbeat */
  noit_watchdog_child_eventer_heartbeat();

  e = eventer_alloc();
  e->mask = EVENTER_RECURRENT;
  e->callback = noitice_hup;
  eventer_add_recurrent(e);

  /* Initialize all of our listeners */
  noit_console_init(APPNAME);
  noit_console_conf_init();
  noit_console_conf_checks_init();
  noit_capabilities_listener_init();
  noit_jlog_listener_init();
  noit_http_rest_init();
  noit_check_rest_init();
  noit_filters_rest_init();
  noit_livestream_listener_init();

  noit_module_init();

  /* Drop privileges */
  if(chrootpath && noit_security_chroot(chrootpath)) {
    noitL(noit_stderr, "Failed to chroot(), exiting.\n");
    exit(2);
  }
  if(noit_security_usergroup(droptouser, droptogroup, noit_false)) {
    noitL(noit_stderr, "Failed to drop privileges, exiting.\n");
    exit(2);
  }

  /* Prepare for launch... */
  noit_filters_init();
  noit_poller_init();
  noit_listener_init(APPNAME);

  /* Write our log out, and setup a watchdog to write it out on change. */
  noit_conf_write_log(NULL);
  noit_conf_coalesce_changes(10); /* 10 seconds of no changes before we write */
  noit_conf_watch_and_journal_watchdog(noit_conf_write_log, NULL);

  eventer_loop();
  return 0;
}

int main(int argc, char **argv) {
  char conf_str[1024];
  char user[32], group[32];

  parse_clargs(argc, argv);

  /* First initialize logging, so we can log errors */
  noit_log_init();
  noit_log_stream_add_stream(noit_debug, noit_stderr);
  noit_log_stream_add_stream(noit_error, noit_stderr);

  /* Next load the configs */
  noit_conf_init(APPNAME);
  if(noit_conf_load(config_file) == -1) {
    fprintf(stderr, "Cannot load config: '%s'\n", config_file);
  }

  /* Reinitialize the logging system now that we have a config */
  snprintf(user, sizeof(user), "%d", getuid());
  snprintf(group, sizeof(group), "%d", getgid());
  if(noit_security_usergroup(droptouser, droptogroup, noit_true)) {
    noitL(noit_stderr, "Failed to drop privileges, exiting.\n");
    exit(-1);
  }
  noit_conf_log_init(APPNAME);
  cli_log_switches();
  if(noit_security_usergroup(user, group, noit_true)) {
    noitL(noit_stderr, "Failed to regain privileges, exiting.\n");
    exit(-1);
  }
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

  noit_watchdog_prefork_init();

  if(chdir("/") != 0) {
    noitL(noit_stderr, "Failed chdir(\"/\"): %s\n", strerror(errno));
    exit(-1);
  }
  if(foreground) return child_main();

  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
  if(fork()) exit(0);
  setsid();
  if(fork()) exit(0);

  signal(SIGHUP, SIG_IGN);
  return noit_watchdog_start_child("noitd", child_main, 0);
}
