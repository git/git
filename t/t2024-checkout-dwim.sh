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

status_uno_is_clean () {
	git status -uno --porcelain >status.actual &&
	test_must_be_empty status.actual
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
		test_commit a_bar &&
		git checkout -b ambiguous_branch_and_file &&
		test_commit a_ambiguous_branch_and_file
	) &&
	git init repo_b &&
	(
		cd repo_b &&
		test_commit b_master &&
		git checkout -b foo &&
		test_commit b_foo &&
		git checkout -b baz &&
		test_commit b_baz &&
		git checkout -b ambiguous_branch_and_file &&
		test_commit b_ambiguous_branch_and_file
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
	status_uno_is_clean &&
	test_must_fail git rev-parse --verify refs/heads/xyzzy &&
	test_branch master
'

test_expect_success 'checkout of branch from multiple remotes fails #1' '
	git checkout -B master &&
	test_might_fail git branch -D foo &&

	test_must_fail git checkout foo &&
	status_uno_is_clean &&
	test_must_fail git rev-parse --verify refs/heads/foo &&
	test_branch master
'

test_expect_success 'when arg matches multiple remotes, do not fallback to interpreting as pathspec' '
	# create a file with name matching remote branch name
	git checkout -b t_ambiguous_branch_and_file &&
	>ambiguous_branch_and_file &&
	git add ambiguous_branch_and_file &&
	git commit -m "ambiguous_branch_and_file" &&

	# modify file to verify that it will not be touched by checkout
	test_when_finished "git checkout -- ambiguous_branch_and_file" &&
	echo "file contents" >ambiguous_branch_and_file &&
	cp ambiguous_branch_and_file expect &&

	test_must_fail git checkout ambiguous_branch_and_file 2>err &&

	test_i18ngrep "matched multiple (2) remote tracking branches" err &&

	# file must not be altered
	test_cmp expect ambiguous_branch_and_file
'

test_expect_success 'checkout of branch from multiple remotes fails with advice' '
	git checkout -B master &&
	test_might_fail git branch -D foo &&
	test_must_fail git checkout foo 2>stderr &&
	test_branch master &&
	status_uno_is_clean &&
	test_i18ngrep "^hint: " stderr &&
	test_must_fail git -c advice.checkoutAmbiguousRemoteBranchName=false \
		checkout foo 2>stderr &&
	test_branch master &&
	status_uno_is_clean &&
	test_i18ngrep ! "^hint: " stderr
'

test_expect_success PERL 'checkout -p with multiple remotes does not print advice' '
	git checkout -B master &&
	test_might_fail git branch -D foo &&

	git checkout -p foo 2>stderr &&
	test_i18ngrep ! "^hint: " stderr &&
	status_uno_is_clean
'

test_expect_success 'checkout of branch from multiple remotes succeeds with checkout.defaultRemote #1' '
	git checkout -B master &&
	status_uno_is_clean &&
	test_might_fail git branch -D foo &&

	git -c checkout.defaultRemote=repo_a checkout foo &&
	status_uno_is_clean &&
	test_branch foo &&
	test_cmp_rev remotes/repo_a/foo HEAD &&
	test_branch_upstream foo repo_a foo
'

test_expect_success 'checkout of branch from a single remote succeeds #1' '
	git checkout -B master &&
	test_might_fail git branch -D bar &&

	git checkout bar &&
	status_uno_is_clean &&
	test_branch bar &&
	test_cmp_rev remotes/repo_a/bar HEAD &&
	test_branch_upstream bar repo_a bar
'

test_expect_success 'checkout of branch from a single remote succeeds #2' '
	git checkout -B master &&
	test_might_fail git branch -D baz &&

	git checkout baz &&
	status_uno_is_clean &&
	test_branch baz &&
	test_cmp_rev remotes/other_b/baz HEAD &&
	test_branch_upstream baz repo_b baz
'

test_expect_success '--no-guess suppresses branch auto-vivification' '
	git checkout -B master &&
	status_uno_is_clean &&
	test_might_fail git branch -D bar &&

	test_must_fail git checkout --no-guess bar &&
	test_must_fail git rev-parse --verify refs/heads/bar &&
	test_branch master
'

test_expect_success 'setup more remotes with unconventional refspecs' '
	git checkout -B master &&
	status_uno_is_clean &&
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
	status_uno_is_clean &&
	test_might_fail git branch -D bar &&

	test_must_fail git checkout bar &&
	status_uno_is_clean &&
	test_must_fail git rev-parse --verify refs/heads/bar &&
	test_branch master
'

test_expect_success 'checkout of branch from multiple remotes fails #3' '
	git checkout -B master &&
	status_uno_is_clean &&
	test_might_fail git branch -D baz &&

	test_must_fail git checkout baz &&
	status_uno_is_clean &&
	test_must_fail git rev-parse --verify refs/heads/baz &&
	test_branch master
'

test_expect_success 'checkout of branch from a single remote succeeds #3' '
	git checkout -B master &&
	status_uno_is_clean &&
	test_might_fail git branch -D spam &&

	git checkout spam &&
	status_uno_is_clean &&
	test_branch spam &&
	test_cmp_rev refs/remotes/extra_dir/repo_c/extra_dir/spam HEAD &&
	test_branch_upstream spam repo_c spam
'

test_expect_success 'checkout of branch from a single remote succeeds #4' '
	git checkout -B master &&
	status_uno_is_clean &&
	test_might_fail git branch -D eggs &&

	git checkout eggs &&
	status_uno_is_clean &&
	test_branch eggs &&
	test_cmp_rev refs/repo_d/eggs HEAD &&
	test_branch_upstream eggs repo_d eggs
'

test_expect_success 'checkout of branch with a file having the same name fails' '
	git checkout -B master &&
	status_uno_is_clean &&
	test_might_fail git branch -D spam &&

	>spam &&
	test_must_fail git checkout spam &&
	status_uno_is_clean &&
	test_must_fail git rev-parse --verify refs/heads/spam &&
	test_branch master
'

test_expect_success 'checkout of branch with a file in subdir having the same name fails' '
	git checkout -B master &&
	status_uno_is_clean &&
	test_might_fail git branch -D spam &&

	>spam &&
	mkdir sub &&
	mv spam sub/spam &&
	test_must_fail git -C sub checkout spam &&
	status_uno_is_clean &&
	test_must_fail git rev-parse --verify refs/heads/spam &&
	test_branch master
'

test_expect_success 'checkout <branch> -- succeeds, even if a file with the same name exists' '
	git checkout -B master &&
	status_uno_is_clean &&
	test_might_fail git branch -D spam &&

	>spam &&
	git checkout spam -- &&
	status_uno_is_clean &&
	test_branch spam &&
	test_cmp_rev refs/remotes/extra_dir/repo_c/extra_dir/spam HEAD &&
	test_branch_upstream spam repo_c spam
'

test_expect_success 'loosely defined local base branch is reported correctly' '

	git checkout master &&
	status_uno_is_clean &&
	git branch strict &&
	git branch loose &&
	git commit --allow-empty -m "a bit more" &&

	test_config branch.strict.remote . &&
	test_config branch.loose.remote . &&
	test_config branch.strict.merge refs/heads/master &&
	test_config branch.loose.merge master &&

	git checkout strict | sed -e "s/strict/BRANCHNAME/g" >expect &&
	status_uno_is_clean &&
	git checkout loose | sed -e "s/loose/BRANCHNAME/g" >actual &&
	status_uno_is_clean &&

	test_cmp expect actual
'

test_expect_success 'reject when arg could be part of dwim branch' '
	git remote add foo file://non-existent-place &&
	git update-ref refs/remotes/foo/dwim-arg HEAD &&
	echo foo >dwim-arg &&
	git add dwim-arg &&
	echo bar >dwim-arg &&
	test_must_fail git checkout dwim-arg &&
	test_must_fail git rev-parse refs/heads/dwim-arg -- &&
	grep bar dwim-arg
'

test_expect_success 'disambiguate dwim branch and checkout path (1)' '
	git update-ref refs/remotes/foo/dwim-arg1 HEAD &&
	echo foo >dwim-arg1 &&
	git add dwim-arg1 &&
	echo bar >dwim-arg1 &&
	git checkout -- dwim-arg1 &&
	test_must_fail git rev-parse refs/heads/dwim-arg1 -- &&
	grep foo dwim-arg1
'

test_expect_success 'disambiguate dwim branch and checkout path (2)' '
	git update-ref refs/remotes/foo/dwim-arg2 HEAD &&
	echo foo >dwim-arg2 &&
	git add dwim-arg2 &&
	echo bar >dwim-arg2 &&
	git checkout dwim-arg2 -- &&
	git rev-parse refs/heads/dwim-arg2 -- &&
	grep bar dwim-arg2
'

test_done
