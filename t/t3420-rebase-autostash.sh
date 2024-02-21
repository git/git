#!/bin/sh
#
# Copyright (c) 2013 Ramkumar Ramachandra
#

test_description='git rebase --autostash tests'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	echo hello-world >file0 &&
	git add . &&
	test_tick &&
	git commit -m "initial commit" &&
	git checkout -b feature-branch &&
	echo another-hello >file1 &&
	echo goodbye >file2 &&
	git add . &&
	test_tick &&
	git commit -m "second commit" &&
	echo final-goodbye >file3 &&
	git add . &&
	test_tick &&
	git commit -m "third commit" &&
	git checkout -b unrelated-onto-branch main &&
	echo unrelated >file4 &&
	git add . &&
	test_tick &&
	git commit -m "unrelated commit" &&
	git checkout -b related-onto-branch main &&
	echo conflicting-change >file2 &&
	git add . &&
	test_tick &&
	git commit -m "related commit" &&
	remove_progress_re="$(printf "s/.*\\r//")"
'

create_expected_success_apply () {
	cat >expected <<-EOF
	$(grep "^Created autostash: [0-9a-f][0-9a-f]*\$" actual)
	First, rewinding head to replay your work on top of it...
	Applying: second commit
	Applying: third commit
	Applied autostash.
	EOF
}

create_expected_success_merge () {
	q_to_cr >expected <<-EOF
	$(grep "^Created autostash: [0-9a-f][0-9a-f]*\$" actual)
	Applied autostash.
	Successfully rebased and updated refs/heads/rebased-feature-branch.
	EOF
}

create_expected_failure_apply () {
	cat >expected <<-EOF
	$(grep "^Created autostash: [0-9a-f][0-9a-f]*\$" actual)
	First, rewinding head to replay your work on top of it...
	Applying: second commit
	Applying: third commit
	Applying autostash resulted in conflicts.
	Your changes are safe in the stash.
	You can run "git stash pop" or "git stash drop" at any time.
	EOF
}

create_expected_failure_merge () {
	cat >expected <<-EOF
	$(grep "^Created autostash: [0-9a-f][0-9a-f]*\$" actual)
	Applying autostash resulted in conflicts.
	Your changes are safe in the stash.
	You can run "git stash pop" or "git stash drop" at any time.
	Successfully rebased and updated refs/heads/rebased-feature-branch.
	EOF
}

