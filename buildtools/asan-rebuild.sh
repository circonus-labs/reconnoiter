#!/bin/bash

OS="$1"
if test -z "${OS}" ; then
    echo Usage: asan-rebuild.sh '<OS>' '<optional_addl_make_switches>...' 1>&2
    exit 1
fi
shift
source ./buildtools/$OS/env-asan.inc
autoreconf -i
./buildtools/${OS}/configure.sh
EXIT_CODE="$?"
if [ "$EXIT_CODE" != "0" ] ; then
  echo "*************************"
  echo "*** Configure failed! ***"
  echo "*************************"
  exit $EXIT_CODE
fi
./buildtools/build-clean.sh $@
EXIT_CODE="$?"
if [ "$EXIT_CODE" != "0" ] ; then
  echo "*********************"
  echo "*** Build failed! ***"
  echo "*********************"
else
  echo "Successfully completed asan rebuild."
fi
