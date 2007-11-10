#!/bin/sh

test_description='cherry picking and reverting a merge

		b---c
	       /   /
	initial---a

'

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
	git checkout master &&
	git merge side &&
	git tag c

'

test_expect_success 'cherry-pick a non-merge with -m should fail' '

	git reset --hard &&
	git checkout a^0 &&
	! git cherry-pick -m 1 b &&
	git diff --exit-code a --

'

test_expect_success 'cherry pick a merge without -m should fail' '

	git reset --hard &&
	git checkout a^0 &&
	! git cherry-pick c &&
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
	! git cherry-pick -m 3 c

'

test_expect_success 'revert a non-merge with -m should fail' '

	git reset --hard &&
	git checkout c^0 &&
	! git revert -m 1 b &&
	git diff --exit-code c

'

test_expect_success 'revert a merge without -m should fail' '

	git reset --hard &&
	git checkout c^0 &&
	! git revert c &&
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
	! git revert -m 3 c &&
	git diff --exit-code c

'

test_done
