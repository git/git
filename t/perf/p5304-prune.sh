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

test_expect_success 'repack with bitmaps' '
	git repack -adb
'

# We have to create the object in each trial run, since otherwise
# runs after the first see no object and just skip the traversal entirely!
test_perf 'prune with bitmaps' '
	echo "probably not present in repo" | git hash-object -w --stdin &&
	git prune
'

test_done
