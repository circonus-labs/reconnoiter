#!/bin/sh

PATH=$PATH:/opt/circonus/bin:/opt/pgsql9214/bin
export PATH

if [ -f /opt/llvm-5.0.0/bin/llvm-symbolizer ]
then
  export ASAN_SYMBOLIZER_PATH=/opt/llvm-5.0.0/bin/llvm-symbolizer
else
  export ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer
fi
export ASAN_OPTIONS=detect_leaks=0,alloc_dealloc_mismatch=1


DIR=`dirname $0`
LD_LIBRARY_PATH=$DIR/../../src
export LD_LIBRARY_PATH

mtevbusted $@
