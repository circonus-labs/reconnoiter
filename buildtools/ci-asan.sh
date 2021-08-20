#!/bin/bash

OS="$1"
if test -z "${OS}" ; then
    echo Usage: ci-asan.sh '<OS>' 1>&2
    exit 1
fi
shift   # further arguments are passed to 'make'

# Remember the working directory when we started
original_wd="$PWD"

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
make $@
P OK

# Don't exit if tests fail, as there may be ASAN logs we'll want to see.
set +e

H Test
make check
ERR_STATUS=$?

# If ASAN logs exist, ensure they're symbolized and print them out.
# This ensures they will be recorded in CI history.
# Print them in ascending time order to aid with matching to test failures.
pushd $original_wd > /dev/null

if [[ -n "$(find test/busted -maxdepth 1 -name 'asan.log*' -print -quit)" ]]; then
    pushd test/busted > /dev/null

    H ASAN Logs
    for log in $(ls -tr asan.log.*); do
        printf "-- Begin %s --\n" $log
        /usr/bin/asan_symbolize / < $log | c++filt
        printf "-- End %s --\n\n" $log
    done

    popd > /dev/null
fi

if [[ $ERR_STATUS -gt 0 ]]; then
    exit $ERR_STATUS
fi

P OK
