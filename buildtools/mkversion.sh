#!/bin/sh

STATUS=`git status 2>&1`
if [ $? -eq 0 ]; then
  echo "Building version info from git"
  HASH=`git show --format=%H | head -1`
  TSTAMP=`git show --format=%at | head -1`
  echo "    * version -> $HASH"
  SYM=`git symbolic-ref -q HEAD | awk -F'/' '{ print $NF }'`
  if [ -z "$SYM" ]; then
    SYM="detached"
  elif [ -z "`echo $SYM | grep '^tags/'`" ]; then
    SYM="branches/$SYM"
  fi
  echo "    * symbolic -> $SYM"
  BRANCH=$SYM
  VSTR=`printf "$BRANCH" | sed -e 's#^tags/##;' | sed -e 's#^branches/##;'`
  VERSION="$HASH.$TSTAMP"
  if [ -n "`echo $STATUS | grep 'Changed but not updated'`" ]; then
    VERSION="$HASH.modified.$TSTAMP"
  fi
else
  BRANCH=exported
  echo "    * exported"
fi

if [ -r "$1" ]; then
  eval `cat noit_version.h | awk '/^#define/ { print $2"="$3;}'`
  if [ "$NOIT_BRANCH" = "$BRANCH" -a "$NOIT_VERSION" = "$VERSION" ]; then
    echo "    * version unchanged"
    exit
  fi
fi

cat > $1 <<EOF
#ifndef NOIT_VERSION_H
#ifndef NOIT_BRANCH
#define NOIT_BRANCH "$BRANCH"
#endif
#ifndef NOIT_VERSION
#define NOIT_VERSION "$VERSION"
#endif

#if defined(NOIT_VERSION_IMPL)
const char *noit_branch = "$BRANCH";
const char *noit_git_hash = "$HASH";
const char *noit_version = "$VSTR";
#elif defined(NOIT_VERSION_DECL)
extern const char *noit_branch;
extern const char *noit_git_hash;
extern const char *noit_version;
#endif

#include <stdio.h>
#include <string.h>
#include <mtev_str.h>

static inline int noit_build_version(char *buff, int len) {
  mtev_strlcpy(buff, "$VSTR.$VERSION", len);
  return strlen(buff);
}

#endif
EOF
