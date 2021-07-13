#!/bin/sh

test_description='Tests performance using midx bitmaps'
. ./perf-lib.sh
. "${TEST_DIRECTORY}/perf/lib-bitmap.sh"

test_perf_large_repo

test_expect_success 'enable multi-pack index' '
	git config core.multiPackIndex true
'

test_perf 'setup multi-pack index' '
	git repack -ad &&
	git multi-pack-index write --bitmap
'

test_full_bitmap

test_expect_success 'create partial bitmap state' '
	# pick a commit to represent the repo tip in the past
	cutoff=$(git rev-list HEAD~100 -1) &&
	orig_tip=$(git rev-parse HEAD) &&

	# now pretend we have just one tip
	rm -rf .git/logs .git/refs/* .git/packed-refs &&
	git update-ref HEAD $cutoff &&

	# and then repack, which will leave us with a nice
	# big bitmap pack of the "old" history, and all of
	# the new history will be loose, as if it had been pushed
	# up incrementally and exploded via unpack-objects
	git repack -Ad &&
	git multi-pack-index write --bitmap &&

	# and now restore our original tip, as if the pushes
	# had happened
	git update-ref HEAD $orig_tip
'

test_partial_bitmap

test_done
