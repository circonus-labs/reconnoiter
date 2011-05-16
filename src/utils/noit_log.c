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
#include <sys/uio.h>
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
#include "utils/noit_atomic.h"
#include "jlog/jlog.h"
#include "jlog/jlog_private.h"
#ifdef DTRACE_ENABLED
#include "utils/dtrace_probes.h"
#else
#define NOIT_LOG_LOG(a,b,c,d)
#define NOIT_LOG_LOG_ENABLED() 0
#endif

static int DEBUG_LOG_ENABLED() {
  static int enabled = -1;
  if(enabled == -1) {
    char *env = getenv("NOIT_LOG_DEBUG");
    enabled = env ? atoi(env) : 0;
  }
  return enabled;
}
#define debug_printf(a...) do { \
  if(DEBUG_LOG_ENABLED()) fprintf(stderr, a); \
} while(0)

struct _noit_log_stream {
  unsigned enabled:1;
  unsigned debug:1;
  unsigned timestamps:1;
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
  noit_atomic32_t written;
  unsigned deps_materialized:1;
  unsigned debug_below:1;
  unsigned timestamps_below:1;
};

static noit_hash_table noit_loggers = NOIT_HASH_EMPTY;
static noit_hash_table noit_logops = NOIT_HASH_EMPTY;
noit_log_stream_t noit_stderr = NULL;
noit_log_stream_t noit_error = NULL;
noit_log_stream_t noit_debug = NULL;

int noit_log_global_enabled() {
  return NOIT_LOG_LOG_ENABLED();
}

#define MATERIALIZE_DEPS(ls) do { \
  if(!(ls)->deps_materialized) materialize_deps(ls); \
} while(0)

static void materialize_deps(noit_log_stream_t ls) {
  if(ls->deps_materialized) return;
  if(ls->debug) ls->debug_below = 1;
  if(ls->timestamps) ls->timestamps_below = 1;
  if(ls->debug_below == 0 || ls->timestamps_below == 0) {
    /* we might have children than need these */
    struct _noit_log_stream_outlet_list *node;
    for(node = ls->outlets; node; node = node->next) {
      MATERIALIZE_DEPS(node->outlet);
      if(!ls->debug) ls->debug_below = node->outlet->debug_below;
      if(!ls->timestamps) ls->timestamps_below = node->outlet->timestamps_below;
    }
  }
  ls->deps_materialized = 1;
}
static int
posix_logio_open(noit_log_stream_t ls) {
  int fd;
  struct stat sb;
  ls->mode = 0664;
  fd = open(ls->path, O_CREAT|O_WRONLY|O_APPEND, ls->mode);
  debug_printf("opened '%s' => %d\n", ls->path, fd);
  if(fd < 0) {
    ls->op_ctx = NULL;
    return -1;
  }
  if(fstat(fd, &sb) == 0) ls->written = (int32_t)sb.st_size;
  ls->op_ctx = (void *)(vpsized_int)fd;
  return 0;
}
static int
posix_logio_reopen(noit_log_stream_t ls) {
  if(ls->path) {
    pthread_rwlock_t *lock = ls->lock;
    int newfd, oldfd, rv = -1;
    if(lock) pthread_rwlock_wrlock(lock);
    oldfd = (int)(vpsized_int)ls->op_ctx;
    newfd = open(ls->path, O_CREAT|O_WRONLY|O_APPEND, ls->mode);
    ls->written = 0;
    if(newfd >= 0) {
      struct stat sb;
      ls->op_ctx = (void *)(vpsized_int)newfd;
      if(oldfd >= 0) close(oldfd);
      rv = 0;
      if(fstat(newfd, &sb) == 0) ls->written = (int32_t)sb.st_size;
    }
    if(lock) pthread_rwlock_unlock(lock);
    return rv;
  }
  return -1;
}
static int
posix_logio_write(noit_log_stream_t ls, const void *buf, size_t len) {
  int fd, rv = -1;
  pthread_rwlock_t *lock = ls->lock;
  if(lock) pthread_rwlock_rdlock(lock);
  fd = (int)(vpsized_int)ls->op_ctx;
  debug_printf("writing to %d\n", fd);
  if(fd >= 0) rv = write(fd, buf, len);
  if(lock) pthread_rwlock_unlock(lock);
  if(rv > 0) noit_atomic_add32(&ls->written, rv);
  return rv;
}
static int
posix_logio_writev(noit_log_stream_t ls, const struct iovec *iov, int iovcnt) {
  int fd, rv = -1;
  pthread_rwlock_t *lock = ls->lock;
  if(lock) pthread_rwlock_rdlock(lock);
  fd = (int)(vpsized_int)ls->op_ctx;
  debug_printf("writ(v)ing to %d\n", fd);
  if(fd >= 0) rv = writev(fd, iov, iovcnt);
  if(lock) pthread_rwlock_unlock(lock);
  if(rv > 0) noit_atomic_add32(&ls->written, rv);
  return rv;
}
static int
posix_logio_close(noit_log_stream_t ls) {
  int fd, rv;
  pthread_rwlock_t *lock = ls->lock;
  if(lock) pthread_rwlock_wrlock(lock);
  fd = (int)(vpsized_int)ls->op_ctx;
  rv = close(fd);
  if(lock) pthread_rwlock_unlock(lock);
  return rv;
}
static size_t
posix_logio_size(noit_log_stream_t ls) {
  int fd;
  size_t s = (size_t)-1;
  struct stat sb;
  pthread_rwlock_t *lock = ls->lock;
  if(lock) pthread_rwlock_rdlock(lock);
  fd = (int)(vpsized_int)ls->op_ctx;
  if(fstat(fd, &sb) == 0) {
    s = (size_t)sb.st_size;
  }
  if(lock) pthread_rwlock_unlock(lock);
  return s;
}
static int
posix_logio_rename(noit_log_stream_t ls, const char *name) {
  int rv = 0;
  char autoname[PATH_MAX];
  pthread_rwlock_t *lock = ls->lock;
  if(name == NOIT_LOG_RENAME_AUTOTIME) {
    time_t now = time(NULL);
    snprintf(autoname, sizeof(autoname), "%s.%llu",
             ls->path, (unsigned long long)now);
    name = autoname;
  }
  if(!strcmp(name, ls->path)) return 0; /* noop */
  if(lock) pthread_rwlock_rdlock(lock);
  rv = rename(ls->path, name);
  if(lock) pthread_rwlock_unlock(lock);
  return -1;
}
static logops_t posix_logio_ops = {
  posix_logio_open,
  posix_logio_reopen,
  posix_logio_write,
  posix_logio_writev,
  posix_logio_close,
  posix_logio_size,
  posix_logio_rename
};

