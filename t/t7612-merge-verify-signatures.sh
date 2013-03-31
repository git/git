#!/bin/sh

test_description='merge signature verification tests'
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
	sed -e "s/bad/forged bad/" raw >forged &&
	git hash-object -w -t commit forged >forged.commit &&
	git checkout initial &&

	git checkout -b side-untrusted &&
	echo 3 >baz && git add baz &&
	test_tick && git commit -SB7227189 -m "untrusted on side"

	git checkout master
'

test_expect_success GPG 'merge unsigned commit with verification' '
	test_must_fail git merge --ff-only --verify-signatures side-unsigned 2>mergeerror &&
	test_i18ngrep "does not have a GPG signature" mergeerror
'

test_expect_success GPG 'merge commit with bad signature with verification' '
	test_must_fail git merge --ff-only --verify-signatures $(cat forged.commit) 2>mergeerror &&
	test_i18ngrep "has a bad GPG signature" mergeerror
'

test_expect_success GPG 'merge commit with untrusted signature with verification' '
	test_must_fail git merge --ff-only --verify-signatures side-untrusted 2>mergeerror &&
	test_i18ngrep "has an untrusted GPG signature" mergeerror
'

test_expect_success GPG 'merge signed commit with verification' '
	git merge --verbose --ff-only --verify-signatures side-signed >mergeoutput &&
	test_i18ngrep "has a good GPG signature" mergeoutput
'

test_expect_success GPG 'merge commit with bad signature without verification' '
	git merge $(cat forged.commit)
'

test_done
