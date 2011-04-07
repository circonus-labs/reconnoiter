dnl
dnl $ Id: $
dnl

PHP_ARG_WITH(jlog, jlog,[  --with-jlog[=DIR]    With jlog support])


if test "$PHP_JLOG" != "no"; then
  if test "$PHP_JLOG" == "yes"; then
	if test -d /opt/msys ; then
      PHP_JLOG="/opt/msys/jlog"
	else
      PHP_JLOG="/opt/ecelerity"
    fi
  fi
  CPPFLAGS="$CPPFLAGS $INCLUDES -DHAVE_JLOG"
  
  PHP_ADD_INCLUDE(..)
  PHP_ADD_INCLUDE(../..)
  PHP_ADD_INCLUDE(.)
  PHP_ADD_INCLUDE($PHP_JLOG/include)
  case $PHP_JLOG in
    *ecelerity*)
      dnl has architecture specific include dir
      archdir=`uname -p`
  		PHP_ADD_INCLUDE($PHP_JLOG/include/$archdir)
      ;;
  esac
  OLD_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $INCLUDES -DHAVE_JLOG"
  CPPFLAGS="$OLD_CPPFLAGS"
  PHP_SUBST(JLOG_SHARED_LIBADD)

  PHP_ADD_LIBRARY_WITH_PATH(jlog, $PHP_JLOG/lib64, JLOG_SHARED_LIBADD)


  PHP_SUBST(JLOG_SHARED_LIBADD)
  AC_DEFINE(HAVE_JLOG, 1, [ ])
  PHP_NEW_EXTENSION(jlog, jlog.c , $ext_shared)

fi

