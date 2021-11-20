#!/bin/sh

test_description='verify sort functions'

. ./test-lib.sh

test_expect_success 'llist_mergesort()' '
	test-tool mergesort test
'

test_done
