#!/bin/sh

test_description='tests for git branch --track'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit one &&
	test_commit two
'

test_expect_success 'checkout --track -b creates a new tracking branch' '
	git checkout --track -b branch1 master &&
	test $(git rev-parse --abbrev-ref HEAD) = branch1 &&
	test $(git config --get branch.branch1.remote) = . &&
	test $(git config --get branch.branch1.merge) = refs/heads/master
'

test_expect_success 'checkout --track -b rejects an extra path argument' '
	test_must_fail git checkout --track -b branch2 master one.t 2>err &&
	test_i18ngrep "cannot be used with updating paths" err
'

test_done
