#!/bin/sh

test_description='verify sort functions'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'DEFINE_LIST_SORT_DEBUG' '
	test-tool mergesort test
'

test_done
