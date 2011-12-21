#!/bin/sh

test_description='git apply should exit non-zero with unrecognized input.'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit 1
'

test_expect_success 'apply --check exits non-zero with unrecognized input' '
	test_must_fail git apply --check - <<-\EOF
	I am not a patch
	I look nothing like a patch
	git apply must fail
	EOF
'

test_done
