#!/bin/sh

test_description="path collisions during parallel checkout

Parallel checkout must detect path collisions to:

1) Avoid racily writing to different paths that represent the same file on disk.
2) Report the colliding entries on clone.

The tests in this file exercise parallel checkout's collision detection code in
both these mechanics.
"

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-parallel-checkout.sh"

TEST_ROOT="$PWD"

test_expect_success CASE_INSENSITIVE_FS 'setup' '
	empty_oid=$(git hash-object -w --stdin </dev/null) &&
	cat >objs <<-EOF &&
	100644 $empty_oid	FILE_X
	100644 $empty_oid	FILE_x
	100644 $empty_oid	file_X
	100644 $empty_oid	file_x
	EOF
	git update-index --index-info <objs &&
	git commit -m "colliding files" &&
	git tag basename_collision &&

	write_script "$TEST_ROOT"/logger_script <<-\EOF
	echo "$@" >>filter.log
	EOF
'

test_workers_in_event_trace ()
{
	test $1 -eq $(grep ".event.:.child_start..*checkout--worker" $2 | wc -l)
}

test_expect_success CASE_INSENSITIVE_FS 'worker detects basename collision' '
	GIT_TRACE2_EVENT="$(pwd)/trace" git \
		-c checkout.workers=2 -c checkout.thresholdForParallelism=0 \
		checkout . &&

	test_workers_in_event_trace 2 trace &&
	collisions=$(grep -i "category.:.pcheckout.,.key.:.collision/basename.,.value.:.file_x.}" trace | wc -l) &&
	test $collisions -eq 3
'

test_expect_success CASE_INSENSITIVE_FS 'worker detects dirname collision' '
	test_config filter.logger.smudge "\"$TEST_ROOT/logger_script\" %f" &&
	empty_oid=$(git hash-object -w --stdin </dev/null) &&

	# By setting a filter command to "a", we make it ineligible for parallel
	# checkout, and thus it is checked out *first*. This way we can ensure
	# that "A/B" and "A/C" will both collide with the regular file "a".
	#
	attr_oid=$(echo "a filter=logger" | git hash-object -w --stdin) &&

	cat >objs <<-EOF &&
	100644 $empty_oid	A/B
	100644 $empty_oid	A/C
	100644 $empty_oid	a
	100644 $attr_oid	.gitattributes
	EOF
	git rm -rf . &&
	git update-index --index-info <objs &&

	rm -f trace filter.log &&
	GIT_TRACE2_EVENT="$(pwd)/trace" git \
		-c checkout.workers=2 -c checkout.thresholdForParallelism=0 \
		checkout . &&

	# Check that "a" (and only "a") was filtered
	echo a >expected.log &&
	test_cmp filter.log expected.log &&

	# Check that it used the right number of workers and detected the collisions
	test_workers_in_event_trace 2 trace &&
	grep "category.:.pcheckout.,.key.:.collision/dirname.,.value.:.A/B.}" trace &&
	grep "category.:.pcheckout.,.key.:.collision/dirname.,.value.:.A/C.}" trace
'

test_expect_success SYMLINKS,CASE_INSENSITIVE_FS 'do not follow symlinks colliding with leading dir' '
	empty_oid=$(git hash-object -w --stdin </dev/null) &&
	symlink_oid=$(echo "./e" | git hash-object -w --stdin) &&
	mkdir e &&

	cat >objs <<-EOF &&
	120000 $symlink_oid	D
	100644 $empty_oid	d/x
	100644 $empty_oid	e/y
	EOF
	git rm -rf . &&
	git update-index --index-info <objs &&

	set_checkout_config 2 0 &&
	test_checkout_workers 2 git checkout . &&
	test_path_is_dir e &&
	test_path_is_missing e/x
'

# The two following tests check that parallel checkout correctly reports
# colliding entries on clone. The sequential code detects a collision by
# calling lstat() before trying to open(O_CREAT) a file. (Note that this only
# works for clone.) Then, to find the pair of a colliding item k, it searches
# cache_entry[0, k-1]. This is not sufficient in parallel checkout because:
#
# - A colliding file may be created between the lstat() and open() calls;
# - A colliding entry might appear in the second half of the cache_entry array.
#
test_expect_success CASE_INSENSITIVE_FS 'collision report on clone (w/ racy file creation)' '
	git reset --hard basename_collision &&
	set_checkout_config 2 0 &&
	test_checkout_workers 2 git clone . clone-repo 2>stderr &&

	grep FILE_X stderr &&
	grep FILE_x stderr &&
	grep file_X stderr &&
	grep file_x stderr &&
	grep "the following paths have collided" stderr
'

# This test ensures that the collision report code is correctly looking for
# colliding peers in the second half of the cache_entry array. This is done by
# defining a smudge command for the *last* array entry, which makes it
# non-eligible for parallel-checkout. Thus, it is checked out *first*, before
# spawning the workers.
#
# Note: this test doesn't work on Windows because, on this system, the
# collision report code uses strcmp() to find the colliding pairs when
# core.ignoreCase is false. And we need this setting for this test so that only
# 'file_x' matches the pattern of the filter attribute. But the test works on
# OSX, where the colliding pairs are found using inode.
#
test_expect_success CASE_INSENSITIVE_FS,!MINGW,!CYGWIN \
	'collision report on clone (w/ colliding peer after the detected entry)' '

	test_config_global filter.logger.smudge "\"$TEST_ROOT/logger_script\" %f" &&
	git reset --hard basename_collision &&
	echo "file_x filter=logger" >.gitattributes &&
	git add .gitattributes &&
	git commit -m "filter for file_x" &&

	rm -rf clone-repo &&
	set_checkout_config 2 0 &&
	test_checkout_workers 2 \
		git -c core.ignoreCase=false clone . clone-repo 2>stderr &&

	grep FILE_X stderr &&
	grep FILE_x stderr &&
	grep file_X stderr &&
	grep file_x stderr &&
	grep "the following paths have collided" stderr &&

	# Check that only "file_x" was filtered
	echo file_x >expected.log &&
	test_cmp clone-repo/filter.log expected.log
'

test_done
