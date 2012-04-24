#!/bin/sh

test_description='check various push.default settings'
. ./test-lib.sh

test_expect_success 'setup bare remotes' '
	git init --bare repo1 &&
	git remote add parent1 repo1 &&
	git init --bare repo2 &&
	git remote add parent2 repo2 &&
	test_commit one &&
	git push parent1 HEAD &&
	git push parent2 HEAD
'

# $1 = local revision
# $2 = remote revision (tested to be equal to the local one)
check_pushed_commit () {
	git log -1 --format='%h %s' "$1" >expect &&
	git --git-dir=repo1 log -1 --format='%h %s' "$2" >actual &&
	test_cmp expect actual
}

# $1 = push.default value
# $2 = expected target branch for the push
test_push_success () {
	git -c push.default="$1" push &&
	check_pushed_commit HEAD "$2"
}

# $1 = push.default value
# check that push fails and does not modify any remote branch
test_push_failure () {
	git --git-dir=repo1 log --no-walk --format='%h %s' --all >expect &&
	test_must_fail git -c push.default="$1" push &&
	git --git-dir=repo1 log --no-walk --format='%h %s' --all >actual &&
	test_cmp expect actual
}

test_expect_success '"upstream" pushes to configured upstream' '
	git checkout master &&
	test_config branch.master.remote parent1 &&
	test_config branch.master.merge refs/heads/foo &&
	test_commit two &&
	test_push_success upstream foo
'

test_expect_success '"upstream" does not push on unconfigured remote' '
	git checkout master &&
	test_unconfig branch.master.remote &&
	test_config push.default upstream &&
	test_commit three &&
	test_push_failure upstream
'

test_expect_success '"upstream" does not push on unconfigured branch' '
	git checkout master &&
	test_config branch.master.remote parent1 &&
	test_unconfig branch.master.merge &&
	test_config push.default upstream
	test_commit four &&
	test_push_failure upstream
'

test_expect_success '"upstream" does not push when remotes do not match' '
	git checkout master &&
	test_config branch.master.remote parent1 &&
	test_config branch.master.merge refs/heads/foo &&
	test_config push.default upstream &&
	test_commit five &&
	test_must_fail git push parent2
'

test_done
