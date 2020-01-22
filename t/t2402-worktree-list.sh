#!/bin/sh

test_description='test git worktree list'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit init
'

test_expect_success 'rev-parse --git-common-dir on main worktree' '
	git rev-parse --git-common-dir >actual &&
	echo .git >expected &&
	test_cmp expected actual &&
	mkdir sub &&
	git -C sub rev-parse --git-common-dir >actual2 &&
	echo ../.git >expected2 &&
	test_cmp expected2 actual2
'

test_expect_success 'rev-parse --git-path objects linked worktree' '
	echo "$(git rev-parse --show-toplevel)/.git/objects" >expect &&
	test_when_finished "rm -rf linked-tree actual expect && git worktree prune" &&
	git worktree add --detach linked-tree master &&
	git -C linked-tree rev-parse --git-path objects >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees from main' '
	echo "$(git rev-parse --show-toplevel) $(git rev-parse --short HEAD) [$(git symbolic-ref --short HEAD)]" >expect &&
	test_when_finished "rm -rf here out actual expect && git worktree prune" &&
	git worktree add --detach here master &&
	echo "$(git -C here rev-parse --show-toplevel) $(git rev-parse --short HEAD) (detached HEAD)" >>expect &&
	git worktree list >out &&
	sed "s/  */ /g" <out >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees from linked' '
	echo "$(git rev-parse --show-toplevel) $(git rev-parse --short HEAD) [$(git symbolic-ref --short HEAD)]" >expect &&
	test_when_finished "rm -rf here out actual expect && git worktree prune" &&
	git worktree add --detach here master &&
	echo "$(git -C here rev-parse --show-toplevel) $(git rev-parse --short HEAD) (detached HEAD)" >>expect &&
	git -C here worktree list >out &&
	sed "s/  */ /g" <out >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees --porcelain' '
	echo "worktree $(git rev-parse --show-toplevel)" >expect &&
	echo "HEAD $(git rev-parse HEAD)" >>expect &&
	echo "branch $(git symbolic-ref HEAD)" >>expect &&
	echo >>expect &&
	test_when_finished "rm -rf here actual expect && git worktree prune" &&
	git worktree add --detach here master &&
	echo "worktree $(git -C here rev-parse --show-toplevel)" >>expect &&
	echo "HEAD $(git rev-parse HEAD)" >>expect &&
	echo "detached" >>expect &&
	echo >>expect &&
	git worktree list --porcelain >actual &&
	test_cmp expect actual
'

test_expect_success 'bare repo setup' '
	git init --bare bare1 &&
	echo "data" >file1 &&
	git add file1 &&
	git commit -m"File1: add data" &&
	git push bare1 master &&
	git reset --hard HEAD^
'

test_expect_success '"list" all worktrees from bare main' '
	test_when_finished "rm -rf there out actual expect && git -C bare1 worktree prune" &&
	git -C bare1 worktree add --detach ../there master &&
	echo "$(pwd)/bare1 (bare)" >expect &&
	echo "$(git -C there rev-parse --show-toplevel) $(git -C there rev-parse --short HEAD) (detached HEAD)" >>expect &&
	git -C bare1 worktree list >out &&
	sed "s/  */ /g" <out >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees --porcelain from bare main' '
	test_when_finished "rm -rf there actual expect && git -C bare1 worktree prune" &&
	git -C bare1 worktree add --detach ../there master &&
	echo "worktree $(pwd)/bare1" >expect &&
	echo "bare" >>expect &&
	echo >>expect &&
	echo "worktree $(git -C there rev-parse --show-toplevel)" >>expect &&
	echo "HEAD $(git -C there rev-parse HEAD)" >>expect &&
	echo "detached" >>expect &&
	echo >>expect &&
	git -C bare1 worktree list --porcelain >actual &&
	test_cmp expect actual
'

test_expect_success '"list" all worktrees from linked with a bare main' '
	test_when_finished "rm -rf there out actual expect && git -C bare1 worktree prune" &&
	git -C bare1 worktree add --detach ../there master &&
	echo "$(pwd)/bare1 (bare)" >expect &&
	echo "$(git -C there rev-parse --show-toplevel) $(git -C there rev-parse --short HEAD) (detached HEAD)" >>expect &&
	git -C there worktree list >out &&
	sed "s/  */ /g" <out >actual &&
	test_cmp expect actual
'

test_expect_success 'bare repo cleanup' '
	rm -rf bare1
'

test_expect_success 'broken main worktree still at the top' '
	git init broken-main &&
	(
		cd broken-main &&
		test_commit new &&
		git worktree add linked &&
		cat >expected <<-EOF &&
		worktree $(pwd)
		HEAD $ZERO_OID

		EOF
		cd linked &&
		echo "worktree $(pwd)" >expected &&
		echo "ref: .broken" >../.git/HEAD &&
		git worktree list --porcelain >out &&
		head -n 3 out >actual &&
		test_cmp ../expected actual &&
		git worktree list >out &&
		head -n 1 out >actual.2 &&
		grep -F "(error)" actual.2
	)
'

test_expect_success 'linked worktrees are sorted' '
	mkdir sorted &&
	git init sorted/main &&
	(
		cd sorted/main &&
		test_tick &&
		test_commit new &&
		git worktree add ../first &&
		git worktree add ../second &&
		git worktree list --porcelain >out &&
		grep ^worktree out >actual
	) &&
	cat >expected <<-EOF &&
	worktree $(pwd)/sorted/main
	worktree $(pwd)/sorted/first
	worktree $(pwd)/sorted/second
	EOF
	test_cmp expected sorted/main/actual
'

test_done
