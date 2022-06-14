#!/bin/sh

test_description='test operations trying to overwrite refs at worktree HEAD'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	test_commit init &&
	git branch -f fake-1 &&
	git branch -f fake-2 &&

	for i in 1 2 3 4
	do
		test_commit $i &&
		git branch wt-$i &&
		git worktree add wt-$i wt-$i || return 1
	done
'

test_expect_success 'refuse to overwrite: checked out in worktree' '
	for i in 1 2 3 4
	do
		test_must_fail git branch -f wt-$i HEAD 2>err
		grep "cannot force update the branch" err || return 1
	done
'

test_done
