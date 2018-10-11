#!/bin/sh

# This test can give false success if your machine is sufficiently
# slow or all trials happened to happen on second boundaries.

test_description='racy split index'

. ./test-lib.sh

test_expect_success 'setup' '
	# Only split the index when the test explicitly says so.
	sane_unset GIT_TEST_SPLIT_INDEX &&
	git config splitIndex.maxPercentChange 100 &&

	echo "cached content" >racy-file &&
	git add racy-file &&
	git commit -m initial &&

	echo something >other-file &&
	# No raciness with this file.
	test-tool chmtime =-20 other-file &&

	echo "+cached content" >expect
'

check_cached_diff () {
	git diff-index --patch --cached $EMPTY_TREE racy-file >diff &&
	tail -1 diff >actual &&
	test_cmp expect actual
}

trials="0 1 2 3 4"
for trial in $trials
do
	test_expect_success "split the index while adding a racily clean file #$trial" '
		rm -f .git/index .git/sharedindex.* &&

		# The next three commands must be run within the same
		# second (so both writes to racy-file result in the same
		# mtime) to create the interesting racy situation.
		echo "cached content" >racy-file &&

		# Update and split the index.  The cache entry of
		# racy-file will be stored only in the shared index.
		git update-index --split-index --add racy-file &&

		# File size must stay the same.
		echo "dirty worktree" >racy-file &&

		# Subsequent git commands should notice that racy-file
		# and the split index have the same mtime, and check
		# the content of the file to see if it is actually
		# clean.
		check_cached_diff
	'
done

for trial in $trials
do
	test_expect_success "add a racily clean file to an already split index #$trial" '
		rm -f .git/index .git/sharedindex.* &&

		git update-index --split-index &&

		# The next three commands must be run within the same
		# second.
		echo "cached content" >racy-file &&

		# Update the split index.  The cache entry of racy-file
		# will be stored only in the split index.
		git update-index --add racy-file &&

		# File size must stay the same.
		echo "dirty worktree" >racy-file &&

		# Subsequent git commands should notice that racy-file
		# and the split index have the same mtime, and check
		# the content of the file to see if it is actually
		# clean.
		check_cached_diff
	'
done

for trial in $trials
do
	test_expect_success "split the index when the index contains a racily clean cache entry #$trial" '
		rm -f .git/index .git/sharedindex.* &&

		# The next three commands must be run within the same
		# second.
		echo "cached content" >racy-file &&

		git update-index --add racy-file &&

		# File size must stay the same.
		echo "dirty worktree" >racy-file &&

		# Now wait a bit to ensure that the split index written
		# below will get a more recent mtime than racy-file.
		sleep 1 &&

		# Update and split the index when the index contains
		# the racily clean cache entry of racy-file.
		# A corresponding replacement cache entry with smudged
		# stat data should be added to the new split index.
		git update-index --split-index --add other-file &&

		# Subsequent git commands should notice the smudged
		# stat data in the replacement cache entry and that it
		# doesnt match with the file the worktree.
		check_cached_diff
	'
done

for trial in $trials
do
	test_expect_success "update the split index when it contains a new racily clean cache entry #$trial" '
		rm -f .git/index .git/sharedindex.* &&

		git update-index --split-index &&

		# The next three commands must be run within the same
		# second.
		echo "cached content" >racy-file &&

		# Update the split index.  The cache entry of racy-file
		# will be stored only in the split index.
		git update-index --add racy-file &&

		# File size must stay the same.
		echo "dirty worktree" >racy-file &&

		# Now wait a bit to ensure that the split index written
		# below will get a more recent mtime than racy-file.
		sleep 1 &&

		# Update the split index when the racily clean cache
		# entry of racy-file is only stored in the split index.
		# An updated cache entry with smudged stat data should
		# be added to the new split index.
		git update-index --add other-file &&

		# Subsequent git commands should notice the smudged
		# stat data.
		check_cached_diff
	'
done

for trial in $trials
do
	test_expect_success "update the split index when a racily clean cache entry is stored only in the shared index #$trial" '
		rm -f .git/index .git/sharedindex.* &&

		# The next three commands must be run within the same
		# second.
		echo "cached content" >racy-file &&

		# Update and split the index.  The cache entry of
		# racy-file will be stored only in the shared index.
		git update-index --split-index --add racy-file &&

		# File size must stay the same.
		echo "dirty worktree" >racy-file &&

		# Now wait a bit to ensure that the split index written
		# below will get a more recent mtime than racy-file.
		sleep 1 &&

		# Update the split index when the racily clean cache
		# entry of racy-file is only stored in the shared index.
		# A corresponding replacement cache entry with smudged
		# stat data should be added to the new split index.
		git update-index --add other-file &&

		# Subsequent git commands should notice the smudged
		# stat data.
		check_cached_diff
	'
done

for trial in $trials
do
	test_expect_success "update the split index after unpack trees() copied a racily clean cache entry from the shared index #$trial" '
		rm -f .git/index .git/sharedindex.* &&

		# The next three commands must be run within the same
		# second.
		echo "cached content" >racy-file &&

		# Update and split the index.  The cache entry of
		# racy-file will be stored only in the shared index.
		git update-index --split-index --add racy-file &&

		# File size must stay the same.
		echo "dirty worktree" >racy-file &&

		# Now wait a bit to ensure that the split index written
		# below will get a more recent mtime than racy-file.
		sleep 1 &&

		# Update the split index after unpack_trees() copied the
		# racily clean cache entry of racy-file from the shared
		# index.  A corresponding replacement cache entry
		# with smudged stat data should be added to the new
		# split index.
		git read-tree -m HEAD &&

		# Subsequent git commands should notice the smudged
		# stat data.
		check_cached_diff
	'
done

test_done
