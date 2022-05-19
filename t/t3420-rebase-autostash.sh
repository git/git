#!/bin/sh
#
# Copyright (c) 2013 Ramkumar Ramachandra
#

test_description='but rebase --autostash tests'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	echo hello-world >file0 &&
	but add . &&
	test_tick &&
	but cummit -m "initial cummit" &&
	but checkout -b feature-branch &&
	echo another-hello >file1 &&
	echo goodbye >file2 &&
	but add . &&
	test_tick &&
	but cummit -m "second cummit" &&
	echo final-goodbye >file3 &&
	but add . &&
	test_tick &&
	but cummit -m "third cummit" &&
	but checkout -b unrelated-onto-branch main &&
	echo unrelated >file4 &&
	but add . &&
	test_tick &&
	but cummit -m "unrelated cummit" &&
	but checkout -b related-onto-branch main &&
	echo conflicting-change >file2 &&
	but add . &&
	test_tick &&
	but cummit -m "related cummit" &&
	remove_progress_re="$(printf "s/.*\\r//")"
'

create_expected_success_apply () {
	cat >expected <<-EOF
	$(grep "^Created autostash: [0-9a-f][0-9a-f]*\$" actual)
	First, rewinding head to replay your work on top of it...
	Applying: second cummit
	Applying: third cummit
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
	Applying: second cummit
	Applying: third cummit
	Applying autostash resulted in conflicts.
	Your changes are safe in the stash.
	You can run "but stash pop" or "but stash drop" at any time.
	EOF
}

create_expected_failure_merge () {
	cat >expected <<-EOF
	$(grep "^Created autostash: [0-9a-f][0-9a-f]*\$" actual)
	Applying autostash resulted in conflicts.
	Your changes are safe in the stash.
	You can run "but stash pop" or "but stash drop" at any time.
	Successfully rebased and updated refs/heads/rebased-feature-branch.
	EOF
}

