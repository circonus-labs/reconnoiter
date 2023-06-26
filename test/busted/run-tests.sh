#!/bin/bash

PATH=$PATH:/opt/circonus/bin:/opt/pgsql9214/bin
export PATH

DIR=`dirname $0`
LD_LIBRARY_PATH=$DIR/../../src
export LD_LIBRARY_PATH

if [[ -z "$BUILD_ASAN" && -z "$BUILD_UBSAN" ]]; then
  mtevbusted $@
else
  if [[ -n "$BUILD_ASAN" ]]; then
    if [[ -z "$ASAN_SYMBOLIZER_PATH" ]]; then
      if [[ -r /etc/lsb-release ]]; then
          . /etc/lsb-release
          case $DISTRIB_RELEASE in
              20.04)
                  ASAN_SYMBOLIZER_PATH=`which llvm-symbolizer-14`
                  ;;
              22.04)
                  ASAN_SYMBOLIZER_PATH=`which llvm-symbolizer-15`
                  ;;
          esac
      fi
    fi

    # disable leak detection unless specifically enabled
    if [[ "$LEAKS" != "1" ]]; then
      ASAN_OPTIONS="detect_leaks=0,$ASAN_OPTIONS"
    fi

    ASAN_OPTIONS="${ASAN_OPTIONS},log_path=${PWD}/${DIR}/asan.log"
    export ASAN_SYMBOLIZER_PATH ASAN_OPTIONS

    ASANRT_PATH=`./getasanlib.sh`

    echo "Running tests with ASAN"
    LD_PRELOAD=$ASANRT_PATH mtevbusted $@
  elif [[ -n "$BUILD_UBSAN" ]]; then
    UBSAN_OPTIONS="${UBSAN_OPTIONS},log_path=${PWD}/${DIR}/ubsan.log"
    export UBSAN_OPTIONS

    echo "Running tests with UBSAN"
    mtevbusted $@
  fi
fi
