#!/bin/sh

test_description='tests for git branch --track'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit one &&
	test_commit two
'

test_expect_success 'checkout --track -b creates a new tracking branch' '
	git checkout --track -b branch1 main &&
	test $(git rev-parse --abbrev-ref HEAD) = branch1 &&
	test $(git config --get branch.branch1.remote) = . &&
	test $(git config --get branch.branch1.merge) = refs/heads/main
'

test_expect_success 'checkout --track -b rejects an extra path argument' '
	test_must_fail git checkout --track -b branch2 main one.t 2>err &&
	test_i18ngrep "cannot be used with updating paths" err
'

test_done
