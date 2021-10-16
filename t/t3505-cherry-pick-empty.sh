#!/bin/sh

test_description='test cherry-picking an empty commit'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '

	echo first > file1 &&
	git add file1 &&
	test_tick &&
	git commit -m "first" &&

	git checkout -b empty-message-branch &&
	echo third >> file1 &&
	git add file1 &&
	test_tick &&
	git commit --allow-empty-message -m "" &&

	git checkout main &&
	git checkout -b empty-change-branch &&
	test_tick &&
	git commit --allow-empty -m "empty"

'

test_expect_success 'cherry-pick an empty commit' '
	git checkout main &&
	test_expect_code 1 git cherry-pick empty-change-branch
'

test_expect_success 'index lockfile was removed' '
	test ! -f .git/index.lock
'

test_expect_success 'cherry-pick a commit with an empty message' '
	test_when_finished "git reset --hard empty-message-branch~1" &&
	git checkout main &&
	git cherry-pick empty-message-branch
'

test_expect_success 'index lockfile was removed' '
	test ! -f .git/index.lock
'

test_expect_success 'cherry-pick a commit with an empty message with --allow-empty-message' '
	git checkout -f main &&
	git cherry-pick --allow-empty-message empty-message-branch
'

test_expect_success 'cherry pick an empty non-ff commit without --allow-empty' '
	git checkout main &&
	echo fourth >>file2 &&
	git add file2 &&
	git commit -m "fourth" &&
	test_must_fail git cherry-pick empty-change-branch
'

test_expect_success 'cherry pick an empty non-ff commit with --allow-empty' '
	git checkout main &&
	git cherry-pick --allow-empty empty-change-branch
'

test_expect_success 'cherry pick with --keep-redundant-commits' '
	git checkout main &&
	git cherry-pick --keep-redundant-commits HEAD^
'

test_expect_success 'cherry-pick a commit that becomes no-op (prep)' '
	git checkout main &&
	git branch fork &&
	echo foo >file2 &&
	git add file2 &&
	test_tick &&
	git commit -m "add file2 on main" &&

	git checkout fork &&
	echo foo >file2 &&
	git add file2 &&
	test_tick &&
	git commit -m "add file2 on the side"
'

test_expect_success 'cherry-pick a no-op without --keep-redundant' '
	git reset --hard &&
	git checkout fork^0 &&
	test_must_fail git cherry-pick main
'

test_expect_success 'cherry-pick a no-op with --keep-redundant' '
	git reset --hard &&
	git checkout fork^0 &&
	git cherry-pick --keep-redundant-commits main &&
	git show -s --format=%s >actual &&
	echo "add file2 on main" >expect &&
	test_cmp expect actual
'

test_done
