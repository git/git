#!/bin/sh

test_description='test <branch>@{push} syntax'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

resolve () {
	echo "$2" >expect &&
	git rev-parse --symbolic-full-name "$1" >actual &&
	test_cmp expect actual
}

test_expect_success 'setup' '
	git init --bare parent.git &&
	git init --bare other.git &&
	git remote add origin parent.git &&
	git remote add other other.git &&
	test_commit base &&
	git push origin HEAD &&
	git branch --set-upstream-to=origin/main main &&
	git branch --track topic origin/main &&
	git push origin topic &&
	git push other topic
'

test_expect_success '@{push} with default=nothing' '
	test_config push.default nothing &&
	test_must_fail git rev-parse main@{push} &&
	test_must_fail git rev-parse main@{PUSH} &&
	test_must_fail git rev-parse main@{PuSH}
'

test_expect_success '@{push} with default=simple' '
	test_config push.default simple &&
	resolve main@{push} refs/remotes/origin/main &&
	resolve main@{PUSH} refs/remotes/origin/main &&
	resolve main@{pUSh} refs/remotes/origin/main
'

test_expect_success 'triangular @{push} fails with default=simple' '
	test_config push.default simple &&
	test_must_fail git rev-parse topic@{push}
'

test_expect_success '@{push} with default=current' '
	test_config push.default current &&
	resolve topic@{push} refs/remotes/origin/topic
'

test_expect_success '@{push} with default=matching' '
	test_config push.default matching &&
	resolve topic@{push} refs/remotes/origin/topic
'

test_expect_success '@{push} with pushremote defined' '
	test_config push.default current &&
	test_config branch.topic.pushremote other &&
	resolve topic@{push} refs/remotes/other/topic
'

test_expect_success '@{push} with push refspecs' '
	test_config push.default nothing &&
	test_config remote.origin.push refs/heads/*:refs/heads/magic/* &&
	git push &&
	resolve topic@{push} refs/remotes/origin/magic/topic
'

test_expect_success 'resolving @{push} fails with a detached HEAD' '
	git checkout HEAD^0 &&
	test_when_finished "git checkout -" &&
	test_must_fail git rev-parse @{push}
'

test_done
