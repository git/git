#!/bin/sh

test_description='Peter MacMillan'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	echo Hello >file &&
	git add file &&
	test_tick &&
	git cummit -m V1 &&
	echo Hello world >file &&
	git add file &&
	git checkout -b other
'

test_expect_success 'check all changes are staged' '
	git diff --exit-code
'

test_expect_success 'second cummit' '
	git cummit -m V2
'

test_expect_success 'check' '
	git diff --cached --exit-code
'

test_done
