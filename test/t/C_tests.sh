#!/bin/bash

RV=0

run() {
	$*
	if [[ $? -ne 0 ]]; then
		RV=$?
	fi
}
run ../../src/modules/histogram_test

exit $RV
