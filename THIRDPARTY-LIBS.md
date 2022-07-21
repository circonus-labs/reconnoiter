# Third-Party Libraries

Some Reconnoiter dependencies are not typically available with Linux/BSD
distributions. This document explains how to get them installed. It assumes
`/usr/local` as the prefix. If this does not suit your environment, modify the
following instuctions accordingly.

The following are listed in alphabetical order. There is no particular order of
installation.

## cURL

Obtain source for version 7.49.0 or later from the [download
page](https://curl.se/download.html) and extract it.

The only required option is which SSL/TLS library to build against. OpenSSL is
the most common, and recommended. Ensure that you have the appropriate OpenSSL
headers installed.

```
./configure --prefix=/usr/local --with-openssl
make
sudo make install
```


## flatcc

Requires CMake.

```
git clone https://github.com/dvidelabs/flatcc.git
cd flatcc
git checkout tags/v0.6.0
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
export CFLAGS="-D_REENTRANT -DLUAJIT_ENABLE_GC64"
make BUILDMODE="dynamic" TARGET_LIBS="-lgcc_s" PREFIX="/usr/local"
sudo make install
sudo ln -sf luajit-2.1.0-beta3 /usr/local/bin/luajit
```


## NetSNMP

Obtain source for 5.7.1 or later from the [download
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
