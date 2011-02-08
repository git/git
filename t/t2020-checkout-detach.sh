#!/bin/sh

test_description='checkout into detached HEAD state'
. ./test-lib.sh

check_detached () {
	test_must_fail git symbolic-ref -q HEAD >/dev/null
}

check_not_detached () {
	git symbolic-ref -q HEAD >/dev/null
}

reset () {
	git checkout master &&
	check_not_detached
}

test_expect_success 'setup' '
	test_commit one &&
	test_commit two &&
	git branch branch &&
	git tag tag
'

test_expect_success 'checkout branch does not detach' '
	reset &&
	git checkout branch &&
	check_not_detached
'

test_expect_success 'checkout tag detaches' '
	reset &&
	git checkout tag &&
	check_detached
'

test_expect_success 'checkout branch by full name detaches' '
	reset &&
	git checkout refs/heads/branch &&
	check_detached
'

test_expect_success 'checkout non-ref detaches' '
	reset &&
	git checkout branch^ &&
	check_detached
'

test_expect_success 'checkout ref^0 detaches' '
	reset &&
	git checkout branch^0 &&
	check_detached
'

test_expect_success 'checkout --detach detaches' '
	reset &&
	git checkout --detach branch &&
	check_detached
'

test_expect_success 'checkout --detach without branch name' '
	reset &&
	git checkout --detach &&
	check_detached
'

test_expect_success 'checkout --detach errors out for non-commit' '
	reset &&
	test_must_fail git checkout --detach one^{tree} &&
	check_not_detached
'

test_expect_success 'checkout --detach errors out for extra argument' '
	reset &&
	git checkout master &&
	test_must_fail git checkout --detach tag one.t &&
	check_not_detached
'

test_expect_success 'checkout --detached and -b are incompatible' '
	reset &&
	test_must_fail git checkout --detach -b newbranch tag &&
	check_not_detached
'

test_expect_success 'checkout --detach moves HEAD' '
	reset &&
	git checkout one &&
	git checkout --detach two &&
	git diff --exit-code HEAD &&
	git diff --exit-code two
'

test_done
