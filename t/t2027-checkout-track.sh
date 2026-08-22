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

test_expect_success 'checkout --create-if-missing --track creates branch from current branch' '
	test_when_finished "
		git checkout main &&
		git branch -D branch2
	" &&
	git checkout main &&
	git checkout --create-if-missing branch2 --track &&
	test $(git rev-parse --abbrev-ref HEAD) = branch2 &&
	test_cmp_config . branch.branch2.remote &&
	test_cmp_config refs/heads/main branch.branch2.merge
'

test_expect_success 'checkout --create-if-missing --track uses current branch for existing branch' '
	test_when_finished "
		git checkout main &&
		git branch -D branch3 branch3-source
	" &&
	git checkout -b branch3-source main &&
	git branch branch3 main &&
	git checkout --create-if-missing branch3 --track >out 2>err &&
	test_grep "branch '\''branch3'\'' set up to track '\''branch3-source'\''." out &&
	test_grep "Switched to existing branch '\''branch3'\''" err &&
	test_cmp_config . branch.branch3.remote &&
	test_cmp_config refs/heads/branch3-source branch.branch3.merge
'

test_expect_success 'checkout --create-if-missing --track fails from detached HEAD without start-point' '
	test_when_finished "
		git checkout main &&
		git branch -D branch4
	" &&
	git branch branch4 main &&
	git checkout --detach main &&
	test_must_fail git checkout --create-if-missing branch4 --track 2>err &&
	test_grep "cannot set up tracking information; starting point '\''HEAD'\'' is not a branch" err
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

test_expect_success 'ambiguous tracking info' '
	# Set up a few remote repositories
	git init --bare --initial-branch=trunk src1 &&
	git init --bare --initial-branch=trunk src2 &&
	git push src1 one:refs/heads/trunk &&
	git push src2 two:refs/heads/trunk &&

	git remote add -f src1 "file://$PWD/src1" &&
	git remote add -f src2 "file://$PWD/src2" &&

	# DWIM
	test_must_fail git checkout trunk 2>hint.checkout &&
	test_grep "hint: *git checkout --track" hint.checkout &&

	test_must_fail git switch trunk 2>hint.switch &&
	test_grep "hint: *git switch --track" hint.switch
'

test_done
