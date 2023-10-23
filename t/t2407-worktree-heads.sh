#!/bin/sh

test_description='test operations trying to overwrite refs at worktree HEAD'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	test_commit init &&

	for i in 1 2 3 4
	do
		git checkout -b conflict-$i &&
		echo "not I" >$i.t &&
		git add $i.t &&
		git commit -m "will conflict" &&

		git checkout - &&
		test_commit $i &&
		git branch wt-$i &&
		git branch fake-$i &&
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
		test_must_fail git branch -f wt-$i HEAD 2>err &&
		grep "cannot force update the branch" err &&

		test_must_fail git branch -D wt-$i 2>err &&
		grep "cannot delete branch" err || return 1
	done
'

test_expect_success !SANITIZE_LEAK 'refuse to overwrite: worktree in bisect' '
	test_when_finished git -C wt-4 bisect reset &&

	# Set up a bisect so HEAD no longer points to wt-4.
	git -C wt-4 bisect start &&
	git -C wt-4 bisect bad wt-4 &&
	git -C wt-4 bisect good wt-1 &&

	test_must_fail git branch -f wt-4 HEAD 2>err &&
	grep "cannot force update the branch '\''wt-4'\'' used by worktree at.*wt-4" err
'

test_expect_success !SANITIZE_LEAK 'refuse to overwrite: worktree in rebase (apply)' '
	test_when_finished git -C wt-2 rebase --abort &&

	# This will fail part-way through due to a conflict.
	test_must_fail git -C wt-2 rebase --apply conflict-2 &&

	test_must_fail git branch -f wt-2 HEAD 2>err &&
	grep "cannot force update the branch '\''wt-2'\'' used by worktree at.*wt-2" err
'

test_expect_success !SANITIZE_LEAK 'refuse to overwrite: worktree in rebase (merge)' '
	test_when_finished git -C wt-2 rebase --abort &&

	# This will fail part-way through due to a conflict.
	test_must_fail git -C wt-2 rebase conflict-2 &&

	test_must_fail git branch -f wt-2 HEAD 2>err &&
	grep "cannot force update the branch '\''wt-2'\'' used by worktree at.*wt-2" err
'

test_expect_success !SANITIZE_LEAK 'refuse to overwrite: worktree in rebase with --update-refs' '
	test_when_finished git -C wt-3 rebase --abort &&

	git branch -f can-be-updated wt-3 &&
	test_must_fail git -C wt-3 rebase --update-refs conflict-3 &&

	for i in 3 4
	do
		test_must_fail git branch -f can-be-updated HEAD 2>err &&
		grep "cannot force update the branch '\''can-be-updated'\'' used by worktree at.*wt-3" err ||
			return 1
	done
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
	test_when_finished git -C wt-4 bisect reset &&

	# Set up a bisect so HEAD no longer points to wt-4.
	git -C wt-4 bisect start &&
	git -C wt-4 bisect bad wt-4 &&
	git -C wt-4 bisect good wt-1 &&

	test_must_fail git fetch server +refs/heads/wt-4:refs/heads/wt-4 2>err &&
	grep "refusing to fetch into branch" err
'

test_expect_success !SANITIZE_LEAK 'refuse to fetch over ref: worktree in rebase' '
	test_when_finished git -C wt-3 rebase --abort &&

	# This will fail part-way through due to a conflict.
	test_must_fail git -C wt-3 rebase conflict-3 &&

	test_must_fail git fetch server +refs/heads/wt-3:refs/heads/wt-3 2>err &&
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
		grep "cannot force update the branch '\''fake-$i'\'' used by worktree at" err ||
			return 1
	done
'

. "$TEST_DIRECTORY"/lib-rebase.sh

test_expect_success !SANITIZE_LEAK 'refuse to overwrite during rebase with --update-refs' '
	git commit --fixup HEAD~2 --allow-empty &&
	(
		set_cat_todo_editor &&
		test_must_fail git rebase -i --update-refs HEAD~3 >todo &&
		! grep "update-refs" todo
	) &&
	git branch -f allow-update HEAD~2 &&
	(
		set_cat_todo_editor &&
		test_must_fail git rebase -i --update-refs HEAD~3 >todo &&
		grep "update-ref refs/heads/allow-update" todo
	)
'

# This must be the last test in this file
test_expect_success '$EDITOR and friends are unchanged' '
	test_editor_unchanged
'

test_done
