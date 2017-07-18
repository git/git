#!/bin/sh

test_description='test <branch>@{push} syntax'
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
	git branch --set-upstream-to=origin/master master &&
	git branch --track topic origin/master &&
	git push origin topic &&
	git push other topic
'

test_expect_success '@{push} with default=nothing' '
	test_config push.default nothing &&
	test_must_fail git rev-parse master@{push}
'

test_expect_success '@{push} with default=simple' '
	test_config push.default simple &&
	resolve master@{push} refs/remotes/origin/master
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

test_done
