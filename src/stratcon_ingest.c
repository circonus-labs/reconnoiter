/*
 * Copyright (c) 2007-2009, OmniTI Computer Consulting, Inc.
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

#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <zlib.h>
#include <assert.h>
#include <errno.h>

#include <eventer/eventer.h>
#include <mtev_log.h>
#include <mtev_b64.h>
#include <mtev_str.h>
#include <mtev_mkdir.h>
#include <mtev_getip.h>
#include <mtev_conf.h>
#include <mtev_rest.h>
#include <mtev_watchdog.h>

#include "noit_mtev_bridge.h"
#include "stratcon_datastore.h"
#include "stratcon_ingest.h"
#include "stratcon_realtime_http.h"
#include "stratcon_iep.h"
#include "noit_check.h"

static void
stratcon_ingest_sweep_journals_int(const char *base,
                                   char *first, char *second, 
                                   char *third, mtev_boolean sweeping,
                                   int (*test)(const char *),
                                   int (*unlink_test)(const char *),
                                   int (*ingest)(const char *fullpath,
                                                 const char *remote_str,
                                                 const char *remote_cn,
                                                 const char *id_str,
                                                 const mtev_boolean sweeping)) {
  char path[PATH_MAX];
  DIR *root;
  struct dirent *de, *entry;
  int i = 0, cnt = 0;
  char **entries;
  int size = 0;

  snprintf(path, sizeof(path), "%s%s%s%s%s%s%s", base,
           first ? "/" : "", first ? first : "",
           second ? "/" : "", second ? second : "",
           third ? "/" : "", third ? third : "");
#ifdef _PC_NAME_MAX
  size = pathconf(path, _PC_NAME_MAX);
#endif
  size = MAX(size, PATH_MAX + 128);
  de = alloca(size);
  root = opendir(path);
  if(!root) return;
  while(portable_readdir_r(root, de, &entry) == 0 && entry != NULL) {
    mtev_watchdog_child_heartbeat();
    cnt++;
  }
  closedir(root);
  root = opendir(path);
  if(!root) return;
  entries = malloc(sizeof(*entries) * cnt);
  while(portable_readdir_r(root, de, &entry) == 0 && entry != NULL) {
    mtev_watchdog_child_heartbeat();
    if(i < cnt) {
      entries[i++] = strdup(entry->d_name);
    }
  }
  closedir(root);
  cnt = i; /* could have changed, directories are fickle */
  qsort(entries, i, sizeof(*entries),
        (int (*)(const void *, const void *))strcasecmp);
  for(i=0; i<cnt; i++) {
    if(!strcmp(entries[i], ".") || !strcmp(entries[i], "..")) continue;
    if(!first)
      stratcon_ingest_sweep_journals_int(base, entries[i], NULL, NULL, sweeping, test, unlink_test, ingest);
    else if(!second)
      stratcon_ingest_sweep_journals_int(base, first, entries[i], NULL, sweeping, test, unlink_test, ingest);
    else if(!third)
      stratcon_ingest_sweep_journals_int(base, first, second, entries[i], sweeping, test, unlink_test, ingest);
    else
    {
      char fullpath[PATH_MAX];
      snprintf(fullpath, sizeof(fullpath), "%s/%s/%s/%s/%s", base,
               first,second,third,entries[i]);
      mtev_watchdog_child_heartbeat();
      if(test && test(entries[i])) {
        ingest(fullpath,first,second,third,sweeping);
      }
      else if (unlink_test && unlink_test(entries[i])) {
        if ((unlink(fullpath) != 0) && (errno != ENOENT)) {
          mtevL(mtev_error, "Couldn't unlink file %s - %s\n", fullpath, strerror(errno));
        }
      }
    }
  }
  for(i=0; i<cnt; i++)
    free(entries[i]);
  free(entries);
}
void
stratcon_ingest_sweep_journals(const char *base, int (*test)(const char *),
                               int (*unlink_test)(const char *),
                               int (*ingest)(const char *fullpath,
                                             const char *remote_str,
                                             const char *remote_cn,
                                             const char *id_str,
                                             const mtev_boolean sweeping)) {
  stratcon_ingest_sweep_journals_int(base, NULL,NULL,NULL, mtev_true, test, unlink_test, ingest);
}

