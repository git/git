#!/bin/sh

test_description='Tests performance using midx bitmaps'
. ./perf-lib.sh
. "${TEST_DIRECTORY}/perf/lib-bitmap.sh"

test_bitmap () {
	local enabled="$1"

	test_expect_success "remove existing repo (lookup=$enabled)" '
		rm -fr * .git
	'

	test_perf_large_repo

	# we need to create the tag up front such that it is covered by the repack and
	# thus by generated bitmaps.
	test_expect_success 'create tags' '
		git tag --message="tag pointing to HEAD" perf-tag HEAD
	'

	test_expect_success "use lookup table: $enabled" '
		git config pack.writeBitmapLookupTable '"$enabled"'
	'

	test_expect_success "start with bitmapped pack (lookup=$enabled)" '
		git repack -adb
	'

	test_perf "setup multi-pack index (lookup=$enabled)" '
		git multi-pack-index write --bitmap
	'

	test_expect_success "drop pack bitmap (lookup=$enabled)" '
		rm -f .git/objects/pack/pack-*.bitmap
	'

	test_full_bitmap

	test_expect_success "create partial bitmap state (lookup=$enabled)" '
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
}

test_bitmap false
test_bitmap true

test_done
