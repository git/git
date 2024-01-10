#!/bin/sh

test_description="Test that merge state is as expected after failed merge"

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME
TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'Ensure we restore original state if no merge strategy handles it' '
	test_commit --no-tag "Initial" base base &&

	for b in branch1 branch2 branch3
	do
		git checkout -b $b main &&
		test_commit --no-tag "Change on $b" base $b || return 1
	done &&

	git checkout branch1 &&
	# This is a merge that octopus cannot handle.  Note, that it does not
	# just hit conflicts, it completely fails and says that it cannot
	# handle this type of merge.
	test_expect_code 2 git merge branch2 branch3 >output 2>&1 &&
	grep "fatal: merge program failed" output &&
	grep "Should not be doing an octopus" output &&

	# Make sure we did not leave stray changes around when no appropriate
	# merge strategy was found
	git diff --exit-code --name-status &&
	test_path_is_missing .git/MERGE_HEAD
'

test_done
