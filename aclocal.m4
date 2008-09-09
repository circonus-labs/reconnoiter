

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

