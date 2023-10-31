#!/bin/sh

test_description='tests for git branch --track'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
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
	test_grep "cannot be used with updating paths" err
'

test_expect_success 'checkout --track -b overrides autoSetupMerge=inherit' '
	# Set up tracking config on main
	test_config branch.main.remote origin &&
	test_config branch.main.merge refs/heads/some-branch &&
	test_config branch.autoSetupMerge inherit &&
	# With --track=inherit, we copy the tracking config from main
	git checkout --track=inherit -b b1 main &&
	test_cmp_config origin branch.b1.remote &&
	test_cmp_config refs/heads/some-branch branch.b1.merge &&
	# With branch.autoSetupMerge=inherit, we do the same
	git checkout -b b2 main &&
	test_cmp_config origin branch.b2.remote &&
	test_cmp_config refs/heads/some-branch branch.b2.merge &&
	# But --track overrides this
	git checkout --track -b b3 main &&
	test_cmp_config . branch.b3.remote &&
	test_cmp_config refs/heads/main branch.b3.merge &&
	# And --track=direct does as well
	git checkout --track=direct -b b4 main &&
	test_cmp_config . branch.b4.remote &&
	test_cmp_config refs/heads/main branch.b4.merge
'

test_done
