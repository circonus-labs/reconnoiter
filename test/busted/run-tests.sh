#!/bin/sh

PATH=$PATH:/opt/circonus/bin:/opt/pgsql9214/bin
export PATH

DIR=`dirname $0`
LD_LIBRARY_PATH=$DIR/../../src
export LD_LIBRARY_PATH

mtevbusted $@
