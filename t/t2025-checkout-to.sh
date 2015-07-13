#!/bin/sh

test_description='test git checkout --to'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit init
'

test_expect_success 'checkout --to not updating paths' '
	test_must_fail git checkout --to -- init.t
'

test_expect_success 'checkout --to an existing worktree' '
	mkdir -p existing/subtree &&
	test_must_fail git checkout --detach --to existing master
'

test_expect_success 'checkout --to an existing empty worktree' '
	mkdir existing_empty &&
	git checkout --detach --to existing_empty master
'

test_expect_success 'checkout --to refuses to checkout locked branch' '
	test_must_fail git checkout --to zere master &&
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

test_expect_success 'checkout --to a new worktree' '
	git rev-parse HEAD >expect &&
	git checkout --detach --to here master &&
	(
		cd here &&
		test_cmp ../init.t init.t &&
		test_must_fail git symbolic-ref HEAD &&
		git rev-parse HEAD >actual &&
		test_cmp ../expect actual &&
		git fsck
	)
'

test_expect_success 'checkout --to a new worktree from a subdir' '
	(
		mkdir sub &&
		cd sub &&
		git checkout --detach --to here master &&
		cd here &&
		test_cmp ../../init.t init.t
	)
'

test_expect_success 'checkout --to from a linked checkout' '
	(
		cd here &&
		git checkout --detach --to nested-here master &&
		cd nested-here &&
		git fsck
	)
'

test_expect_success 'checkout --to a new worktree creating new branch' '
	git checkout --to there -b newmaster master &&
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

test_expect_success 'not die the same branch is already checked out' '
	(
		cd here &&
		git checkout --ignore-other-worktrees --to anothernewmaster newmaster
	)
'

test_expect_success 'not die on re-checking out current branch' '
	(
		cd there &&
		git checkout newmaster
	)
'

test_expect_success 'checkout --to from a bare repo' '
	(
		git clone --bare . bare &&
		cd bare &&
		git checkout --to ../there2 -b bare-master master
	)
'

test_expect_success 'checkout from a bare repo without --to' '
	(
		cd bare &&
		test_must_fail git checkout master
	)
'

test_expect_success 'checkout with grafts' '
	test_when_finished rm .git/info/grafts &&
	test_commit abc &&
	SHA1=`git rev-parse HEAD` &&
	test_commit def &&
	test_commit xyz &&
	echo "`git rev-parse HEAD` $SHA1" >.git/info/grafts &&
	cat >expected <<-\EOF &&
	xyz
	abc
	EOF
	git log --format=%s -2 >actual &&
	test_cmp expected actual &&
	git checkout --detach --to grafted master &&
	git --git-dir=grafted/.git log --format=%s -2 >actual &&
	test_cmp expected actual
'

test_done
