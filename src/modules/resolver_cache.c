/*
 * Copyright (c) 2015, Circonus, Inc. All rights reserved.
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
 *     * Neither the name Circonus, Inc. nor the names
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

#include <libgen.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#include <mtev_hash.h>

#include "noit_mtev_bridge.h"
#include "noit_module.h"
#include "noit_check_resolver.h"
#include "resolver_cache.xmlh"

static const char *resolver_cache_file = NULL;
static time_t last_update = 0;
static int write_interval = 120;

static int resolver_cache_getfd() {
  static int resolver_cache_fd = -1;
  if(resolver_cache_fd == -1) {
    resolver_cache_fd = open(resolver_cache_file, O_RDWR|O_CREAT, 0600);
    if(resolver_cache_fd < 0) {
      mtevL(noit_error, "resolver cache failed to open '%s': %s\n",
            resolver_cache_file, strerror(errno));
    }
  }
  return resolver_cache_fd;
}

static int
resolver_cache_onload(mtev_image_t *self) {
  return 0;
}

static int
resolver_cache_config(mtev_dso_generic_t *self, mtev_hash_table *o) {
  const char *write_interval_str;
  mtev_hash_retr_str(o, "cachefile", strlen("cachefile"), &resolver_cache_file);
  if(mtev_hash_retr_str(o, "interval", strlen("interval"), &write_interval_str))
    write_interval = atoi(write_interval_str);
  return 0;
}

static mtev_hook_return_t
resolver_cache_store_impl(void *closure, const char *key, const void *b, int blen) {
  int fd, rv;
  ssize_t expected = 0;
  struct iovec iov[4];
  uint32_t keylen, vallen;
  fd = resolver_cache_getfd();
  if(fd < 0) return MTEV_HOOK_ABORT;

  if(key == NULL) {
    /* This is a test if the cache is open for writing */
    time_t now = time(NULL);
    if(now - last_update < write_interval) return MTEV_HOOK_ABORT;
    if(ftruncate(fd, 0) != 0) {
      mtevL(noit_error, "resolver_cache could not truncate cachfile: %s.\n",
            strerror(errno));
      return MTEV_HOOK_ABORT;
    }
    lseek(fd, 0, SEEK_SET);
    last_update = now;
    return MTEV_HOOK_CONTINUE;
  }

  keylen = strlen(key) + 1;
  vallen = blen;
  iov[0].iov_base = &keylen;
  iov[0].iov_len =  sizeof(uint32_t);
  expected += iov[0].iov_len;
  iov[1].iov_base = &vallen;
  iov[1].iov_len =  sizeof(uint32_t);
  expected += iov[1].iov_len;
  iov[2].iov_base = (void *)key;
  iov[2].iov_len =  keylen;
  expected += iov[2].iov_len;
  iov[3].iov_base = (void *)b;
  iov[3].iov_len = vallen;
  expected += iov[3].iov_len;
  while((rv = writev(fd, iov, 4)) == -1 && errno == EINTR);
  if(rv != expected) {
    if(rv >= 0)
      mtevL(noit_error, "failed to write to resolver cache (ret: %d != %d)\n", rv, (int)expected);
    else
      mtevL(noit_error, "failed to write to resolver cache: %s\n", strerror(errno));
    return MTEV_HOOK_ABORT;
  }
  return MTEV_HOOK_CONTINUE;
}

static mtev_hook_return_t
resolver_cache_load_impl(void *closure, char **key, void **b, int *blen) {
  int fd, rv;
  char *tmpkey = NULL, *tmpb = NULL;
  uint32_t lens[2];

  fd = resolver_cache_getfd();
  if(fd < 0) return MTEV_HOOK_ABORT;
  rv = read(fd, lens, sizeof(lens));
  if(rv == 0) {
    /* we only load at boot, so we've nothing new to write out */
    last_update = time(NULL);
    return MTEV_HOOK_DONE;
  }
    
  if(sizeof(lens) != rv ||
     lens[0] > 1024 || lens[1] > 1024) {
    mtevL(noit_error, "detected corruption in resolver cache.\n");
    goto bail;
  }
  tmpkey = malloc(lens[0]);
  tmpb = malloc(lens[1]);
  if(read(fd, tmpkey, lens[0]) != lens[0]) {
    mtevL(noit_error, "bad key read in resolver cache.\n"); goto bail;
  }
  if(read(fd, tmpb, lens[1]) != lens[1]) {
    mtevL(noit_error, "bad record read in resolver cache.\n"); goto bail;
  }
  if(tmpkey[lens[0]-1] != '\0') {
    mtevL(noit_error, "detected key corruption in resolver cache: %s.\n", tmpkey);
    goto bail;
  }
  *key = tmpkey;
  *b = tmpb;
  *blen = (int) lens[1];
  return MTEV_HOOK_CONTINUE;
 bail:
  if(tmpkey) free(tmpkey);
  if(tmpb) free(tmpb);
  return MTEV_HOOK_ABORT;
}

static int
resolver_cache_init(mtev_dso_generic_t *self) {
  if(resolver_cache_file == NULL) {
    const char ifs_str[2] = { IFS_CH, '\0' };
    char path[MAXPATHLEN];
    const char *file;
    file = mtev_conf_config_filename();
    strlcpy(path, file, sizeof(path));
    file = dirname(path);
    if(file != path) strlcpy(path, file, sizeof(path));
    strlcat(path, ifs_str, sizeof(path));
    strlcat(path, "resolver.cache", sizeof(path));
    resolver_cache_file = strdup(path);
  }
  noit_resolver_cache_store_hook_register("resolver_cache", resolver_cache_store_impl, NULL);
  noit_resolver_cache_load_hook_register("resolver_cache", resolver_cache_load_impl, NULL);
  return 0;
}

mtev_dso_generic_t resolver_cache = {
  {
    .magic = MTEV_GENERIC_MAGIC,
    .version = MTEV_GENERIC_ABI_VERSION,
    .name = "resolver_cache",
    .description = "dns_cache backingstore",
    .xml_description = resolver_cache_xml_description,
    .onload = resolver_cache_onload
  },
  resolver_cache_config,
  resolver_cache_init
};

