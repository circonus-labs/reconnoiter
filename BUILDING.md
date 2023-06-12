# Building Reconnoiter

## Requirements

Compiler:
 * Support for C11 and C++20 standards is required. This means GCC 11 or later;
   Clang/LLVM is less tested but recent versions should work.

### Third-party Libraries

[Important build notes](THIRDPARTY-LIBS.md)

Required Libraries:
 * [Abseil C++](https://abseil.io/) common libraries, version 20230125 or
   later, for Protobuf and gRPC.
 * [c-ares](https://c-ares.org/) version 1.10.0+, for asynchronous DNS
   resolution in libcurl and gRPC.
 * [Concurrency Kit](https://github.com/concurrencykit/ck) (libck), version 0.7.1 or later ([Patch required](THIRDPARTY-LIBS.md#concurrencykit-libck))
 * [flatcc](https://github.com/dvidelabs/flatcc), version 0.6.0. ([Patch required](THIRDPARTY-LIBS.md#flatcc))
 * [gRPC](https://github.com/grpc/grpc), version 1.55 or later, for the
   OpenTelemetry module.
 * [libcircllhist](https://github.com/openhistogram/libcircllhist)
 * [libcircmetrics](https://github.com/circonus-labs/libcircmetrics)
 * libcurl 7.49.0 or later (for `CURLOPT_CONNECT_TO` support)
 * [libmtev](https://github.com/circonus-labs/libmtev)
 * [LMDB](https://www.symas.com/lmdb)
 * [LuaJIT](https://luajit.org/luajit.html) version 2.1.
 * NetSNMP 5.8+
   * NetSNMP is required for the lua-snmp bindings that power OID lookups. The
     standard SNMP querying implementation is in Java and shipped with the
     repo.  A pure-C SNMP check implementation exists, but requires
     [patches](THIRDPARTY-LIBS.md#netsnmp) (producing libnetsnmp-c).
 * [nghttp2](https://nghttp2.org/) 1.39.0+, for HTTP/2 support in libcurl.
 * [lz4](https://github.com/lz4/lz4)
 * [JLog](https://github.com/omniti-labs/jlog)
 * [Picklingtools](http://www.picklingtools.com) ([Patch required](THIRDPARTY-LIBS.md#picklingtools))
 * PostgreSQL 8.4+
 * Protobuf 3.23+
 * Protobuf-C 1.4+ ([Patch required](THIRDPARTY-LIBS.md#protobuf-c))
 * [RE2](https://github.com/google/re2), version 2023-03-01 or later, for gRPC.
 * [snappy-c](https://github.com/andikleen/snappy-c.git)
 * [udns](https://www.corpit.ru/mjt/udns.html) version 0.4
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

    sudo add-apt-repository ppa:ubuntu-toolchain-r/test
    sudo apt-get update && sudo apt-get install gcc-11 g++-11

    sudo apt-get install autoconf build-essential cmake \
      libapr1-dev libaprutil1-dev libcurl4-openssl-dev libhwloc-dev \
      liblmdb-dev liblz4-dev libncurses-dev libnghttp2-dev libpcre3-dev \
      libpq-dev librabbitmq-dev libsqlite3-dev libssl-dev libudns-dev \
      libwslay-dev libxslt1-dev libyajl-dev openjdk-8-jdk-headless pkg-config \
      uuid-dev xsltproc zlib1g-dev

Ensure the necessary compiler version is used:

    export CC=gcc-11
    export CXX=g++-11

Follow [build instructions for third-party libraries](THIRDPARTY-LIBS.md) that
are not available as packages. Then come back here and continue below.

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
    sudo yum install devtoolset-11
    sudo yum install autoconf apr-devel apr-util-devel c-ares-devel cmake \
      hwloc-devel java-1.8.0-openjdk-devel libtermcap-devel libuuid-devel \
      libxslt-devel ncurses-devel openssl openssl-devel pcre-devel \
      pkgconfig postgresql-devel sqlite-devel udns-devel yajl-devel \
      zlib-devel
    scl enable devtoolset-11 bash

Follow [build instructions for third-party libraries](THIRDPARTY-LIBS.md) that
are not available as packages. Then come back here and continue below.

    git clone https://github.com/circonus-labs/reconnoiter
    cd reconnoiter
    autoreconf -i
    CPPFLAGS="-I/usr/local/include/luajit-2.1" ./configure
    make
    sudo make install
