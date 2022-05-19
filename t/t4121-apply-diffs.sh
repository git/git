#!/bin/sh

test_description='but apply for contextually independent diffs'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

echo '1
2
3
4
5
6
7
8' >file

test_expect_success 'setup' \
	'but add file &&
	but cummit -q -m 1 &&
	but checkout -b test &&
	mv file file.tmp &&
	echo 0 >file &&
	cat file.tmp >>file &&
	rm file.tmp &&
	but cummit -a -q -m 2 &&
	echo 9 >>file &&
	but cummit -a -q -m 3 &&
	but checkout main'

test_expect_success \
	'check if contextually independent diffs for the same file apply' \
	'( but diff test~2 test~1 && but diff test~1 test~0 )| but apply'

test_done
