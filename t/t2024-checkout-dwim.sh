#!/bin/sh

test_description='checkout <branch>

Ensures that checkout on an unborn branch does what the user expects'

. ./test-lib.sh

# Is the current branch "refs/heads/$1"?
test_branch () {
	printf "%s\n" "refs/heads/$1" >expect.HEAD &&
	git symbolic-ref HEAD >actual.HEAD &&
	test_cmp expect.HEAD actual.HEAD
}

# Is branch "refs/heads/$1" set to pull from "$2/$3"?
test_branch_upstream () {
	printf "%s\n" "$2" "refs/heads/$3" >expect.upstream &&
	{
		git config "branch.$1.remote" &&
		git config "branch.$1.merge"
	} >actual.upstream &&
	test_cmp expect.upstream actual.upstream
}

test_expect_success 'setup' '
	git init repo_a &&
	(
		cd repo_a &&
		test_commit a_master &&
		git checkout -b foo &&
		test_commit a_foo &&
		git checkout -b bar &&
		test_commit a_bar
	) &&
	git init repo_b &&
	(
		cd repo_b &&
		test_commit b_master &&
		git checkout -b foo &&
		test_commit b_foo &&
		git checkout -b baz &&
		test_commit b_baz
	) &&
	git remote add repo_a repo_a &&
	git remote add repo_b repo_b &&
	git config remote.repo_b.fetch \
		"+refs/heads/*:refs/remotes/other_b/*" &&
	git fetch --all
'

test_expect_success 'checkout of non-existing branch fails' '
	git checkout -B master &&
	test_might_fail git branch -D xyzzy &&

	test_must_fail git checkout xyzzy &&
	test_must_fail git rev-parse --verify refs/heads/xyzzy &&
	test_branch master
'

test_expect_success 'checkout of branch from multiple remotes fails' '
	git checkout -B master &&
	test_might_fail git branch -D foo &&

	test_must_fail git checkout foo &&
	test_must_fail git rev-parse --verify refs/heads/foo &&
	test_branch master
'

test_expect_success 'checkout of branch from a single remote succeeds #1' '
	git checkout -B master &&
	test_might_fail git branch -D bar &&

	git checkout bar &&
	test_branch bar &&
	test_cmp_rev remotes/repo_a/bar HEAD &&
	test_branch_upstream bar repo_a bar
'

test_expect_success 'checkout of branch from a single remote succeeds #2' '
	git checkout -B master &&
	test_might_fail git branch -D baz &&

	git checkout baz &&
	test_branch baz &&
	test_cmp_rev remotes/other_b/baz HEAD &&
	test_branch_upstream baz repo_b baz
'

test_expect_success '--no-guess suppresses branch auto-vivification' '
	git checkout -B master &&
	test_might_fail git branch -D bar &&

	test_must_fail git checkout --no-guess bar &&
	test_must_fail git rev-parse --verify refs/heads/bar &&
	test_branch master
'

test_done
