/*
 * Copyright (c) 2005-2009, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *    * Neither the name OmniTI Computer Consulting, Inc. nor the names
 *      of its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
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

#define DEFAULT_JLOG_SUBSCRIBER "stratcon"

#include "noit_defines.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <assert.h>
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#if HAVE_DIRENT_H
#include <dirent.h>
#endif

#define noit_log_impl
#include "utils/noit_log.h"
#include "utils/noit_hash.h"
#include "jlog/jlog.h"
#include "jlog/jlog_private.h"

struct _noit_log_stream {
  unsigned enabled:1;
  unsigned debug:1;
  /* Above is exposed... */
  char *type;
  char *name;
  int mode;
  char *path;
  logops_t *ops;
  void *op_ctx;
  noit_hash_table *config;
  struct _noit_log_stream_outlet_list *outlets;
  pthread_rwlock_t *lock;
};

static noit_hash_table noit_loggers = NOIT_HASH_EMPTY;
static noit_hash_table noit_logops = NOIT_HASH_EMPTY;
noit_log_stream_t noit_stderr = NULL;
noit_log_stream_t noit_error = NULL;
noit_log_stream_t noit_debug = NULL;

static int
posix_logio_open(noit_log_stream_t ls) {
  int fd;
  ls->mode = 0664;
  fd = open(ls->path, O_CREAT|O_WRONLY|O_APPEND, ls->mode);
  if(fd < 0) {
    ls->op_ctx = NULL;
    return -1;
  }
  ls->op_ctx = (void *)fd;
  return 0;
}
static int
posix_logio_reopen(noit_log_stream_t ls) {
  if(ls->path) {
    int newfd, oldfd;
    oldfd = (int)ls->op_ctx;
    newfd = open(ls->path, O_CREAT|O_WRONLY|O_APPEND, ls->mode);
    if(newfd >= 0) {
      ls->op_ctx = (void *)newfd;
      if(oldfd >= 0) close(oldfd);
      return 0;
    }
  }
  return -1;
}
static int
posix_logio_write(noit_log_stream_t ls, const void *buf, size_t len) {
  int fd;
  fd = (int)ls->op_ctx;
  if(fd < 0) return -1;
  return write(fd, buf, len);
}
static int
posix_logio_close(noit_log_stream_t ls) {
  int fd;
  fd = (int)ls->op_ctx;
  return close(fd);
}
static size_t
posix_logio_size(noit_log_stream_t ls) {
  int fd;
  struct stat sb;
  fd = (int)ls->op_ctx;
  if(fstat(fd, &sb) == 0) {
    return (size_t)sb.st_size;
  }
  return -1;
}
static logops_t posix_logio_ops = {
  posix_logio_open,
  posix_logio_reopen,
  posix_logio_write,
  posix_logio_close,
  posix_logio_size
};

static int
jlog_lspath_to_fspath(noit_log_stream_t ls, char *buff, int len,
                      char **subout) {
  char *sub;
  if(subout) *subout = NULL;
  if(!ls->path) return -1;
  strlcpy(buff, ls->path, len);
  sub = strchr(buff, '(');
  if(sub) {
    char *esub = strchr(sub, ')');
    if(esub) {
      *esub = '\0';
      *sub = '\0';
      sub += 1;
      if(subout) *subout = sub;
    }
  }
  return strlen(buff);
}

/* These next functions arr basically cribbed from jlogctl.c */
static int
is_datafile(const char *f, u_int32_t *logid) {
  int i;
  u_int32_t l = 0;
  for(i=0; i<8; i++) {
    if((f[i] >= '0' && f[i] <= '9') ||
       (f[i] >= 'a' && f[i] <= 'f')) {
      l <<= 4;
      l |= (f[i] < 'a') ? (f[i] - '0') : (f[i] - 'a' + 10);
    }
    else
      return 0;
  }
  if(f[i] != '\0') return 0;
  if(logid) *logid = l;
  return 1;
}

static int
jlog_logio_cleanse(noit_log_stream_t ls) {
  jlog_ctx *log;
  DIR *d;
  struct dirent de, *entry;
  int cnt = 0;
  char path[PATH_MAX];

  log = (jlog_ctx *)ls->op_ctx;
  if(!log) return -1;
  if(jlog_lspath_to_fspath(ls, path, sizeof(path), NULL) <= 0) return -1;
  d = opendir(path);
  if(!d) return -1;
  while(portable_readdir_r(d, &de, &entry) == 0 && entry != NULL) {
    u_int32_t logid;
    if(is_datafile(entry->d_name, &logid)) {
      int rv;
      struct stat st;
      char fullfile[PATH_MAX];
      char fullidx[PATH_MAX];

      snprintf(fullfile, sizeof(fullfile), "%s/%s", path, entry->d_name);
      snprintf(fullidx, sizeof(fullidx), "%s/%s" INDEX_EXT,
               path, entry->d_name);
      while((rv = stat(fullfile, &st)) != 0 && errno == EINTR);
      if(rv == 0) {
        int readers;
        readers = __jlog_pending_readers(log, logid);
        if(readers == 0) {
          unlink(fullfile);
          unlink(fullidx);
        }
      }
    }
  }
  closedir(d);
  return cnt;
}

