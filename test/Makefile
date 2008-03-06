CC=gcc
CPPFLAGS=-I$(top_srcdir)/src
CFLAGS=-g -O2 -g -Wall -I/usr/local/include -I/usr/local/include/postgresql -I/usr/local/include/libxml2 -I/usr/local/include
LDFLAGS= -Wl,-E -L/usr/local/lib
AR=/usr/bin/ar
RANLIB=ranlib
LIBS=-lssl -lcrypto -lcurses -ltermcap -lutil -luuid -lpthread  -L/usr/local/lib -lxml2 -lz -L/usr/local/lib -liconv -lm
PGLIBS=-lssl -lcrypto -lcurses -ltermcap -lutil -luuid -lpthread  -lpq
INSTALL=/usr/bin/install -c

prefix=/usr/local
exec_prefix=${prefix}
bindir=${exec_prefix}/bin
sbindir=${exec_prefix}/sbin
libdir=${exec_prefix}/lib
includedir=${prefix}/include
libexecdir=${exec_prefix}/libexec
datarootdir = ${prefix}/share
mandir=${datarootdir}/man
mansubdir=man
docdir=${prefix}/docs
sysconfdir=${prefix}/etc
srcdir=.
top_srcdir=..

all:	testcerts
clean:	clean-keys
# This stuff if all cert stuff to make testing the daemons easier

demoCA-dir:
	@mkdir -p demoCA
	@touch demoCA/index.txt
	@test -f demoCA/serial || echo 00 > demoCA/serial

test-ca.key:
	openssl genrsa -out test-ca.key

test-ca.csr:	test-ca.key
	openssl req -key test-ca.key -days 365 -new -out test-ca.csr

test-ca.crt:	test-ca.key test-ca.csr
	openssl x509 -req -in test-ca.csr -signkey test-ca.key -out test-ca.crt

test.key:
	openssl genrsa -out test.key

test.csr:	test.key
	openssl req -key test.key -days 365 -new -out test.csr

test.crt:	test.csr test-ca.key test-ca.crt
	openssl ca -config demo-openssl.cnf -in test.csr -out test.crt -outdir . -keyfile test-ca.key -cert test-ca.crt -days 120

test-strat.key:
	openssl genrsa -out test-strat.key

test-strat.csr:	test-strat.key
	openssl req -key test-strat.key -days 365 -new -out test-strat.csr

test-strat.crt:	test-strat.csr test-ca.key
	openssl ca -config demo-openssl.cnf -in test-strat.csr -out test-strat.crt -outdir . -keyfile test-ca.key -cert test-ca.crt -days 120

testcerts:	demoCA-dir test.key test.crt test-strat.key test-strat.crt test-ca.key test-ca.crt

clean-keys:
	rm -f *.key *.csr *.crt *.pem
	rm -rf demoCA

