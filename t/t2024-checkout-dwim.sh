#!/bin/sh

test_description='checkout <branch>

Ensures that checkout on an unborn branch does what the user expects'

. ./test-lib.sh

# Is the current branch "refs/heads/$1"?
test_branch () {
	printf "%s\n" "refs/heads/$1" >expect.HEAD &&
	but symbolic-ref HEAD >actual.HEAD &&
	test_cmp expect.HEAD actual.HEAD
}

# Is branch "refs/heads/$1" set to pull from "$2/$3"?
test_branch_upstream () {
	printf "%s\n" "$2" "refs/heads/$3" >expect.upstream &&
	{
		but config "branch.$1.remote" &&
		but config "branch.$1.merge"
	} >actual.upstream &&
	test_cmp expect.upstream actual.upstream
}

status_uno_is_clean () {
	but status -uno --porcelain >status.actual &&
	test_must_be_empty status.actual
}

test_expect_success 'setup' '
	test_cummit my_main &&
	but init repo_a &&
	(
		cd repo_a &&
		test_cummit a_main &&
		but checkout -b foo &&
		test_cummit a_foo &&
		but checkout -b bar &&
		test_cummit a_bar &&
		but checkout -b ambiguous_branch_and_file &&
		test_cummit a_ambiguous_branch_and_file
	) &&
	but init repo_b &&
	(
		cd repo_b &&
		test_cummit b_main &&
		but checkout -b foo &&
		test_cummit b_foo &&
		but checkout -b baz &&
		test_cummit b_baz &&
		but checkout -b ambiguous_branch_and_file &&
		test_cummit b_ambiguous_branch_and_file
	) &&
	but remote add repo_a repo_a &&
	but remote add repo_b repo_b &&
	but config remote.repo_b.fetch \
		"+refs/heads/*:refs/remotes/other_b/*" &&
	but fetch --all
'

test_expect_success 'checkout of non-existing branch fails' '
	but checkout -B main &&
	test_might_fail but branch -D xyzzy &&

	test_must_fail but checkout xyzzy &&
	status_uno_is_clean &&
	test_must_fail but rev-parse --verify refs/heads/xyzzy &&
	test_branch main
'

test_expect_success 'checkout of branch from multiple remotes fails #1' '
	but checkout -B main &&
	test_might_fail but branch -D foo &&

	test_must_fail but checkout foo &&
	status_uno_is_clean &&
	test_must_fail but rev-parse --verify refs/heads/foo &&
	test_branch main
'

test_expect_success 'when arg matches multiple remotes, do not fallback to interpreting as pathspec' '
	# create a file with name matching remote branch name
	but checkout -b t_ambiguous_branch_and_file &&
	>ambiguous_branch_and_file &&
	but add ambiguous_branch_and_file &&
	but cummit -m "ambiguous_branch_and_file" &&

	# modify file to verify that it will not be touched by checkout
	test_when_finished "but checkout -- ambiguous_branch_and_file" &&
	echo "file contents" >ambiguous_branch_and_file &&
	cp ambiguous_branch_and_file expect &&

	test_must_fail but checkout ambiguous_branch_and_file 2>err &&

	test_i18ngrep "matched multiple (2) remote tracking branches" err &&

	# file must not be altered
	test_cmp expect ambiguous_branch_and_file
'

test_expect_success 'checkout of branch from multiple remotes fails with advice' '
	but checkout -B main &&
	test_might_fail but branch -D foo &&
	test_must_fail but checkout foo 2>stderr &&
	test_branch main &&
	status_uno_is_clean &&
	test_i18ngrep "^hint: " stderr &&
	test_must_fail but -c advice.checkoutAmbiguousRemoteBranchName=false \
		checkout foo 2>stderr &&
	test_branch main &&
	status_uno_is_clean &&
	test_i18ngrep ! "^hint: " stderr
'

test_expect_success PERL 'checkout -p with multiple remotes does not print advice' '
	but checkout -B main &&
	test_might_fail but branch -D foo &&

	but checkout -p foo 2>stderr &&
	test_i18ngrep ! "^hint: " stderr &&
	status_uno_is_clean
'

test_expect_success 'checkout of branch from multiple remotes succeeds with checkout.defaultRemote #1' '
	but checkout -B main &&
	status_uno_is_clean &&
	test_might_fail but branch -D foo &&

	but -c checkout.defaultRemote=repo_a checkout foo &&
	status_uno_is_clean &&
	test_branch foo &&
	test_cmp_rev remotes/repo_a/foo HEAD &&
	test_branch_upstream foo repo_a foo
'

test_expect_success 'checkout of branch from a single remote succeeds #1' '
	but checkout -B main &&
	test_might_fail but branch -D bar &&

	but checkout bar &&
	status_uno_is_clean &&
	test_branch bar &&
	test_cmp_rev remotes/repo_a/bar HEAD &&
	test_branch_upstream bar repo_a bar
'

test_expect_success 'checkout of branch from a single remote succeeds #2' '
	but checkout -B main &&
	test_might_fail but branch -D baz &&

	but checkout baz &&
	status_uno_is_clean &&
	test_branch baz &&
	test_cmp_rev remotes/other_b/baz HEAD &&
	test_branch_upstream baz repo_b baz
'

test_expect_success '--no-guess suppresses branch auto-vivification' '
	but checkout -B main &&
	status_uno_is_clean &&
	test_might_fail but branch -D bar &&

	test_must_fail but checkout --no-guess bar &&
	test_must_fail but rev-parse --verify refs/heads/bar &&
	test_branch main
'

test_expect_success 'checkout.guess = false suppresses branch auto-vivification' '
	but checkout -B main &&
	status_uno_is_clean &&
	test_might_fail but branch -D bar &&

	test_config checkout.guess false &&
	test_must_fail but checkout bar &&
	test_must_fail but rev-parse --verify refs/heads/bar &&
	test_branch main
