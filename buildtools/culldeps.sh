#!/bin/bash

perl -ni -e 's/(\s)\/\S+/$1/g; next if /^\s*\\?$/; print "\n" if /^\S/; print;' $*