static int
jlog_logio_reopen(noit_log_stream_t ls) {
  char **subs;
  int i;
  /* reopening only has the effect of removing temporary subscriptions */
  /* (they start with ~ in our hair-brained model */

  if(ls->lock) pthread_rwlock_wrlock(ls->lock);
  if(jlog_ctx_list_subscribers(ls->op_ctx, &subs) == -1)
    goto bail;

  for(i=0;subs[i];i++)
    if(subs[i][0] == '~')
      jlog_ctx_remove_subscriber(ls->op_ctx, subs[i]);

  jlog_ctx_list_subscribers_dispose(ls->op_ctx, subs);
  jlog_logio_cleanse(ls);
 bail:
  if(ls->lock) pthread_rwlock_unlock(ls->lock);
  return 0;
}
static int
jlog_logio_open(noit_log_stream_t ls) {
  char path[PATH_MAX], *sub;
  jlog_ctx *log = NULL;

  if(jlog_lspath_to_fspath(ls, path, sizeof(path), &sub) <= 0) return -1;
  log = jlog_new(path);
  if(!log) return -1;
  /* Open the writer. */
  if(jlog_ctx_open_writer(log)) {
    /* If that fails, we'll give one attempt at initiailizing it. */
    /* But, since we attempted to open it as a writer, it is tainted. */
    /* path: close, new, init, close, new, writer, add subscriber */
    jlog_ctx_close(log);
    log = jlog_new(path);
    if(jlog_ctx_init(log)) {
      noitL(noit_error, "Cannot init jlog writer: %s\n",
            jlog_ctx_err_string(log));
      jlog_ctx_close(log);
      return -1;
    }
    /* After it is initialized, we can try to reopen it as a writer. */
    jlog_ctx_close(log);
    log = jlog_new(path);
    if(jlog_ctx_open_writer(log)) {
      noitL(noit_error, "Cannot open jlog writer: %s\n",
            jlog_ctx_err_string(log));
      jlog_ctx_close(log);
      return -1;
    }
    /* The first time we open after an init, we should add the subscriber. */
    if(sub)
      jlog_ctx_add_subscriber(log, sub, JLOG_BEGIN);
    else
      jlog_ctx_add_subscriber(log, DEFAULT_JLOG_SUBSCRIBER, JLOG_BEGIN);
  }
  ls->op_ctx = log;
  /* We do this to clean things up */
  jlog_logio_reopen(ls);
  return 0;
}
static int
jlog_logio_write(noit_log_stream_t ls, const void *buf, size_t len) {
  int rv = -1;
  if(!ls->op_ctx) return -1;
  pthread_rwlock_rdlock(ls->lock);
  if(jlog_ctx_write((jlog_ctx *)ls->op_ctx, buf, len) == 0)
    rv = len;
  pthread_rwlock_unlock(ls->lock);
  return rv;
}
static int
jlog_logio_close(noit_log_stream_t ls) {
  if(ls->op_ctx) {
    jlog_ctx_close((jlog_ctx *)ls->op_ctx);
    ls->op_ctx = NULL;
  }
  return 0;
}
static size_t
jlog_logio_size(noit_log_stream_t ls) {
  size_t size;
  if(!ls->op_ctx) return -1;
  pthread_rwlock_rdlock(ls->lock);
  size = jlog_raw_size((jlog_ctx *)ls->op_ctx);
  pthread_rwlock_unlock(ls->lock);
  return size;
}
static logops_t jlog_logio_ops = {
  jlog_logio_open,
  jlog_logio_reopen,
  jlog_logio_write,
  jlog_logio_close,
  jlog_logio_size
};

void
noit_log_init() {
  noit_hash_init(&noit_loggers);
  noit_hash_init(&noit_logops);
  noit_register_logops("file", &posix_logio_ops);
  noit_register_logops("jlog", &jlog_logio_ops);
  noit_stderr = noit_log_stream_new_on_fd("stderr", 2, NULL);
  noit_error = noit_log_stream_new("error", NULL, NULL, NULL, NULL);
  noit_debug = noit_log_stream_new("debug", NULL, NULL, NULL, NULL);
}

