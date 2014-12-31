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

#include "noit_defines.h"

#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/time.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>

#include "noit_config.h"
#include "utils/noit_log.h"
#include "utils/noit_security.h"

#ifdef HAVE_PRIV_H
#include <priv.h>
#endif

#define BAIL(a...) do { noitL(noit_error, a); return -1; } while(0)

extern int chroot(const char *);
int
noit_security_chroot(const char *path) {
  if(chroot(path)) BAIL("chroot: %s\n", strerror(errno));
  if(chdir("/")) BAIL("chdir: %s\n", strerror(errno));
  return 0;
}

static inline int isuinteger(const char *user) {
  char *cp;
  if (user == NULL || *user == '\0' || isblank(*user)) return 0;
  if (strtol (user, &cp, 10) < 0) return 0;
  return (*cp == '\0');
}
static struct passwd *
__getpwnam_r(const char *user, struct passwd *pw,
             char *buf, size_t len) {
#ifdef HAVE_GETPWNAM_R_POSIX
  struct passwd *r;
  if(0 == getpwnam_r(user, pw, buf, len, &r)) return r;
  return NULL;
#else
#if HAVE_GETPWNAM_R
  return getpwnam_r(user, pw, buf, len);
#else
  return getpwnam(user);
#endif
#endif
}
static struct group *
__getgrnam_r(const char *group, struct group *gr,
             char *buf, size_t len) {
#ifdef HAVE_GETGRNAM_R_POSIX
  struct group *r;
  if(0 == getgrnam_r(group, gr, buf, len, &r)) return r;
  return NULL;
#else
#ifdef HAVE_GETGRNAM_R
  return getgrnam_r(group, gr, buf, len);
#else
  return getgrnam(group);
#endif
#endif
}

int
noit_security_usergroup(const char *user, const char *group, noit_boolean effective) {
  static long pwnam_buflen = 0;
  static long grnam_buflen = 0;
  uid_t uid = 0;
  gid_t gid = 0;

  if(pwnam_buflen == 0)
#ifdef _SC_GETPW_R_SIZE_MAX
    pwnam_buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
#else
    pwnam_buflen = 100; /* This shouldn't be used, so size is not important. */
#endif
  if(grnam_buflen == 0)
#ifdef _SC_GETGR_R_SIZE_MAX
    grnam_buflen = sysconf(_SC_GETGR_R_SIZE_MAX);
#else
    grnam_buflen = 100;
#endif

  if(user) {
    if(isuinteger(user)) uid = atoi(user);
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
    if(isuinteger(group)) gid = atoi(group);
    else {
      struct group *gr, _gr;
      char *buf;
      if(NULL == (buf = alloca(grnam_buflen))) BAIL("alloca failed\n");
      if(NULL == (gr = __getgrnam_r(group, &_gr, buf, grnam_buflen)))
        BAIL("Cannot find group '%s'\n", group);
      gid = gr->gr_gid;
    }
  }

  if(!user && !group) return 0;

#if defined(CAP_SUPPORTED) && defined(HAVE_SETPPRIV)
  if(!effective && getpflags(PRIV_AWARE)) {
    int rv;
    priv_set_t *set;
    set = priv_allocset();
    priv_addset(set, "proc_setid");
    rv = setppriv(PRIV_ON, PRIV_EFFECTIVE, set);
    priv_freeset(set);
    if(rv) BAIL("setppriv(proc_setid) failed");
  }
#endif

  if(group) {
    if(!effective && gid == 0) BAIL("Cannot use this function to setgid(0)\n");
    if((effective ? setegid(gid) : setgid(gid)) != 0)
      BAIL("setgid(%d) failed: %s\n", (int)gid, strerror(errno));
  }
  if(user) {
    if(!effective && uid == 0) BAIL("Cannot use this function to setuid(0)\n");
    if((effective ? seteuid(uid) : setuid(uid)) != 0) 
      BAIL("setgid(%d) failed: %s\n", (int)gid, strerror(errno));

    if(!effective) {
#if defined(CAP_SUPPORTED) && defined(HAVE_SETPPRIV)
      if(getpflags(PRIV_AWARE)) {
        int rv;
        priv_set_t *set;
        set = priv_allocset();
        priv_addset(set, "proc_setid");
        rv = setppriv(PRIV_OFF, PRIV_EFFECTIVE, set);
        if(rv) BAIL("setppriv(off, effective, proc_setid) failed");
        getppriv(PRIV_EFFECTIVE, set);
        rv = setppriv(PRIV_SET, PRIV_PERMITTED, set);
        if(rv) BAIL("setppriv(effective -> permitted) failed");
        priv_freeset(set);
      }
#endif

      if(setuid(0) == 0) BAIL("setuid(0) worked, and it shouldn't have.\n");
      if(setgid(0) == 0) BAIL("setgid(0) worked, and it shouldn't have.\n");
    }
  }

  return 0;
}

int noit_security_setcaps(noit_security_captype_t type,
                          const char *capstring) {
#ifndef CAP_SUPPORTED
  noitL(noit_error, "Capabilities not supported on this platform.\n");
  return -1;
#endif
#ifdef HAVE_SETPPRIV
  int rv;
  const char *endptr;
  char *str;
  priv_set_t *set, *old;
  priv_ptype_t ptype = "Permitted";
  set = priv_str_to_set(capstring, ",", &endptr);
  if(!set) {
    noitL(noit_error, "Cannot translate '%s' to privilege set.\n", capstring);
    return -1;
  }
  switch(type) {
    case NOIT_SECURITY_CAP_PERMITTED: ptype = "Permitted"; break;
    case NOIT_SECURITY_CAP_EFFECTIVE: ptype = "Effective"; break;
    case NOIT_SECURITY_CAP_INHERITABLE: ptype = "Inheritable"; break;
  }

  old = priv_allocset();
  getppriv(ptype, old);
  str = priv_set_to_str(old, ',', PRIV_STR_PORT);
  noitL(noit_debug, "Old privs(%s) -> %s\n", ptype, str);
  priv_freeset(old);
  free(str);
  rv = setppriv(PRIV_SET, ptype, set);
  str = priv_set_to_str(set, ',', PRIV_STR_PORT);
  noitL(noit_debug, "%s privs(%s) -> %s\n",
        (rv == 0) ? "Changed to" : "Failed to change to", ptype, str);
  free(str);
  priv_freeset(set);

  return rv;
#else
  return -1;
#endif
}
