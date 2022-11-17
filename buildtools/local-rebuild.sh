#!/bin/bash

OS=`./buildtools/determine-os.sh local-rebuild.sh $@ 2>/dev/null`
if [ "$?" != "0" ] ; then echo "OS detection error" ; exit 1 ; fi
if [ "$OS" == "$1" ] ; then shift; fi;
echo "Using OS: $OS"

BUILD_INCLUDE_ENV=$1
INCLUDE_ENV="./buildtools/$OS/env-$BUILD_INCLUDE_ENV.inc"
if [ ! -f $INCLUDE_ENV ] ; then
  BUILD_INCLUDE_ENV="normal"
  INCLUDE_ENV="./buildtools/$OS/env.inc"
  echo -n > .latest_buildtype
else
  shift
  echo -n $BUILD_INCLUDE_ENV > .latest_buildtype
fi

ASAN_BUILD=""
if [[ "$BUILD_INCLUDE_ENV" == "asan" ]] ; then
  ASAN_BUILD=" with ASAN libs"
  export BUILD_ASAN=1
fi
echo "Performing $BUILD_INCLUDE_ENV rebuild${ASAN_BUILD}..."
source $INCLUDE_ENV

autoreconf -i
./buildtools/${OS}/configure.sh
EXIT_CODE="$?"
if [ "$EXIT_CODE" != "0" ] ; then
  echo "*************************"
  echo "*** Configure failed! ***"
  echo "*************************"
  exit $EXIT_CODE
fi
./buildtools/build-clean.sh EXTRA_CFLAGS="-Wall -Werror" EXTRA_CXXFLAGS="-Wall -Werror" $@
EXIT_CODE="$?"

echo
grep -A 10 "Compile-time Configuration" config.log
echo
echo "Primary tooling:"
grep -e "^gcc " config.log | head -n1
grep -e "^g++ " config.log | head -n1
grep -e "clang version" config.log | head -n1

echo

if [ "$EXIT_CODE" != "0" ] ; then
  echo "*********************"
  echo "*** Build failed! ***"
  echo "*********************"
else
  echo "Successfully completed $BUILD_INCLUDE_ENV rebuild${ASAN_BUILD}."
fi
