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
#      example, a partial commit with "git commit foo"
#      will use a temporary index. Or they may not need
#      an index at all (e.g., creating a new commit
#      to refer to an existing tree).

test_description='check pruning of dependent objects'
. ./test-lib.sh

# We care about reachability, so we do not want to use
# the normal test_commit, which creates extra tags.
add () {
	echo "$1" >"$1" &&
	git add "$1"
}
commit () {
	test_tick &&
	add "$1" &&
	git commit -m "$1"
}

maybe_repack () {
	if test -n "$repack"; then
		git repack -ad
	fi
}

for repack in '' true; do
	title=${repack:+repack}
	title=${title:-loose}

	test_expect_success "make repo completely empty ($title)" '
		rm -rf .git &&
		git init
	'

	test_expect_success "disable reflogs ($title)" '
		git config core.logallrefupdates false &&
		git reflog expire --expire=all --all
	'

	test_expect_success "setup basic history ($title)" '
		commit base
	'

	test_expect_success "create and abandon some objects ($title)" '
		git checkout -b experiment &&
		commit abandon &&
		maybe_repack &&
		git checkout master &&
		git branch -D experiment
	'

	test_expect_success "simulate time passing ($title)" '
		test-tool chmtime --get -86400 $(find .git/objects -type f)
	'

	test_expect_success "start writing new commit with old blob ($title)" '
		tree=$(
			GIT_INDEX_FILE=index.tmp &&
			export GIT_INDEX_FILE &&
			git read-tree HEAD &&
			add unrelated &&
			add abandon &&
			git write-tree
		)
	'

	test_expect_success "simultaneous gc ($title)" '
		git gc --prune=12.hours.ago
	'

	test_expect_success "finish writing out commit ($title)" '
		commit=$(echo foo | git commit-tree -p HEAD $tree) &&
		git update-ref HEAD $commit
	'

	# "abandon" blob should have been rescued by reference from new tree
	test_expect_success "repository passes fsck ($title)" '
		git fsck
	'

	test_expect_success "abandon objects again ($title)" '
		git reset --hard HEAD^ &&
		test-tool chmtime --get -86400 $(find .git/objects -type f)
	'

	test_expect_success "start writing new commit with same tree ($title)" '
		tree=$(
			GIT_INDEX_FILE=index.tmp &&
			export GIT_INDEX_FILE &&
			git read-tree HEAD &&
			add abandon &&
			add unrelated &&
			git write-tree
		)
	'

	test_expect_success "simultaneous gc ($title)" '
		git gc --prune=12.hours.ago
	'

	# tree should have been refreshed by write-tree
	test_expect_success "finish writing out commit ($title)" '
		commit=$(echo foo | git commit-tree -p HEAD $tree) &&
		git update-ref HEAD $commit
	'
done

test_expect_success 'do not complain about existing broken links (commit)' '
	cat >broken-commit <<-EOF &&
	tree $(test_oid 001)
	parent $(test_oid 002)
	author whatever <whatever@example.com> 1234 -0000
	committer whatever <whatever@example.com> 1234 -0000

	some message
	EOF
	commit=$(git hash-object -t commit -w broken-commit) &&
	git gc -q 2>stderr &&
	verbose git cat-file -e $commit &&
	test_must_be_empty stderr
'

test_expect_success 'do not complain about existing broken links (tree)' '
	cat >broken-tree <<-EOF &&
	100644 blob $(test_oid 003)	foo
	EOF
	tree=$(git mktree --missing <broken-tree) &&
	git gc -q 2>stderr &&
	git cat-file -e $tree &&
	test_must_be_empty stderr
'

test_expect_success 'do not complain about existing broken links (tag)' '
	cat >broken-tag <<-EOF &&
	object $(test_oid 004)
	type commit
	tag broken
	tagger whatever <whatever@example.com> 1234 -0000

	this is a broken tag
	EOF
	tag=$(git hash-object -t tag -w broken-tag) &&
	git gc -q 2>stderr &&
	git cat-file -e $tag &&
	test_must_be_empty stderr
'

test_done
