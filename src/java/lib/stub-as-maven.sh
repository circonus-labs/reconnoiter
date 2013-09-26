#!/bin/bash

for i in *.jar
do
  FILE=`echo $i | sed -e 's/.jar$//;'`
  ARTIFACT=`echo $FILE | sed -e 's/-[0-9].*//;'`
  VERSION=`echo ${FILE:${#ARTIFACT}} | sed -e 's/^-//;'`
  FVER=$VERSION
  if [ -z "$FVER" ]; then FVER=0.1; fi
  FVER=`echo $FVER | sed -e 's/-.*//;'`

  mkdir -p $ARTIFACT/$ARTIFACT/$FVER
  ln -sf ../../../$i $ARTIFACT/$ARTIFACT/$FVER/$ARTIFACT-$FVER.jar
done
