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
	mkdir existing &&
	test_must_fail git checkout --detach --to existing master
'

test_expect_success 'checkout --to a new worktree' '
	git checkout --to here master &&
	(
		cd here &&
		test_cmp ../init.t init.t &&
		git symbolic-ref HEAD >actual &&
		echo refs/heads/master >expect &&
		test_cmp expect actual &&
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
		git checkout --to nested-here master &&
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

test_done
