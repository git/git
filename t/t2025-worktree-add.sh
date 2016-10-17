#!/bin/sh

test_description='test git worktree add'

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

test_expect_success 'setup' '
	test_commit init
'

test_expect_success '"add" an existing worktree' '
	mkdir -p existing/subtree &&
	test_must_fail git worktree add --detach existing master
'

test_expect_success '"add" an existing empty worktree' '
	mkdir existing_empty &&
	git worktree add --detach existing_empty master
'

test_expect_success '"add" using shorthand - fails when no previous branch' '
	test_must_fail git worktree add existing_short -
'

test_expect_success '"add" using - shorthand' '
	git checkout -b newbranch &&
	echo hello >myworld &&
	git add myworld &&
	git commit -m myworld &&
	git checkout master &&
	git worktree add short-hand - &&
	echo refs/heads/newbranch >expect &&
	git -C short-hand rev-parse --symbolic-full-name HEAD >actual &&
	test_cmp expect actual
'

test_expect_success '"add" refuses to checkout locked branch' '
	test_must_fail git worktree add zere master &&
	! test -d zere &&
	! test -d .git/worktrees/zere
'

test_expect_success 'checking out paths not complaining about linked checkouts' '
	(
	cd existing_empty &&
	echo dirty >>init.t &&
	git checkout master -- init.t
	)
'

test_expect_success '"add" worktree' '
	git rev-parse HEAD >expect &&
	git worktree add --detach here master &&
	(
		cd here &&
		test_cmp ../init.t init.t &&
		test_must_fail git symbolic-ref HEAD &&
		git rev-parse HEAD >actual &&
		test_cmp ../expect actual &&
		git fsck
	)
'

test_expect_success '"add" worktree from a subdir' '
	(
		mkdir sub &&
		cd sub &&
		git worktree add --detach here master &&
		cd here &&
		test_cmp ../../init.t init.t
	)
'

test_expect_success '"add" from a linked checkout' '
	(
		cd here &&
		git worktree add --detach nested-here master &&
		cd nested-here &&
		git fsck
	)
'

test_expect_success '"add" worktree creating new branch' '
	git worktree add -b newmaster there master &&
	(
		cd there &&
		test_cmp ../init.t init.t &&
		git symbolic-ref HEAD >actual &&
		echo refs/heads/newmaster >expect &&
		test_cmp expect actual &&
		git fsck
	)
'

test_expect_success 'die the same branch is already checked out' '
	(
		cd here &&
		test_must_fail git checkout newmaster
	)
'

test_expect_success SYMLINKS 'die the same branch is already checked out (symlink)' '
	head=$(git -C there rev-parse --git-path HEAD) &&
	ref=$(git -C there symbolic-ref HEAD) &&
	rm "$head" &&
	ln -s "$ref" "$head" &&
	test_must_fail git -C here checkout newmaster
'

test_expect_success 'not die the same branch is already checked out' '
	(
		cd here &&
		git worktree add --force anothernewmaster newmaster
	)
'

test_expect_success 'not die on re-checking out current branch' '
	(
		cd there &&
		git checkout newmaster
	)
'

test_expect_success '"add" from a bare repo' '
	(
		git clone --bare . bare &&
		cd bare &&
		git worktree add -b bare-master ../there2 master
	)
'

test_expect_success 'checkout from a bare repo without "add"' '
	(
		cd bare &&
		test_must_fail git checkout master
	)
'

test_expect_success '"add" default branch of a bare repo' '
	(
		git clone --bare . bare2 &&
		cd bare2 &&
		git worktree add ../there3 master
	)
'

test_expect_success 'checkout with grafts' '
	test_when_finished rm .git/info/grafts &&
	test_commit abc &&
	SHA1=$(git rev-parse HEAD) &&
	test_commit def &&
	test_commit xyz &&
	echo "$(git rev-parse HEAD) $SHA1" >.git/info/grafts &&
	cat >expected <<-\EOF &&
	xyz
	abc
	EOF
	git log --format=%s -2 >actual &&
	test_cmp expected actual &&
	git worktree add --detach grafted master &&
	git --git-dir=grafted/.git log --format=%s -2 >actual &&
	test_cmp expected actual
'

