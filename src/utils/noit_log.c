/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include "utils/noit_log.h"
#include "utils/noit_hash.h"

static noit_hash_table noit_loggers = NOIT_HASH_EMPTY;
noit_log_stream_t noit_stderr = NULL;
noit_log_stream_t noit_error = NULL;
noit_log_stream_t noit_debug = NULL;

static int
posix_logio_open(noit_log_stream_t ls) {
  int fd;
  fd = open(ls->path, O_CREAT|O_WRONLY|O_APPEND);
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
    newfd = open(ls->path, O_CREAT|O_WRONLY|O_APPEND);
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
static logops_t posix_logio_ops = {
  posix_logio_open,
  posix_logio_reopen,
  posix_logio_write,
  posix_logio_close,
};

void
noit_log_init() {
  noit_hash_init(&noit_loggers);
  noit_stderr = noit_log_stream_new_on_fd("stderr", 2);
  noit_error = noit_log_stream_new("error", NULL);
  noit_debug = noit_log_stream_new("debug", NULL);
}

noit_log_stream_t
noit_log_stream_new_on_fd(const char *name, int fd) {
  noit_log_stream_t ls;
  ls = calloc(1, sizeof(*ls));
  ls->name = strdup(name);
  ls->ops = &posix_logio_ops;
  ls->op_ctx = (void *)fd;
  ls->enabled = 1;
  if(noit_hash_store(&noit_loggers, ls->name, strlen(ls->name), ls) == 0) {
    free(ls->name);
    free(ls);
    return NULL;
  }
  return ls;
}

noit_log_stream_t
noit_log_stream_new_on_file(const char *path) {
  noit_log_stream_t ls;
  ls = calloc(1, sizeof(*ls));
  ls->path = strdup(path);
  ls->ops = &posix_logio_ops;
  if(ls->ops->openop(ls)) {
    free(ls->path);
    free(ls);
    return NULL;
  }
  ls->name = strdup(ls->path);
  ls->enabled = 1;
  if(noit_hash_store(&noit_loggers, ls->name, strlen(ls->name), ls) == 0) {
    free(ls->path);
    free(ls->name);
    free(ls);
    return NULL;
  }
  return ls;
}

noit_log_stream_t
noit_log_stream_new(const char *name, logops_t *ops) {
  noit_log_stream_t ls;
  ls = calloc(1, sizeof(*ls));
  ls->name = strdup(name);
  ls->enabled = 1;
  ls->ops = ops ? ops : &posix_logio_ops;
  if(!ops) ls->op_ctx = (void *)-1;
  if(noit_hash_store(&noit_loggers, ls->name, strlen(ls->name), ls) == 0) {
    free(ls->name);
    free(ls);
    return NULL;
  }
  return ls;
}

noit_log_stream_t
noit_log_stream_find(const char *name) {
  noit_log_stream_t ls;
  if(noit_hash_retrieve(&noit_loggers, name, strlen(name), (void **)&ls)) {
    return ls;
  }
  return NULL;
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
  ls->ops->reopenop(ls);
  for(node = ls->outlets; node; node = node->next) {
    noit_log_stream_reopen(node->outlet);
  }
}

void
noit_log_stream_close(noit_log_stream_t ls) {
  struct _noit_log_stream_outlet_list *node;
  ls->ops->closeop(ls);
  for(node = ls->outlets; node; node = node->next) {
    noit_log_stream_close(node->outlet);
  }
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
    free(ls);
  }
}

void
noit_vlog(noit_log_stream_t ls, struct timeval *now,
          const char *file, int line,
          const char *format, va_list arg) {
  char buffer[4096];
  struct _noit_log_stream_outlet_list *node;
#ifdef va_copy
  va_list copy;
#endif

  if(ls->enabled) {
    int len;
#ifdef va_copy
    va_copy(copy, arg);
    len = vsnprintf(buffer, sizeof(buffer), format, copy);
    va_end(copy);
#else
    len = vsnprintf(buffer, sizeof(buffer), format, arg);
#endif
    ls->ops->writeop(ls, buffer, len); /* Not much one can do about errors */

    for(node = ls->outlets; node; node = node->next) {
  #ifdef va_copy
      va_copy(copy, arg);
      noit_vlog(node->outlet, now, file, line, format, copy);
      va_end(copy);
  #else
      noit_vlog(node->outlet, now, file, line, format, arg);
  #endif
    }
  }
}

void
noit_log(noit_log_stream_t ls, struct timeval *now,
         const char *file, int line, const char *format, ...) {
  va_list arg;
  va_start(arg, format);
  noit_vlog(ls, now, file, line, format, arg);
  va_end(arg);
}

