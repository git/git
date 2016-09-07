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
	test_commit my_master &&
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

test_expect_success 'checkout of branch from multiple remotes fails #1' '
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

test_expect_success 'setup more remotes with unconventional refspecs' '
	git checkout -B master &&
	git init repo_c &&
	(
		cd repo_c &&
		test_commit c_master &&
		git checkout -b bar &&
		test_commit c_bar &&
		git checkout -b spam &&
		test_commit c_spam
	) &&
	git init repo_d &&
	(
		cd repo_d &&
		test_commit d_master &&
		git checkout -b baz &&
		test_commit d_baz &&
		git checkout -b eggs &&
		test_commit d_eggs
	) &&
	git remote add repo_c repo_c &&
	git config remote.repo_c.fetch \
		"+refs/heads/*:refs/remotes/extra_dir/repo_c/extra_dir/*" &&
	git remote add repo_d repo_d &&
	git config remote.repo_d.fetch \
		"+refs/heads/*:refs/repo_d/*" &&
	git fetch --all
'

test_expect_success 'checkout of branch from multiple remotes fails #2' '
	git checkout -B master &&
	test_might_fail git branch -D bar &&

	test_must_fail git checkout bar &&
	test_must_fail git rev-parse --verify refs/heads/bar &&
	test_branch master
'

test_expect_success 'checkout of branch from multiple remotes fails #3' '
	git checkout -B master &&
	test_might_fail git branch -D baz &&

	test_must_fail git checkout baz &&
	test_must_fail git rev-parse --verify refs/heads/baz &&
	test_branch master
'

test_expect_success 'checkout of branch from a single remote succeeds #3' '
	git checkout -B master &&
	test_might_fail git branch -D spam &&

	git checkout spam &&
	test_branch spam &&
	test_cmp_rev refs/remotes/extra_dir/repo_c/extra_dir/spam HEAD &&
	test_branch_upstream spam repo_c spam
'

test_expect_success 'checkout of branch from a single remote succeeds #4' '
	git checkout -B master &&
	test_might_fail git branch -D eggs &&

	git checkout eggs &&
	test_branch eggs &&
	test_cmp_rev refs/repo_d/eggs HEAD &&
	test_branch_upstream eggs repo_d eggs
'

test_expect_success 'checkout of branch with a file having the same name fails' '
	git checkout -B master &&
	test_might_fail git branch -D spam &&

	>spam &&
	test_must_fail git checkout spam &&
	test_must_fail git rev-parse --verify refs/heads/spam &&
	test_branch master
'

test_expect_success 'checkout of branch with a file in subdir having the same name fails' '
	git checkout -B master &&
	test_might_fail git branch -D spam &&

	>spam &&
	mkdir sub &&
	mv spam sub/spam &&
	test_must_fail git -C sub checkout spam &&
	test_must_fail git rev-parse --verify refs/heads/spam &&
	test_branch master
'

test_expect_success 'checkout <branch> -- succeeds, even if a file with the same name exists' '
	git checkout -B master &&
	test_might_fail git branch -D spam &&

	>spam &&
	git checkout spam -- &&
	test_branch spam &&
	test_cmp_rev refs/remotes/extra_dir/repo_c/extra_dir/spam HEAD &&
	test_branch_upstream spam repo_c spam
'

test_expect_success 'loosely defined local base branch is reported correctly' '

	git checkout master &&
	git branch strict &&
	git branch loose &&
	git commit --allow-empty -m "a bit more" &&

	test_config branch.strict.remote . &&
	test_config branch.loose.remote . &&
	test_config branch.strict.merge refs/heads/master &&
	test_config branch.loose.merge master &&

	git checkout strict | sed -e "s/strict/BRANCHNAME/g" >expect &&
	git checkout loose | sed -e "s/loose/BRANCHNAME/g" >actual &&

	test_cmp expect actual
'

test_done
