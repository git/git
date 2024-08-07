#!/bin/sh

test_description='"git fetch/pull --set-upstream" basic tests.'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

check_config () {
	printf "%s\n" "$2" "$3" >"expect.$1" &&
	{
		git config "branch.$1.remote" && git config "branch.$1.merge"
	} >"actual.$1" &&
	test_cmp "expect.$1" "actual.$1"
}

check_config_missing () {
	test_expect_code 1 git config "branch.$1.remote" &&
	test_expect_code 1 git config "branch.$1.merge"
}

clear_config () {
	for branch in "$@"; do
		test_might_fail git config --unset-all "branch.$branch.remote"
		test_might_fail git config --unset-all "branch.$branch.merge"
	done
}

ensure_fresh_upstream () {
	rm -rf parent && git init --bare parent
}

test_expect_success 'setup bare parent fetch' '
	ensure_fresh_upstream &&
	git remote add upstream parent
'

test_expect_success 'setup commit on main and other fetch' '
	test_commit one &&
	git push upstream main &&
	git checkout -b other &&
	test_commit two &&
	git push upstream other
'

# tests for fetch --set-upstream

test_expect_success 'fetch --set-upstream does not set upstream w/o branch' '
	clear_config main other &&
	git checkout main &&
	git fetch --set-upstream upstream &&
	check_config_missing main &&
	check_config_missing other
'

test_expect_success 'fetch --set-upstream upstream main sets branch main but not other' '
	clear_config main other &&
	git fetch --set-upstream upstream main &&
	check_config main upstream refs/heads/main &&
	check_config_missing other
'

test_expect_success 'fetch --set-upstream upstream other sets branch other' '
	clear_config main other &&
	git fetch --set-upstream upstream other &&
	check_config main upstream refs/heads/other &&
	check_config_missing other
'

test_expect_success 'fetch --set-upstream main:other does not set the branch other2' '
	clear_config other2 &&
	git fetch --set-upstream upstream main:other2 &&
	check_config_missing other2
'

test_expect_success 'fetch --set-upstream ./does-not-exist fails with invalid url' '
	# main explicitly not cleared, we check that it is not touched from previous value
	clear_config other other2 &&
	test_must_fail git fetch --set-upstream ./does-not-exist &&
	check_config main upstream refs/heads/other &&
	check_config_missing other &&
	check_config_missing other2
'

test_expect_success 'fetch --set-upstream with valid URL sets upstream to URL' '
	clear_config other other2 &&
	url="file://$PWD" &&
	git fetch --set-upstream "$url" &&
	check_config main "$url" HEAD &&
	check_config_missing other &&
	check_config_missing other2
'

test_expect_success 'fetch --set-upstream with a detached HEAD' '
	git checkout HEAD^0 &&
	test_when_finished "git checkout -" &&
	cat >expect <<-\EOF &&
	warning: could not set upstream of HEAD to '"'"'main'"'"' from '"'"'upstream'"'"' when it does not point to any branch.
	EOF
	git fetch --set-upstream upstream main 2>actual.raw &&
	grep ^warning: actual.raw >actual &&
	test_cmp expect actual
'

# tests for pull --set-upstream

test_expect_success 'setup bare parent pull' '
	git remote rm upstream &&
	ensure_fresh_upstream &&
	git remote add upstream parent
'

test_expect_success 'setup commit on main and other pull' '
	test_commit three &&
	git push --tags upstream main &&
	test_commit four &&
	git push upstream other
'

test_expect_success 'pull --set-upstream upstream main sets branch main but not other' '
	clear_config main other &&
	git pull --no-rebase --set-upstream upstream main &&
	check_config main upstream refs/heads/main &&
	check_config_missing other
'

test_expect_success 'pull --set-upstream main:other2 does not set the branch other2' '
	clear_config other2 &&
	git pull --no-rebase --set-upstream upstream main:other2 &&
	check_config_missing other2
'

test_expect_success 'pull --set-upstream upstream other sets branch main' '
	clear_config main other &&
	git pull --no-rebase --set-upstream upstream other &&
	check_config main upstream refs/heads/other &&
	check_config_missing other
'

test_expect_success 'pull --set-upstream upstream tag does not set the tag' '
	clear_config three &&
	git pull --no-rebase --tags --set-upstream upstream three &&
	check_config_missing three
'

test_expect_success 'pull --set-upstream ./does-not-exist fails with invalid url' '
	# main explicitly not cleared, we check that it is not touched from previous value
	clear_config other other2 three &&
	test_must_fail git pull --set-upstream ./does-not-exist &&
	check_config main upstream refs/heads/other &&
	check_config_missing other &&
	check_config_missing other2 &&
	check_config_missing three
'

test_expect_success 'pull --set-upstream upstream HEAD sets branch HEAD' '
	clear_config main other &&
	git pull --no-rebase --set-upstream upstream HEAD &&
	check_config main upstream HEAD &&
	git checkout other &&
	git pull --no-rebase --set-upstream upstream HEAD &&
	check_config other upstream HEAD
'

test_expect_success 'pull --set-upstream upstream with more than one branch does nothing' '
	clear_config main three &&
	git pull --no-rebase --set-upstream upstream main three &&
	check_config_missing main &&
	check_config_missing three
'

test_expect_success 'pull --set-upstream with valid URL sets upstream to URL' '
	clear_config main other other2 &&
	git checkout main &&
	url="file://$PWD" &&
	git pull --set-upstream "$url" &&
	check_config main "$url" HEAD &&
	check_config_missing other &&
	check_config_missing other2
'

test_expect_success 'pull --set-upstream with valid URL and branch sets branch' '
	clear_config main other other2 &&
	git checkout main &&
	url="file://$PWD" &&
	git pull --set-upstream "$url" main &&
	check_config main "$url" refs/heads/main &&
	check_config_missing other &&
	check_config_missing other2
'

test_expect_success 'pull --set-upstream with a detached HEAD' '
	git checkout HEAD^0 &&
	test_when_finished "git checkout -" &&
	cat >expect <<-\EOF &&
	warning: could not set upstream of HEAD to '"'"'main'"'"' from '"'"'upstream'"'"' when it does not point to any branch.
	EOF
	git pull --no-rebase --set-upstream upstream main 2>actual.raw &&
	grep ^warning: actual.raw >actual &&
	test_cmp expect actual
'

test_done
