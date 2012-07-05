#!/bin/sh

test_description='diff --no-index'

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir a &&
	mkdir b &&
	echo 1 >a/1 &&
	echo 2 >a/2 &&
	git init repo &&
	echo 1 >repo/a &&
	mkdir -p non/git &&
	echo 1 >non/git/a &&
	echo 1 >non/git/b
'

test_expect_success 'git diff --no-index directories' '
	git diff --no-index a b >cnt
	test $? = 1 && test_line_count = 14 cnt
'

test_expect_success 'git diff --no-index relative path outside repo' '
	(
		cd repo &&
		test_expect_code 0 git diff --no-index a ../non/git/a &&
		test_expect_code 0 git diff --no-index ../non/git/a ../non/git/b
	)
'

test_done
