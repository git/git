#!/bin/sh

test_description='verify safe.directory checks'

. ./test-lib.sh

BUT_TEST_ASSUME_DIFFERENT_OWNER=1
export BUT_TEST_ASSUME_DIFFERENT_OWNER

expect_rejected_dir () {
	test_must_fail but status 2>err &&
	grep "safe.directory" err
}

test_expect_success 'safe.directory is not set' '
	expect_rejected_dir
'

test_expect_success 'safe.directory does not match' '
	but config --global safe.directory bogus &&
	expect_rejected_dir
'

test_expect_success 'path exist as different key' '
	but config --global foo.bar "$(pwd)" &&
	expect_rejected_dir
'

test_expect_success 'safe.directory matches' '
	but config --global --add safe.directory "$(pwd)" &&
	but status
'

test_expect_success 'safe.directory matches, but is reset' '
	but config --global --add safe.directory "" &&
	expect_rejected_dir
'

test_expect_success 'safe.directory=*' '
	but config --global --add safe.directory "*" &&
	but status
'

test_expect_success 'safe.directory=*, but is reset' '
	but config --global --add safe.directory "" &&
	expect_rejected_dir
'

test_done
