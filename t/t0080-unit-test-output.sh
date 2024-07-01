#!/bin/sh

test_description='Test the output of the unit test framework'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'TAP output from unit tests' '
	! test-tool example-tap >actual &&
	test_cmp "$TEST_DIRECTORY"/t0080/expect actual
'

test_done
