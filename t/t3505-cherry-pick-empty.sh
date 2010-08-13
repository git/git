#!/bin/sh

test_description='test cherry-picking an empty commit'

. ./test-lib.sh

test_expect_success setup '

	echo first > file1 &&
	git add file1 &&
	test_tick &&
	git commit -m "first" &&

	git checkout -b empty-branch &&
	test_tick &&
	git commit --allow-empty -m "empty" &&

	echo third >> file1 &&
	git add file1 &&
	test_tick &&
	git commit --allow-empty-message -m ""

'

test_expect_success 'cherry-pick an empty commit' '
	git checkout master && {
		git cherry-pick empty-branch^
		test "$?" = 1
	}
'

test_expect_success 'index lockfile was removed' '

	test ! -f .git/index.lock

'

test_expect_success 'cherry-pick a commit with an empty message' '
	git checkout master && {
		git cherry-pick empty-branch
		test "$?" = 1
	}
'

test_expect_success 'index lockfile was removed' '

	test ! -f .git/index.lock

'

test_done
