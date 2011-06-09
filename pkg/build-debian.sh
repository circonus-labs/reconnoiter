#!/bin/sh

set -e

ln -sf pkg/debian debian

HEADFILE="pkg/HEAD"
HEAD=`git log -1 HEAD --pretty=%h`
[ -f $HEADFILE ] && OLDHEAD=`cat $HEADFILE`

if [ "x$HEAD" != "x$OLDHEAD" ]; then
	dch -v `date +"%Y%m%d%H%M%S"` "HEAD `git log -1 HEAD --abbrev-commit --pretty=oneline`"
	echo $HEAD > $HEADFILE
fi

dpkg-buildpackage -rfakeroot -tc

rm debian