typedef struct jlog_line {
  char *buf;
  char buf_static[512];
  char *buf_dynamic;
  int len;
  void *next;
} jlog_line;

typedef struct {
  jlog_ctx *log;
  pthread_t writer;
  void *head;
  int gen;  /* generation */
} jlog_asynch_ctx;

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
  jlog_asynch_ctx *actx;
  jlog_ctx *log;
  DIR *d;
  struct dirent *de, *entry;
  int cnt = 0;
  char path[PATH_MAX];
  int size = 0;

  actx = (jlog_asynch_ctx *)ls->op_ctx;
  if(!actx) return -1;
  log = actx->log;
  if(!log) return -1;
  if(jlog_lspath_to_fspath(ls, path, sizeof(path), NULL) <= 0) return -1;
  d = opendir(path);

#ifdef _PC_NAME_MAX
  size = pathconf(path, _PC_NAME_MAX);
#endif
  size = MIN(size, PATH_MAX + 128);
  de = alloca(size);

  if(!d) return -1;
  while(portable_readdir_r(d, de, &entry) == 0 && entry != NULL) {
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

static jlog_line *
jlog_asynch_pop(jlog_asynch_ctx *actx, jlog_line **iter) {
  jlog_line *h = NULL, *rev = NULL;

  if(*iter) { /* we have more on the previous list */
    h = *iter;
    *iter = h->next;
    return h;
  }

  while(1) {
    h = (void *)(volatile void *)actx->head;
    if(noit_atomic_casptr((volatile void **)&actx->head, NULL, h) == h) break;
    /* TODO: load-load */
  }
  while(h) {
    /* which unshifted things into the queue -- it's backwards, reverse it */
    jlog_line *tmp = h;
    h = h->next;
    tmp->next = rev;
    rev = tmp;
  }
  if(rev) *iter = rev->next;
  else *iter = NULL;
  return rev;
}
void
jlog_asynch_push(jlog_asynch_ctx *actx, jlog_line *n) {
  while(1) {
    n->next = (void *)(volatile void *)actx->head;
    if(noit_atomic_casptr((volatile void **)&actx->head, n, n->next) == n->next) return;
    /* TODO: load-load */
  }
}
static void *
jlog_logio_asynch_writer(void *vls) {
  noit_log_stream_t ls = vls;
  jlog_asynch_ctx *actx = ls->op_ctx;
  jlog_line *iter = NULL;
  int gen = actx->gen;
  noitL(noit_error, "starting asynchronous jlog writer[%d/%p]\n",
        (int)getpid(), (void *)pthread_self());
  while(gen == actx->gen) {
    pthread_rwlock_t *lock;
    int fast = 0, max = 1000;
    jlog_line *line;
    lock = ls->lock;
    if(lock) pthread_rwlock_rdlock(lock);
    while(max > 0 && NULL != (line = jlog_asynch_pop(actx, &iter))) {
      jlog_ctx_write(actx->log, line->buf_dynamic ?
                                  line->buf_dynamic :
                                  line->buf_static,
                     line->len);
      if(line->buf_dynamic != NULL) free(line->buf_dynamic);
      free(line);
      fast = 1;
      max--;
    }
    if(lock) pthread_rwlock_unlock(lock);
    if(max > 0) {
      /* we didn't hit our limit... so we ran the queue dry */
      /* 200ms if there was nothing, 10ms otherwise */
      usleep(fast ? 10000 : 200000);
    }
  }
  noitL(noit_error, "stopping asynchronous jlog writer[%d/%p]\n",
        (int)getpid(), (void *)pthread_self());
  pthread_exit((void *)0);
}
static int
jlog_logio_reopen(noit_log_stream_t ls) {
  void *unused;
  char **subs;
  jlog_asynch_ctx *actx = ls->op_ctx;
  pthread_rwlock_t *lock = ls->lock;
  int i;
  /* reopening only has the effect of removing temporary subscriptions */
  /* (they start with ~ in our hair-brained model */

  if(lock) pthread_rwlock_wrlock(lock);
  if(jlog_ctx_list_subscribers(actx->log, &subs) == -1)
    goto bail;

  for(i=0;subs[i];i++)
    if(subs[i][0] == '~')
      jlog_ctx_remove_subscriber(actx->log, subs[i]);

  jlog_ctx_list_subscribers_dispose(actx->log, subs);
  jlog_logio_cleanse(ls);
 bail:
  if(lock) pthread_rwlock_unlock(lock);

  actx->gen++;
  pthread_join(actx->writer, &unused);
  if(pthread_create(&actx->writer, NULL, jlog_logio_asynch_writer, ls) != 0)
    return -1;
  
  return 0;
}
static int
jlog_logio_open(noit_log_stream_t ls) {
  char path[PATH_MAX], *sub, **subs, *p;
  jlog_asynch_ctx *actx;
  jlog_ctx *log = NULL;
  int i, listed, found;

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
  }

  /* Add or remove subscribers according to the current configuration. */
  listed = jlog_ctx_list_subscribers(log, &subs);
  if(listed == -1) {
    noitL(noit_error, "Cannot list jlog subscribers: %s\n",
          jlog_ctx_err_string(log));
    return -1;
  }

  if(sub) {
    /* Match all configured subscribers against jlog's list. */
    for(p=strtok(sub, ",");p;p=strtok(NULL, ",")) {
      for(i=0;i<listed;i++) {
        if((subs[i]) && (strcmp(p, subs[i]) == 0)) {
          free(subs[i]);
          subs[i] = NULL;
          break;
        }
      }
      if(i == listed)
        jlog_ctx_add_subscriber(log, p, JLOG_BEGIN);
    }

    /* Remove all unmatched subscribers. */
    for(i=0;i<listed;i++) {
      if(subs[i]) {
        jlog_ctx_remove_subscriber(log, subs[i]);
        free(subs[i]);
        subs[i] = NULL;
      }
    }

    free(subs);
    subs = NULL;
  } else {
    /* Remove all subscribers other than DEFAULT_JLOG_SUBSCRIBER. */
    found = 0;
    for(i=0;i<listed;i++) {
      if((subs[i]) && (strcmp(DEFAULT_JLOG_SUBSCRIBER, subs[i]) == 0)) {
        found = 1;
        continue;
      }
      jlog_ctx_remove_subscriber(log, subs[i]);
    }

    /* Add DEFAULT_JLOG_SUBSCRIBER if it wasn't already on the jlog's list. */
    if(!found)
      jlog_ctx_add_subscriber(log, DEFAULT_JLOG_SUBSCRIBER, JLOG_BEGIN);

    jlog_ctx_list_subscribers_dispose(log, subs);
  }

  actx = calloc(1, sizeof(*actx));
  actx->log = log;
  ls->op_ctx = actx;

  if(pthread_create(&actx->writer, NULL, jlog_logio_asynch_writer, ls) != 0)
    return -1;

  /* We do this to clean things up */
  jlog_logio_reopen(ls);
  return 0;
}
static int
jlog_logio_write(noit_log_stream_t ls, const void *buf, size_t len) {
  int rv = -1;
  jlog_asynch_ctx *actx;
  jlog_line *line;
  if(!ls->op_ctx) return -1;
  actx = ls->op_ctx;
  line = calloc(1, sizeof(*line));
  if(len > sizeof(line->buf_static)) {
    line->buf_dynamic = malloc(len);
    memcpy(line->buf_dynamic, buf, len);
  }
  else {
    memcpy(line->buf_static, buf, len);
  }
  line->len = len;
  jlog_asynch_push(actx, line);
  return rv;
}
static int
jlog_logio_close(noit_log_stream_t ls) {
  if(ls->op_ctx) {
    jlog_asynch_ctx *actx = ls->op_ctx;
    jlog_ctx_close(actx->log);
    ls->op_ctx = NULL;
  }
  return 0;
}
static size_t
jlog_logio_size(noit_log_stream_t ls) {
  size_t size;
  jlog_asynch_ctx *actx;
  pthread_rwlock_t *lock = ls->lock;
  if(!ls->op_ctx) return -1;
  actx = ls->op_ctx;
  if(lock) pthread_rwlock_rdlock(lock);
  size = jlog_raw_size(actx->log);
  if(lock) pthread_rwlock_unlock(lock);
  return size;
}
static int
jlog_logio_rename(noit_log_stream_t ls, const char *newname) {
  /* Not supported (and makes no sense) */
  return -1;
}
static logops_t jlog_logio_ops = {
  jlog_logio_open,
  jlog_logio_reopen,
  jlog_logio_write,
  NULL,
  jlog_logio_close,
  jlog_logio_size,
  jlog_logio_rename
};

