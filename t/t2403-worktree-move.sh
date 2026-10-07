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

test_expect_success '"move" invokes post-worktree hook in the calling repository' '
	test_hook post-worktree <<-\EOF &&
	test "$#" = 4 || exit 1
	test "$1" = move || exit 0
	{
		printf "%s\n" "$@" &&
		test-tool path-utils real_path . &&
		git rev-parse --absolute-git-dir
	} >hook.actual
	EOF
	git worktree add --detach hook-source &&
	git worktree move hook-source hook-destination &&
	{
		test_write_lines move hook-source "$(pwd)/hook-source" "$(pwd)/hook-destination" &&
		test-tool path-utils real_path . &&
		git rev-parse --absolute-git-dir
	} >hook.expect &&
	test_cmp hook.expect hook.actual
'

test_expect_success 'failing post-worktree move event leaves worktree moved' '
	test_hook post-worktree <<-\EOF &&
	test "$1" = move || exit 0
	exit 1
	EOF
	git worktree add --detach hook-failing-source &&
	test_must_fail git worktree move hook-failing-source hook-failing-destination &&
	test_path_is_missing hook-failing-source &&
	git -C hook-failing-destination status --porcelain >actual &&
	test_must_be_empty actual
'

test_expect_success 'post-worktree move keeps the ID and passes absolute paths with spaces' '
	test_when_finished "rm -rf movehook" &&
	git init movehook &&
	test_commit -C movehook base &&
	git -C movehook worktree add --relative-paths --detach "source tree" &&
	git -C movehook worktree add --detach caller &&
	id=$(basename "$(git -C "movehook/source tree" rev-parse --absolute-git-dir)") &&
	test_hook -C movehook post-worktree <<-\EOF &&
	test "$#" = 4 &&
	{
		printf "%s\n" "$@" &&
		git rev-parse --show-toplevel
	} >hook.actual
	EOF
	git -C movehook/caller worktree move --relative-paths "../source tree" "../destination tree" &&
	test_write_lines move "$id" "$(pwd)/movehook/source tree" \
		"$(pwd)/movehook/destination tree" "$(pwd)/movehook/caller" >hook.expect &&
	test_cmp hook.expect movehook/caller/hook.actual &&
	test_path_is_dir "movehook/destination tree"
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
	test -d .git/worktrees/to-be-gone &&
	mv to-be-gone gone &&
	git worktree remove to-be-gone &&
	test_path_is_missing .git/worktrees/to-be-gone
'

test_expect_success 'NOT remove missing-but-locked worktree' '
	git worktree add gone-but-locked &&
	git worktree lock gone-but-locked &&
	test -d .git/worktrees/gone-but-locked &&
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

test_expect_success '"remove" invokes post-worktree remove event' '
	test_hook post-worktree <<-\EOF &&
	test "$#" = 4 || exit 1
	test "$1" = remove || exit 0
	printf "%s\n" "$@" >hook.actual
	EOF
	git worktree add --detach wt-hooked &&
	git worktree remove wt-hooked &&
	test_write_lines remove wt-hooked "$(pwd)/wt-hooked" "" >hook.expect &&
	test_cmp hook.expect hook.actual
'

test_expect_success '"remove" of missing worktree invokes post-worktree hook' '
	test_when_finished "rm -rf wt-moved-away" &&
	test_hook post-worktree <<-\EOF &&
	test "$1" = remove || exit 0
	printf "%s\n" "$@" >hook.actual
	EOF
	rm -f hook.actual &&
	git worktree add --detach wt-elsewhere &&
	mv wt-elsewhere wt-moved-away &&
	git worktree remove wt-elsewhere &&
	test_write_lines remove wt-elsewhere "$(pwd)/wt-elsewhere" "" >hook.expect &&
	test_cmp hook.expect hook.actual
'

test_expect_success 'refused "remove" does not invoke post-worktree hook' '
	git worktree add --detach wt-kept &&
	test_when_finished "git worktree remove --force --force wt-kept || :" &&
	test_hook post-worktree <<-\EOF &&
	>hook.ran
	EOF
	git worktree lock wt-kept &&
	test_must_fail git worktree remove wt-kept &&
	test_path_is_missing hook.ran
'

test_expect_success 'failing post-worktree remove event fails "remove", worktree is gone' '
	test_hook post-worktree <<-\EOF &&
	test "$1" = remove || exit 0
	exit 1
	EOF
	git worktree add --detach wt-doomed &&
	test_must_fail git worktree remove wt-doomed &&
	test_path_is_missing wt-doomed &&
	test_path_is_missing .git/worktrees/wt-doomed
'

test_expect_success 'post-worktree remove preserves paths with spaces' '
	git worktree add --detach "remove tree" &&
	id=$(basename "$(git -C "remove tree" rev-parse --absolute-git-dir)") &&
	test_hook post-worktree <<-\EOF &&
	test "$#" = 4 &&
	printf "%s\n" "$@" >hook.actual
	EOF
	git worktree remove "remove tree" &&
	test_write_lines remove "$id" "$(pwd)/remove tree" "" >hook.expect &&
	test_cmp hook.expect hook.actual
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

test_done
