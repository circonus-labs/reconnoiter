/*
 * Copyright (c) 2007-2010, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 * Copyright (c) 2015-2017, Circonus, Inc. All rights reserved.
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

#include "noit_config.h"
#include "noit_version.h"
#include <mtev_defines.h>

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

#include <mtev_main.h>
#include <eventer/eventer.h>
#include <mtev_memory.h>
#include <mtev_log.h>
#include <mtev_hash.h>
#include <mtev_security.h>
#include <mtev_watchdog.h>
#include <mtev_lockfile.h>
#include <mtev_listener.h>
#include <mtev_console.h>
#include <mtev_rest.h>
#include <mtev_reverse_socket.h>
#include <mtev_capabilities_listener.h>
#include <mtev_conf.h>
#include <mtev_events_rest.h>
#include <mtev_stats.h>
#include <mtev_cluster.h>

#include "noit_mtev_bridge.h"
#include "noit_jlog_listener.h"
#include "noit_check_rest.h"
#include "noit_livestream_listener.h"
#include "noit_websocket_handler.h"
#include "noit_module.h"
#include "noit_conf_checks.h"
#include "noit_filters.h"
#include "noit_metric_director.h"
#include "noit_check_log_helpers.h"
#include "noit_check_tools_shared.h"
#include "noit_check.h"

#define APPNAME "noit"
#define CHILD_WATCHDOG_TIMEOUT 5 /*seconds*/

static char *config_file = ETC_DIR "/" APPNAME ".conf";
static char *xpath = NULL;
static const char *droptouser = NULL;
static const char *droptogroup = NULL;
static const char *chrootpath = NULL;
static int foreground = 0;
static int debug = 0;
static int strict_module_load = 0;
static char *glider = NULL;

#include "man/noitd.usage.h"
static void usage(const char *progname) {
  printf("Usage for %s:\n", progname);
#ifdef NOITD_USAGE
  mtevAssert(write(STDOUT_FILENO,
               NOITD_USAGE,
               sizeof(NOITD_USAGE)-1) == sizeof(NOITD_USAGE)-1);
#else
  printf("\nError in usage, build problem.\n");
#endif
  return;
}

