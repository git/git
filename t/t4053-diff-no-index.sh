#!/bin/sh

test_description='diff --no-index'

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir a &&
	mkdir b &&
	echo 1 >a/1 &&
	echo 2 >a/2
'

test_expect_success 'git diff --no-index directories' '
	git diff --no-index a b >cnt
	test $? = 1 && test_line_count = 14 cnt
'

test_done
