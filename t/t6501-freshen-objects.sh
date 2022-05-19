#!/bin/sh
#
# This test covers the handling of objects which might have old
# mtimes in the filesystem (because they were used previously)
# and are just now becoming referenced again.
#
# We're going to do two things that are a little bit "fake" to
# help make our simulation easier:
#
#   1. We'll turn off reflogs. You can still run into
#      problems with reflogs on, but your objects
#      don't get pruned until both the reflog expiration
#      has passed on their references, _and_ they are out
#      of prune's expiration period. Dropping reflogs
#      means we only have to deal with one variable in our tests,
#      but the results generalize.
#
#   2. We'll use a temporary index file to create our
#      works-in-progress. Most workflows would mention
#      referenced objects in the index, which prune takes
#      into account. However, many operations don't. For
#      example, a partial cummit with "but cummit foo"
#      will use a temporary index. Or they may not need
#      an index at all (e.g., creating a new cummit
#      to refer to an existing tree).

test_description='check pruning of dependent objects'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# We care about reachability, so we do not want to use
# the normal test_cummit, which creates extra tags.
add () {
	echo "$1" >"$1" &&
	but add "$1"
}
cummit () {
	test_tick &&
	add "$1" &&
	but cummit -m "$1"
}

maybe_repack () {
	case "$title" in
	loose)
		: skip repack
		;;
	repack)
		but repack -ad
		;;
	bitmap)
		but repack -adb
		;;
	*)
		echo >&2 "unknown test type in maybe_repack"
		return 1
		;;
	esac
}

for title in loose repack bitmap
do
	test_expect_success "make repo completely empty ($title)" '
		rm -rf .but &&
		but init
	'

	test_expect_success "disable reflogs ($title)" '
		but config core.logallrefupdates false &&
		but reflog expire --expire=all --all
	'

	test_expect_success "setup basic history ($title)" '
		cummit base
	'

	test_expect_success "create and abandon some objects ($title)" '
		but checkout -b experiment &&
		cummit abandon &&
		maybe_repack &&
		but checkout main &&
		but branch -D experiment
	'

	test_expect_success "simulate time passing ($title)" '
		test-tool chmtime --get -86400 $(find .but/objects -type f)
	'

	test_expect_success "start writing new cummit with old blob ($title)" '
		tree=$(
			BUT_INDEX_FILE=index.tmp &&
			export BUT_INDEX_FILE &&
			but read-tree HEAD &&
			add unrelated &&
			add abandon &&
			but write-tree
		)
	'

	test_expect_success "simultaneous gc ($title)" '
		but gc --prune=12.hours.ago
	'

	test_expect_success "finish writing out cummit ($title)" '
		cummit=$(echo foo | but cummit-tree -p HEAD $tree) &&
		but update-ref HEAD $cummit
	'

	# "abandon" blob should have been rescued by reference from new tree
	test_expect_success "repository passes fsck ($title)" '
		but fsck
	'

	test_expect_success "abandon objects again ($title)" '
		but reset --hard HEAD^ &&
		test-tool chmtime --get -86400 $(find .but/objects -type f)
	'

	test_expect_success "start writing new cummit with same tree ($title)" '
		tree=$(
			BUT_INDEX_FILE=index.tmp &&
			export BUT_INDEX_FILE &&
			but read-tree HEAD &&
			add abandon &&
			add unrelated &&
			but write-tree
		)
	'

	test_expect_success "simultaneous gc ($title)" '
		but gc --prune=12.hours.ago
	'

	# tree should have been refreshed by write-tree
	test_expect_success "finish writing out cummit ($title)" '
		cummit=$(echo foo | but cummit-tree -p HEAD $tree) &&
		but update-ref HEAD $cummit
	'
done

test_expect_success 'do not complain about existing broken links (cummit)' '
	cat >broken-cummit <<-EOF &&
	tree $(test_oid 001)
	parent $(test_oid 002)
	author whatever <whatever@example.com> 1234 -0000
	cummitter whatever <whatever@example.com> 1234 -0000

	some message
	EOF
	cummit=$(but hash-object -t cummit -w broken-cummit) &&
	but gc -q 2>stderr &&
	verbose but cat-file -e $cummit &&
	test_must_be_empty stderr
'

test_expect_success 'do not complain about existing broken links (tree)' '
	cat >broken-tree <<-EOF &&
	100644 blob $(test_oid 003)	foo
	EOF
	tree=$(but mktree --missing <broken-tree) &&
	but gc -q 2>stderr &&
	but cat-file -e $tree &&
	test_must_be_empty stderr
'

test_expect_success 'do not complain about existing broken links (tag)' '
	cat >broken-tag <<-EOF &&
	object $(test_oid 004)
	type cummit
	tag broken
	tagger whatever <whatever@example.com> 1234 -0000

	this is a broken tag
	EOF
	tag=$(but hash-object -t tag -w broken-tag) &&
	but gc -q 2>stderr &&
	but cat-file -e $tag &&
	test_must_be_empty stderr
'

test_done
