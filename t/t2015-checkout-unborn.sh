#!/bin/sh

test_description='checkout from unborn branch'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir parent &&
	(cd parent &&
	 but init &&
	 echo content >file &&
	 but add file &&
	 but cummit -m base
	) &&
	but fetch parent main:origin
'

test_expect_success 'checkout from unborn preserves untracked files' '
	echo precious >expect &&
	echo precious >file &&
	test_must_fail but checkout -b new origin &&
	test_cmp expect file
'

test_expect_success 'checkout from unborn preserves index contents' '
	echo precious >expect &&
	echo precious >file &&
	but add file &&
	test_must_fail but checkout -b new origin &&
	test_cmp expect file &&
	but show :file >file &&
	test_cmp expect file
'

test_expect_success 'checkout from unborn merges identical index contents' '
	echo content >file &&
	but add file &&
	but checkout -b new origin
'

test_expect_success 'checking out another branch from unborn state' '
	but checkout --orphan newroot &&
	but checkout -b anothername &&
	test_must_fail but show-ref --verify refs/heads/newroot &&
	but symbolic-ref HEAD >actual &&
	echo refs/heads/anothername >expect &&
	test_cmp expect actual
'

test_expect_success 'checking out in a newly created repo' '
	test_create_repo empty &&
	(
		cd empty &&
		but symbolic-ref HEAD >expect &&
		test_must_fail but checkout &&
		but symbolic-ref HEAD >actual &&
		test_cmp expect actual
	)
'

test_done
