#!/bin/sh

test_description='but update-index --assume-unchanged test.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	: >file &&
	but add file &&
	but cummit -m initial &&
	but branch other &&
	echo upstream >file &&
	but add file &&
	but cummit -m upstream
'

test_expect_success 'do not switch branches with dirty file' '
	but reset --hard &&
	but checkout other &&
	echo dirt >file &&
	but update-index --assume-unchanged file &&
	test_must_fail but checkout - 2>err &&
	test_i18ngrep overwritten err
'

test_done
