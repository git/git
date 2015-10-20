#!/bin/sh

test_description='basic git gc tests
'

. ./test-lib.sh

test_expect_success 'gc empty repository' '
	git gc
'

test_expect_success 'gc does not leave behind pid file' '
	git gc &&
	test_path_is_missing .git/gc.pid
'

test_expect_success 'gc --gobbledegook' '
	test_expect_code 129 git gc --nonsense 2>err &&
	test_i18ngrep "[Uu]sage: git gc" err
'

test_expect_success 'gc -h with invalid configuration' '
	mkdir broken &&
	(
		cd broken &&
		git init &&
		echo "[gc] pruneexpire = CORRUPT" >>.git/config &&
		test_expect_code 129 git gc -h >usage 2>&1
	) &&
	test_i18ngrep "[Uu]sage" broken/usage
'

test_expect_success 'gc is not aborted due to a stale symref' '
	git init remote &&
	(
		cd remote &&
		test_commit initial &&
		git clone . ../client &&
		git branch -m develop &&
		cd ../client &&
		git fetch --prune &&
		git gc
	)
'

test_done
