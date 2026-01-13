#!/bin/sh

test_description='tests for git-history command'

. ./test-lib.sh

test_expect_success 'does nothing without any arguments' '
	git history >out 2>&1 &&
	test_must_be_empty out
'

test_expect_success 'raises an error with unknown argument' '
	test_must_fail git history garbage 2>err &&
	test_grep "unrecognized argument: garbage" err
'

test_done
