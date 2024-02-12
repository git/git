#!/bin/sh

test_description='check that example code compiles and runs'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'decorate' '
	test-tool example-decorate
'

test_done
