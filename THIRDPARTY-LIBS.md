# Third-Party Libraries

Some Reconnoiter dependencies are not typically available with Linux/BSD
distributions, or the available version is too low. This document explains how
to get them installed. It assumes `/usr/local` as the prefix. If this does not
suit your environment, modify the following instuctions accordingly.

Depending on your platform, some of these may already have been installed. You
only need to build the ones that are not already packaged.

Build these in order from top to bottom. If patches are required, replace
`[reconnoiter source]` with the path to your git clone of Reconnoiter.


## ConcurrencyKit (libck)

```
git clone https://github.com/concurrencykit/ck.git
cd ck
patch -p1 < [reconnoiter source]/patches/ck-cpp_compat.patch
./configure
make
sudo make install
```

## nghttp2

Obtain source for version 1.39.0 or later from the [download
page](https://github.com/nghttp2/nghttp2/releases) and extract it.

```
./configure --prefix=/usr/local --enable-lib-only
make
sudo make install
```

## cURL

Obtain source for version 7.49.0 or later from the [download
page](https://curl.se/download.html) and extract it.

It is recommended to use OpenSSL for SSL/TLS support. Ensure that you have the
appropriate OpenSSL headers installed.

```
LDFLAGS="-Wl,-rpath=/usr/local/lib" ./configure \
  --with-openssl \
  --enable-thread \
  --enable-ares \
  --with-nghttp2=/usr/local
make
sudo make install
```

## lz4

Substitute desired version for `X.Y.Z` below.

```
git clone https://github.com/lz4/lz4.git
cd lz4
git checkout tags/vX.Y.Z
make
sudo make BINDIR=/usr/local/bin LIBDIR=/usr/local/lib install
```

## flatcc

Requires CMake.

```
git clone https://github.com/dvidelabs/flatcc.git
cd flatcc
git checkout tags/v0.6.0
patch -p1 < [reconnoiter source]/patches/flatcc.patch
./scripts/initbuild.sh make
mkdir -p build/install
cd build/install
cmake ../.. \
  -DBUILD_SHARED_LIBS=on \
  -DFLATCC_INSTALL=on \
  -DCMAKE_INSTALL_PREFIX:PATH=/usr/local \
  -DCMAKE_INSTALL_RPATH=/usr/local/lib
sudo make install
```


## Abseil

Obtain source for Abseil C++ version 20230125 or later. CMake is required to
build.

```
mkdir -p build/install
cd build/install
cmake ../.. \
    -DCMAKE_INSTALL_LIBDIR=/usr/local/lib \
    -DCMAKE_INSTALL_RPATH=/usr/local/lib \
    -DCMAKE_INSTALL_PREFIX:PATH=/usr/local \
    -DCMAKE_CXX_STANDARD=17 \
    -DABSL_PROPAGATE_CXX_STD=ON \
    -DABSL_ENABLE_INSTALL=ON \
    -DBUILD_SHARED_LIBS=ON
sudo make install
```


## Protobuf

Obtain source for version 3.23 or later. CMake is required to build.

```
mkdir -p build/install
cd build/install
cmake ../.. \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_INSTALL_PREFIX:PATH=/usr/local \
    -Dprotobuf_BUILD_TESTS=OFF \
    -Dprotobuf_ABSL_PROVIDER=package \
    -Dprotobuf_BUILD_SHARED_LIBS=ON
sudo make install
```


## Protobuf-C

Obtain source for 1.4.1 or later. The patch updates the version to "1.4.2"
which at this time has not been released. It appears that development has
stalled on this project, so this patch may become unnecessary if development
resumes in the future.

```
git clone https://github.com/protobuf-c/protobuf-c.git
cd protobuf-c
git checkout tags/v1.4.1
patch -p1 < [reconnoiter source]/patches/protobuf-c.patch
autoreconf -i
./configure
make
sudo make install
```


## RE2

Obtain source for version 2023-03-01 or later. CMake is required to build.

```
mkdir -p build/install
cd build/install
cmake ../.. \
    -DCMAKE_INSTALL_LIBDIR=/usr/local/lib \
    -DCMAKE_INSTALL_RPATH=/usr/local/lib \
    -DCMAKE_INSTALL_PREFIX:PATH=/usr/local \
    -DRE2_BUILD_TESTING=OFF \
    -DBUILD_SHARED_LIBS=ON
sudo make install
```


## gRPC

Obtain source for version 1.55 or later. CMake is required to build.

```
mkdir -p build/install
cd build/install
cmake ../.. \
    -DCMAKE_INSTALL_LIBDIR=/usr/local/lib \
    -DCMAKE_INSTALL_RPATH=/usr/local/lib \
    -DCMAKE_INSTALL_PREFIX:PATH=/usr/local \
    -DBUILD_SHARED_LIBS=ON \
    -DgRPC_INSTALL=ON \
    -DgRPC_BUILD_TESTS=OFF \
    -DgRPC_ABSL_PROVIDER=package \
    -DgRPC_CARES_PROVIDER=package \
    -DgRPC_PROTOBUF_PROVIDER=package \
    -DgRPC_RE2_PROVIDER=package \
    -DgRPC_SSL_PROVIDER=package \
    -DgRPC_ZLIB_PROVIDER=package
sudo make install
```


## LMDB

This is a subset of the OpenLDAP code.

```
git clone https://git.openldap.org/openldap/openldap.git
cd openldap/libraries/liblmdb
make prefix=/usr/local
sudo make install
```


## LuaJIT

Reconnoiter requires version 2.1.

```
git clone https://github.com/LuaJIT/LuaJIT.git
cd LuaJIT
git checkout v2.1
make CFLAGS="-D_REENTRANT -DLUAJIT_ENABLE_GC64" \
     LDFLAGS="-L/usr/local/lib -Wl,-rpath=/usr/local/lib" \
     BUILDMODE="dynamic" \
     TARGET_LIBS="-lgcc_s" \
     PREFIX="/usr/local"
sudo make install
sudo ln -sf luajit-2.1.0-beta3 /usr/local/bin/luajit
```


## JLog

```
git clone https://github.com/omniti-labs/jlog.git
cd jlog
autoconf
./configure
make
sudo make install
```


## FQ

This is an optional dependency for both Reconnoiter and libmtev.

```
git clone https://github.com/circonus-labs/fq.git
cd fq
make VENDOR_LDFLAGS="-L/usr/local/lib -Wl,-rpath=/usr/local/lib"
sudo make install
```


## NetSNMP

Obtain source for 5.8 or later from the [download
page](http://www.net-snmp.org/download.html) and extract it.

```
patch -p1 < [reconnoiter source]/patches/snmp-c.patch
./configure --prefix=/usr/local \
            --with-defaults \
            --with-default-snmp-version=3 \
            --enable-agentx-dom-sock-only \
            --enable-ipv6 \
            --enable-mfd-rewrites \
            --disable-embedded-perl \
            --without-perl-modules \
            --disable-static
make snmplib
sudo make installheaders
sudo make -C snmplib install
sudo make -C mibs mibsinstall
```


## PicklingTools

Obtain source for 1.7.0 or later from the [download
page](http://www.picklingtools.com/downloads) and extract it.

One only needs the C++ library.

```
patch -p1 < [reconnoiter source]/patches/picklingtools-Makefile.Linux.factored.patch
patch -p1 < [reconnoiter source]/patches/picklingtools-warnings.patch
cd C++
make -f Makefile.Linux.factored libptools.so
sudo cp libptools.so /usr/local/lib/
sudo cp *.h opencontainers_1_8_5/include/*.h /usr/local/include/
```

## protobuf

Reconnoiter requires version 3.19 or later.

```
git clone https://github.com/protocolbuffers/protobuf.git
cd protobuf
git checkout tags/v3.19.6
autoreconf -i
LDFLAGS="-L/usr/local/lib -Wl,-rpath=/usr/local/lib" \
  ./configure --disable-static
make
sudo make install
```


## protobuf-c

Reconnoiter requires version 1.4.0 or later.

```
git clone https://github.com/protobuf-c/protobuf-c.git
cd protobuf-c
git checkout tags/v1.4.0
autoreconf -i
PKG_CONFIG_PATH=/usr/local/lib/pkgconfig \
  LDFLAGS="-L/usr/local/lib -Wl,-rpath=/usr/local/lib" \
  ./configure --disable-static
make
sudo make install
```


## snappy-c

```
git clone https://github.com/andikleen/snappy-c.git
cd snappy-c
make
sudo mkdir /usr/local/include/snappy
sudo cp *.h /usr/local/include/snappy/
sudo cp scmd /usr/local/bin/
sudo cp libsnappyc.so.1 /usr/local/lib/
sudo ln -s libsnappyc.so.1 /usr/local/lib/libsnappyc.so
```

## libcircllhist

```
git clone https://github.com/openhistogram/libcircllhist.git
cd libcircllhist
autoconf
./configure
make
sudo make install
```

## libcircmetrics

```
git clone https://github.com/circonus-labs/libcircmetrics.git
cd libcircmetrics
autoconf
./configure
make
sudo make install
```


## wslay

Obtain source for 1.0.0 or later from the [download
page](https://github.com/tatsuhiro-t/wslay/releases) and extract it.

```
autoreconf -i
./configure --disable-static
make -C lib
sudo make -C lib install
```

## udns

Obtain source for version 0.4 from https://www.corpit.ru/mjt/udns/udns-0.4.tar.gz
and extract it.

```
patch -p1 < [reconnoiter source]/patches/udns.patch
./configure
make
make shared
sudo make install
```

## libmtev

Reconnoiter shares many dependencies with libmtev so you should finish
installing the libraries on this page before proceeding with the
[libmtev build instructions](https://github.com/circonus-labs/libmtev/blob/master/BUILDING.md).

```
git clone https://github.com/circonus-labs/libmtev
cd libmtev
autoreconf -i -I buildtools
CPPFLAGS="-I/usr/local/include/luajit-2.1" \
  CFLAGS="-Wno-uninitialized -Wno-misleading-indentation -Wno-free-nonheap-object" \
  ./configure
make
sudo make install
```
