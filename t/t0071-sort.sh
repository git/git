#!/bin/sh

test_description='verify sort functions'

. ./test-lib.sh

test_expect_success 'DEFINE_LIST_SORT_DEBUG' '
	test-tool mergesort test
'

test_done
