#!/bin/sh

test_description='Test reflog interaction with detached HEAD'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

reset_state () {
	rm -rf .git && "$TAR" xf .git-saved.tar
}

test_expect_success setup '
	test_tick &&
	git commit --allow-empty -m initial &&
	git branch side &&
	test_tick &&
	git commit --allow-empty -m second &&
	"$TAR" cf .git-saved.tar .git
'

test_expect_success baseline '
	reset_state &&
	git rev-parse main main^ >expect &&
	git log -g --format=%H >actual &&
	test_cmp expect actual
'

test_expect_success 'switch to branch' '
	reset_state &&
	git rev-parse side main main^ >expect &&
	git checkout side &&
	git log -g --format=%H >actual &&
	test_cmp expect actual
'

test_expect_success 'detach to other' '
	reset_state &&
	git rev-parse main side main main^ >expect &&
	git checkout side &&
	git checkout main^0 &&
	git log -g --format=%H >actual &&
	test_cmp expect actual
'

test_expect_success 'detach to self' '
	reset_state &&
	git rev-parse main main main^ >expect &&
	git checkout main^0 &&
	git log -g --format=%H >actual &&
	test_cmp expect actual
'

test_expect_success 'attach to self' '
	reset_state &&
	git rev-parse main main main main^ >expect &&
	git checkout main^0 &&
	git checkout main &&
	git log -g --format=%H >actual &&
	test_cmp expect actual
'

test_expect_success 'attach to other' '
	reset_state &&
	git rev-parse side main main main^ >expect &&
	git checkout main^0 &&
	git checkout side &&
	git log -g --format=%H >actual &&
	test_cmp expect actual
'

test_done
