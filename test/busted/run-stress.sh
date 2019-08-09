#!/bin/bash
set -o pipefail
cd "$(dirname $0)"

if [[ -z $CI_ENV_ITERATIONS ]]; then
    CI_ENV_ITERATIONS=1000
fi

COUNT=1
FAILCOUNT=0

rm run-stress.log

while [[ $COUNT -le $CI_ENV_ITERATIONS ]]; do
  echo "*****************"
  echo "  *****************"
  echo "    *****************"
  echo "      Iteration $COUNT of $CI_ENV_ITERATIONS..."
  echo "    *****************"
  echo "  *****************"
  echo "*****************"
  ./run-tests.sh $@ | stdbuf -i 0 -o 0 -e 0 sed -e "s/^/$COUNT: /" | tee run-stress-iteration.log
  RESULT=$?
  cat run-stress-iteration.log >> run-stress.log
  rm run-stress-iteration.log
  if [ "$RESULT" = 0 ]
  then
    echo "Iteration passed tests."
  else
    echo "Failure encountered!"
    let FAILCOUNT=FAILCOUNT+1

    if [[ -z $CI_ENV_CONTINUE_ON_FAILURE ]]; then
        exit 1
    fi
  fi
  let COUNT=COUNT+1
done

if [[ $FAILCOUNT -gt 0 ]]; then
    echo "$FAILCOUNT of $CI_ENV_ITERATIONS iterations failed."
    exit 1
else
    echo "$CI_ENV_ITERATIONS iterations passed."
fi
