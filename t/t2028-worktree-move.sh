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

test_expect_success 'move non-worktree' '
	mkdir abc &&
	test_must_fail git worktree move abc def
'

test_expect_success 'move locked worktree' '
	git worktree lock source &&
	test_must_fail git worktree move source destination &&
	git worktree unlock source
'

test_expect_success 'move worktree' '
	toplevel="$(pwd)" &&
	git worktree move source destination &&
	test_path_is_missing source &&
	git worktree list --porcelain | grep "^worktree" >actual &&
	cat <<-EOF >expected &&
	worktree $toplevel
	worktree $toplevel/destination
	worktree $toplevel/elsewhere
	EOF
	test_cmp expected actual &&
	git -C destination log --format=%s >actual2 &&
	echo init >expected2 &&
	test_cmp expected2 actual2
'

test_expect_success 'move main worktree' '
	test_must_fail git worktree move . def
'

test_expect_success 'remove main worktree' '
	test_must_fail git worktree remove .
'

test_expect_success 'remove locked worktree' '
	git worktree lock destination &&
	test_must_fail git worktree remove destination &&
	git worktree unlock destination
'

test_expect_success 'remove worktree with dirty tracked file' '
	echo dirty >>destination/init.t &&
	test_must_fail git worktree remove destination
'

test_expect_success 'remove worktree with untracked file' '
	git -C destination checkout init.t &&
	: >destination/untracked &&
	test_must_fail git worktree remove destination
'

test_expect_success 'force remove worktree with untracked file' '
	git worktree remove --force destination &&
	test_path_is_missing destination
'

test_done
