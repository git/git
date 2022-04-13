#!/bin/sh

test_description='verify safe.directory checks'

. ./test-lib.sh

GIT_TEST_ASSUME_DIFFERENT_OWNER=1
export GIT_TEST_ASSUME_DIFFERENT_OWNER

expect_rejected_dir () {
	test_must_fail git status 2>err &&
	grep "safe.directory" err
}

test_expect_success 'safe.directory is not set' '
	expect_rejected_dir
'

test_expect_success 'safe.directory does not match' '
	git config --global safe.directory bogus &&
	expect_rejected_dir
'

test_expect_success 'safe.directory matches' '
	git config --global --add safe.directory "$(pwd)" &&
	git status
'

test_expect_success 'safe.directory matches, but is reset' '
	git config --global --add safe.directory "" &&
	expect_rejected_dir
'

test_done
