#!/bin/bash

# Helper script for OS detection
# $1 must be calling script name
# $2 is optionally a buildtools subfolder, if not will try to auto-detect
# exit code is 0 for success, 1 for failure

# check if first arg is a valid OS subfolder
if [ ! -z "$2" ] && [ -d "buildtools/$2" ] ; then OS="$2" ; shift ; else
# else try to auto-identify OS
cat /etc/*release 2>/dev/null | grep "CentOS Linux 7" 1>&2
if [ "$?" == "0" ] ; then OS="el7" ; else
cat /etc/*release 2>/dev/null | grep "Ubuntu 20.04" 1>&2
if [ "$?" == "0" ] ; then OS="u2004" ; else
echo "Usage: $1 '<optional_OS>' '<optional_addl_make_switches>...'" 1>&2
exit 1
fi; fi; fi;

echo $OS
exit 0
