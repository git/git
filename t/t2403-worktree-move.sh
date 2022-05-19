#!/bin/sh

test_description='test but worktree move, remove, lock and unlock'

. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit init &&
	but worktree add source &&
	but worktree list --porcelain >out &&
	grep "^worktree" out >actual &&
	cat <<-EOF >expected &&
	worktree $(pwd)
	worktree $(pwd)/source
	EOF
	test_cmp expected actual
'

test_expect_success 'lock main worktree' '
	test_must_fail but worktree lock .
'

test_expect_success 'lock linked worktree' '
	but worktree lock --reason hahaha source &&
	echo hahaha >expected &&
	test_cmp expected .but/worktrees/source/locked
'

test_expect_success 'lock linked worktree from another worktree' '
	rm .but/worktrees/source/locked &&
	but worktree add elsewhere &&
	but -C elsewhere worktree lock --reason hahaha ../source &&
	echo hahaha >expected &&
	test_cmp expected .but/worktrees/source/locked
'

test_expect_success 'lock worktree twice' '
	test_must_fail but worktree lock source &&
	echo hahaha >expected &&
	test_cmp expected .but/worktrees/source/locked
'

test_expect_success 'lock worktree twice (from the locked worktree)' '
	test_must_fail but -C source worktree lock . &&
	echo hahaha >expected &&
	test_cmp expected .but/worktrees/source/locked
'

test_expect_success 'unlock main worktree' '
	test_must_fail but worktree unlock .
'

test_expect_success 'unlock linked worktree' '
	but worktree unlock source &&
	test_path_is_missing .but/worktrees/source/locked
'

test_expect_success 'unlock worktree twice' '
	test_must_fail but worktree unlock source &&
	test_path_is_missing .but/worktrees/source/locked
'

test_expect_success 'move non-worktree' '
	mkdir abc &&
	test_must_fail but worktree move abc def
'

test_expect_success 'move locked worktree' '
	but worktree lock source &&
	test_when_finished "but worktree unlock source" &&
	test_must_fail but worktree move source destination
'

test_expect_success 'move worktree' '
	but worktree move source destination &&
	test_path_is_missing source &&
	but worktree list --porcelain >out &&
	grep "^worktree.*/destination$" out &&
	! grep "^worktree.*/source$" out &&
	but -C destination log --format=%s >actual2 &&
	echo init >expected2 &&
	test_cmp expected2 actual2
'

test_expect_success 'move main worktree' '
	test_must_fail but worktree move . def
'

test_expect_success 'move worktree to another dir' '
	mkdir some-dir &&
	but worktree move destination some-dir &&
	test_when_finished "but worktree move some-dir/destination destination" &&
	test_path_is_missing destination &&
	but worktree list --porcelain >out &&
	grep "^worktree.*/some-dir/destination$" out &&
	but -C some-dir/destination log --format=%s >actual2 &&
	echo init >expected2 &&
	test_cmp expected2 actual2
'

test_expect_success 'move locked worktree (force)' '
	test_when_finished "
		but worktree unlock flump || :
		but worktree remove flump || :
		but worktree unlock ploof || :
		but worktree remove ploof || :
		" &&
	but worktree add --detach flump &&
	but worktree lock flump &&
	test_must_fail but worktree move flump ploof" &&
	test_must_fail but worktree move --force flump ploof" &&
	but worktree move --force --force flump ploof
'

test_expect_success 'refuse to move worktree atop existing path' '
	>bobble &&
	but worktree add --detach beeble &&
	test_must_fail but worktree move beeble bobble
'

test_expect_success 'move atop existing but missing worktree' '
	but worktree add --detach gnoo &&
	but worktree add --detach pneu &&
	rm -fr pneu &&
	test_must_fail but worktree move gnoo pneu &&
	but worktree move --force gnoo pneu &&

	but worktree add --detach nu &&
	but worktree lock nu &&
	rm -fr nu &&
	test_must_fail but worktree move pneu nu &&
	test_must_fail but worktree --force move pneu nu &&
	but worktree move --force --force pneu nu
'

test_expect_success 'move a repo with uninitialized submodule' '
	but init withsub &&
	(
		cd withsub &&
		test_cummit initial &&
		but submodule add "$PWD"/.but sub &&
		but cummit -m withsub &&
		but worktree add second HEAD &&
		but worktree move second third
	)
'

test_expect_success 'not move a repo with initialized submodule' '
	(
		cd withsub &&
		but -C third submodule update &&
		test_must_fail but worktree move third forth
	)
'

test_expect_success 'remove main worktree' '
	test_must_fail but worktree remove .
'

test_expect_success 'remove locked worktree' '
	but worktree lock destination &&
	test_when_finished "but worktree unlock destination" &&
	test_must_fail but worktree remove destination
'

test_expect_success 'remove worktree with dirty tracked file' '
	echo dirty >>destination/init.t &&
	test_when_finished "but -C destination checkout init.t" &&
	test_must_fail but worktree remove destination
'

test_expect_success 'remove worktree with untracked file' '
	: >destination/untracked &&
	test_must_fail but worktree remove destination
'

test_expect_success 'force remove worktree with untracked file' '
	but worktree remove --force destination &&
	test_path_is_missing destination
'

test_expect_success 'remove missing worktree' '
	but worktree add to-be-gone &&
	test -d .but/worktrees/to-be-gone &&
	mv to-be-gone gone &&
	but worktree remove to-be-gone &&
	test_path_is_missing .but/worktrees/to-be-gone
'

test_expect_success 'NOT remove missing-but-locked worktree' '
	but worktree add gone-but-locked &&
	but worktree lock gone-but-locked &&
	test -d .but/worktrees/gone-but-locked &&
	mv gone-but-locked really-gone-now &&
	test_must_fail but worktree remove gone-but-locked &&
	test_path_is_dir .but/worktrees/gone-but-locked
'

test_expect_success 'proper error when worktree not found' '
	for i in noodle noodle/bork
	do
		test_must_fail but worktree lock $i 2>err &&
		test_i18ngrep "not a working tree" err || return 1
	done
'

test_expect_success 'remove locked worktree (force)' '
	but worktree add --detach gumby &&
	test_when_finished "but worktree remove gumby || :" &&
	but worktree lock gumby &&
	test_when_finished "but worktree unlock gumby || :" &&
	test_must_fail but worktree remove gumby &&
	test_must_fail but worktree remove --force gumby &&
	but worktree remove --force --force gumby
'

test_expect_success 'remove cleans up .but/worktrees when empty' '
	but init moog &&
	(
		cd moog &&
		test_cummit bim &&
		but worktree add --detach goom &&
		test_path_exists .but/worktrees &&
		but worktree remove goom &&
		test_path_is_missing .but/worktrees
	)
'

test_expect_success 'remove a repo with uninitialized submodule' '
	(
		cd withsub &&
		but worktree add to-remove HEAD &&
		but worktree remove to-remove
	)
'

test_expect_success 'not remove a repo with initialized submodule' '
	(
		cd withsub &&
		but worktree add to-remove HEAD &&
		but -C to-remove submodule update &&
		test_must_fail but worktree remove to-remove
	)
'

test_done
