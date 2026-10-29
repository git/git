#!/bin/sh

test_description='prune $GIT_DIR/worktrees'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success initialize '
	git commit --allow-empty -m init
'

test_expect_success 'worktree prune on normal repo' '
	git worktree prune &&
	test_must_fail git worktree prune abc
'

test_expect_success 'prune files inside $GIT_DIR/worktrees' '
	mkdir .git/worktrees &&
	: >.git/worktrees/abc &&
	git worktree prune --verbose 2>actual &&
	cat >expect <<EOF &&
Removing worktrees/abc: not a valid directory
EOF
	test_cmp expect actual &&
	test_path_is_missing .git/worktrees/abc &&
	test_path_is_missing .git/worktrees
'

test_expect_success 'prune directories without gitdir' '
	mkdir -p .git/worktrees/def/abc &&
	: >.git/worktrees/def/def &&
	cat >expect <<EOF &&
Removing worktrees/def: gitdir file does not exist
EOF
	git worktree prune --verbose 2>actual &&
	test_cmp expect actual &&
	test_path_is_missing .git/worktrees/def &&
	test_path_is_missing .git/worktrees
'

test_expect_success SANITY 'prune directories with unreadable gitdir' '
	mkdir -p .git/worktrees/def/abc &&
	: >.git/worktrees/def/def &&
	: >.git/worktrees/def/gitdir &&
	chmod u-r .git/worktrees/def/gitdir &&
	git worktree prune --verbose 2>actual &&
	test_grep "Removing worktrees/def: unable to read gitdir file" actual &&
	test_path_is_missing .git/worktrees/def &&
	test_path_is_missing .git/worktrees
'

test_expect_success 'prune directories with invalid gitdir' '
	mkdir -p .git/worktrees/def/abc &&
	: >.git/worktrees/def/def &&
	: >.git/worktrees/def/gitdir &&
	git worktree prune --verbose 2>actual &&
	test_grep "Removing worktrees/def: invalid gitdir file" actual &&
	test_path_is_missing .git/worktrees/def &&
	test_path_is_missing .git/worktrees
'

test_expect_success 'prune directories with gitdir pointing to nowhere' '
	mkdir -p .git/worktrees/def/abc &&
	: >.git/worktrees/def/def &&
	echo "$(pwd)"/nowhere >.git/worktrees/def/gitdir &&
	git worktree prune --verbose 2>actual &&
	test_grep "Removing worktrees/def: gitdir file points to non-existent location" actual &&
	test_path_is_missing .git/worktrees/def &&
	test_path_is_missing .git/worktrees
'

test_expect_success 'not prune locked checkout' '
	test_when_finished rm -r .git/worktrees &&
	mkdir -p .git/worktrees/ghi &&
	: >.git/worktrees/ghi/locked &&
	git worktree prune &&
	test_path_is_dir .git/worktrees/ghi
'

test_expect_success 'not prune recent checkouts' '
	test_when_finished rm -r .git/worktrees &&
	git worktree add jlm HEAD &&
	test_path_is_dir .git/worktrees/jlm &&
	rm -rf jlm &&
	git worktree prune --verbose --expire=2.days.ago &&
	test_path_is_dir .git/worktrees/jlm
'

test_expect_success 'not prune proper checkouts' '
	test_when_finished rm -r .git/worktrees &&
	git worktree add --detach "$PWD/nop" main &&
	git worktree prune &&
	test_path_is_dir .git/worktrees/nop
'

test_expect_success 'prune duplicate (linked/linked)' '
	test_when_finished rm -fr .git/worktrees w1 w2 &&
	git worktree add --detach w1 &&
	git worktree add --detach w2 &&
	sed "s/w2/w1/" .git/worktrees/w2/gitdir >.git/worktrees/w2/gitdir.new &&
	mv .git/worktrees/w2/gitdir.new .git/worktrees/w2/gitdir &&
	git worktree prune --verbose 2>actual &&
	test_grep "duplicate entry" actual &&
	test_path_is_dir .git/worktrees/w1 &&
	test_path_is_missing .git/worktrees/w2
'

test_expect_success 'prune duplicate (main/linked)' '
	test_when_finished rm -fr repo wt &&
	test_create_repo repo &&
	test_commit -C repo x &&
	git -C repo worktree add --detach ../wt &&
	rm -fr wt &&
	mv repo wt &&
	git -C wt worktree prune --verbose 2>actual &&
	test_grep "duplicate entry" actual &&
	test_path_is_missing .git/worktrees/wt
'

