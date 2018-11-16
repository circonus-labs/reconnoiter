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

#include <mtev_defines.h>
#include "noit_version.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

#include <eventer/eventer.h>
#include <mtev_memory.h>
#include <mtev_log.h>
#include <mtev_hash.h>
#include <mtev_security.h>
#include <mtev_watchdog.h>
#include <mtev_lockfile.h>
#include <mtev_main.h>
#include <mtev_listener.h>
#include <mtev_console.h>
#include <mtev_conf.h>
#include <mtev_rest.h>
#include <mtev_reverse_socket.h>
#include <mtev_events_rest.h>
#include <mtev_capabilities_listener.h>
#include <mtev_heap_profiler.h>
#include "noit_mtev_bridge.h"
#include "noit_config.h"
#include "noit_module.h"
#include "noit_check_tools.h"
#include "stratcon_jlog_streamer.h"
#include "stratcon_datastore.h"
#include "stratcon_iep.h"
#include "stratcon_realtime_http.h"

#define APPNAME "stratcon"
static char *config_file = ETC_DIR "/" APPNAME ".conf";
static const char *droptouser = NULL;
static const char *droptogroup = NULL;
static const char *chrootpath = NULL;
static int foreground = 0;
static int debug = 0;
static int strict_module_load = 0;
static char *glider = NULL;

#include "man/stratcond.usage.h"
static void usage(const char *progname) {
  printf("Usage for %s:\n", progname);
#ifdef STRATCOND_USAGE
  mtevAssert(write(STDOUT_FILENO,
              STRATCOND_USAGE,
              sizeof(STRATCOND_USAGE)-1) == sizeof(STRATCOND_USAGE)-1);
#else
  printf("\nError in usage, build problem.\n");
#endif
  return;
}

void parse_clargs(int argc, char **argv) {
  int c;
  while((c = getopt(argc, argv, "Mrshc:dDu:g:n:t:l:L:G:")) != EOF) {
    switch(c) {
      case 'M':
        strict_module_load = 1;
        break;
      case 'G':
        glider = strdup(optarg);
        break;
      case 'l':
        mtev_main_enable_log(optarg);
        break;
      case 'L':
        mtev_main_disable_log(optarg);
        break;
      case 'n':
        {
          char *cp = strchr(optarg, ':');
          if(!cp) mtev_listener_skip(optarg, 0);
          else {
            if(cp == optarg) {
              *cp++ = '\0';
              mtev_listener_skip(NULL, atoi(cp));
            }
            else {
              *cp++ = '\0';
              mtev_listener_skip(optarg, atoi(cp));
            }
          }
        }
        break;
      case 'r':
        stratcon_iep_set_enabled(0);
        break;
      case 's':
        stratcon_datastore_set_enabled(0);
        break;
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
        foreground++;
        break;
      default:
        break;
    }
  }
}

const char *reverse_prefix = "noit/"; /* namespace out connections */
const char *reverse_prefix_cns[] = { "noit/", NULL };

static int child_main() {
  char stratcon_version[80];

  mtev_watchdog_child_heartbeat();
  noit_mtev_bridge_init();

  /* Next (re)load the configs */
  if(mtev_conf_load(config_file) == -1) {
    fprintf(stderr, "Cannot load config: '%s'\n", config_file);
    exit(2);
  }

  mtev_log_reopen_all();
  mtevL(noit_notice, "process starting: %d\n", (int)getpid());
  mtev_log_go_asynch();

  if(eventer_init() == -1) {
    mtevL(noit_stderr, "Cannot initialize eventer\n");
    exit(-1);
  }
  /* rotation init requires, eventer_init() */
  mtev_conf_log_init_rotate(APPNAME, mtev_false);

  mtev_watchdog_child_eventer_heartbeat();

  mtev_dso_init();
  mtev_console_init(APPNAME);
  mtev_console_conf_init();
  mtev_http_rest_init();
  mtev_reverse_socket_init(reverse_prefix, reverse_prefix_cns);
  mtev_reverse_socket_acl(mtev_reverse_socket_denier);
  mtev_events_rest_init();
  mtev_capabilities_listener_init();
  mtev_heap_profiler_rest_init();
  noit_build_version(stratcon_version, sizeof(stratcon_version));
  mtev_capabilities_add_feature("stratcon", stratcon_version);
  stratcon_realtime_http_init(APPNAME);
  mtev_listener_init(APPNAME);

  mtev_dso_post_init();
  if(strict_module_load && mtev_dso_load_failures() > 0) {
    mtevL(noit_stderr, "Failed to load some modules and -M given.\n");
    exit(2);
  }

  if(stratcon_datastore_get_enabled())
    stratcon_datastore_init();

  /* Drop privileges */
  mtev_conf_security_init(APPNAME, droptouser, droptogroup, chrootpath);

  stratcon_jlog_streamer_init(APPNAME);

  if(stratcon_iep_get_enabled())
    stratcon_iep_init();
  if(stratcon_datastore_get_enabled()) {
    /* Write our log out, and setup a watchdog to write it out on change. */
    stratcon_datastore_saveconfig(NULL);
    mtev_conf_coalesce_changes(10); /* 10 seconds of no changes before we write */
  }
  else
    mtev_conf_coalesce_changes(INT_MAX);

  mtev_conf_watch_and_journal_watchdog(stratcon_datastore_saveconfig, NULL);

  mtevL(mtev_debug, "stratcond ready\n");
  eventer_loop();
  return 0;
}

void
stratcond_init_globals(void) {
  noit_check_tools_shared_init_globals();
  stratcon_datastore_init_globals();
  stratcon_iep_init_globals();
  stratcon_jlog_streamer_init_globals();
}

int main(int argc, char **argv) {
  mtev_memory_init();
  parse_clargs(argc, argv);
  stratcond_init_globals();
  return mtev_main(APPNAME, config_file, debug, foreground,
                   MTEV_LOCK_OP_LOCK, glider, droptouser, droptogroup,
                   child_main);
}
