#!/bin/sh

test_description="Tests performance of update-ref"

. ./perf-lib.sh

test_perf_fresh_repo

test_expect_success "setup" '
	test_cummit PRE &&
	test_cummit POST &&
	for i in $(test_seq 5000)
	do
		printf "start\ncreate refs/heads/%d PRE\ncummit\n" $i &&
		printf "start\nupdate refs/heads/%d POST PRE\ncummit\n" $i &&
		printf "start\ndelete refs/heads/%d POST\ncummit\n" $i || return 1
	done >instructions
'

test_perf "update-ref" '
	for i in $(test_seq 1000)
	do
		but update-ref refs/heads/branch PRE &&
		but update-ref refs/heads/branch POST PRE &&
		but update-ref -d refs/heads/branch || return 1
	done
'

test_perf "update-ref --stdin" '
	but update-ref --stdin <instructions >/dev/null
'

test_done
