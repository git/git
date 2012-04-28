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

test_expect_success '"upstream" pushes to configured upstream' '
	git checkout master &&
	test_config branch.master.remote parent1 &&
	test_config branch.master.merge refs/heads/foo &&
	test_config push.default upstream &&
	test_commit two &&
	git push &&
	echo two >expect &&
	git --git-dir=repo1 log -1 --format=%s foo >actual &&
	test_cmp expect actual
'

test_expect_success '"upstream" does not push on unconfigured remote' '
	git checkout master &&
	test_unconfig branch.master.remote &&
	test_config push.default upstream &&
	test_commit three &&
	test_must_fail git push
'

test_expect_success '"upstream" does not push on unconfigured branch' '
	git checkout master &&
	test_config branch.master.remote parent1 &&
	test_unconfig branch.master.merge &&
	test_config push.default upstream
	test_commit four &&
	test_must_fail git push
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
