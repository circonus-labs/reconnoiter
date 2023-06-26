#!/bin/bash

if [[ -r /etc/lsb-release ]]; then
    . /etc/lsb-release
    case $DISTRIB_RELEASE in
        20.04)
            CLANG_VER="14"
            CLANG_LIB="14.0.6"
            DEFAULT_ASANRT_PATH="/usr/lib/llvm-14/lib/clang/14.0.6/lib/linux/libclang_rt.asan-x86_64.so"
            ;;
        22.04)
            CLANG_VER="15"
            CLANG_LIB="15.0.7"
            DEFAULT_ASANRT_PATH="/usr/lib/llvm-15/lib/clang/15.0.7/lib/linux/libclang_rt.asan-x86_64.so"
            ;;
    esac
fi

ASANRT_PATH=`LD_LIBRARY_PATH=/usr/lib/llvm-$CLANG_VER/lib/clang/$CLANG_LIB/lib/linux /usr/bin/ldd ../../src/noitd | grep libclang_rt.asan | cut -f 3 -d ' '`
if [[ $ASANRT_PATH = "" ]]; then
  ASANRT_PATH=$DEFAULT_ASANRT_PATH
fi
echo -n $ASANRT_PATH
