#!/bin/sh

test_description='merge signature verification tests'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success GPG 'create signed commits' '
	echo 1 >file && git add file &&
	test_tick && git commit -m initial &&
	git tag initial &&

	git checkout -b side-signed &&
	echo 3 >elif && git add elif &&
	test_tick && git commit -S -m "signed on side" &&
	git checkout initial &&

	git checkout -b side-unsigned &&
	echo 3 >foo && git add foo &&
	test_tick && git commit -m "unsigned on side" &&
	git checkout initial &&

	git checkout -b side-bad &&
	echo 3 >bar && git add bar &&
	test_tick && git commit -S -m "bad on side" &&
	git cat-file commit side-bad >raw &&
	sed -e "s/^bad/forged bad/" raw >forged &&
	git hash-object -w -t commit forged >forged.commit &&
	git checkout initial &&

	git checkout -b side-untrusted &&
	echo 3 >baz && git add baz &&
	test_tick && git commit -SB7227189 -m "untrusted on side" &&

	git checkout main
'

test_expect_success GPG 'merge unsigned commit with verification' '
	test_when_finished "git reset --hard && git checkout initial" &&
	test_must_fail git merge --ff-only --verify-signatures side-unsigned 2>mergeerror &&
	test_grep "does not have a GPG signature" mergeerror
'

test_expect_success GPG 'merge unsigned commit with merge.verifySignatures=true' '
	test_when_finished "git reset --hard && git checkout initial" &&
	test_config merge.verifySignatures true &&
	test_must_fail git merge --ff-only side-unsigned 2>mergeerror &&
	test_grep "does not have a GPG signature" mergeerror
'

test_expect_success GPG 'merge commit with bad signature with verification' '
	test_when_finished "git reset --hard && git checkout initial" &&
	test_must_fail git merge --ff-only --verify-signatures $(cat forged.commit) 2>mergeerror &&
	test_grep "has a bad GPG signature" mergeerror
'

test_expect_success GPG 'merge commit with bad signature with merge.verifySignatures=true' '
	test_when_finished "git reset --hard && git checkout initial" &&
	test_config merge.verifySignatures true &&
	test_must_fail git merge --ff-only $(cat forged.commit) 2>mergeerror &&
	test_grep "has a bad GPG signature" mergeerror
'

test_expect_success GPG 'merge commit with untrusted signature with verification' '
	test_when_finished "git reset --hard && git checkout initial" &&
	test_must_fail git merge --ff-only --verify-signatures side-untrusted 2>mergeerror &&
	test_grep "has an untrusted GPG signature" mergeerror
'

test_expect_success GPG 'merge commit with untrusted signature with verification and high minTrustLevel' '
	test_when_finished "git reset --hard && git checkout initial" &&
	test_config gpg.minTrustLevel marginal &&
	test_must_fail git merge --ff-only --verify-signatures side-untrusted 2>mergeerror &&
	test_grep "has an untrusted GPG signature" mergeerror
'

test_expect_success GPG 'merge commit with untrusted signature with verification and low minTrustLevel' '
	test_when_finished "git reset --hard && git checkout initial" &&
	test_config gpg.minTrustLevel undefined &&
	git merge --ff-only --verify-signatures side-untrusted >mergeoutput &&
	test_grep "has a good GPG signature" mergeoutput
'

test_expect_success GPG 'merge commit with untrusted signature with merge.verifySignatures=true' '
	test_when_finished "git reset --hard && git checkout initial" &&
	test_config merge.verifySignatures true &&
	test_must_fail git merge --ff-only side-untrusted 2>mergeerror &&
	test_grep "has an untrusted GPG signature" mergeerror
'

test_expect_success GPG 'merge commit with untrusted signature with merge.verifySignatures=true and minTrustLevel' '
	test_when_finished "git reset --hard && git checkout initial" &&
	test_config merge.verifySignatures true &&
	test_config gpg.minTrustLevel marginal &&
	test_must_fail git merge --ff-only side-untrusted 2>mergeerror &&
	test_grep "has an untrusted GPG signature" mergeerror
'

test_expect_success GPG 'merge signed commit with verification' '
	test_when_finished "git reset --hard && git checkout initial" &&
	git merge --verbose --ff-only --verify-signatures side-signed >mergeoutput &&
	test_grep "has a good GPG signature" mergeoutput
'

test_expect_success GPG 'merge signed commit with merge.verifySignatures=true' '
	test_when_finished "git reset --hard && git checkout initial" &&
	test_config merge.verifySignatures true &&
	git merge --verbose --ff-only side-signed >mergeoutput &&
	test_grep "has a good GPG signature" mergeoutput
'

test_expect_success GPG 'merge commit with bad signature without verification' '
	test_when_finished "git reset --hard && git checkout initial" &&
	git merge $(cat forged.commit)
'

test_expect_success GPG 'merge commit with bad signature with merge.verifySignatures=false' '
	test_when_finished "git reset --hard && git checkout initial" &&
	test_config merge.verifySignatures false &&
	git merge $(cat forged.commit)
'

test_expect_success GPG 'merge commit with bad signature with merge.verifySignatures=true and --no-verify-signatures' '
	test_when_finished "git reset --hard && git checkout initial" &&
	test_config merge.verifySignatures true &&
	git merge --no-verify-signatures $(cat forged.commit)
'

test_expect_success GPG 'merge unsigned commit into unborn branch' '
	test_when_finished "git checkout initial" &&
	git checkout --orphan unborn &&
	test_must_fail git merge --verify-signatures side-unsigned 2>mergeerror &&
	test_grep "does not have a GPG signature" mergeerror
'

test_done
