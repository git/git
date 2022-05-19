#!/bin/sh

test_description='performance tests of prune'
. ./perf-lib.sh

test_perf_default_repo

test_expect_success 'remove reachable loose objects' '
	but repack -ad
'

test_expect_success 'remove unreachable loose objects' '
	but prune
'

test_expect_success 'confirm there are no loose objects' '
	but count-objects | grep ^0
'

test_perf 'prune with no objects' '
	but prune
'

test_expect_success 'repack with bitmaps' '
	but repack -adb
'

# We have to create the object in each trial run, since otherwise
# runs after the first see no object and just skip the traversal entirely!
test_perf 'prune with bitmaps' '
	echo "probably not present in repo" | but hash-object -w --stdin &&
	but prune
'

test_done
