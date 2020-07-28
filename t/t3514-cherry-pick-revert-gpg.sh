#!/bin/sh
#
# Copyright (c) 2020 Doan Tran Cong Danh
#

test_description='test {cherry-pick,revert} --[no-]gpg-sign'

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

if ! test_have_prereq GPG
then
	skip_all='skip all test {cherry-pick,revert} --[no-]gpg-sign, gpg not available'
	test_done
fi

test_gpg_sign () {
	local must_fail= will=will fake_editor=
	if test "x$1" = "x!"
	then
		must_fail=test_must_fail
		will="won't"
		shift
	fi
	conf=$1
	cmd=$2
	cmit=$3
	shift 3
	test_expect_success "$cmd $* $cmit with commit.gpgsign=$conf $will sign commit" "
		git reset --hard tip &&
		git config commit.gpgsign $conf &&
		git $cmd $* $cmit &&
		git rev-list tip.. >rev-list &&
		$must_fail git verify-commit \$(cat rev-list)
	"
}

test_expect_success 'setup' '
	test_commit one &&
	git switch -c side &&
	test_commit side1 &&
	test_commit side2 &&
	git switch - &&
	test_commit two &&
	test_commit three &&
	test_commit tip
'

test_gpg_sign ! false cherry-pick   side
test_gpg_sign ! false cherry-pick ..side
test_gpg_sign   true  cherry-pick   side
test_gpg_sign   true  cherry-pick ..side
test_gpg_sign ! true  cherry-pick   side --no-gpg-sign
test_gpg_sign ! true  cherry-pick ..side --no-gpg-sign
test_gpg_sign ! true  cherry-pick   side --gpg-sign --no-gpg-sign
test_gpg_sign ! true  cherry-pick ..side --gpg-sign --no-gpg-sign
test_gpg_sign   false cherry-pick   side --no-gpg-sign --gpg-sign
test_gpg_sign   false cherry-pick ..side --no-gpg-sign --gpg-sign
test_gpg_sign   true  cherry-pick   side --edit
test_gpg_sign   true  cherry-pick ..side --edit
test_gpg_sign ! true  cherry-pick   side --edit --no-gpg-sign
test_gpg_sign ! true  cherry-pick ..side --edit --no-gpg-sign
test_gpg_sign ! true  cherry-pick   side --edit --gpg-sign --no-gpg-sign
test_gpg_sign ! true  cherry-pick ..side --edit --gpg-sign --no-gpg-sign
test_gpg_sign   false cherry-pick   side --edit --no-gpg-sign --gpg-sign
test_gpg_sign   false cherry-pick ..side --edit --no-gpg-sign --gpg-sign

test_gpg_sign ! false revert HEAD  --edit
test_gpg_sign ! false revert two.. --edit
test_gpg_sign   true  revert HEAD  --edit
test_gpg_sign   true  revert two.. --edit
test_gpg_sign ! true  revert HEAD  --edit --no-gpg-sign
test_gpg_sign ! true  revert two.. --edit --no-gpg-sign
test_gpg_sign ! true  revert HEAD  --edit --gpg-sign --no-gpg-sign
test_gpg_sign ! true  revert two.. --edit --gpg-sign --no-gpg-sign
test_gpg_sign   false revert HEAD  --edit --no-gpg-sign --gpg-sign
test_gpg_sign   false revert two.. --edit --no-gpg-sign --gpg-sign
test_gpg_sign   true  revert HEAD  --no-edit
test_gpg_sign   true  revert two.. --no-edit
test_gpg_sign ! true  revert HEAD  --no-edit --no-gpg-sign
test_gpg_sign ! true  revert two.. --no-edit --no-gpg-sign
test_gpg_sign ! true  revert HEAD  --no-edit --gpg-sign --no-gpg-sign
test_gpg_sign ! true  revert two.. --no-edit --gpg-sign --no-gpg-sign
test_gpg_sign   false revert HEAD  --no-edit --no-gpg-sign --gpg-sign

test_done
