/*
 * Copyright (c) 2007, OmniTI Computer Consulting, Inc.
 * All rights reserved.
 */

#include "noit_defines.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>

#include "noit_config.h"
#include "utils/noit_log.h"
#include "utils/noit_security.h"

#define BAIL(a...) do { noitL(noit_error, a); return -1; } while(0)

int
noit_security_chroot(const char *path) {
  if(chroot(path)) BAIL("chroot: %s\n", strerror(errno));
  if(chdir("/")) BAIL("chdir: %s\n", strerror(errno));
  return 0;
}

static inline int ispositiveinteger(const char *user) {
  const char *cp = user;
  while(*cp) {
    switch(cp == user) {
      case 0:
        if(*cp < '0' || *cp > '9') return 0;
        break;
      default:
        if(*cp < '1' || *cp > '9') return 0;
    }
    cp++;
  }
  return (cp != user);
}
static struct passwd *
__getpwnam_r(const char *user, struct passwd *pw,
             char *buf, size_t len) {
#ifdef HAVE_GETPWNAM_R_POSIX
  return getpwnam_r(user, pw, buf, len);
#else
  struct passwd *r;
  if(0 == getpwnam_r(user, pw, buf, len, &r)) return r;
  return NULL;
#endif
}
static struct group *
__getgrnam_r(const char *group, struct group *gr,
             char *buf, size_t len) {
#ifdef HAVE_GETGRNAM_R_POSIX
  return getgrnam_r(group, gr, buf, len);
#else
  struct group *r;
  if(0 == getgrnam_r(group, gr, buf, len, &r)) return r;
  return NULL;
#endif
}

int
noit_security_usergroup(const char *user, const char *group) {
  static long pwnam_buflen = 0;
  static long grnam_buflen = 0;
  uid_t uid;
  gid_t gid;

  if(pwnam_buflen == 0) pwnam_buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
  if(grnam_buflen == 0) grnam_buflen = sysconf(_SC_GETGR_R_SIZE_MAX);

  if(user) {
    if(ispositiveinteger(user)) uid = atoi(user);
    else {
      struct passwd *pw, _pw;
      char *buf;
      if(NULL == (buf = alloca(pwnam_buflen))) BAIL("alloca failed\n");
      if(NULL == (pw = __getpwnam_r(user, &_pw, buf, pwnam_buflen)))
        BAIL("Cannot find user '%s'\n", user);
      uid = pw->pw_uid;
    }
  }

  if(group) {
    if(ispositiveinteger(group)) gid = atoi(group);
    else {
      struct group *gr, _gr;
      char *buf;
      if(NULL == (buf = alloca(grnam_buflen))) BAIL("alloca failed\n");
      if(NULL == (gr = __getgrnam_r(user, &_gr, buf, grnam_buflen)))
        BAIL("Cannot find group '%s'\n", user);
      gid = gr->gr_gid;
    }
  }

  if(group) {
    if(gid == 0) BAIL("Cannot use this function to setgid(0)\n");
    if(setgid(gid) != 0)
      BAIL("setgid(%d) failed: %s\n", (int)gid, strerror(errno));
  }
  if(user) {
    if(uid == 0) BAIL("Cannot use this function to setuid(0)\n");
    if(setuid(uid) != 0) 
      BAIL("setgid(%d) failed: %s\n", (int)gid, strerror(errno));
    if(setuid(0) == 0)
      BAIL("setuid(0) worked, and it shouldn't have.\n");
    if(setgid(0) == 0)
      BAIL("setgid(0) worked, and it shouldn't have.\n");
  }
  return 0;
}
