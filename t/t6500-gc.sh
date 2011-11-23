#!/bin/sh

test_description='basic git gc tests
'

. ./test-lib.sh

test_expect_success 'gc empty repository' '
	git gc
'

test_expect_success 'gc --gobbledegook' '
	test_expect_code 129 git gc --nonsense 2>err &&
	grep "[Uu]sage: git gc" err
'

test_expect_success 'gc -h with invalid configuration' '
	mkdir broken &&
	(
		cd broken &&
		git init &&
		echo "[gc] pruneexpire = CORRUPT" >>.git/config &&
		test_expect_code 129 git gc -h >usage 2>&1
	) &&
	grep "[Uu]sage" broken/usage
'

test_done
