#!/bin/sh

test_description='test git worktree move, remove, lock and unlock'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit init &&
	git worktree add source &&
	git worktree list --porcelain | grep "^worktree" >actual &&
	cat <<-EOF >expected &&
	worktree $(pwd)
	worktree $(pwd)/source
	EOF
	test_cmp expected actual
'

test_expect_success 'lock main worktree' '
	test_must_fail git worktree lock .
'

test_expect_success 'lock linked worktree' '
	git worktree lock --reason hahaha source &&
	echo hahaha >expected &&
	test_cmp expected .git/worktrees/source/locked
'

test_expect_success 'lock linked worktree from another worktree' '
	rm .git/worktrees/source/locked &&
	git worktree add elsewhere &&
	git -C elsewhere worktree lock --reason hahaha ../source &&
	echo hahaha >expected &&
	test_cmp expected .git/worktrees/source/locked
'

test_expect_success 'lock worktree twice' '
	test_must_fail git worktree lock source &&
	echo hahaha >expected &&
	test_cmp expected .git/worktrees/source/locked
'

test_expect_success 'lock worktree twice (from the locked worktree)' '
	test_must_fail git -C source worktree lock . &&
	echo hahaha >expected &&
	test_cmp expected .git/worktrees/source/locked
'

test_expect_success 'unlock main worktree' '
	test_must_fail git worktree unlock .
'

test_expect_success 'unlock linked worktree' '
	git worktree unlock source &&
	test_path_is_missing .git/worktrees/source/locked
'

test_expect_success 'unlock worktree twice' '
	test_must_fail git worktree unlock source &&
	test_path_is_missing .git/worktrees/source/locked
'

test_done
