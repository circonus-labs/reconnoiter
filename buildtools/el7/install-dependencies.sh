#!/bin/bash
#
SUDO=/usr/bin/sudo
YUM=/usr/bin/yum

DEPS_SYSTEM="
    autoconf
    automake
    binutils
    gcc
    gcc-c++
    glibc-headers
    libtool
    make
"

DEPS_BUILD="
    circonus-developer-build-maven
    circonus-developer-jq
    circonus-field-ck
    circonus-field-jdk
    circonus-platform-database-mysql-client
    circonus-platform-database-postgresql-client
    circonus-platform-library-apr
    circonus-platform-library-apr-util
    circonus-platform-library-curl
    circonus-platform-library-flatcc
    circonus-platform-library-hwloc
    circonus-platform-library-jlog
    circonus-platform-library-libcircllhist
    circonus-platform-library-libmtev
    circonus-platform-library-libssh2
    circonus-platform-library-net-snmp-c
    circonus-platform-library-protobuf-c
    circonus-platform-library-udns
    circonus-platform-library-uuid
    circonus-platform-library-yajl
    circonus-platform-library-yajl
    circonus-platform-runtime-luajit
    lapack-devel
    libxml2-devel
    libxslt-devel
    ncurses-devel
    openssl-devel
    pcre-devel
    rsync
    zlib-devel
"

DEPS_CIRC="
    circonus-platform-lua-luasocket
    circonus-developer-mtevbusted
"

echo "Installing buildenv and reconnoiter deps"
$SUDO $YUM -y install $DEPS_SYSTEM $DEPS_BUILD $DEPS_CIRC
