#!/bin/sh

test_description='cherry picking and reverting a merge

		b---c
	       /   /
	initial---a

'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '

	>A &&
	>B &&
	but add A B &&
	but cummit -m "Initial" &&
	but tag initial &&
	but branch side &&
	echo new line >A &&
	but cummit -m "add line to A" A &&
	but tag a &&
	but checkout side &&
	echo new line >B &&
	but cummit -m "add line to B" B &&
	but tag b &&
	but checkout main &&
	but merge side &&
	but tag c

'

test_expect_success 'cherry-pick -m complains of bogus numbers' '
	# expect 129 here to distinguish between cases where
	# there was nothing to cherry-pick
	test_expect_code 129 but cherry-pick -m &&
	test_expect_code 129 but cherry-pick -m foo b &&
	test_expect_code 129 but cherry-pick -m -1 b &&
	test_expect_code 129 but cherry-pick -m 0 b
'

test_expect_success 'cherry-pick explicit first parent of a non-merge' '

	but reset --hard &&
	but checkout a^0 &&
	but cherry-pick -m 1 b &&
	but diff --exit-code c --

'

test_expect_success 'cherry pick a merge without -m should fail' '

	but reset --hard &&
	but checkout a^0 &&
	test_must_fail but cherry-pick c &&
	but diff --exit-code a --

'

test_expect_success 'cherry pick a merge (1)' '

	but reset --hard &&
	but checkout a^0 &&
	but cherry-pick -m 1 c &&
	but diff --exit-code c

'

test_expect_success 'cherry pick a merge (2)' '

	but reset --hard &&
	but checkout b^0 &&
	but cherry-pick -m 2 c &&
	but diff --exit-code c

'

test_expect_success 'cherry pick a merge relative to nonexistent parent should fail' '

	but reset --hard &&
	but checkout b^0 &&
	test_must_fail but cherry-pick -m 3 c

'

test_expect_success 'revert explicit first parent of a non-merge' '

	but reset --hard &&
	but checkout c^0 &&
	but revert -m 1 b &&
	but diff --exit-code a --

'

test_expect_success 'revert a merge without -m should fail' '

	but reset --hard &&
	but checkout c^0 &&
	test_must_fail but revert c &&
	but diff --exit-code c

'

test_expect_success 'revert a merge (1)' '

	but reset --hard &&
	but checkout c^0 &&
	but revert -m 1 c &&
	but diff --exit-code a --

'

test_expect_success 'revert a merge (2)' '

	but reset --hard &&
	but checkout c^0 &&
	but revert -m 2 c &&
	but diff --exit-code b --

'

test_expect_success 'revert a merge relative to nonexistent parent should fail' '

	but reset --hard &&
	but checkout c^0 &&
	test_must_fail but revert -m 3 c &&
	but diff --exit-code c

'

test_done