void
noit_register_logops(const char *name, logops_t *ops) {
  noit_hash_store(&noit_logops, strdup(name), strlen(name), ops);
}

void *
noit_log_stream_get_ctx(noit_log_stream_t ls) {
  return ls->op_ctx;
}

void
noit_log_stream_set_ctx(noit_log_stream_t ls, void *nctx) {
  ls->op_ctx = nctx;
}

const char *
noit_log_stream_get_type(noit_log_stream_t ls) {
  return ls->type;
}

const char *
noit_log_stream_get_name(noit_log_stream_t ls) {
  return ls->name;
}

const char *
noit_log_stream_get_path(noit_log_stream_t ls) {
  return ls->path;
}

noit_log_stream_t
noit_log_stream_new_on_fd(const char *name, int fd, noit_hash_table *config) {
  noit_log_stream_t ls;
  ls = calloc(1, sizeof(*ls));
  ls->name = strdup(name);
  ls->ops = &posix_logio_ops;
  ls->op_ctx = (void *)fd;
  ls->enabled = 1;
  ls->config = config;
  ls->lock = calloc(1, sizeof(*ls->lock));
  pthread_rwlock_init(ls->lock, NULL);
  /* This double strdup of ls->name is needed, look for the next one
   * for an explanation.
   */
  if(noit_hash_store(&noit_loggers,
                     strdup(ls->name), strlen(ls->name), ls) == 0) {
    free(ls->name);
    free(ls);
    return NULL;
  }
  return ls;
}

noit_log_stream_t
noit_log_stream_new_on_file(const char *path, noit_hash_table *config) {
  return noit_log_stream_new(path, "file", path, NULL, config);
}

noit_log_stream_t
noit_log_stream_new(const char *name, const char *type, const char *path,
                    void *ctx, noit_hash_table *config) {
  noit_log_stream_t ls, saved;
  struct _noit_log_stream tmpbuf;
  ls = calloc(1, sizeof(*ls));
  ls->name = strdup(name);
  ls->path = path ? strdup(path) : NULL;
  ls->type = type ? strdup(type) : NULL;
  ls->enabled = 1;
  ls->config = config;
  if(!type)
    ls->ops = NULL;
  else if(!noit_hash_retrieve(&noit_logops, type, strlen(type),
                              (void **)&ls->ops))
    goto freebail;
 
  if(ls->ops && ls->ops->openop(ls)) goto freebail;

  saved = noit_log_stream_find(name);
  if(saved) {
    pthread_rwlock_t *lock = saved->lock;
    memcpy(&tmpbuf, saved, sizeof(*saved));
    memcpy(saved, ls, sizeof(*saved));
    memcpy(ls, &tmpbuf, sizeof(*saved));
    saved->lock = lock;

    ls->lock = NULL;
    noit_log_stream_free(ls);
    ls = saved;
  }
  else {
    /* We strdup the name *again*.  We'going to kansas city shuffle the
     * ls later (see memcpy above).  However, if don't strdup, then the
     * noit_log_stream_free up there will sweep our key right our from
     * under us.
     */
    if(noit_hash_store(&noit_loggers,
                       strdup(ls->name), strlen(ls->name), ls) == 0)
      goto freebail;
    ls->lock = calloc(1, sizeof(*ls->lock));
    pthread_rwlock_init(ls->lock, NULL);
  }
  /* This is for things that don't open on paths */
  if(ctx) ls->op_ctx = ctx;
  return ls;

 freebail:
  fprintf(stderr, "Failed to instantiate logger(%s,%s,%s)\n",
          name, type ? type : "[null]", path ? path : "[null]");
  free(ls->name);
  if(ls->path) free(ls->path);
  if(ls->type) free(ls->type);
  free(ls);
  return NULL;
}

noit_log_stream_t
noit_log_stream_find(const char *name) {
  void *vls;
  if(noit_hash_retrieve(&noit_loggers, name, strlen(name), &vls)) {
    return (noit_log_stream_t)vls;
  }
  return NULL;
}

void
noit_log_stream_remove(const char *name) {
  noit_hash_delete(&noit_loggers, name, strlen(name), NULL, NULL);
}

void
noit_log_stream_add_stream(noit_log_stream_t ls, noit_log_stream_t outlet) {
  struct _noit_log_stream_outlet_list *newnode;
  newnode = calloc(1, sizeof(*newnode));
  newnode->outlet = outlet;
  newnode->next = ls->outlets;
  ls->outlets = newnode;
}

