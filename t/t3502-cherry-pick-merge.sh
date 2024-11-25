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
	git add A B &&
	git commit -m "Initial" &&
	git tag initial &&
	git branch side &&
	echo new line >A &&
	git commit -m "add line to A" A &&
	git tag a &&
	git checkout side &&
	echo new line >B &&
	git commit -m "add line to B" B &&
	git tag b &&
	git checkout main &&
	git merge side &&
	git tag c

'

test_expect_success 'cherry-pick -m complains of bogus numbers' '
	# expect 129 here to distinguish between cases where
	# there was nothing to cherry-pick
	test_expect_code 129 git cherry-pick -m &&
	test_expect_code 129 git cherry-pick -m foo b &&
	test_expect_code 129 git cherry-pick -m -1 b &&
	test_expect_code 129 git cherry-pick -m 0 b
'

test_expect_success 'cherry-pick explicit first parent of a non-merge' '

	git reset --hard &&
	git checkout a^0 &&
	git cherry-pick -m 1 b &&
	git diff --exit-code c --

'

test_expect_success 'cherry pick a merge without -m should fail' '

	git reset --hard &&
	git checkout a^0 &&
	test_must_fail git cherry-pick c &&
	git diff --exit-code a --

'

test_expect_success 'cherry pick a merge (1)' '

	git reset --hard &&
	git checkout a^0 &&
	git cherry-pick -m 1 c &&
	git diff --exit-code c

'

test_expect_success 'cherry pick a merge (2)' '

	git reset --hard &&
	git checkout b^0 &&
	git cherry-pick -m 2 c &&
	git diff --exit-code c

'

test_expect_success 'cherry pick a merge relative to nonexistent parent should fail' '

	git reset --hard &&
	git checkout b^0 &&
	test_must_fail git cherry-pick -m 3 c

'

test_expect_success 'revert explicit first parent of a non-merge' '

	git reset --hard &&
	git checkout c^0 &&
	git revert -m 1 b &&
	git diff --exit-code a --

'

test_expect_success 'revert a merge without -m should fail' '

	git reset --hard &&
	git checkout c^0 &&
	test_must_fail git revert c &&
	git diff --exit-code c

'

test_expect_success 'revert a merge (1)' '

	git reset --hard &&
	git checkout c^0 &&
	git revert -m 1 c &&
	git diff --exit-code a --

'

test_expect_success 'revert a merge (2)' '

	git reset --hard &&
	git checkout c^0 &&
	git revert -m 2 c &&
	git diff --exit-code b --

'

test_expect_success 'revert a merge relative to nonexistent parent should fail' '

	git reset --hard &&
	git checkout c^0 &&
	test_must_fail git revert -m 3 c &&
	git diff --exit-code c

'

test_done
