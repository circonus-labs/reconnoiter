#!/bin/bash
set -o pipefail
cd "$(dirname $0)"

if [[ -z $CI_ENV_ITERATIONS ]]; then
    CI_ENV_ITERATIONS=1000
fi

PREVIOUS=""
TESTFILE=""
if [[ -z $TEST_LIST ]]; then
  if [[ -z $RUN_TEST_FILTER ]]; then
    TESTFILE="ALL TESTS"
  else
    TESTFILE="RUN TEST FILTER $RUN_TEST_FILTER"
  fi
else
  TESTFILE="TEST LIST $TEST_LIST"
fi
for i in "$@"; do
  if [ "$PREVIOUS" = "-file" ]; then
    TESTFILE=$i
  elif [ "$PREVIOUS" = "-exclude-tag" ]; then
    TESTFILE="EXCLUDE TAG $i"
  elif [ "$PREVIOUS" = "-tag" ]; then
    TESTFILE="TAG $i"
  elif [ "$PREVIOUS" = "-filter-out" ]; then
    TESTFILE="FILTER OUT $i"
  elif [ "$PREVIOUS" = "-filter" ]; then
    TESTFILE="FILTER $i"
  fi
  PREVIOUS=$i
done

COUNT=1
FAILCOUNT=0

rm run-stress.log

while [[ $COUNT -le $CI_ENV_ITERATIONS ]]; do
  echo "*****************"
  echo "  *****************"
  echo "    *****************"
  echo "      Iteration $COUNT of $CI_ENV_ITERATIONS..."
  if [[ $FAILCOUNT -gt 0 ]]; then
    echo "      $FAILCOUNT of $CI_ENV_ITERATIONS iterations failed."
  else
    echo "      All iterations passed so far."
  fi
  echo "    *****************"
  echo "  *****************"
  echo "*****************"
  ./run-tests.sh $@ | stdbuf -i 0 -o 0 -e 0 sed -e "s/^/$COUNT: /" | tee run-stress-iteration.log
  RESULT=$?
  if [ "$RESULT" = 130 ]; then
    echo "Aborted."
    exit $RESULT
  fi
  cat run-stress-iteration.log >> run-stress.log
  rm run-stress-iteration.log
  if [ "$RESULT" = 0 ]
  then
    echo "Iteration $COUNT passed tests."
  else
    echo "Failure encountered!"
    let FAILCOUNT=FAILCOUNT+1

    if [[ -z $CI_ENV_CONTINUE_ON_FAILURE ]]; then
        exit 1
    fi
  fi
  let COUNT=COUNT+1
done

echo

if [[ $FAILCOUNT -gt 0 ]]; then
    PERCENTBASE=$((100*$FAILCOUNT))
    PERCENTBASE=$(($PERCENTBASE/$CI_ENV_ITERATIONS))
    PERCENT=$((200*$FAILCOUNT))
    PERCENT=$(($PERCENT/$CI_ENV_ITERATIONS))
    PERCENT=$(($PERCENT%2 + $PERCENTBASE))
    echo "RUN STRESS: $TESTFILE - $PERCENT% failures - $FAILCOUNT of $CI_ENV_ITERATIONS iterations failed."
    exit $FAILCOUNT
fi

echo "RUN STRESS: $TESTFILE - No failures - $CI_ENV_ITERATIONS iterations passed."
exit 0
