/*
 * Copyright (c) 2007-2010, OmniTI Computer Consulting, Inc.
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

#include "noit_main.h"
#include "eventer/eventer.h"
#include "utils/noit_log.h"
#include "utils/noit_hash.h"
#include "utils/noit_security.h"
#include "utils/noit_watchdog.h"
#include "utils/noit_lockfile.h"
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
static int strict_module_load = 0;
static char **enable_logs;
static int enable_logs_cnt = 0;
static char **disable_logs;
static int disable_logs_cnt = 0;
static char *glider = NULL;

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

void parse_clargs(int argc, char **argv) {
  int c;
  enable_logs = calloc(argc, sizeof(*enable_logs));
  disable_logs = calloc(argc, sizeof(*disable_logs));
  while((c = getopt(argc, argv, "Mhc:dDu:g:t:l:L:G:")) != EOF) {
    switch(c) {
      case 'G':
        glider = strdup(optarg);
        break;
      case 'M':
        strict_module_load = 1;
        break;
      case 'h':
        usage(argv[0]);
        exit(1);
        break;
      case 'l':
        noit_main_enable_log(optarg);
        break;
      case 'L':
        noit_main_disable_log(optarg);
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
    exit(2);
  }

  noit_log_reopen_all();

  signal(SIGHUP, request_conf_reload);

  /* initialize the eventer */
  if(eventer_init() == -1) {
    noitL(noit_stderr, "Cannot initialize eventer\n");
    exit(-1);
  }
  /* rotation init requires, eventer_init() */
  noit_conf_log_init_rotate(APPNAME, noit_false);

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
  if(strict_module_load && noit_module_load_failures() > 0) {
    noitL(noit_stderr, "Failed to load some modules and -M given.\n");
    exit(2);
  }

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
  parse_clargs(argc, argv);
  return noit_main(APPNAME, config_file, debug, foreground,
                   glider, droptouser, droptogroup, child_main);
}
