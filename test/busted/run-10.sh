#!/bin/bash

find . -name *_spec.lua | grep "$RUN_TEST_FILTER" > test_list
echo -e "PERCENT\tTESTFILE PATH" > results.log
if [ -z "$CI_ENV_ITERATIONS" ]; then
  CI_ENV_ITERATIONS=10
fi
export CI_ENV_ITERATIONS
export CI_ENV_CONTINUE_ON_FAILURE=1
while read sourcefile; do
  ./run-stress.sh -file $sourcefile
  FAILCOUNT=$?
  if [ "$FAILCOUNT" = "130" ]; then
    exit $FAILCOUNT
  fi
  PERCENTBASE=$((100*($CI_ENV_ITERATIONS - $FAILCOUNT)))
  PERCENTBASE=$(($PERCENTBASE/$CI_ENV_ITERATIONS))
  PERCENT=$((200*($CI_ENV_ITERATIONS - $FAILCOUNT)))
  PERCENT=$(($PERCENT/$CI_ENV_ITERATIONS))
  PERCENT=$(($PERCENT%2 + $PERCENTBASE))
  echo -e "${PERCENT}% \t${sourcefile}" >> results.log
done < test_list
echo "LIST OF TESTS WITH FAILURES"
cat results.log | grep -v "PERCENT" | grep -v "100%"
echo "END OF LIST"
NUM_TESTFILES=`cat test_list | wc -l`
echo "${NUM_TESTFILES} were run."
