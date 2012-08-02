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
	git commit --allow-empty-message -m "" &&

	git checkout master &&
	git checkout -b empty-branch2 &&
	test_tick &&
	git commit --allow-empty -m "empty"

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

test_expect_success 'cherry-pick a commit with an empty message with --allow-empty-message' '
	git checkout -f master &&
	git cherry-pick --allow-empty-message empty-branch
'

test_expect_success 'cherry pick an empty non-ff commit without --allow-empty' '
	git checkout master &&
	echo fourth >>file2 &&
	git add file2 &&
	git commit -m "fourth" &&
	test_must_fail git cherry-pick empty-branch2
'

test_expect_success 'cherry pick an empty non-ff commit with --allow-empty' '
	git checkout master &&
	git cherry-pick --allow-empty empty-branch2
'

test_expect_success 'cherry pick with --keep-redundant-commits' '
	git checkout master &&
	git cherry-pick --keep-redundant-commits HEAD^
'

test_expect_success 'cherry-pick a commit that becomes no-op (prep)' '
	git checkout master &&
	git branch fork &&
	echo foo >file2 &&
	git add file2 &&
	test_tick &&
	git commit -m "add file2 on master" &&

	git checkout fork &&
	echo foo >file2 &&
	git add file2 &&
	test_tick &&
	git commit -m "add file2 on the side"
'

test_expect_success 'cherry-pick a no-op without --keep-redundant' '
	git reset --hard &&
	git checkout fork^0 &&
	test_must_fail git cherry-pick master
'

test_expect_success 'cherry-pick a no-op with --keep-redundant' '
	git reset --hard &&
	git checkout fork^0 &&
	git cherry-pick --keep-redundant-commits master &&
	git show -s --format='%s' >actual &&
	echo "add file2 on master" >expect &&
	test_cmp expect actual
'

test_done
