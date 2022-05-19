#!/bin/sh

test_description='test <branch>@{push} syntax'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

resolve () {
	echo "$2" >expect &&
	but rev-parse --symbolic-full-name "$1" >actual &&
	test_cmp expect actual
}

test_expect_success 'setup' '
	but init --bare parent.but &&
	but init --bare other.but &&
	but remote add origin parent.but &&
	but remote add other other.but &&
	test_cummit base &&
	but push origin HEAD &&
	but branch --set-upstream-to=origin/main main &&
	but branch --track topic origin/main &&
	but push origin topic &&
	but push other topic
'

test_expect_success '@{push} with default=nothing' '
	test_config push.default nothing &&
	test_must_fail but rev-parse main@{push} &&
	test_must_fail but rev-parse main@{PUSH} &&
	test_must_fail but rev-parse main@{PuSH}
'

test_expect_success '@{push} with default=simple' '
	test_config push.default simple &&
	resolve main@{push} refs/remotes/origin/main &&
	resolve main@{PUSH} refs/remotes/origin/main &&
	resolve main@{pUSh} refs/remotes/origin/main
'

test_expect_success 'triangular @{push} fails with default=simple' '
	test_config push.default simple &&
	test_must_fail but rev-parse topic@{push}
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
	but push &&
	resolve topic@{push} refs/remotes/origin/magic/topic
'

test_expect_success 'resolving @{push} fails with a detached HEAD' '
	but checkout HEAD^0 &&
	test_when_finished "but checkout -" &&
	test_must_fail but rev-parse @{push}
'

test_done
