#!/bin/sh

test_description='test cherry-picking an empty cummit'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '

	echo first > file1 &&
	but add file1 &&
	test_tick &&
	but cummit -m "first" &&

	but checkout -b empty-message-branch &&
	echo third >> file1 &&
	but add file1 &&
	test_tick &&
	but cummit --allow-empty-message -m "" &&

	but checkout main &&
	but checkout -b empty-change-branch &&
	test_tick &&
	but cummit --allow-empty -m "empty"

'

test_expect_success 'cherry-pick an empty cummit' '
	but checkout main &&
	test_expect_code 1 but cherry-pick empty-change-branch
'

test_expect_success 'index lockfile was removed' '
	test ! -f .but/index.lock
'

test_expect_success 'cherry-pick a cummit with an empty message' '
	test_when_finished "but reset --hard empty-message-branch~1" &&
	but checkout main &&
	but cherry-pick empty-message-branch
'

test_expect_success 'index lockfile was removed' '
	test ! -f .but/index.lock
'

test_expect_success 'cherry-pick a cummit with an empty message with --allow-empty-message' '
	but checkout -f main &&
	but cherry-pick --allow-empty-message empty-message-branch
'

test_expect_success 'cherry pick an empty non-ff cummit without --allow-empty' '
	but checkout main &&
	echo fourth >>file2 &&
	but add file2 &&
	but cummit -m "fourth" &&
	test_must_fail but cherry-pick empty-change-branch
'

test_expect_success 'cherry pick an empty non-ff cummit with --allow-empty' '
	but checkout main &&
	but cherry-pick --allow-empty empty-change-branch
'

test_expect_success 'cherry pick with --keep-redundant-cummits' '
	but checkout main &&
	but cherry-pick --keep-redundant-cummits HEAD^
'

test_expect_success 'cherry-pick a cummit that becomes no-op (prep)' '
	but checkout main &&
	but branch fork &&
	echo foo >file2 &&
	but add file2 &&
	test_tick &&
	but cummit -m "add file2 on main" &&

	but checkout fork &&
	echo foo >file2 &&
	but add file2 &&
	test_tick &&
	but cummit -m "add file2 on the side"
'

test_expect_success 'cherry-pick a no-op without --keep-redundant' '
	but reset --hard &&
	but checkout fork^0 &&
	test_must_fail but cherry-pick main
'

test_expect_success 'cherry-pick a no-op with --keep-redundant' '
	but reset --hard &&
	but checkout fork^0 &&
	but cherry-pick --keep-redundant-cummits main &&
	but show -s --format=%s >actual &&
	echo "add file2 on main" >expect &&
	test_cmp expect actual
'

test_done
