#!/bin/bash

RV=0

run() {
	$*
	if [[ $? -ne 0 ]]; then
		RV=$?
	fi
}
export LD_LIBRARY_PATH=../../src
run ../test_tags
exit $RV
