#!/bin/bash

# Helper script for OS detection
# $1 must be calling script name
# $2 is optionally a buildtools subfolder, if not will try to auto-detect
# exit code is 0 for success, 1 for failure

# check if first arg is a valid OS subfolder
if [[ -n "$2" ]] && [[ -d "buildtools/$2" ]] ; then
    OS="$2"
    shift
# else try to auto-identify OS
elif grep -qE '(Red Hat|CentOS).*7' /etc/redhat-release 2>/dev/null ; then
    OS="el7"
elif [[ -r /etc/lsb-release ]] ; then
    . /etc/lsb-release
    case $DISTRIB_RELEASE in
        "20.04") OS="u2004" ;;
        "22.04") OS="u2204" ;;
              *) exit 1 ;;
    esac
else
    echo "Usage: $1 '<optional_OS>' '<optional_addl_make_switches>...'" 1>&2
    exit 1
fi

echo $OS
exit 0
