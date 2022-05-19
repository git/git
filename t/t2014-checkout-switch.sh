#!/bin/sh

test_description='Peter MacMillan'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	echo Hello >file &&
	but add file &&
	test_tick &&
	but cummit -m V1 &&
	echo Hello world >file &&
	but add file &&
	but checkout -b other
'

test_expect_success 'check all changes are staged' '
	but diff --exit-code
'

test_expect_success 'second cummit' '
	but cummit -m V2
'

test_expect_success 'check' '
	but diff --cached --exit-code
'

test_done
