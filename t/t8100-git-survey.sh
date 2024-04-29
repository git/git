#!/bin/sh

test_description='git survey'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=0
export TEST_PASSES_SANITIZE_LEAK

. ./test-lib.sh

test_expect_success 'git survey -h shows experimental warning' '
	test_expect_code 0 git survey -h >usage &&
	test_grep "EXPERIMENTAL!" usage
'

test_done
