#!/bin/bash

OS=`./buildtools/determine-os.sh local-build.sh $@ 2>/dev/null`
if [ "$?" != "0" ] ; then echo "OS detection error" ; exit 1 ; fi
if [ "$OS" == "$1" ] ; then shift; fi;
echo "Using OS: $OS"

BUILD_INCLUDE_ENV=$1
if [[ -f .latest_buildtype ]] ; then
  BUILD_INCLUDE_ENV=`cat .latest_buildtype`
fi
INCLUDE_ENV="./buildtools/$OS/env-$BUILD_INCLUDE_ENV.inc"
if [ ! -f $INCLUDE_ENV ] ; then
  TEST_INCLUDE_ENV="./buildtools/$OS/env-$1.inc"
  if [ -f $TEST_INCLUDE_ENV ] ; then
    echo "Ignoring $1 because last rebuild was normal"
    echo "You must do a rebuild to switch build types"
    shift
  fi
  BUILD_INCLUDE_ENV="normal"
  INCLUDE_ENV="./buildtools/$OS/env.inc"
  echo -n > .latest_buildtype
else
  if [[ $BUILD_INCLUDE_ENV == $1 ]] ; then
    shift
  else
    TEST_INCLUDE_ENV="./buildtools/$OS/env-$1.inc"
    if [ -f $TEST_INCLUDE_ENV ] ; then
      echo "Ignoring $1 because last rebuild was $BUILD_INCLUDE_ENV"
      echo "You must do a rebuild to switch build types"
      shift
    fi
  fi
  echo -n $BUILD_INCLUDE_ENV > .latest_buildtype
fi

ASAN_BUILD=""
if [[ "$BUILD_INCLUDE_ENV" == "asan" ]] ; then
  ASAN_BUILD=" with ASAN libs"
  export BUILD_ASAN=1
fi
echo "Performing $BUILD_INCLUDE_ENV tests${ASAN_BUILD}..."
source $INCLUDE_ENV

echo ${MAKE} check EXTRA_CFLAGS="-Wall -Werror" EXTRA_CXXFLAGS="-Wall -Werror" $@
${MAKE} check EXTRA_CFLAGS="-Wall -Werror" EXTRA_CXXFLAGS="-Wall -Werror" $@
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
  echo "*** Tests failed! ***"
  echo "*********************"
else
  echo "Successfully completed $BUILD_INCLUDE_ENV tests${ASAN_BUILD}."
fi
