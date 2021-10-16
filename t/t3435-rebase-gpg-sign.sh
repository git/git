#!/bin/sh
#
# Copyright (c) 2020 Doan Tran Cong Danh
#

test_description='test rebase --[no-]gpg-sign'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-rebase.sh"
. "$TEST_DIRECTORY/lib-gpg.sh"

if ! test_have_prereq GPG
then
	skip_all='skip all test rebase --[no-]gpg-sign, gpg not available'
	test_done
fi

test_rebase_gpg_sign () {
	local must_fail= will=will fake_editor=
	if test "x$1" = "x!"
	then
		must_fail=test_must_fail
		will="won't"
		shift
	fi
	conf=$1
	shift
	test_expect_success "rebase $* with commit.gpgsign=$conf $will sign commit" "
		git reset two &&
		git config commit.gpgsign $conf &&
		set_fake_editor &&
		FAKE_LINES='r 1 p 2' git rebase --force-rebase --root $* &&
		$must_fail git verify-commit HEAD^ &&
		$must_fail git verify-commit HEAD
	"
}

test_expect_success 'setup' '
	test_commit one &&
	test_commit two &&
	test_must_fail git verify-commit HEAD &&
	test_must_fail git verify-commit HEAD^
'

test_expect_success 'setup: merge commit' '
	test_commit fork-point &&
	git switch -c side &&
	test_commit three &&
	git switch main &&
	git merge --no-ff side &&
	git tag merged
'

test_rebase_gpg_sign ! false
test_rebase_gpg_sign   true
test_rebase_gpg_sign ! true  --no-gpg-sign
test_rebase_gpg_sign ! true  --gpg-sign --no-gpg-sign
test_rebase_gpg_sign   false --no-gpg-sign --gpg-sign
test_rebase_gpg_sign   true  -i
test_rebase_gpg_sign ! true  -i --no-gpg-sign
test_rebase_gpg_sign ! true  -i --gpg-sign --no-gpg-sign
test_rebase_gpg_sign   false -i --no-gpg-sign --gpg-sign

test_expect_failure 'rebase -p --no-gpg-sign override commit.gpgsign' '
	test_when_finished "git clean -f" &&
	git reset --hard merged &&
	git config commit.gpgsign true &&
	git rebase -p --no-gpg-sign --onto=one fork-point main &&
	test_must_fail git verify-commit HEAD
'

test_expect_success 'rebase -r, merge strategy, --gpg-sign will sign commit' '
	git reset --hard merged &&
	test_unconfig commit.gpgsign &&
	git rebase -fr --gpg-sign -s resolve --root &&
	git verify-commit HEAD
'

test_expect_success 'rebase -r, merge strategy, commit.gpgsign=true will sign commit' '
	git reset --hard merged &&
	git config commit.gpgsign true &&
	git rebase -fr -s resolve --root &&
	git verify-commit HEAD
'

test_expect_success 'rebase -r, merge strategy, commit.gpgsign=false --gpg-sign will sign commit' '
	git reset --hard merged &&
	git config commit.gpgsign false &&
	git rebase -fr --gpg-sign -s resolve --root &&
	git verify-commit HEAD
'

test_expect_success "rebase -r, merge strategy, commit.gpgsign=true --no-gpg-sign won't sign commit" '
	git reset --hard merged &&
	git config commit.gpgsign true &&
	git rebase -fr --no-gpg-sign -s resolve --root &&
	test_must_fail git verify-commit HEAD
'

test_expect_success 'rebase -r --gpg-sign will sign commit' '
	git reset --hard merged &&
	test_unconfig commit.gpgsign &&
	git rebase -fr --gpg-sign --root &&
	git verify-commit HEAD
'

test_expect_success 'rebase -r with commit.gpgsign=true will sign commit' '
	git reset --hard merged &&
	git config commit.gpgsign true &&
	git rebase -fr --root &&
	git verify-commit HEAD
'

test_expect_success 'rebase -r --gpg-sign with commit.gpgsign=false will sign commit' '
	git reset --hard merged &&
	git config commit.gpgsign false &&
	git rebase -fr --gpg-sign --root &&
	git verify-commit HEAD
'

test_expect_success "rebase -r --no-gpg-sign with commit.gpgsign=true won't sign commit" '
	git reset --hard merged &&
	git config commit.gpgsign true &&
	git rebase -fr --no-gpg-sign --root &&
	test_must_fail git verify-commit HEAD
'

test_done
