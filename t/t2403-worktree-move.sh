#!/bin/sh

test_description='test git worktree move, remove, lock and unlock'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit init &&
	git worktree add source &&
	git worktree list --porcelain >out &&
	grep "^worktree" out >actual &&
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
	test_when_finished "git worktree unlock source" &&
	test_must_fail git worktree move source destination
'

test_expect_success 'move worktree' '
	git worktree move source destination &&
	test_path_is_missing source &&
	git worktree list --porcelain >out &&
	test_grep "^worktree.*/destination$" out &&
	test_grep ! "^worktree.*/source$" out &&
	git -C destination log --format=%s >actual2 &&
	echo init >expected2 &&
	test_cmp expected2 actual2
'

test_expect_success 'move main worktree' '
	test_must_fail git worktree move . def
'

test_expect_success 'move worktree to another dir' '
	mkdir some-dir &&
	git worktree move destination some-dir &&
	test_when_finished "git worktree move some-dir/destination destination" &&
	test_path_is_missing destination &&
	git worktree list --porcelain >out &&
	test_grep "^worktree.*/some-dir/destination$" out &&
	git -C some-dir/destination log --format=%s >actual2 &&
	echo init >expected2 &&
	test_cmp expected2 actual2
'

test_expect_success 'move locked worktree (force)' '
	test_when_finished "
		git worktree unlock flump || :
		git worktree remove flump || :
		git worktree unlock ploof || :
		git worktree remove ploof || :
		" &&
	git worktree add --detach flump &&
	git worktree lock flump &&
	test_must_fail git worktree move flump ploof" &&
	test_must_fail git worktree move --force flump ploof" &&
	git worktree move --force --force flump ploof
'

test_expect_success 'refuse to move worktree atop existing path' '
	>bobble &&
	git worktree add --detach beeble &&
	test_must_fail git worktree move beeble bobble
'

test_expect_success 'move atop existing but missing worktree' '
	git worktree add --detach gnoo &&
	git worktree add --detach pneu &&
	rm -fr pneu &&
	test_must_fail git worktree move gnoo pneu &&
	git worktree move --force gnoo pneu &&

	git worktree add --detach nu &&
	git worktree lock nu &&
	rm -fr nu &&
	test_must_fail git worktree move pneu nu &&
	test_must_fail git worktree --force move pneu nu &&
	git worktree move --force --force pneu nu
'

test_expect_success 'move a repo with uninitialized submodule' '
	git init withsub &&
	(
		cd withsub &&
		test_commit initial &&
		git -c protocol.file.allow=always \
			submodule add "$PWD"/.git sub &&
		git commit -m withsub &&
		git worktree add second HEAD &&
		git worktree move second third
	)
'

test_expect_success 'not move a repo with initialized submodule' '
	(
		cd withsub &&
		git -c protocol.file.allow=always -C third submodule update &&
		test_must_fail git worktree move third forth
	)
'

test_expect_success 'remove main worktree' '
	test_must_fail git worktree remove .
'

test_expect_success 'remove locked worktree' '
	git worktree lock destination &&
	test_when_finished "git worktree unlock destination" &&
	test_must_fail git worktree remove destination
'

test_expect_success 'remove worktree with dirty tracked file' '
	echo dirty >>destination/init.t &&
	test_when_finished "git -C destination checkout init.t" &&
	test_must_fail git worktree remove destination
'

test_expect_success 'remove worktree with untracked file' '
	: >destination/untracked &&
	test_must_fail git worktree remove destination
'

test_expect_success 'force remove worktree with untracked file' '
	git worktree remove --force destination &&
	test_path_is_missing destination
'

test_expect_success 'remove missing worktree' '
	git worktree add to-be-gone &&
	test_path_is_dir .git/worktrees/to-be-gone &&
	mv to-be-gone gone &&
	git worktree remove to-be-gone &&
	test_path_is_missing .git/worktrees/to-be-gone
'

test_expect_success 'NOT remove missing-but-locked worktree' '
	git worktree add gone-but-locked &&
	git worktree lock gone-but-locked &&
	test_path_is_dir .git/worktrees/gone-but-locked &&
	mv gone-but-locked really-gone-now &&
	test_must_fail git worktree remove gone-but-locked &&
	test_path_is_dir .git/worktrees/gone-but-locked
'

test_expect_success 'proper error when worktree not found' '
	for i in noodle noodle/bork
	do
		test_must_fail git worktree lock $i 2>err &&
		test_grep "not a working tree" err || return 1
	done
'

test_expect_success 'remove locked worktree (force)' '
	git worktree add --detach gumby &&
	test_when_finished "git worktree remove gumby || :" &&
	git worktree lock gumby &&
	test_when_finished "git worktree unlock gumby || :" &&
	test_must_fail git worktree remove gumby &&
	test_must_fail git worktree remove --force gumby &&
	git worktree remove --force --force gumby
'

test_expect_success 'remove cleans up .git/worktrees when empty' '
	git init moog &&
	(
		cd moog &&
		test_commit bim &&
		git worktree add --detach goom &&
		test_path_exists .git/worktrees &&
		git worktree remove goom &&
		test_path_is_missing .git/worktrees
	)
'

