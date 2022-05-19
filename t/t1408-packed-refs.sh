#!/bin/sh

test_description='packed-refs entries are covered by loose refs'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	test_tick &&
	but cummit --allow-empty -m one &&
	one=$(but rev-parse HEAD) &&
	but for-each-ref >actual &&
	echo "$one cummit	refs/heads/main" >expect &&
	test_cmp expect actual &&

	but pack-refs --all &&
	but for-each-ref >actual &&
	echo "$one cummit	refs/heads/main" >expect &&
	test_cmp expect actual &&

	but checkout --orphan another &&
	test_tick &&
	but cummit --allow-empty -m two &&
	two=$(but rev-parse HEAD) &&
	but checkout -B main &&
	but branch -D another &&

	but for-each-ref >actual &&
	echo "$two cummit	refs/heads/main" >expect &&
	test_cmp expect actual &&

	but reflog expire --expire=now --all &&
	but prune &&
	but tag -m v1.0 v1.0 main
'

test_expect_success 'no error from stale entry in packed-refs' '
	but describe main >actual 2>&1 &&
	echo "v1.0" >expect &&
	test_cmp expect actual
'

test_done
