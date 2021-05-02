#!/bin/sh

test_description='cherry-pick should rerere for conflicts'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	test_commit foo &&
	test_commit foo-main foo &&
	test_commit bar-main bar &&

	git checkout -b dev foo &&
	test_commit foo-dev foo &&
	test_commit bar-dev bar &&
	git config rerere.enabled true
'

test_expect_success 'conflicting merge' '
	test_must_fail git merge main
'

test_expect_success 'fixup' '
	echo foo-resolved >foo &&
	echo bar-resolved >bar &&
	git commit -am resolved &&
	cp foo foo-expect &&
	cp bar bar-expect &&
	git reset --hard HEAD^
'

test_expect_success 'cherry-pick conflict with --rerere-autoupdate' '
	test_must_fail git cherry-pick --rerere-autoupdate foo..bar-main &&
	test_cmp foo-expect foo &&
	git diff-files --quiet &&
	test_must_fail git cherry-pick --continue &&
	test_cmp bar-expect bar &&
	git diff-files --quiet &&
	git cherry-pick --continue &&
	git reset --hard bar-dev
'

test_expect_success 'cherry-pick conflict respects rerere.autoUpdate' '
	test_config rerere.autoUpdate true &&
	test_must_fail git cherry-pick foo..bar-main &&
	test_cmp foo-expect foo &&
	git diff-files --quiet &&
	test_must_fail git cherry-pick --continue &&
	test_cmp bar-expect bar &&
	git diff-files --quiet &&
	git cherry-pick --continue &&
	git reset --hard bar-dev
'

test_expect_success 'cherry-pick conflict with --no-rerere-autoupdate' '
	test_config rerere.autoUpdate true &&
	test_must_fail git cherry-pick --no-rerere-autoupdate foo..bar-main &&
	test_cmp foo-expect foo &&
	test_must_fail git diff-files --quiet &&
	git add foo &&
	test_must_fail git cherry-pick --continue &&
	test_cmp bar-expect bar &&
	test_must_fail git diff-files --quiet &&
	git add bar &&
	git cherry-pick --continue &&
	git reset --hard bar-dev
'

test_expect_success 'cherry-pick --continue rejects --rerere-autoupdate' '
	test_must_fail git cherry-pick --rerere-autoupdate foo..bar-main &&
	test_cmp foo-expect foo &&
	git diff-files --quiet &&
	test_must_fail git cherry-pick --continue --rerere-autoupdate >actual 2>&1 &&
	echo "fatal: cherry-pick: --rerere-autoupdate cannot be used with --continue" >expect &&
	test_cmp expect actual &&
	test_must_fail git cherry-pick --continue --no-rerere-autoupdate >actual 2>&1 &&
	echo "fatal: cherry-pick: --no-rerere-autoupdate cannot be used with --continue" >expect &&
	test_cmp expect actual &&
	git cherry-pick --abort
'

test_expect_success 'cherry-pick --rerere-autoupdate more than once' '
	test_must_fail git cherry-pick --rerere-autoupdate --rerere-autoupdate foo..bar-main &&
	test_cmp foo-expect foo &&
	git diff-files --quiet &&
	git cherry-pick --abort &&
	test_must_fail git cherry-pick --rerere-autoupdate --no-rerere-autoupdate --rerere-autoupdate foo..bar-main &&
	test_cmp foo-expect foo &&
	git diff-files --quiet &&
	git cherry-pick --abort &&
	test_must_fail git cherry-pick --rerere-autoupdate --no-rerere-autoupdate foo..bar-main &&
	test_must_fail git diff-files --quiet &&
	git cherry-pick --abort
'

test_expect_success 'cherry-pick conflict without rerere' '
	test_config rerere.enabled false &&
	test_must_fail git cherry-pick foo-main &&
	grep ===== foo &&
	grep foo-dev foo &&
	grep foo-main foo
'

test_done
