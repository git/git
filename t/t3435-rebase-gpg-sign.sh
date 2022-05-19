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
	test_expect_success "rebase $* with cummit.gpgsign=$conf $will sign cummit" "
		but reset two &&
		but config cummit.gpgsign $conf &&
		set_fake_editor &&
		FAKE_LINES='r 1 p 2' but rebase --force-rebase --root $* &&
		$must_fail but verify-commit HEAD^ &&
		$must_fail but verify-commit HEAD
	"
}

test_expect_success 'setup' '
	test_cummit one &&
	test_cummit two &&
	test_must_fail but verify-commit HEAD &&
	test_must_fail but verify-commit HEAD^
'

test_expect_success 'setup: merge cummit' '
	test_cummit fork-point &&
	but switch -c side &&
	test_cummit three &&
	but switch main &&
	but merge --no-ff side &&
	but tag merged
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

test_expect_failure 'rebase -p --no-gpg-sign override cummit.gpgsign' '
	test_when_finished "but clean -f" &&
	but reset --hard merged &&
	but config cummit.gpgsign true &&
	but rebase -p --no-gpg-sign --onto=one fork-point main &&
	test_must_fail but verify-commit HEAD
'

test_expect_success 'rebase -r, merge strategy, --gpg-sign will sign cummit' '
	but reset --hard merged &&
	test_unconfig cummit.gpgsign &&
	but rebase -fr --gpg-sign -s resolve --root &&
	but verify-commit HEAD
'

test_expect_success 'rebase -r, merge strategy, cummit.gpgsign=true will sign cummit' '
	but reset --hard merged &&
	but config cummit.gpgsign true &&
	but rebase -fr -s resolve --root &&
	but verify-commit HEAD
'

test_expect_success 'rebase -r, merge strategy, cummit.gpgsign=false --gpg-sign will sign cummit' '
	but reset --hard merged &&
	but config cummit.gpgsign false &&
	but rebase -fr --gpg-sign -s resolve --root &&
	but verify-commit HEAD
'

test_expect_success "rebase -r, merge strategy, cummit.gpgsign=true --no-gpg-sign won't sign cummit" '
	but reset --hard merged &&
	but config cummit.gpgsign true &&
	but rebase -fr --no-gpg-sign -s resolve --root &&
	test_must_fail but verify-commit HEAD
'

test_expect_success 'rebase -r --gpg-sign will sign cummit' '
	but reset --hard merged &&
	test_unconfig cummit.gpgsign &&
	but rebase -fr --gpg-sign --root &&
	but verify-commit HEAD
'

test_expect_success 'rebase -r with cummit.gpgsign=true will sign cummit' '
	but reset --hard merged &&
	but config cummit.gpgsign true &&
	but rebase -fr --root &&
	but verify-commit HEAD
'

test_expect_success 'rebase -r --gpg-sign with cummit.gpgsign=false will sign cummit' '
	but reset --hard merged &&
	but config cummit.gpgsign false &&
	but rebase -fr --gpg-sign --root &&
	but verify-commit HEAD
'

test_expect_success "rebase -r --no-gpg-sign with cummit.gpgsign=true won't sign cummit" '
	but reset --hard merged &&
	but config cummit.gpgsign true &&
	but rebase -fr --no-gpg-sign --root &&
	test_must_fail but verify-commit HEAD
'

test_done
