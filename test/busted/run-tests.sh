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
    if [[ -f /opt/llvm-5.0.0/bin/llvm-symbolizer ]]
    then
      ASAN_SYMBOLIZER_PATH=/opt/llvm-5.0.0/bin/llvm-symbolizer
    else
      ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer
    fi

    ASAN_OPTIONS="${ASAN_OPTIONS},log_path=${PWD}/${DIR}/asan.log"
    export ASAN_SYMBOLIZER_PATH ASAN_OPTIONS

    echo "Running tests with ASAN"
    mtevbusted-asan $@
  elif [[ -n "$BUILD_UBSAN" ]]; then
    UBSAN_OPTIONS="${UBSAN_OPTIONS},log_path=${PWD}/${DIR}/ubsan.log"
    export UBSAN_OPTIONS

    echo "Running tests with UBSAN"
    mtevbusted $@
  fi
fi
