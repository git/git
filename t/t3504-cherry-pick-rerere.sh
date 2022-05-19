#!/bin/sh

test_description='cherry-pick should rerere for conflicts'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	test_cummit foo &&
	test_cummit foo-main foo &&
	test_cummit bar-main bar &&

	but checkout -b dev foo &&
	test_cummit foo-dev foo &&
	test_cummit bar-dev bar &&
	but config rerere.enabled true
'

test_expect_success 'conflicting merge' '
	test_must_fail but merge main
'

test_expect_success 'fixup' '
	echo foo-resolved >foo &&
	echo bar-resolved >bar &&
	but cummit -am resolved &&
	cp foo foo-expect &&
	cp bar bar-expect &&
	but reset --hard HEAD^
'

test_expect_success 'cherry-pick conflict with --rerere-autoupdate' '
	test_must_fail but cherry-pick --rerere-autoupdate foo..bar-main &&
	test_cmp foo-expect foo &&
	but diff-files --quiet &&
	test_must_fail but cherry-pick --continue &&
	test_cmp bar-expect bar &&
	but diff-files --quiet &&
	but cherry-pick --continue &&
	but reset --hard bar-dev
'

test_expect_success 'cherry-pick conflict repsects rerere.autoUpdate' '
	test_config rerere.autoUpdate true &&
	test_must_fail but cherry-pick foo..bar-main &&
	test_cmp foo-expect foo &&
	but diff-files --quiet &&
	test_must_fail but cherry-pick --continue &&
	test_cmp bar-expect bar &&
	but diff-files --quiet &&
	but cherry-pick --continue &&
	but reset --hard bar-dev
'

test_expect_success 'cherry-pick conflict with --no-rerere-autoupdate' '
	test_config rerere.autoUpdate true &&
	test_must_fail but cherry-pick --no-rerere-autoupdate foo..bar-main &&
	test_cmp foo-expect foo &&
	test_must_fail but diff-files --quiet &&
	but add foo &&
	test_must_fail but cherry-pick --continue &&
	test_cmp bar-expect bar &&
	test_must_fail but diff-files --quiet &&
	but add bar &&
	but cherry-pick --continue &&
	but reset --hard bar-dev
'

test_expect_success 'cherry-pick --continue rejects --rerere-autoupdate' '
	test_must_fail but cherry-pick --rerere-autoupdate foo..bar-main &&
	test_cmp foo-expect foo &&
	but diff-files --quiet &&
	test_must_fail but cherry-pick --continue --rerere-autoupdate >actual 2>&1 &&
	echo "fatal: cherry-pick: --rerere-autoupdate cannot be used with --continue" >expect &&
	test_cmp expect actual &&
	test_must_fail but cherry-pick --continue --no-rerere-autoupdate >actual 2>&1 &&
	echo "fatal: cherry-pick: --no-rerere-autoupdate cannot be used with --continue" >expect &&
	test_cmp expect actual &&
	but cherry-pick --abort
'

test_expect_success 'cherry-pick --rerere-autoupdate more than once' '
	test_must_fail but cherry-pick --rerere-autoupdate --rerere-autoupdate foo..bar-main &&
	test_cmp foo-expect foo &&
	but diff-files --quiet &&
	but cherry-pick --abort &&
	test_must_fail but cherry-pick --rerere-autoupdate --no-rerere-autoupdate --rerere-autoupdate foo..bar-main &&
	test_cmp foo-expect foo &&
	but diff-files --quiet &&
	but cherry-pick --abort &&
	test_must_fail but cherry-pick --rerere-autoupdate --no-rerere-autoupdate foo..bar-main &&
	test_must_fail but diff-files --quiet &&
	but cherry-pick --abort
'

test_expect_success 'cherry-pick conflict without rerere' '
	test_config rerere.enabled false &&
	test_must_fail but cherry-pick foo-main &&
	grep ===== foo &&
	grep foo-dev foo &&
	grep foo-main foo
'

test_done