testrebase () {
	type=$1
	dotest=$2

	test_expect_success "rebase$type: dirty worktree, --no-autostash" '
		test_config rebase.autostash true &&
		git reset --hard &&
		git checkout -b rebased-feature-branch feature-branch &&
		test_when_finished git branch -D rebased-feature-branch &&
		test_when_finished git checkout feature-branch &&
		echo dirty >>file3 &&
		test_must_fail git rebase$type --no-autostash unrelated-onto-branch
	'

	test_expect_success "rebase$type: dirty worktree, non-conflicting rebase" '
		test_config rebase.autostash true &&
		git reset --hard &&
		git checkout -b rebased-feature-branch feature-branch &&
		echo dirty >>file3 &&
		git rebase$type unrelated-onto-branch >actual 2>&1 &&
		grep unrelated file4 &&
		grep dirty file3 &&
		git checkout feature-branch
	'

	test_expect_success "rebase$type --autostash: check output" '
		test_when_finished git branch -D rebased-feature-branch &&
		suffix=${type#\ --} && suffix=${suffix:-apply} &&
		if test ${suffix} = "interactive"; then
			suffix=merge
		fi &&
		create_expected_success_$suffix &&
		sed "$remove_progress_re" <actual >actual2 &&
		test_cmp expected actual2
	'

	test_expect_success "rebase$type: dirty index, non-conflicting rebase" '
		test_config rebase.autostash true &&
		git reset --hard &&
		git checkout -b rebased-feature-branch feature-branch &&
		test_when_finished git branch -D rebased-feature-branch &&
		echo dirty >>file3 &&
		git add file3 &&
		git rebase$type unrelated-onto-branch &&
		grep unrelated file4 &&
		grep dirty file3 &&
		git checkout feature-branch
	'

	test_expect_success "rebase$type: conflicting rebase" '
		test_config rebase.autostash true &&
		git reset --hard &&
		git checkout -b rebased-feature-branch feature-branch &&
		test_when_finished git branch -D rebased-feature-branch &&
		echo dirty >>file3 &&
		test_must_fail git rebase$type related-onto-branch &&
		test_path_is_file $dotest/autostash &&
		test_path_is_missing file3 &&
		rm -rf $dotest &&
		git reset --hard &&
		git checkout feature-branch
	'

	test_expect_success "rebase$type: --continue" '
		test_config rebase.autostash true &&
		git reset --hard &&
		git checkout -b rebased-feature-branch feature-branch &&
		test_when_finished git branch -D rebased-feature-branch &&
		echo dirty >>file3 &&
		test_must_fail git rebase$type related-onto-branch &&
		test_path_is_file $dotest/autostash &&
		test_path_is_missing file3 &&
		echo "conflicting-plus-goodbye" >file2 &&
		git add file2 &&
		git rebase --continue &&
		test_path_is_missing $dotest/autostash &&
		grep dirty file3 &&
		git checkout feature-branch
	'

	test_expect_success "rebase$type: --skip" '
		test_config rebase.autostash true &&
		git reset --hard &&
		git checkout -b rebased-feature-branch feature-branch &&
		test_when_finished git branch -D rebased-feature-branch &&
		echo dirty >>file3 &&
		test_must_fail git rebase$type related-onto-branch &&
		test_path_is_file $dotest/autostash &&
		test_path_is_missing file3 &&
		git rebase --skip &&
		test_path_is_missing $dotest/autostash &&
		grep dirty file3 &&
		git checkout feature-branch
	'

	test_expect_success "rebase$type: --abort" '
		test_config rebase.autostash true &&
		git reset --hard &&
		git checkout -b rebased-feature-branch feature-branch &&
		test_when_finished git branch -D rebased-feature-branch &&
		echo dirty >>file3 &&
		test_must_fail git rebase$type related-onto-branch &&
		test_path_is_file $dotest/autostash &&
		test_path_is_missing file3 &&
		git rebase --abort &&
		test_path_is_missing $dotest/autostash &&
		grep dirty file3 &&
		git checkout feature-branch
	'

	test_expect_success "rebase$type: --quit" '
		test_config rebase.autostash true &&
		git reset --hard &&
		git checkout -b rebased-feature-branch feature-branch &&
		test_when_finished git branch -D rebased-feature-branch &&
		echo dirty >>file3 &&
		git diff >expect &&
		test_must_fail git rebase$type related-onto-branch &&
		test_path_is_file $dotest/autostash &&
		test_path_is_missing file3 &&
		git rebase --quit &&
		test_when_finished git stash drop &&
		test_path_is_missing $dotest/autostash &&
		! grep dirty file3 &&
		git stash show -p >actual &&
		test_cmp expect actual &&
		git reset --hard &&
		git checkout feature-branch
	'

	test_expect_success "rebase$type: non-conflicting rebase, conflicting stash" '
		test_config rebase.autostash true &&
		git reset --hard &&
		git checkout -b rebased-feature-branch feature-branch &&
		echo dirty >file4 &&
		git add file4 &&
		git rebase$type unrelated-onto-branch >actual 2>&1 &&
		test_path_is_missing $dotest &&
		git reset --hard &&
		grep unrelated file4 &&
		! grep dirty file4 &&
		git checkout feature-branch &&
		git stash pop &&
		grep dirty file4
	'

	test_expect_success "rebase$type: check output with conflicting stash" '
		test_when_finished git branch -D rebased-feature-branch &&
		suffix=${type#\ --} && suffix=${suffix:-apply} &&
		if test ${suffix} = "interactive"; then
			suffix=merge
		fi &&
		create_expected_failure_$suffix &&
		sed "$remove_progress_re" <actual >actual2 &&
		test_cmp expected actual2
	'
}

test_expect_success "rebase: fast-forward rebase" '
	test_config rebase.autostash true &&
	git reset --hard &&
	git checkout -b behind-feature-branch feature-branch~1 &&
	test_when_finished git branch -D behind-feature-branch &&
	echo dirty >>file1 &&
	git rebase feature-branch &&
	grep dirty file1 &&
	git checkout feature-branch
'

test_expect_success "rebase: noop rebase" '
	test_config rebase.autostash true &&
	git reset --hard &&
	git checkout -b same-feature-branch feature-branch &&
	test_when_finished git branch -D same-feature-branch &&
	echo dirty >>file1 &&
	git rebase feature-branch &&
	grep dirty file1 &&
	git checkout feature-branch
'

testrebase " --apply" .git/rebase-apply
testrebase " --merge" .git/rebase-merge
testrebase " --interactive" .git/rebase-merge

test_expect_success 'abort rebase -i with --autostash' '
	test_when_finished "git reset --hard" &&
	echo uncommitted-content >file0 &&
	(
		write_script abort-editor.sh <<-\EOF &&
			echo >"$1"
		EOF
		test_set_editor "$(pwd)/abort-editor.sh" &&
		test_must_fail git rebase -i --autostash HEAD^ &&
		rm -f abort-editor.sh
	) &&
	echo uncommitted-content >expected &&
	test_cmp expected file0
'

test_expect_success 'restore autostash on editor failure' '
	test_when_finished "git reset --hard" &&
	echo uncommitted-content >file0 &&
	(
		test_set_editor "false" &&
		test_must_fail git rebase -i --autostash HEAD^
	) &&
	echo uncommitted-content >expected &&
	test_cmp expected file0
'

test_expect_success 'autostash is saved on editor failure with conflict' '
	test_when_finished "git reset --hard" &&
	echo uncommitted-content >file0 &&
	(
		write_script abort-editor.sh <<-\EOF &&
			echo conflicting-content >file0
			exit 1
		EOF
		test_set_editor "$(pwd)/abort-editor.sh" &&
		test_must_fail git rebase -i --autostash HEAD^ &&
		rm -f abort-editor.sh
	) &&
	echo conflicting-content >expected &&
	test_cmp expected file0 &&
	git checkout file0 &&
	git stash pop &&
	echo uncommitted-content >expected &&
	test_cmp expected file0
'

test_expect_success 'autostash with dirty submodules' '
	test_when_finished "git reset --hard && git checkout main" &&
	git checkout -b with-submodule &&
	git -c protocol.file.allow=always submodule add ./ sub &&
	test_tick &&
	git commit -m add-submodule &&
	echo changed >sub/file0 &&
	git rebase -i --autostash HEAD
'

test_expect_success 'branch is left alone when possible' '
	git checkout -b unchanged-branch &&
	echo changed >file0 &&
	git rebase --autostash unchanged-branch &&
	test changed = "$(cat file0)" &&
	test unchanged-branch = "$(git rev-parse --abbrev-ref HEAD)"
'

test_expect_success 'never change active branch' '
	git checkout -b not-the-feature-branch unrelated-onto-branch &&
	test_when_finished "git reset --hard && git checkout main" &&
	echo changed >file0 &&
	git rebase --autostash not-the-feature-branch feature-branch &&
	test_cmp_rev not-the-feature-branch unrelated-onto-branch
'

test_expect_success 'autostash commit is marked as reachable' '
	echo changed >file0 &&
	git rebase --autostash --exec "git prune --expire=now" \
		feature-branch^ feature-branch &&
	# git rebase succeeds if the stash cannot be applied so we need to check
	# the contents of file0
	echo changed >expect &&
	test_cmp expect file0
'

test_done
