#!/bin/sh

test_description='Tests pack performance using bitmaps'
. ./perf-lib.sh
. "${TEST_DIRECTORY}/perf/lib-bitmap.sh"

test_perf_large_repo

# note that we do everything through config,
# since we want to be able to compare bitmap-aware
# but versus non-bitmap but
#
# We intentionally use the deprecated pack.writebitmaps
# config so that we can test against older versions of but.
test_expect_success 'setup bitmap config' '
	but config pack.writebitmaps true
'

# we need to create the tag up front such that it is covered by the repack and
# thus by generated bitmaps.
test_expect_success 'create tags' '
	but tag --message="tag pointing to HEAD" perf-tag HEAD
'

test_perf 'repack to disk' '
	but repack -ad
'

test_full_bitmap

test_expect_success 'create partial bitmap state' '
	# pick a cummit to represent the repo tip in the past
	cutoff=$(but rev-list HEAD~100 -1) &&
	orig_tip=$(but rev-parse HEAD) &&

	# now kill off all of the refs and pretend we had
	# just the one tip
	rm -rf .but/logs .but/refs/* .but/packed-refs &&
	but update-ref HEAD $cutoff &&

	# and then repack, which will leave us with a nice
	# big bitmap pack of the "old" history, and all of
	# the new history will be loose, as if it had been pushed
	# up incrementally and exploded via unpack-objects
	but repack -Ad &&

	# and now restore our original tip, as if the pushes
	# had happened
	but update-ref HEAD $orig_tip
'

test_partial_bitmap

test_done
