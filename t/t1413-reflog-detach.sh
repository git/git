#!/bin/sh

test_description='Test reflog interaction with detached HEAD'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

reset_state () {
	rm -rf .but && "$TAR" xf .but-saved.tar
}

test_expect_success setup '
	test_tick &&
	but cummit --allow-empty -m initial &&
	but branch side &&
	test_tick &&
	but cummit --allow-empty -m second &&
	"$TAR" cf .but-saved.tar .but
'

test_expect_success baseline '
	reset_state &&
	but rev-parse main main^ >expect &&
	but log -g --format=%H >actual &&
	test_cmp expect actual
'

test_expect_success 'switch to branch' '
	reset_state &&
	but rev-parse side main main^ >expect &&
	but checkout side &&
	but log -g --format=%H >actual &&
	test_cmp expect actual
'

test_expect_success 'detach to other' '
	reset_state &&
	but rev-parse main side main main^ >expect &&
	but checkout side &&
	but checkout main^0 &&
	but log -g --format=%H >actual &&
	test_cmp expect actual
'

test_expect_success 'detach to self' '
	reset_state &&
	but rev-parse main main main^ >expect &&
	but checkout main^0 &&
	but log -g --format=%H >actual &&
	test_cmp expect actual
'

test_expect_success 'attach to self' '
	reset_state &&
	but rev-parse main main main main^ >expect &&
	but checkout main^0 &&
	but checkout main &&
	but log -g --format=%H >actual &&
	test_cmp expect actual
'

test_expect_success 'attach to other' '
	reset_state &&
	but rev-parse side main main main^ >expect &&
	but checkout main^0 &&
	but checkout side &&
	but log -g --format=%H >actual &&
	test_cmp expect actual
'

test_done
