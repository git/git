#!/bin/sh

test_description='per-worktree refs'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit initial &&
	test_commit wt1 &&
	test_commit wt2 &&
	git worktree add wt1 wt1 &&
	git worktree add wt2 wt2 &&
	git checkout initial &&
	git update-ref refs/worktree/foo HEAD &&
	git -C wt1 update-ref refs/worktree/foo HEAD &&
	git -C wt2 update-ref refs/worktree/foo HEAD
'

test_expect_success 'refs/worktree must not be packed' '
	git pack-refs --all &&
	test_path_is_missing .git/refs/tags/wt1 &&
	test_path_is_file .git/refs/worktree/foo &&
	test_path_is_file .git/worktrees/wt1/refs/worktree/foo &&
	test_path_is_file .git/worktrees/wt2/refs/worktree/foo
'

test_expect_success 'refs/worktree are per-worktree' '
	test_cmp_rev worktree/foo initial &&
	( cd wt1 && test_cmp_rev worktree/foo wt1 ) &&
	( cd wt2 && test_cmp_rev worktree/foo wt2 )
'

test_done
