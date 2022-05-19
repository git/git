#!/bin/sh

test_description='tracking branch update checks for but push'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	echo 1 >file &&
	but add file &&
	but cummit -m 1 &&
	but branch b1 &&
	but branch b2 &&
	but branch b3 &&
	but clone . aa &&
	but checkout b1 &&
	echo b1 >>file &&
	but cummit -a -m b1 &&
	but checkout b2 &&
	echo b2 >>file &&
	but cummit -a -m b2
'

test_expect_success 'prepare pushable branches' '
	cd aa &&
	b1=$(but rev-parse origin/b1) &&
	b2=$(but rev-parse origin/b2) &&
	but checkout -b b1 origin/b1 &&
	echo aa-b1 >>file &&
	but cummit -a -m aa-b1 &&
	but checkout -b b2 origin/b2 &&
	echo aa-b2 >>file &&
	but cummit -a -m aa-b2 &&
	but checkout main &&
	echo aa-main >>file &&
	but cummit -a -m aa-main
'

test_expect_success 'mixed-success push returns error' '
	test_must_fail but push origin :
'

test_expect_success 'check tracking branches updated correctly after push' '
	test "$(but rev-parse origin/main)" = "$(but rev-parse main)"
'

test_expect_success 'check tracking branches not updated for failed refs' '
	test "$(but rev-parse origin/b1)" = "$b1" &&
	test "$(but rev-parse origin/b2)" = "$b2"
'

test_expect_success 'deleted branches have their tracking branches removed' '
	but push origin :b1 &&
	test "$(but rev-parse origin/b1)" = "origin/b1"
'

test_expect_success 'already deleted tracking branches ignored' '
	but branch -d -r origin/b3 &&
	but push origin :b3 >output 2>&1 &&
	! grep "^error: " output
'

test_done
