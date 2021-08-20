#!/bin/bash
#
SUDO=/usr/bin/sudo
APT_GET=/usr/bin/apt-get

DEPS_SYSTEM="
    autoconf
    automake
    binutils
    gcc
    g++
    libc6-dev
    libtool-bin
    make
"

DEPS_BUILD="
    libncurses5-dev
    libpcre3-dev
    libssl-dev
    libxml2-dev
    libxslt1-dev
    zlib1g-dev
    liblapack-dev
    rsync
    xsltproc
    fop
    gawk
"

DEPS_CIRC="
    circonus-developer-build-maven
    circonus-developer-mtevbusted
    circonus-developer-protobuf-c-compiler
    circonus-field-ck
    circonus-field-jdk
    circonus-platform-database-lmdb
    circonus-platform-database-lmdb-asan
    circonus-platform-database-postgresql-client
    circonus-platform-library-apr
    circonus-platform-library-apr-util
    circonus-platform-library-curl
    circonus-platform-library-flatcc
    circonus-platform-library-jemalloc
    circonus-platform-library-jlog
    circonus-platform-library-libcircllhist
    circonus-platform-library-libmtev
    circonus-platform-library-libmtev-asan
    circonus-platform-library-libsnappy-c
    circonus-platform-library-net-snmp-c
    circonus-platform-library-hwloc
    circonus-platform-library-picklingtools
    circonus-platform-library-protobuf
    circonus-platform-library-protobuf-c
    circonus-platform-library-udns
    circonus-platform-library-uuid
    circonus-platform-library-yajl
    circonus-platform-lua-luasocket
    circonus-platform-lua-pgmoon
    circonus-platform-runtime-luajit
"

echo "Installing buildenv and reconnoiter deps"

circonus_key="$(/usr/bin/apt-key list 6D4FA648)"

if [[ -z $circonus_key ]]; then
    echo "Circonus package signing key is not installed."
    echo "Please see https://github.com/circonus/circonus-wiki/blob/master/PackageSigning.md#gpg-key"
    exit 1
fi

if ! $SUDO $APT_GET update; then
    echo "Failed to refresh repo metadata"
    exit 1
fi

if ! $SUDO $APT_GET -y install $DEPS_SYSTEM $DEPS_BUILD $DEPS_CIRC ; then
    echo "Failed to install one or more dependencies"
    exit 1
fi
