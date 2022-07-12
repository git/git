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
	done &&

	# Create a server that updates each branch by one commit
	git init server &&
	test_commit -C server initial &&
	git remote add server ./server &&
	for i in 1 2 3 4
	do
		git -C server checkout -b wt-$i &&
		test_commit -C server A-$i || return 1
	done &&
	for i in 1 2
	do
		git -C server checkout -b fake-$i &&
		test_commit -C server f-$i || return 1
	done
'

test_expect_success 'refuse to overwrite: checked out in worktree' '
	for i in 1 2 3 4
	do
		test_must_fail git branch -f wt-$i HEAD 2>err
		grep "cannot force update the branch" err &&

		test_must_fail git branch -D wt-$i 2>err
		grep "Cannot delete branch" err || return 1
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

test_expect_success !SANITIZE_LEAK 'refuse to fetch over ref: checked out' '
	test_must_fail git fetch server +refs/heads/wt-3:refs/heads/wt-3 2>err &&
	grep "refusing to fetch into branch '\''refs/heads/wt-3'\''" err &&

	# General fetch into refs/heads/ will fail on first ref,
	# so use a generic error message check.
	test_must_fail git fetch server +refs/heads/*:refs/heads/* 2>err &&
	grep "refusing to fetch into branch" err
'

test_expect_success !SANITIZE_LEAK 'refuse to fetch over ref: worktree in bisect' '
	test_when_finished rm -rf .git/worktrees/wt-*/BISECT_* &&

	touch .git/worktrees/wt-4/BISECT_LOG &&
	echo refs/heads/fake-2 >.git/worktrees/wt-4/BISECT_START &&

	test_must_fail git fetch server +refs/heads/fake-2:refs/heads/fake-2 2>err &&
	grep "refusing to fetch into branch" err
'

test_expect_success !SANITIZE_LEAK 'refuse to fetch over ref: worktree in rebase' '
	test_when_finished rm -rf .git/worktrees/wt-*/rebase-merge &&

	mkdir -p .git/worktrees/wt-4/rebase-merge &&
	touch .git/worktrees/wt-4/rebase-merge/interactive &&
	echo refs/heads/fake-1 >.git/worktrees/wt-4/rebase-merge/head-name &&
	echo refs/heads/fake-2 >.git/worktrees/wt-4/rebase-merge/onto &&

	test_must_fail git fetch server +refs/heads/fake-1:refs/heads/fake-1 2>err &&
	grep "refusing to fetch into branch" err
'

test_expect_success 'refuse to overwrite when in error states' '
	test_when_finished rm -rf .git/worktrees/wt-*/rebase-merge &&
	test_when_finished rm -rf .git/worktrees/wt-*/BISECT_* &&

	# Both branches are currently under rebase.
	mkdir -p .git/worktrees/wt-3/rebase-merge &&
	touch .git/worktrees/wt-3/rebase-merge/interactive &&
	echo refs/heads/fake-1 >.git/worktrees/wt-3/rebase-merge/head-name &&
	echo refs/heads/fake-2 >.git/worktrees/wt-3/rebase-merge/onto &&
	mkdir -p .git/worktrees/wt-4/rebase-merge &&
	touch .git/worktrees/wt-4/rebase-merge/interactive &&
	echo refs/heads/fake-2 >.git/worktrees/wt-4/rebase-merge/head-name &&
	echo refs/heads/fake-1 >.git/worktrees/wt-4/rebase-merge/onto &&

	# Both branches are currently under bisect.
	touch .git/worktrees/wt-4/BISECT_LOG &&
	echo refs/heads/fake-2 >.git/worktrees/wt-4/BISECT_START &&
	touch .git/worktrees/wt-1/BISECT_LOG &&
	echo refs/heads/fake-1 >.git/worktrees/wt-1/BISECT_START &&

	for i in 1 2
	do
		test_must_fail git branch -f fake-$i HEAD 2>err &&
		grep "cannot force update the branch '\''fake-$i'\'' checked out at" err ||
			return 1
	done
'

test_done
