#Building Reconnoiter

## Requirements

 * PostgreSQL 8.4+ is required.
 * NetSNMP 5.7+
 	
 	NetSNMP is required for the lua-snmp bindings that power OID lookups.  The standard SNMP querying implementation is in Java and shipped with the repo.  There pure-C SNMP check requires patches to NetSNMP (producing libnetsnmp-c) and patches are provided in the reconnoiter tree for the bold
 * Fq is required if you want FQ drivers
 * apr is required if you want to build STOMP drivers.
 * mysql libraries for pure-C mysql client (Java recommended)
 * libssh2 is required for the ssh2 check.

## Platforms

### FreeBSD

[Original Message](https://labs.omniti.com/lists/reconnoiter-users/2009-March/000028.html)


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
    # autoconf
    # ./configure LDFLAGS="-L/usr/local/lib"
    # make

### Linux (Debian)

[Original Message](https://labs.omniti.com/lists/reconnoiter-users/2009-March/000027.html)

    #!/bin/sh
    # apt-get install autoconf build-essential \
		zlib1g-dev uuid-dev libpcre3-dev libssl-dev libpq-dev \
		libxslt-dev libapr1-dev libaprutil1-dev xsltproc \
		libncurses5-dev python libssh2-1-dev libsnmp-dev \
		sun-java6-jdk libprotobuf-c0-dev hwloc-nox-dev libck0-dev
		# apt-get libdbi-perl libdbd-pg-perl libwww-curl-perl # if you want to run the tests
		# git clone https://github.com/circonus-labs/reconnoiter
		# cd reconnoiter
		# autoconf
		# LDFLAGS="-ldl -lm" ./configure
		# make

### Linux (CentOS 6.3)

[Original Message](https://labs.omniti.com/lists/reconnoiter-users/2009-September/000184.html)

    #!/bin/sh
    # yum install autoconf subversion \
    	apr-devel apr-util-devel java-devel libssh2-devel libtermcap-devel \
    	libxslt-devel ncurses-devel net-snmp-devel openldap-devel openssl-devel \
    	pcre-devel postgresql-devel udns-devel uuid-devel zlib-devel \
    	libuuid-devel protobuf-c-devel hwloc-devel ck
    # git clone https://github.com/circonus-labs/reconnoiter
    # cd reconnoiter
    # autoconf
    # ./configure
    # make

### OmniOS

	# pkg set-publisher -g http://pkg.omniti.com/omniti-ms/ ms.omniti.com
	# pkg install developer/git developer/build/autoconf system/header \
		developer/gcc48 omniti/library/protobuf-c omniti/library/libpq5 \
		developer/build/gnu-make omniti/library/apr omniti/library/libssh2 \
    omniti/library/hwloc omniti/library/ck
	# git clone git@github.com:circonus-labs/reconnoiter.git
	# cd reconnoiter
	# autoconf
	# ./configure LDFLAGS="-L/opt/omni/lib/" CPPFLAGS="-I/opt/omni/include"
	# make
