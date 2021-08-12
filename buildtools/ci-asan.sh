#!/bin/bash

OS="$1"
if test -z "${OS}" ; then
    echo Usage: ci-asan.sh '<OS>' 1>&2
    exit 1
fi

# Clear environment, with exceptions
for i in $(env | sed 's/=.*//' | grep -Ev '^(CI_ENV|JENKINS|GIT_|JOB_NAME|BUILD_NUMBER)'); do
    unset "$i"
done

source buildtools/$OS/env-asan.inc

set -e

function P { printf "${OS}-ci-asan: %s\n" "$*"; }
function H {
    P --------------------------------------------------------------------------------
    P "$*"
    P --------------------------------------------------------------------------------
}

H Install dependencies
./buildtools/$OS/install-dependencies.sh
P OK

H Build
autoreconf -i
./configure --prefix=/opt/noit/prod \
	    --exec-prefix=/opt/noit/prod \
	    --libdir=/opt/noit/prod/lib \
	    --libexecdir=/opt/noit/prod/libexec
make clean
make
P OK

H Test
make check
P OK