void parse_clargs(int argc, char **argv) {
  int c;
  while((c = getopt(argc, argv, "x:Mhc:dDu:g:n:t:l:L:G:")) != EOF) {
    switch(c) {
      case 'x':
        xpath = strdup(optarg);
        mtev_main_disable_log("notice");
        foreground = 1;
        break;
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
        mtev_main_enable_log(optarg);
        break;
      case 'L':
        mtev_main_disable_log(optarg);
        break;
      case 'n':
        {
          char *cp = optarg ? strchr(optarg, ':') : NULL;
          if(!cp) mtev_listener_skip(optarg, 0);
          else {
            if(cp == optarg) optarg = NULL;
            *cp++ = '\0';
            mtev_listener_skip(optarg, atoi(cp));
          }
        }
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
        foreground++;
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
static int notice_hup(eventer_t e, int mask, void *unused, struct timeval *now) {
  if(__reload_needed) {
    mtevL(noit_error, "SIGHUP received, performing reload\n");
    if(mtev_conf_load(config_file) == -1) {
      mtevL(noit_error, "Cannot load config: '%s'\n", config_file);
      exit(-1);
    }
    noit_poller_reload(NULL);
    __reload_needed = 0;
  }
  return 0;
}

static int noit_console_stopword(const char *word) {
  return(!strcmp(word, "check") ||
         !strcmp(word, "noit") ||
         !strcmp(word, "filterset") ||
         !strcmp(word, "config"));
}
const char *reverse_prefix = "noit/";
const char *reverse_prefix_cns[] = { NULL };

static int child_main() {
  eventer_t e;
  char noit_version[80];

  /* Send out a birth notice. */
  mtev_watchdog_child_heartbeat();

  noit_mtev_bridge_init();
  mtev_override_console_stopword(noit_console_stopword);

  /* Load our config...
   * to ensure it is current w.r.t. to this child starting */
  if(mtev_conf_load(config_file) == -1) {
    mtevL(noit_error, "Cannot load config: '%s'\n", config_file);
    exit(2);
  }
  if(xpath) {
    int cnt, i;
    mtev_conf_section_t *parts = NULL;
    parts = mtev_conf_get_sections(NULL, xpath, &cnt);
    if(cnt == 0) exit(2);
    for(i=0; i<cnt; i++) {
      const char *sup = "";
      if(mtev_conf_env_off(parts[i], NULL)) sup = "[env suppressed]";
      fprintf(stdout, "%d%s: ", i, sup); fflush(stdout);
      mtev_conf_write_section(parts[i], 1);
    }
    free(parts);
    exit(0);
  }

  mtev_log_reopen_all();
  mtevL(noit_notice, "process starting: %d\n", (int)getpid());
  mtev_log_go_asynch();

  signal(SIGHUP, request_conf_reload);

  /* initialize the eventer */
  if(eventer_init() == -1) {
    mtevL(noit_stderr, "Cannot initialize eventer\n");
    exit(-1);
  }
  /* rotation init requires, eventer_init() */
  mtev_conf_log_init_rotate(APPNAME, mtev_false);

  /* Setup our heartbeat */
  mtev_watchdog_child_eventer_heartbeat();

  e = eventer_alloc_recurrent(notice_hup, NULL);
  eventer_add_recurrent(e);

  /* Initialized shared tools */
  noit_check_tools_shared_init();

  /* Initialize all of our listeners */
  mtev_console_init(APPNAME);
  mtev_console_conf_init();
  mtev_capabilities_listener_init();
  noit_build_version(noit_version, sizeof(noit_version));
  mtev_capabilities_add_feature("noit", noit_version);
  mtev_http_rest_init();
  mtev_reverse_socket_init(reverse_prefix, reverse_prefix_cns);
  mtev_reverse_socket_acl(mtev_reverse_socket_denier);
  mtev_events_rest_init();
  mtev_stats_rest_init();
  noit_console_conf_checks_init();
  noit_jlog_listener_init();
  noit_check_rest_init();
  noit_filters_rest_init();
  noit_livestream_listener_init();
  noit_websocket_handler_init();

  mtev_dso_init();
  noit_module_init();
  mtev_dso_post_init();
  if(strict_module_load &&
     (mtev_dso_load_failures() > 0 || noit_module_load_failures() > 0)) {
    mtevL(noit_stderr, "Failed to load some modules and -M given.\n");
    exit(2);
  }

  mtev_listener_init(APPNAME);
  mtev_cluster_init();
  noit_metric_director_init();

  /* Drop privileges */
  mtev_conf_security_init(APPNAME, droptouser, droptogroup, chrootpath);

  /* Prepare for launch... */
  noit_filters_init();
  noit_poller_init();

  /* Allow the noit web dashboard to be served (only if document_root is set) */
  mtevAssert(mtev_http_rest_register_auth(
    "GET", "/", "^(.*)$", mtev_rest_simple_file_handler,
           mtev_http_rest_client_cert_auth
  ) == 0);

  /* Write our log out, and setup a watchdog to write it out on change. */
  noit_conf_write_log(NULL);
  mtev_conf_coalesce_changes(10); /* 10 seconds of no changes before we write */
  mtev_conf_watch_and_journal_watchdog(noit_conf_write_log, NULL);

  eventer_loop();
  return 0;
}

void
noitd_init_globals(void) {
  noit_check_init_globals();
  noit_check_tools_shared_init_globals();
  noit_conf_checks_init_globals();
  noit_filters_init_globals();
  noit_jlog_listener_init_globals();
  noit_metric_director_init_globals();
  noit_module_init_globals();
}

int main(int argc, char **argv) {
  int lock = MTEV_LOCK_OP_LOCK;
  mtev_memory_init();
  parse_clargs(argc, argv);
  if (xpath) lock = MTEV_LOCK_OP_NONE;
  noitd_init_globals();
  return mtev_main(APPNAME, config_file, debug, foreground,
                   lock, glider, droptouser, droptogroup, 
                   child_main);
}
