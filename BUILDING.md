# Building Reconnoiter

## Requirements

Compiler:
 * Support for C11 and C++17 standards is required. This means GCC 9 or later;
   Clang/LLVM is less tested but recent versions should work.

### Third-party Libraries

[Important build notes](THIRDPARTY-LIBS.md)

Required Libraries:
 * [Concurrency Kit](https://github.com/concurrencykit/ck) (libck), version 0.7.1 or later ([Patch required](THIRDPARTY-LIBS.md#concurrencykit-libck))
 * [flatcc](https://github.com/dvidelabs/flatcc), version 0.6.0. ([Patch required](THIRDPARTY-LIBS.md#flatcc))
 * [libcircllhist](https://github.com/openhistogram/libcircllhist)
 * [libcircmetrics](https://github.com/circonus-labs/libcircmetrics)
 * libcurl 7.49.0 or later (for `CURLOPT_CONNECT_TO` support)
 * [libmtev](https://github.com/circonus-labs/libmtev)
 * [LMDB](https://www.symas.com/lmdb)
 * [LuaJIT](https://luajit.org/luajit.html) version 2.1.
 * NetSNMP 5.7+
   * NetSNMP is required for the lua-snmp bindings that power OID lookups. The
     standard SNMP querying implementation is in Java and shipped with the
     repo.  A pure-C SNMP check implementation exists, but requires
     [patches](THIRDPARTY-LIBS.md#netsnmp) (producing libnetsnmp-c).
 * lz4
 * [JLog](https://github.com/omniti-labs/jlog)
 * [Picklingtools](http://www.picklingtools.com) ([Patch required](THIRDPARTY-LIBS.md#picklingtools))
 * PostgreSQL 8.4+
 * Protobuf 3.19+
 * Protobuf-C 1.4+
 * [snappy-c](https://github.com/andikleen/snappy-c.git)
 * [udns](https://www.corpit.ru/mjt/udns.html)
 * [wslay](https://github.com/tatsuhiro-t/wslay) for WebSockets support.
 * [yajl](https://github.com/lloyd/yajl)

Optional Libraries:
 * [Apache Portable Runtime](https://apr.apache.org) (libapr) is required
   if you want to build STOMP drivers.
 * [FQ](https://github.com/circonus-labs/fq) is required if you want FQ drivers.
 * [Java JDK](https://openjdk.org/projects/jdk/) to build the Jezebel sidecar
   for certain SQL database checks, as well as the standard SNMP check support.
 * SQLite 3 (for FQ)


## Platforms

### Linux (Ubuntu)

Tested on 20.04 LTS

Install dependencies that are available as packages:

    sudo apt-get install autoconf build-essential cmake \
      libapr1-dev libaprutil1-dev libcurl4-openssl-dev libhwloc-dev \
      liblz4-dev libncurses-dev libnghttp2-dev libpcre3-dev \
      libpq-dev librabbitmq-dev libsqlite3-dev libssl-dev libudns-dev \
      libwslay-dev libxslt1-dev libyajl-dev openjdk-8-jdk-headless pkg-config \
      uuid-dev xsltproc zlib1g-dev

Follow [build instructions for third-party libraries](THIRDPARTY-LIBS.md) that
are not available as packages.

    git clone https://github.com/circonus-labs/reconnoiter
    cd reconnoiter
    autoreconf -i
    CPPFLAGS="-I/usr/local/include/luajit-2.1" ./configure
    make
    sudo make install

### Linux (CentOS 7)

Install dependencies that are available as packages:

    sudo yum groupinstall "Development Tools"
    sudo yum --enablerepo=extras install centos-release-scl
    sudo yum install devtoolset-9
    sudo yum install autoconf apr-devel apr-util-devel cmake hwloc-devel \
      java-1.8.0-openjdk-devel libnghttp2-devel librabbitmq-devel \
      libtermcap-devel libuuid-devel libxslt-devel lz4-devel ncurses-devel \
      openssl openssl-devel pcre-devel pkgconfig postgresql-devel \
      sqlite-devel udns-devel yajl-devel zlib-devel
    scl enable devtoolset-9 bash

Follow [build instructions for third-party libraries](THIRDPARTY-LIBS.md) that
are not available as packages.

    git clone https://github.com/circonus-labs/reconnoiter
    cd reconnoiter
    autoreconf -i
    CPPFLAGS="-I/usr/local/include/luajit-2.1" ./configure
    make
    sudo make install
