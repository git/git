#!/bin/sh

test_description='"git fetch/pull --set-upstream" basic tests.'
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

test_expect_success 'setup commit on master and other fetch' '
	test_commit one &&
	git push upstream master &&
	git checkout -b other &&
	test_commit two &&
	git push upstream other
'

# tests for fetch --set-upstream

test_expect_success 'fetch --set-upstream does not set upstream w/o branch' '
	clear_config master other &&
	git checkout master &&
	git fetch --set-upstream upstream &&
	check_config_missing master &&
	check_config_missing other
'

test_expect_success 'fetch --set-upstream upstream master sets branch master but not other' '
	clear_config master other &&
	git fetch --set-upstream upstream master &&
	check_config master upstream refs/heads/master &&
	check_config_missing other
'

test_expect_success 'fetch --set-upstream upstream other sets branch other' '
	clear_config master other &&
	git fetch --set-upstream upstream other &&
	check_config master upstream refs/heads/other &&
	check_config_missing other
'

test_expect_success 'fetch --set-upstream master:other does not set the branch other2' '
	clear_config other2 &&
	git fetch --set-upstream upstream master:other2 &&
	check_config_missing other2
'

test_expect_success 'fetch --set-upstream http://nosuchdomain.example.com fails with invalid url' '
	# master explicitly not cleared, we check that it is not touched from previous value
	clear_config other other2 &&
	test_must_fail git fetch --set-upstream http://nosuchdomain.example.com &&
	check_config master upstream refs/heads/other &&
	check_config_missing other &&
	check_config_missing other2
'

test_expect_success 'fetch --set-upstream with valid URL sets upstream to URL' '
	clear_config other other2 &&
	url="file://'"$PWD"'" &&
	git fetch --set-upstream "$url" &&
	check_config master "$url" HEAD &&
	check_config_missing other &&
	check_config_missing other2
'

# tests for pull --set-upstream

test_expect_success 'setup bare parent pull' '
	git remote rm upstream &&
	ensure_fresh_upstream &&
	git remote add upstream parent
'

test_expect_success 'setup commit on master and other pull' '
	test_commit three &&
	git push --tags upstream master &&
	test_commit four &&
	git push upstream other
'

test_expect_success 'pull --set-upstream upstream master sets branch master but not other' '
	clear_config master other &&
	git pull --set-upstream upstream master &&
	check_config master upstream refs/heads/master &&
	check_config_missing other
'

test_expect_success 'pull --set-upstream master:other2 does not set the branch other2' '
	clear_config other2 &&
	git pull --set-upstream upstream master:other2 &&
	check_config_missing other2
'

test_expect_success 'pull --set-upstream upstream other sets branch master' '
	clear_config master other &&
	git pull --set-upstream upstream other &&
	check_config master upstream refs/heads/other &&
	check_config_missing other
'

test_expect_success 'pull --set-upstream upstream tag does not set the tag' '
	clear_config three &&
	git pull --tags --set-upstream upstream three &&
	check_config_missing three
'

test_expect_success 'pull --set-upstream http://nosuchdomain.example.com fails with invalid url' '
	# master explicitly not cleared, we check that it is not touched from previous value
	clear_config other other2 three &&
	test_must_fail git pull --set-upstream http://nosuchdomain.example.com &&
	check_config master upstream refs/heads/other &&
	check_config_missing other &&
	check_config_missing other2 &&
	check_config_missing three
'

test_expect_success 'pull --set-upstream upstream HEAD sets branch HEAD' '
	clear_config master other &&
	git pull --set-upstream upstream HEAD &&
	check_config master upstream HEAD &&
	git checkout other &&
	git pull --set-upstream upstream HEAD &&
	check_config other upstream HEAD
'

test_expect_success 'pull --set-upstream upstream with more than one branch does nothing' '
	clear_config master three &&
	git pull --set-upstream upstream master three &&
	check_config_missing master &&
	check_config_missing three
'

test_expect_success 'pull --set-upstream with valid URL sets upstream to URL' '
	clear_config master other other2 &&
	git checkout master &&
	url="file://'"$PWD"'" &&
	git pull --set-upstream "$url" &&
	check_config master "$url" HEAD &&
	check_config_missing other &&
	check_config_missing other2
'

test_expect_success 'pull --set-upstream with valid URL and branch sets branch' '
	clear_config master other other2 &&
	git checkout master &&
	url="file://'"$PWD"'" &&
	git pull --set-upstream "$url" master &&
	check_config master "$url" refs/heads/master &&
	check_config_missing other &&
	check_config_missing other2
'

test_done
