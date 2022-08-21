#!/bin/sh

test_description="Tests performance of update-ref"

. ./perf-lib.sh

test_perf_fresh_repo

test_expect_success "setup" '
	test_commit PRE &&
	test_commit POST &&
	for i in $(test_seq 5000)
	do
		printf "start\ncreate refs/heads/%d PRE\ncommit\n" $i &&
		printf "start\nupdate refs/heads/%d POST PRE\ncommit\n" $i &&
		printf "start\ndelete refs/heads/%d POST\ncommit\n" $i || return 1
	done >instructions
'

test_perf "update-ref" '
	for i in $(test_seq 1000)
	do
		git update-ref refs/heads/branch PRE &&
		git update-ref refs/heads/branch POST PRE &&
		git update-ref -d refs/heads/branch || return 1
	done
'

test_perf "update-ref --stdin" '
	git update-ref --stdin <instructions >/dev/null
'

test_done
