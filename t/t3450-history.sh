#!/bin/sh

test_description='tests for git-history command'

. ./test-lib.sh

test_expect_success 'does nothing without any arguments' '
	test_must_fail git history 2>err &&
	test_grep "need a subcommand" err
'

test_expect_success 'raises an error with unknown argument' '
	test_must_fail git history garbage 2>err &&
	test_grep "unknown subcommand: .garbage." err
'

test_done
