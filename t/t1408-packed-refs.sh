#!/bin/sh

test_description='packed-refs entries are covered by loose refs'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	test_tick &&
	git commit --allow-empty -m one &&
	one=$(git rev-parse HEAD) &&
	git for-each-ref >actual &&
	echo "$one commit	refs/heads/main" >expect &&
	test_cmp expect actual &&

	git pack-refs --all &&
	git for-each-ref >actual &&
	echo "$one commit	refs/heads/main" >expect &&
	test_cmp expect actual &&

	git checkout --orphan another &&
	test_tick &&
	git commit --allow-empty -m two &&
	two=$(git rev-parse HEAD) &&
	git checkout -B main &&
	git branch -D another &&

	git for-each-ref >actual &&
	echo "$two commit	refs/heads/main" >expect &&
	test_cmp expect actual &&

	git reflog expire --expire=now --all &&
	git prune &&
	git tag -m v1.0 v1.0 main
'

test_expect_success 'no error from stale entry in packed-refs' '
	git describe main >actual 2>&1 &&
	echo "v1.0" >expect &&
	test_cmp expect actual
'

test_done