test_expect_success 'remove a repo with uninitialized submodule' '
	test_config_global protocol.file.allow always &&
	(
		cd withsub &&
		git worktree add to-remove HEAD &&
		git worktree remove to-remove
	)
'

test_expect_success 'not remove a repo with initialized submodule' '
	test_config_global protocol.file.allow always &&
	(
		cd withsub &&
		git worktree add to-remove HEAD &&
		git -C to-remove submodule update &&
		test_must_fail git worktree remove to-remove
	)
'

test_expect_success 'move worktree with absolute path to relative path' '
	test_config worktree.useRelativePaths false &&
	git worktree add ./absolute &&
	git worktree move --relative-paths absolute relative &&
	echo "gitdir: ../.git/worktrees/absolute" >expect &&
	test_cmp expect relative/.git &&
	echo "../../../relative/.git" >expect &&
	test_cmp expect .git/worktrees/absolute/gitdir &&
	test_config worktree.useRelativePaths true &&
	git worktree move relative relative2 &&
	echo "gitdir: ../.git/worktrees/absolute" >expect &&
	test_cmp expect relative2/.git &&
	echo "../../../relative2/.git" >expect &&
	test_cmp expect .git/worktrees/absolute/gitdir
'

test_expect_success 'move worktree with relative path to absolute path' '
	test_config worktree.useRelativePaths true &&
	git worktree move --no-relative-paths relative2 absolute &&
	echo "gitdir: $(pwd)/.git/worktrees/absolute" >expect &&
	test_cmp expect absolute/.git &&
	echo "$(pwd)/absolute/.git" >expect &&
	test_cmp expect .git/worktrees/absolute/gitdir
'

test_expect_success 'setup commits that conflict for am, cherry-pick and revert' '
	git worktree add --detach conflict-setup init &&
	(
		cd conflict-setup &&
		test_commit base conflict.t base &&
		git switch --detach init &&
		test_commit side conflict.t side &&
		test_commit side-two
	) &&
	git worktree remove conflict-setup
'

test_expect_success 'remove worktree in the middle of a rebase' '
	test_when_finished "rm -rf mid-rebase; git worktree prune" &&
	git worktree add --detach mid-rebase side &&
	test_must_fail git -C mid-rebase rebase -f -x false init &&
	test_must_fail git worktree remove mid-rebase 2>err &&
	test_grep "in the middle of a rebase" err &&
	test_path_is_dir mid-rebase &&
	git worktree remove --force mid-rebase &&
	test_path_is_missing mid-rebase
'

test_expect_success 'remove worktree in the middle of an am session' '
	test_when_finished "rm -rf mid-am patch; git worktree prune" &&
	git format-patch -1 --stdout side >patch &&
	git worktree add --detach mid-am base &&
	(
		cd mid-am &&
		test_must_fail git am ../patch &&
		git reset --hard
	) &&
	test_must_fail git worktree remove mid-am 2>err &&
	test_grep "in the middle of an am session" err &&
	git worktree remove --force mid-am
'

test_expect_success 'remove worktree in the middle of a bisect' '
	test_when_finished "rm -rf mid-bisect; git worktree prune" &&
	git worktree add --detach mid-bisect side-two &&
	git -C mid-bisect bisect start side-two init &&
	test_must_fail git worktree remove mid-bisect 2>err &&
	test_grep "in the middle of a bisect" err &&
	git worktree remove --force mid-bisect
'

test_expect_success 'remove worktree in the middle of a cherry-pick' '
	test_when_finished "rm -rf mid-pick; git worktree prune" &&
	git worktree add --detach mid-pick base &&
	(
		cd mid-pick &&
		test_must_fail git cherry-pick side &&
		git checkout --ours conflict.t &&
		git add conflict.t &&
		git rev-parse --verify CHERRY_PICK_HEAD
	) &&
	test_path_is_missing .git/worktrees/mid-pick/sequencer &&
	test_must_fail git worktree remove mid-pick 2>err &&
	test_grep "in the middle of a cherry-pick," err &&
	git worktree remove --force mid-pick
'

test_expect_success 'remove worktree in the middle of a revert' '
	test_when_finished "rm -rf mid-revert; git worktree prune" &&
	git worktree add --detach mid-revert base &&
	(
		cd mid-revert &&
		test_must_fail git revert side &&
		git checkout --ours conflict.t &&
		git add conflict.t &&
		git rev-parse --verify REVERT_HEAD
	) &&
	test_path_is_missing .git/worktrees/mid-revert/sequencer &&
	test_must_fail git worktree remove mid-revert 2>err &&
	test_grep "in the middle of a revert" err &&
	git worktree remove --force mid-revert
'

test_expect_success 'remove worktree in the middle of a cherry-pick sequence' '
	test_when_finished "rm -rf mid-pick-seq; git worktree prune" &&
	git worktree add --detach mid-pick-seq base &&
	(
		cd mid-pick-seq &&
		test_must_fail git cherry-pick side side-two &&
		git checkout --theirs conflict.t &&
		git add conflict.t &&
		git commit --no-edit
	) &&
	test_path_is_dir .git/worktrees/mid-pick-seq/sequencer &&
	test_must_fail git worktree remove mid-pick-seq 2>err &&
	test_grep "in the middle of a cherry-pick or revert" err &&
	git worktree remove --force mid-pick-seq
'

test_done