noit_log_stream_t
noit_log_stream_remove_stream(noit_log_stream_t ls, const char *name) {
  noit_log_stream_t outlet;
  struct _noit_log_stream_outlet_list *node, *tmp;
  if(!ls->outlets) return NULL;
  if(!strcmp(ls->outlets->outlet->name, name)) {
    node = ls->outlets;
    ls->outlets = node->next;
    outlet = node->outlet;
    free(node);
    return outlet;
  }
  for(node = ls->outlets; node->next; node = node->next) {
    if(!strcmp(node->next->outlet->name, name)) {
      /* splice */
      tmp = node->next;
      node->next = tmp->next;
      /* pluck */
      outlet = tmp->outlet;
      /* shed */
      free(tmp);
      /* return */
      return outlet;
    }
  }
  return NULL;
}

void noit_log_stream_reopen(noit_log_stream_t ls) {
  struct _noit_log_stream_outlet_list *node;
  if(ls->ops) ls->ops->reopenop(ls);
  for(node = ls->outlets; node; node = node->next) {
    noit_log_stream_reopen(node->outlet);
  }
}

void
noit_log_stream_close(noit_log_stream_t ls) {
  if(ls->ops) ls->ops->closeop(ls);
}

size_t
noit_log_stream_size(noit_log_stream_t ls) {
  if(ls->ops && ls->ops->sizeop) return ls->ops->sizeop(ls);
  return -1;
}

void
noit_log_stream_free(noit_log_stream_t ls) {
  if(ls) {
    struct _noit_log_stream_outlet_list *node;
    if(ls->name) free(ls->name);
    if(ls->path) free(ls->path);
    while(ls->outlets) {
      node = ls->outlets->next;
      free(ls->outlets);
      ls->outlets = node;
    }
    if(ls->config) {
      noit_hash_destroy(ls->config, free, free);
      free(ls->config);
    }
    if(ls->lock) {
      pthread_rwlock_destroy(ls->lock);
      free(ls->lock);
    }
    free(ls);
  }
}

static int
noit_log_line(noit_log_stream_t ls, char *buffer, size_t len) {
  int rv = 0;
  struct _noit_log_stream_outlet_list *node;
  if(ls->ops)
    rv = ls->ops->writeop(ls, buffer, len); /* Not much one can do about errors */
  for(node = ls->outlets; node; node = node->next) {
    int srv = 0;
    srv = noit_log_line(node->outlet, buffer, len);
    if(srv) rv = srv;
  }
  return rv;
}
int
noit_vlog(noit_log_stream_t ls, struct timeval *now,
          const char *file, int line,
          const char *format, va_list arg) {
  int rv = 0, allocd = 0;
  char buffer[4096], *dynbuff = NULL;
  char fbuf[1024];
#ifdef va_copy
  va_list copy;
#endif

  if(ls->enabled) {
    int len;
    if(ls->debug) {
      struct tm _tm, *tm = &_tm;
      char tbuf[32];
      time_t s = (time_t)now->tv_sec;
      tm = localtime_r(&s, &_tm);
      strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", tm);
      snprintf(fbuf, sizeof(fbuf), "[%s.%06d %s:%d] %s",
               tbuf, (int)now->tv_usec, file, line, format);
      format = fbuf;
    }
#ifdef va_copy
    va_copy(copy, arg);
    len = vsnprintf(buffer, sizeof(buffer), format, copy);
    va_end(copy);
#else
    len = vsnprintf(buffer, sizeof(buffer), format, arg);
#endif
    if(len > sizeof(buffer)) {
      allocd = sizeof(buffer);
      while(len > allocd) { /* guaranteed true the first time */
        while(len > allocd) allocd <<= 2;
        if(dynbuff) free(dynbuff);
        dynbuff = malloc(allocd);
        assert(dynbuff);
#ifdef va_copy
        va_copy(copy, arg);
        len = vsnprintf(dynbuff, allocd, format, copy);
        va_end(copy);
#else
        len = vsnprintf(dynbuff, allocd, format, arg);
#endif
      }
      rv = noit_log_line(ls, dynbuff, len);
      free(dynbuff);
    }
    else {
      rv = noit_log_line(ls, buffer, len);
    }
    if(rv == len) return 0;
    return -1;
  }
  return 0;
}

int
noit_log(noit_log_stream_t ls, struct timeval *now,
         const char *file, int line, const char *format, ...) {
  int rv;
  va_list arg;
  va_start(arg, format);
  rv = noit_vlog(ls, now, file, line, format, arg);
  va_end(arg);
  return rv;
}

int
noit_log_reopen_all() {
  noit_hash_iter iter = NOIT_HASH_ITER_ZERO;
  const char *k;
  int klen, rv = 0;
  void *data;
  noit_log_stream_t ls;

  while(noit_hash_next(&noit_loggers, &iter, &k, &klen, &data)) {
    ls = data;
    if(ls->ops) if(ls->ops->reopenop(ls) < 0) rv = -1;
  }
  return rv;
}

