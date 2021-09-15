#!/bin/bash

OS="$1"
if test -z "${OS}" ; then
    echo Usage: ci-ubsan.sh '<OS>' 1>&2
    exit 1
fi
shift   # further arguments are passed to 'make'

# Remember the working directory when we started
original_wd="$PWD"

# Clear environment, with exceptions
for i in $(env | sed 's/=.*//' | grep -Ev '^(CI_ENV|JENKINS|GIT_|JOB_NAME|BUILD_NUMBER)'); do
    unset "$i"
done

source buildtools/$OS/env-ubsan.inc

set -e

function P { printf "${OS}-ci-ubsan: %s\n" "$*"; }
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
make $@
P OK

# Don't exit if tests fail, as there may be UBSAN logs we'll want to see.
set +e

H Test
make check
ERR_STATUS=$?

sleep 1  # we might race with the appearance of logs?

# This is a crazy hack because ubsan logging does not seem to work
pushd $original_wd > /dev/null
pushd test/busted > /dev/null

H Extract UBSAN Logs
find . -name '*.out' | xargs -t grep -Ev '^(\[20|\*)'

popd > /dev/null

if [[ $ERR_STATUS -gt 0 ]]; then
    exit $ERR_STATUS
fi

P OK
