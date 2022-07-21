# Building Reconnoiter

## Requirements

Compiler:
 * Support for C17 and C++20 standards is required. GCC 11 or later, Clang/LLVM
   is less tested but recent versions should work.

### Third-party Libraries

[Important build notes](THIRDPARTY-LIBS.md)

Required Libraries:
 * [Concurrency Kit](https://github.com/concurrencykit/ck) (libck), version 0.7.1 or later
 * [flatcc](https://github.com/dvidelabs/flatcc), version 0.6.0.
 * libcurl 7.49.0 or later (for `CURLOPT_CONNECT_TO` support)
 * [libmtev](https://github.com/circonus-labs/libmtev)
 * [LMDB](https://www.symas.com/lmdb)
 * [LuaJIT](https://luajit.org/luajit.html) version 2.1.
 * NetSNMP 5.7+
   * NetSNMP is required for the lua-snmp bindings that power OID lookups. The
     standard SNMP querying implementation is in Java and shipped with the
     repo.  A pure-C SNMP check implementation exists, but requires
     [patches](THIRDPARTY-LIBS.md#netsnmp) (producing libnetsnmp-c).
 * [Picklingtools](http://www.picklingtools.com) ([Patch required](THIRDPARTY-LIBS.md#picklingtools))
 * PostgreSQL 8.4+
 * Protobuf 3+
 * Protobuf-C 1.4+
 * [snappy-c](https://github.com/andikleen/snappy-c.git)
 * [wslay](https://github.com/tatsuhiro-t/wslay) for WebSockets support.

Optional Libraries:
 * [Apache Portable Runtime](https://apr.apache.org) (libapr) is required
   if you want to build STOMP drivers.
 * [FQ](https://github.com/circonus-labs/fq) is required if you want FQ drivers.
 * [Java JDK](https://openjdk.org/projects/jdk/) to build the Jezebel sidecar
   for certain SQL database checks, as well as the standard SNMP check support.


## Platforms

### FreeBSD

    #!/bin/sh
    # portmaster -g /usr/ports/misc/e2fsprogs-libuuid
    # portmaster -g /usr/ports/devel/pcre
    # portmaster -g /usr/ports/devel/concurrencykit
    # portmaster -g /usr/ports/devel/hwloc
    # portmaster -g /usr/ports/databases/postgresql-libpqxx
    # portmaster -g /usr/ports/net-mgmt/net-snmp
    # portmaster -g /usr/ports/devel/re2c
    # portmaster -g /usr/ports/security/libssh2
    # portmaster -g /usr/ports/textproc/libxml2
    # portmaster -g /usr/ports/textproc/libxslt
    # portmaster -g /usr/ports/www/apache22
    # portmaster -g /usr/ports/lang/php5
    # portmaster -g /usr/ports/devel/protobuf-c
    # cd /usr/local/src
    # git clone https://github.com/circonus-labs/reconnoiter
    # cd reconnoiter
    # aclocal
    # autoreconf -i
    # ./configure LDFLAGS="-L/usr/local/lib"
    # make

### Linux (Debian)

    #!/bin/sh
    # apt-get install autoconf build-essential \
		zlib1g-dev uuid-dev libpcre3-dev libssl-dev libpq-dev \
		libxslt-dev libapr1-dev libaprutil1-dev xsltproc \
		libncurses5-dev python libssh2-1-dev libsnmp-dev \
		sun-java6-jdk libprotobuf-c0-dev hwloc-nox-dev libck0-dev
		# apt-get libdbi-perl libdbd-pg-perl libwww-curl-perl # if you want to run the tests
		# git clone https://github.com/circonus-labs/reconnoiter
		# cd reconnoiter
		# autoreconf -i
		# LDFLAGS="-ldl -lm" ./configure
		# make

### Linux (CentOS 7)

    #!/bin/sh
    # yum install autoconf subversion \
    	apr-devel apr-util-devel java-devel libssh2-devel libtermcap-devel \
    	libxslt-devel ncurses-devel net-snmp-devel openssl-devel \
    	pcre-devel postgresql-devel udns-devel uuid-devel zlib-devel \
    	libuuid-devel protobuf-c-devel hwloc-devel ck
    # git clone https://github.com/circonus-labs/reconnoiter
    # cd reconnoiter
    # autoreconf -i
    # ./configure
    # make
