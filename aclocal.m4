

dnl
dnl checks for password entry functions and header files
dnl
AC_DEFUN(GETPWNAM_R_POSIX,
[

HAVE_GETPWNAM_R_POSIX=""

AC_MSG_CHECKING(for POSIX.1c getpwnam_r)
AC_TRY_LINK([
#include <pwd.h>
#include <stdlib.h>],
  getpwnam_r(NULL,NULL,NULL,0,NULL);,
  AC_DEFINE(HAVE_GETPWNAM_R_POSIX,1,POSIX.1c getpwnam_r)
  AC_MSG_RESULT(yes),
  AC_MSG_RESULT(no))
])

dnl
dnl checks for group entry functions and header files
dnl
AC_DEFUN(GETGRNAM_R_POSIX,
[

HAVE_GETGRNAM_R_POSIX=""

AC_MSG_CHECKING(for POSIX.1c getgrnam_r)
AC_TRY_LINK([
#include <grp.h>
#include <stdlib.h>],
  getgrnam_r(NULL,NULL,NULL,0,NULL);,
  AC_DEFINE(HAVE_GETGRNAM_R_POSIX,1,POSIX.1c getgrnam_r)
  AC_MSG_RESULT(yes),
  AC_MSG_RESULT(no))
])

# LICENSE
#
#   Copyright (c) 2008 Guido U. Draheim <guidod@gmx.de>
#   Copyright (c) 2011 Maarten Bosmans <mkbosmans@gmail.com>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.  This file is offered as-is, without any
#   warranty.

#serial 6

AC_DEFUN([AX_CHECK_COMPILE_FLAG],
[
m4_if(m4_version_compare(m4_defn([AC_AUTOCONF_VERSION]), [2.64]), -1,
  [
    AC_MSG_CHECKING([whether compiler accepts $1])
    AC_MSG_RESULT([skipping, autoconf too old])
  ],
  [
    AS_VAR_PUSHDEF([CACHEVAR],[ax_cv_check_[]_AC_LANG_ABBREV[]flags_$4_$1])dnl
    AC_CACHE_CHECK([whether _AC_LANG compiler accepts $1], CACHEVAR, [
      ax_check_save_flags=$[]_AC_LANG_PREFIX[]FLAGS
      _AC_LANG_PREFIX[]FLAGS="$[]_AC_LANG_PREFIX[]FLAGS $4 $1"
      AC_COMPILE_IFELSE([m4_default([$5],[AC_LANG_PROGRAM()])],
        [AS_VAR_SET(CACHEVAR,[yes])],
        [AS_VAR_SET(CACHEVAR,[no])])
      _AC_LANG_PREFIX[]FLAGS=$ax_check_save_flags])
    AS_VAR_IF(CACHEVAR,yes,
      [m4_default([$2], :)],
      [m4_default([$3], :)])
    AS_VAR_POPDEF([CACHEVAR])dnl
  ])
])dnl AX_CHECK_COMPILE_FLAGS