testrebase () {
	type=$1
	dotest=$2

	test_expect_success "rebase$type: dirty worktree, --no-autostash" '
		test_config rebase.autostash true &&
		but reset --hard &&
		but checkout -b rebased-feature-branch feature-branch &&
		test_when_finished but branch -D rebased-feature-branch &&
		test_when_finished but checkout feature-branch &&
		echo dirty >>file3 &&
		test_must_fail but rebase$type --no-autostash unrelated-onto-branch
	'

	test_expect_success "rebase$type: dirty worktree, non-conflicting rebase" '
		test_config rebase.autostash true &&
		but reset --hard &&
		but checkout -b rebased-feature-branch feature-branch &&
		echo dirty >>file3 &&
		but rebase$type unrelated-onto-branch >actual 2>&1 &&
		grep unrelated file4 &&
		grep dirty file3 &&
		but checkout feature-branch
	'

	test_expect_success "rebase$type --autostash: check output" '
		test_when_finished but branch -D rebased-feature-branch &&
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
		but reset --hard &&
		but checkout -b rebased-feature-branch feature-branch &&
		test_when_finished but branch -D rebased-feature-branch &&
		echo dirty >>file3 &&
		but add file3 &&
		but rebase$type unrelated-onto-branch &&
		grep unrelated file4 &&
		grep dirty file3 &&
		but checkout feature-branch
	'

	test_expect_success "rebase$type: conflicting rebase" '
		test_config rebase.autostash true &&
		but reset --hard &&
		but checkout -b rebased-feature-branch feature-branch &&
		test_when_finished but branch -D rebased-feature-branch &&
		echo dirty >>file3 &&
		test_must_fail but rebase$type related-onto-branch &&
		test_path_is_file $dotest/autostash &&
		test_path_is_missing file3 &&
		rm -rf $dotest &&
		but reset --hard &&
		but checkout feature-branch
	'

	test_expect_success "rebase$type: --continue" '
		test_config rebase.autostash true &&
		but reset --hard &&
		but checkout -b rebased-feature-branch feature-branch &&
		test_when_finished but branch -D rebased-feature-branch &&
		echo dirty >>file3 &&
		test_must_fail but rebase$type related-onto-branch &&
		test_path_is_file $dotest/autostash &&
		test_path_is_missing file3 &&
		echo "conflicting-plus-goodbye" >file2 &&
		but add file2 &&
		but rebase --continue &&
		test_path_is_missing $dotest/autostash &&
		grep dirty file3 &&
		but checkout feature-branch
	'

	test_expect_success "rebase$type: --skip" '
		test_config rebase.autostash true &&
		but reset --hard &&
		but checkout -b rebased-feature-branch feature-branch &&
		test_when_finished but branch -D rebased-feature-branch &&
		echo dirty >>file3 &&
		test_must_fail but rebase$type related-onto-branch &&
		test_path_is_file $dotest/autostash &&
		test_path_is_missing file3 &&
		but rebase --skip &&
		test_path_is_missing $dotest/autostash &&
		grep dirty file3 &&
		but checkout feature-branch
	'

	test_expect_success "rebase$type: --abort" '
		test_config rebase.autostash true &&
		but reset --hard &&
		but checkout -b rebased-feature-branch feature-branch &&
		test_when_finished but branch -D rebased-feature-branch &&
		echo dirty >>file3 &&
		test_must_fail but rebase$type related-onto-branch &&
		test_path_is_file $dotest/autostash &&
		test_path_is_missing file3 &&
		but rebase --abort &&
		test_path_is_missing $dotest/autostash &&
		grep dirty file3 &&
		but checkout feature-branch
	'

	test_expect_success "rebase$type: --quit" '
		test_config rebase.autostash true &&
		but reset --hard &&
		but checkout -b rebased-feature-branch feature-branch &&
		test_when_finished but branch -D rebased-feature-branch &&
		echo dirty >>file3 &&
		but diff >expect &&
		test_must_fail but rebase$type related-onto-branch &&
		test_path_is_file $dotest/autostash &&
		test_path_is_missing file3 &&
		but rebase --quit &&
		test_when_finished but stash drop &&
		test_path_is_missing $dotest/autostash &&
		! grep dirty file3 &&
		but stash show -p >actual &&
		test_cmp expect actual &&
		but reset --hard &&
		but checkout feature-branch
	'

	test_expect_success "rebase$type: non-conflicting rebase, conflicting stash" '
		test_config rebase.autostash true &&
		but reset --hard &&
		but checkout -b rebased-feature-branch feature-branch &&
		echo dirty >file4 &&
		but add file4 &&
		but rebase$type unrelated-onto-branch >actual 2>&1 &&
		test_path_is_missing $dotest &&
		but reset --hard &&
		grep unrelated file4 &&
		! grep dirty file4 &&
		but checkout feature-branch &&
		but stash pop &&
		grep dirty file4
	'

	test_expect_success "rebase$type: check output with conflicting stash" '
		test_when_finished but branch -D rebased-feature-branch &&
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
	but reset --hard &&
	but checkout -b behind-feature-branch feature-branch~1 &&
	test_when_finished but branch -D behind-feature-branch &&
	echo dirty >>file1 &&
	but rebase feature-branch &&
	grep dirty file1 &&
	but checkout feature-branch
'

test_expect_success "rebase: noop rebase" '
	test_config rebase.autostash true &&
	but reset --hard &&
	but checkout -b same-feature-branch feature-branch &&
	test_when_finished but branch -D same-feature-branch &&
	echo dirty >>file1 &&
	but rebase feature-branch &&
	grep dirty file1 &&
	but checkout feature-branch
'

testrebase " --apply" .but/rebase-apply
testrebase " --merge" .but/rebase-merge
testrebase " --interactive" .but/rebase-merge

test_expect_success 'abort rebase -i with --autostash' '
	test_when_finished "but reset --hard" &&
	echo uncummitted-content >file0 &&
	(
		write_script abort-editor.sh <<-\EOF &&
			echo >"$1"
		EOF
		test_set_editor "$(pwd)/abort-editor.sh" &&
		test_must_fail but rebase -i --autostash HEAD^ &&
		rm -f abort-editor.sh
	) &&
	echo uncummitted-content >expected &&
	test_cmp expected file0
'

test_expect_success 'restore autostash on editor failure' '
	test_when_finished "but reset --hard" &&
	echo uncummitted-content >file0 &&
	(
		test_set_editor "false" &&
		test_must_fail but rebase -i --autostash HEAD^
	) &&
	echo uncummitted-content >expected &&
	test_cmp expected file0
'

test_expect_success 'autostash is saved on editor failure with conflict' '
	test_when_finished "but reset --hard" &&
	echo uncummitted-content >file0 &&
	(
		write_script abort-editor.sh <<-\EOF &&
			echo conflicting-content >file0
			exit 1
		EOF
		test_set_editor "$(pwd)/abort-editor.sh" &&
		test_must_fail but rebase -i --autostash HEAD^ &&
		rm -f abort-editor.sh
	) &&
	echo conflicting-content >expected &&
	test_cmp expected file0 &&
	but checkout file0 &&
	but stash pop &&
	echo uncummitted-content >expected &&
	test_cmp expected file0
'

test_expect_success 'autostash with dirty submodules' '
	test_when_finished "but reset --hard && but checkout main" &&
	but checkout -b with-submodule &&
	but submodule add ./ sub &&
	test_tick &&
	but cummit -m add-submodule &&
	echo changed >sub/file0 &&
	but rebase -i --autostash HEAD
'

test_expect_success 'branch is left alone when possible' '
	but checkout -b unchanged-branch &&
	echo changed >file0 &&
	but rebase --autostash unchanged-branch &&
	test changed = "$(cat file0)" &&
	test unchanged-branch = "$(but rev-parse --abbrev-ref HEAD)"
'

test_expect_success 'never change active branch' '
	but checkout -b not-the-feature-branch unrelated-onto-branch &&
	test_when_finished "but reset --hard && but checkout main" &&
	echo changed >file0 &&
	but rebase --autostash not-the-feature-branch feature-branch &&
	test_cmp_rev not-the-feature-branch unrelated-onto-branch
'

test_done
