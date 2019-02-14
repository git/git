#!/bin/sh

test_description='performance tests of prune'
. ./perf-lib.sh

test_perf_default_repo

test_expect_success 'remove reachable loose objects' '
	git repack -ad
'

test_expect_success 'remove unreachable loose objects' '
	git prune
'

test_expect_success 'confirm there are no loose objects' '
	git count-objects | grep ^0
'

test_perf 'prune with no objects' '
	git prune
'

test_done