'

test_expect_success 'setup more remotes with unconventional refspecs' '
	but checkout -B main &&
	status_uno_is_clean &&
	but init repo_c &&
	(
		cd repo_c &&
		test_cummit c_main &&
		but checkout -b bar &&
		test_cummit c_bar &&
		but checkout -b spam &&
		test_cummit c_spam
	) &&
	but init repo_d &&
	(
		cd repo_d &&
		test_cummit d_main &&
		but checkout -b baz &&
		test_cummit d_baz &&
		but checkout -b eggs &&
		test_cummit d_eggs
	) &&
	but remote add repo_c repo_c &&
	but config remote.repo_c.fetch \
		"+refs/heads/*:refs/remotes/extra_dir/repo_c/extra_dir/*" &&
	but remote add repo_d repo_d &&
	but config remote.repo_d.fetch \
		"+refs/heads/*:refs/repo_d/*" &&
	but fetch --all
'

test_expect_success 'checkout of branch from multiple remotes fails #2' '
	but checkout -B main &&
	status_uno_is_clean &&
	test_might_fail but branch -D bar &&

	test_must_fail but checkout bar &&
	status_uno_is_clean &&
	test_must_fail but rev-parse --verify refs/heads/bar &&
	test_branch main
'

test_expect_success 'checkout of branch from multiple remotes fails #3' '
	but checkout -B main &&
	status_uno_is_clean &&
	test_might_fail but branch -D baz &&

	test_must_fail but checkout baz &&
	status_uno_is_clean &&
	test_must_fail but rev-parse --verify refs/heads/baz &&
	test_branch main
'

test_expect_success 'checkout of branch from a single remote succeeds #3' '
	but checkout -B main &&
	status_uno_is_clean &&
	test_might_fail but branch -D spam &&

	but checkout spam &&
	status_uno_is_clean &&
	test_branch spam &&
	test_cmp_rev refs/remotes/extra_dir/repo_c/extra_dir/spam HEAD &&
	test_branch_upstream spam repo_c spam
'

test_expect_success 'checkout of branch from a single remote succeeds #4' '
	but checkout -B main &&
	status_uno_is_clean &&
	test_might_fail but branch -D eggs &&

	but checkout eggs &&
	status_uno_is_clean &&
	test_branch eggs &&
	test_cmp_rev refs/repo_d/eggs HEAD &&
	test_branch_upstream eggs repo_d eggs
'

test_expect_success 'checkout of branch with a file having the same name fails' '
	but checkout -B main &&
	status_uno_is_clean &&
	test_might_fail but branch -D spam &&

	>spam &&
	test_must_fail but checkout spam &&
	status_uno_is_clean &&
	test_must_fail but rev-parse --verify refs/heads/spam &&
	test_branch main
'

test_expect_success 'checkout of branch with a file in subdir having the same name fails' '
	but checkout -B main &&
	status_uno_is_clean &&
	test_might_fail but branch -D spam &&

	>spam &&
	mkdir sub &&
	mv spam sub/spam &&
	test_must_fail but -C sub checkout spam &&
	status_uno_is_clean &&
	test_must_fail but rev-parse --verify refs/heads/spam &&
	test_branch main
'

test_expect_success 'checkout <branch> -- succeeds, even if a file with the same name exists' '
	but checkout -B main &&
	status_uno_is_clean &&
	test_might_fail but branch -D spam &&

	>spam &&
	but checkout spam -- &&
	status_uno_is_clean &&
	test_branch spam &&
	test_cmp_rev refs/remotes/extra_dir/repo_c/extra_dir/spam HEAD &&
	test_branch_upstream spam repo_c spam
'

test_expect_success 'loosely defined local base branch is reported correctly' '

	but checkout main &&
	status_uno_is_clean &&
	but branch strict &&
	but branch loose &&
	but cummit --allow-empty -m "a bit more" &&

	test_config branch.strict.remote . &&
	test_config branch.loose.remote . &&
	test_config branch.strict.merge refs/heads/main &&
	test_config branch.loose.merge main &&

	but checkout strict | sed -e "s/strict/BRANCHNAME/g" >expect &&
	status_uno_is_clean &&
	but checkout loose | sed -e "s/loose/BRANCHNAME/g" >actual &&
	status_uno_is_clean &&

	test_cmp expect actual
'

test_expect_success 'reject when arg could be part of dwim branch' '
	but remote add foo file://non-existent-place &&
	but update-ref refs/remotes/foo/dwim-arg HEAD &&
	echo foo >dwim-arg &&
	but add dwim-arg &&
	echo bar >dwim-arg &&
	test_must_fail but checkout dwim-arg &&
	test_must_fail but rev-parse refs/heads/dwim-arg -- &&
	grep bar dwim-arg
'

test_expect_success 'disambiguate dwim branch and checkout path (1)' '
	but update-ref refs/remotes/foo/dwim-arg1 HEAD &&
	echo foo >dwim-arg1 &&
	but add dwim-arg1 &&
	echo bar >dwim-arg1 &&
	but checkout -- dwim-arg1 &&
	test_must_fail but rev-parse refs/heads/dwim-arg1 -- &&
	grep foo dwim-arg1
'

test_expect_success 'disambiguate dwim branch and checkout path (2)' '
	but update-ref refs/remotes/foo/dwim-arg2 HEAD &&
	echo foo >dwim-arg2 &&
	but add dwim-arg2 &&
	echo bar >dwim-arg2 &&
	but checkout dwim-arg2 -- &&
	but rev-parse refs/heads/dwim-arg2 -- &&
	grep bar dwim-arg2
'

test_done
