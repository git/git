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

test_expect_success 'disable reflogs' '
	git config core.logallrefupdates false &&
	rm -rf .git/logs
'

test_expect_success 'setup basic history' '
	commit base
'

test_expect_success 'create and abandon some objects' '
	git checkout -b experiment &&
	commit abandon &&
	git checkout master &&
	git branch -D experiment
'

test_expect_success 'simulate time passing' '
	find .git/objects -type f |
	xargs test-chmtime -v -86400
'

test_expect_success 'start writing new commit with old blob' '
	tree=$(
		GIT_INDEX_FILE=index.tmp &&
		export GIT_INDEX_FILE &&
		git read-tree HEAD &&
		add unrelated &&
		add abandon &&
		git write-tree
	)
'

test_expect_success 'simultaneous gc' '
	git gc --prune=12.hours.ago
'

test_expect_success 'finish writing out commit' '
	commit=$(echo foo | git commit-tree -p HEAD $tree) &&
	git update-ref HEAD $commit
'

# "abandon" blob should have been rescued by reference from new tree
test_expect_success 'repository passes fsck' '
	git fsck
'

test_done
