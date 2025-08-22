#!/bin/sh
#
# Copyright (c) 2007 Andy Parkins
#

test_description='for-each-ref test'

. ./test-lib.sh

test_expect_success "for-each-ref does not crash with -h" '
	test_expect_code 129 git for-each-ref -h >usage &&
	test_grep "[Uu]sage: git for-each-ref " usage &&
	test_expect_code 129 nongit git for-each-ref -h >usage &&
	test_grep "[Uu]sage: git for-each-ref " usage
'

. "$TEST_DIRECTORY"/for-each-ref-tests.sh

test_done
