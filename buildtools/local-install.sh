#!/bin/bash

OS=`./buildtools/determine-os.sh local-install.sh $@ 2>/dev/null`
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

echo "Performing $BUILD_INCLUDE_ENV install..."
source $INCLUDE_ENV

sudo make install
EXIT_CODE="$?"
if [ "$EXIT_CODE" != "0" ] ; then
  echo "***********************"
  echo "*** Install failed! ***"
  echo "***********************"
else
  echo "Successfully completed $BUILD_INCLUDE_ENV install."
fi
