#!/bin/sh

test_description='checkout from unborn branch protects contents'
. ./test-lib.sh

test_expect_success 'setup' '
	mkdir parent &&
	(cd parent &&
	 git init &&
	 echo content >file &&
	 git add file &&
	 git commit -m base
	) &&
	git fetch parent master:origin
'

test_expect_success 'checkout from unborn preserves untracked files' '
	echo precious >expect &&
	echo precious >file &&
	test_must_fail git checkout -b new origin &&
	test_cmp expect file
'

test_expect_success 'checkout from unborn preserves index contents' '
	echo precious >expect &&
	echo precious >file &&
	git add file &&
	test_must_fail git checkout -b new origin &&
	test_cmp expect file &&
	git show :file >file &&
	test_cmp expect file
'

test_expect_success 'checkout from unborn merges identical index contents' '
	echo content >file &&
	git add file &&
	git checkout -b new origin
'

test_done
