#!/bin/sh

test_description='Test reflog interaction with detached HEAD'
. ./test-lib.sh

reset_state () {
	git checkout master &&
	cp saved_reflog .git/logs/HEAD
}

test_expect_success setup '
	test_tick &&
	git commit --allow-empty -m initial &&
	git branch side &&
	test_tick &&
	git commit --allow-empty -m second &&
	cat .git/logs/HEAD >saved_reflog
'

test_expect_success baseline '
	reset_state &&
	git rev-parse master master^ >expect &&
	git log -g --format=%H >actual &&
	test_cmp expect actual
'

test_expect_success 'switch to branch' '
	reset_state &&
	git rev-parse side master master^ >expect &&
	git checkout side &&
	git log -g --format=%H >actual &&
	test_cmp expect actual
'

test_expect_success 'detach to other' '
	reset_state &&
	git rev-parse master side master master^ >expect &&
	git checkout side &&
	git checkout master^0 &&
	git log -g --format=%H >actual &&
	test_cmp expect actual
'

test_expect_success 'detach to self' '
	reset_state &&
	git rev-parse master master master^ >expect &&
	git checkout master^0 &&
	git log -g --format=%H >actual &&
	test_cmp expect actual
'

test_expect_success 'attach to self' '
	reset_state &&
	git rev-parse master master master master^ >expect &&
	git checkout master^0 &&
	git checkout master &&
	git log -g --format=%H >actual &&
	test_cmp expect actual
'

test_expect_success 'attach to other' '
	reset_state &&
	git rev-parse side master master master^ >expect &&
	git checkout master^0 &&
	git checkout side &&
	git log -g --format=%H >actual &&
	test_cmp expect actual
'

test_done
