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
    liblapack-dev
    libxml2-dev
    libxslt1-dev
    libncurses5-dev
    libssl1.0-dev
    libpcre3-dev
    rsync
    zlib1g-dev
    xsltproc
    fop
"

DEPS_CIRC="
    circonus-platform-lua-pgmoon
    circonus-developer-build-maven
    circonus-developer-protobuf-c-compiler
    circonus-developer-jq
    circonus-field-ck
    circonus-field-jdk
    circonus-platform-database-postgresql-client
    circonus-platform-library-apr
    circonus-platform-library-apr-util
    circonus-platform-library-curl
    circonus-platform-library-flatcc
    circonus-platform-library-hwloc
    circonus-platform-library-jlog
    circonus-platform-library-libcircllhist
    circonus-platform-library-libsnappy-c
    circonus-platform-library-libmtev
    circonus-platform-library-libssh2
    circonus-platform-library-net-snmp-c
    circonus-platform-library-protobuf
    circonus-platform-library-protobuf-c
    circonus-platform-library-udns
    circonus-platform-library-uuid
    circonus-platform-library-yajl
    circonus-platform-library-yajl
    circonus-platform-runtime-luajit
    circonus-platform-lua-luasocket
    circonus-developer-mtevbusted
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