test_expect_success '"add" from relative HEAD' '
	test_commit a &&
	test_commit b &&
	test_commit c &&
	git rev-parse HEAD~1 >expected &&
	git worktree add relhead HEAD~1 &&
	git -C relhead rev-parse HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '"add -b" with <branch> omitted' '
	git worktree add -b burble flornk &&
	test_cmp_rev HEAD burble
'

test_expect_success '"add --detach" with <branch> omitted' '
	git worktree add --detach fishhook &&
	git rev-parse HEAD >expected &&
	git -C fishhook rev-parse HEAD >actual &&
	test_cmp expected actual &&
	test_must_fail git -C fishhook symbolic-ref HEAD
'

test_expect_success '"add" with <branch> omitted' '
	git worktree add wiffle/bat &&
	test_cmp_rev HEAD bat
'

test_expect_success '"add" auto-vivify does not clobber existing branch' '
	test_commit c1 &&
	test_commit c2 &&
	git branch precious HEAD~1 &&
	test_must_fail git worktree add precious &&
	test_cmp_rev HEAD~1 precious &&
	test_path_is_missing precious
'

test_expect_success '"add" no auto-vivify with --detach and <branch> omitted' '
	git worktree add --detach mish/mash &&
	test_must_fail git rev-parse mash -- &&
	test_must_fail git -C mish/mash symbolic-ref HEAD
'

test_expect_success '"add" -b/-B mutually exclusive' '
	test_must_fail git worktree add -b poodle -B poodle bamboo master
'

test_expect_success '"add" -b/--detach mutually exclusive' '
	test_must_fail git worktree add -b poodle --detach bamboo master
'

test_expect_success '"add" -B/--detach mutually exclusive' '
	test_must_fail git worktree add -B poodle --detach bamboo master
'

test_expect_success '"add -B" fails if the branch is checked out' '
	git rev-parse newmaster >before &&
	test_must_fail git worktree add -B newmaster bamboo master &&
	git rev-parse newmaster >after &&
	test_cmp before after
'

test_expect_success 'add -B' '
	git worktree add -B poodle bamboo2 master^ &&
	git -C bamboo2 symbolic-ref HEAD >actual &&
	echo refs/heads/poodle >expected &&
	test_cmp expected actual &&
	test_cmp_rev master^ poodle
'

test_expect_success 'local clone from linked checkout' '
	git clone --local here here-clone &&
	( cd here-clone && git fsck )
'

test_expect_success '"add" worktree with --no-checkout' '
	git worktree add --no-checkout -b swamp swamp &&
	! test -e swamp/init.t &&
	git -C swamp reset --hard &&
	test_cmp init.t swamp/init.t
'

test_expect_success '"add" worktree with --checkout' '
	git worktree add --checkout -b swmap2 swamp2 &&
	test_cmp init.t swamp2/init.t
'

test_expect_success 'put a worktree under rebase' '
	git worktree add under-rebase &&
	(
		cd under-rebase &&
		set_fake_editor &&
		FAKE_LINES="edit 1" git rebase -i HEAD^ &&
		git worktree list | grep "under-rebase.*detached HEAD"
	)
'

test_expect_success 'add a worktree, checking out a rebased branch' '
	test_must_fail git worktree add new-rebase under-rebase &&
	! test -d new-rebase
'

test_expect_success 'checking out a rebased branch from another worktree' '
	git worktree add new-place &&
	test_must_fail git -C new-place checkout under-rebase
'

test_expect_success 'not allow to delete a branch under rebase' '
	(
		cd under-rebase &&
		test_must_fail git branch -D under-rebase
	)
'

test_expect_success 'rename a branch under rebase not allowed' '
	test_must_fail git branch -M under-rebase rebase-with-new-name
'

test_expect_success 'check out from current worktree branch ok' '
	(
		cd under-rebase &&
		git checkout under-rebase &&
		git checkout - &&
		git rebase --abort
	)
'

test_expect_success 'checkout a branch under bisect' '
	git worktree add under-bisect &&
	(
		cd under-bisect &&
		git bisect start &&
		git bisect bad &&
		git bisect good HEAD~2 &&
		git worktree list | grep "under-bisect.*detached HEAD" &&
		test_must_fail git worktree add new-bisect under-bisect &&
		! test -d new-bisect
	)
'

test_expect_success 'rename a branch under bisect not allowed' '
	test_must_fail git branch -M under-bisect bisect-with-new-name
'

test_done