void
noit_log_init() {
  noit_hash_init(&noit_loggers);
  noit_hash_init(&noit_logops);
  noit_register_logops("file", &posix_logio_ops);
  noit_register_logops("jlog", &jlog_logio_ops);
  noit_stderr = noit_log_stream_new_on_fd("stderr", 2, NULL);
  noit_stderr->timestamps = 1;
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

const char *
noit_log_stream_get_property(noit_log_stream_t ls,
                             const char *prop) {
  const char *v;
  if(ls && ls->config &&
     noit_hash_retr_str(ls->config, prop, strlen(prop), &v))
    return v;
  return NULL;
}

void
noit_log_stream_set_property(noit_log_stream_t ls,
                             const char *prop, const char *v) {
  if(!ls) return;
  if(!ls->config) {
    ls->config = calloc(1, sizeof(*ls->config));
    noit_hash_init(ls->config);
  }
  noit_hash_replace(ls->config, prop, strlen(prop), (void *)v, free, free);
}

noit_log_stream_t
noit_log_stream_new_on_fd(const char *name, int fd, noit_hash_table *config) {
  noit_log_stream_t ls;
  ls = calloc(1, sizeof(*ls));
  ls->name = strdup(name);
  ls->ops = &posix_logio_ops;
  ls->op_ctx = (void *)(vpsized_int)fd;
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
  void *vops = NULL;

  ls = calloc(1, sizeof(*ls));
  ls->name = strdup(name);
  ls->path = path ? strdup(path) : NULL;
  ls->type = type ? strdup(type) : NULL;
  ls->enabled = 1;
  ls->config = config;
  if(!type)
    ls->ops = NULL;
  else if(noit_hash_retrieve(&noit_logops, type, strlen(type),
                             &vops))
    ls->ops = vops;
  else
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

int noit_log_stream_rename(noit_log_stream_t ls, const char *newname) {
  return (ls->ops && ls->ops->renameop) ? ls->ops->renameop(ls, newname) : -1;
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

size_t
noit_log_stream_written(noit_log_stream_t ls) {
  return ls->written;
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
noit_log_writev(noit_log_stream_t ls, const struct iovec *iov, int iovcnt) {
  /* This emulates writev into a buffer for ops that don't support it */
  char stackbuff[4096], *tofree = NULL, *buff = NULL;
  int i, s = 0, ins = 0;

  if(!ls->ops) return -1;
  if(ls->ops->writevop) return ls->ops->writevop(ls, iov, iovcnt);
  if(!ls->ops->writeop) return -1;
  if(iovcnt == 1) return ls->ops->writeop(ls, iov[0].iov_base, iov[0].iov_len);

  for(i=0;i<iovcnt;i++) s+=iov[i].iov_len;
  if(s > sizeof(stackbuff)) {
    tofree = buff = malloc(s);
    if(tofree == NULL) return -1;
  }
  else buff = stackbuff;
  for(i=0;i<iovcnt;i++) {
    memcpy(buff + ins, iov[i].iov_base, iov[i].iov_len);
    ins += iov[i].iov_len;
  }
  i = ls->ops->writeop(ls, buff, s);
  if(tofree) free(tofree);
  return i;
}

static int
noit_log_line(noit_log_stream_t ls,
              const char *timebuf, int timebuflen,
              const char *debugbuf, int debugbuflen,
              const char *buffer, size_t len) {
  int rv = 0;
  struct _noit_log_stream_outlet_list *node;
  if(ls->ops) {
    int iovcnt = 0;
    struct iovec iov[3];
    if(ls->timestamps) {
      iov[iovcnt].iov_base = (void *)timebuf;
      iov[iovcnt].iov_len = timebuflen;
      iovcnt++;
    }
    if(ls->debug) {
      iov[iovcnt].iov_base = (void *)debugbuf;
      iov[iovcnt].iov_len = debugbuflen;
      iovcnt++;
    }
    iov[iovcnt].iov_base = (void *)buffer;
    iov[iovcnt].iov_len = len;
    iovcnt++;
    rv = noit_log_writev(ls, iov, iovcnt);
  }
  for(node = ls->outlets; node; node = node->next) {
    int srv = 0;
    debug_printf(" %s -> %s\n", ls->name, node->outlet->name);
    srv = noit_log_line(node->outlet, timebuf, timebuflen, debugbuf, debugbuflen, buffer, len);
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
#ifdef va_copy
  va_list copy;
#endif

  if(ls->enabled || NOIT_LOG_LOG_ENABLED()) {
    int len;
    char tbuf[48], dbuf[80];
    int tbuflen = 0, dbuflen = 0;
    MATERIALIZE_DEPS(ls);
    if(ls->timestamps_below) {
      struct tm _tm, *tm;
      char tempbuf[32];
      time_t s = (time_t)now->tv_sec;
      tm = localtime_r(&s, &_tm);
      strftime(tempbuf, sizeof(tempbuf), "%Y-%m-%d %H:%M:%S", tm);
      snprintf(tbuf, sizeof(tbuf), "[%s.%06d] ", tempbuf, (int)now->tv_usec);
      tbuflen = strlen(tbuf);
    }
    else tbuf[0] = '\0';
    if(ls->debug_below) {
      snprintf(dbuf, sizeof(dbuf), "[%s:%d] ", file, line);
      dbuflen = strlen(dbuf);
    }
    else dbuf[0] = '\0';
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
      NOIT_LOG_LOG(ls->name, (char *)file, line, dynbuff);
      if(ls->enabled)
        rv = noit_log_line(ls, tbuf, tbuflen, dbuf, dbuflen, dynbuff, len);
      free(dynbuff);
    }
    else {
      NOIT_LOG_LOG(ls->name, (char *)file, line, buffer);
      if(ls->enabled)
        rv = noit_log_line(ls, tbuf, tbuflen, dbuf, dbuflen, buffer, len);
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