test_expect_success 'prune invokes post-worktree remove event' '
	test_hook post-worktree <<-\EOF &&
	test "$#" = 4 || exit 1
	test "$1" = remove || exit 0
	printf "[%s][%s][%s][%s]\n" "$@" >hook.actual
	EOF
	git worktree add --detach flushed &&
	rm -rf flushed &&
	git worktree prune &&
	printf "[remove][flushed][%s][]\n" "$(pwd)/flushed" >hook.expect &&
	test_cmp hook.expect hook.actual
'

test_expect_success 'prune invokes post-worktree once per worktree' '
	test_hook post-worktree <<-\EOF &&
	test "$#" = 4 || exit 1
	test "$1" = remove || exit 0
	printf "[%s][%s][%s][%s]\n" "$@" >>hook.actual
	EOF
	git worktree add --detach first &&
	git worktree add --detach second &&
	rm -rf first second hook.actual &&
	git worktree prune &&
	{
		printf "[remove][first][%s][]\n" "$(pwd)/first" &&
		printf "[remove][second][%s][]\n" "$(pwd)/second"
	} >hook.expect &&
	sort hook.actual >hook.sorted &&
	test_cmp hook.expect hook.sorted
'

test_expect_success 'prune --dry-run does not invoke post-worktree hook' '
	git worktree add --detach dry &&
	rm -rf dry &&
	test_when_finished "git worktree prune" &&
	test_hook post-worktree <<-\EOF &&
	>hook.ran
	EOF
	git worktree prune --dry-run &&
	test_path_is_missing hook.ran
'

test_expect_success 'pruned entry with unknown path gives empty hook argument' '
	test_hook post-worktree <<-\EOF &&
	test "$#" = 4 &&
	printf "[%s][%s][%s][%s]\n" "$@" >hook.actual
	EOF
	mkdir -p .git/worktrees/broken &&
	: >.git/worktrees/broken/gitdir &&
	git worktree prune &&
	echo "[remove][broken][][]" >hook.expect &&
	test_cmp hook.expect hook.actual
'

test_expect_success 'failing post-worktree hook does not skip other pruned entries' '
	test_hook post-worktree <<-\EOF &&
	test "$1" = remove || exit 0
	echo "$2" >>hook.actual
	exit 1
	EOF
	git worktree add --detach doomed &&
	git worktree add --detach doomed2 &&
	rm -rf doomed doomed2 hook.actual &&
	test_must_fail git worktree prune &&
	test_path_is_missing .git/worktrees/doomed &&
	test_path_is_missing .git/worktrees/doomed2 &&
	test_write_lines doomed doomed2 >hook.expect &&
	sort hook.actual >hook.sorted &&
	test_cmp hook.expect hook.sorted
'

test_expect_success 'prune duplicate invokes post-worktree remove event' '
	test_when_finished rm -fr .git/worktrees w1 w2 &&
	test_hook post-worktree <<-\EOF &&
	test "$1" = remove || exit 0
	printf "[%s][%s][%s][%s]\n" "$@" >>hook.actual
	EOF
	rm -f hook.actual &&
	git worktree add --detach w1 &&
	git worktree add --detach w2 &&
	sed "s/w2/w1/" .git/worktrees/w2/gitdir >.git/worktrees/w2/gitdir.new &&
	mv .git/worktrees/w2/gitdir.new .git/worktrees/w2/gitdir &&
	git worktree prune &&
	printf "[remove][w2][%s][]\n" "$(pwd)/w1" >hook.expect &&
	test_cmp hook.expect hook.actual
'

test_expect_success 'post-worktree remove gets absolute path with relative worktrees' '
	test_when_finished "rm -rf relhook" &&
	git init relhook &&
	test_commit -C relhook base &&
	test_hook -C relhook post-worktree <<-\EOF &&
	test "$1" = remove || exit 0
	printf "[%s][%s][%s][%s]\n" "$@" >hook.actual
	EOF
	git -C relhook worktree add --relative-paths --detach wt &&
	rm -rf relhook/wt &&
	git -C relhook worktree prune &&
	printf "[remove][wt][%s][]\n" "$(pwd)/relhook/wt" >hook.expect &&
	test_cmp hook.expect relhook/hook.actual
'

test_expect_success 'not prune proper worktrees inside linked worktree with relative paths' '
	test_when_finished rm -rf repo wt_ext &&
	git init repo &&
	(
	    cd repo &&
	    git config worktree.useRelativePaths true &&
	    echo content >file &&
	    git add file &&
	    git commit -m msg &&
	    git worktree add ../wt_ext &&
	    git worktree add wt_int &&
	    cd wt_int &&
	    git worktree prune -v >out &&
	    test_must_be_empty out &&
	    cd ../../wt_ext &&
	    git worktree prune -v >out &&
	    test_must_be_empty out
	)
'

test_done
