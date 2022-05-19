#!/bin/sh
#
# Copyright (c) 2008 Dmitry V. Levin
#

test_description='fetch exit status test'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '

	>file &&
	but add file &&
	but cummit -m initial &&

	but checkout -b side &&
	echo side >file &&
	but cummit -a -m side &&

	but checkout main &&
	echo next >file &&
	but cummit -a -m next
'

test_expect_success 'non-fast-forward fetch' '

	test_must_fail but fetch . main:side

'

test_expect_success 'forced update' '

	but fetch . +main:side

'

test_done
