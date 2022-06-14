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

test_expect_success 'refuse to overwrite: worktree in bisect' '
	test_when_finished rm -rf .git/worktrees/wt-*/BISECT_* &&

	touch .git/worktrees/wt-4/BISECT_LOG &&
	echo refs/heads/fake-2 >.git/worktrees/wt-4/BISECT_START &&

	test_must_fail git branch -f fake-2 HEAD 2>err &&
	grep "cannot force update the branch '\''fake-2'\'' checked out at.*wt-4" err
'

test_expect_success 'refuse to overwrite: worktree in rebase' '
	test_when_finished rm -rf .git/worktrees/wt-*/rebase-merge &&

	mkdir -p .git/worktrees/wt-3/rebase-merge &&
	touch .git/worktrees/wt-3/rebase-merge/interactive &&
	echo refs/heads/fake-1 >.git/worktrees/wt-3/rebase-merge/head-name &&
	echo refs/heads/fake-2 >.git/worktrees/wt-3/rebase-merge/onto &&

	test_must_fail git branch -f fake-1 HEAD 2>err &&
	grep "cannot force update the branch '\''fake-1'\'' checked out at.*wt-3" err
'

test_done
