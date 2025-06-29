#!/bin/sh
#
# Copyright (c) 2012 Michael Haggerty
#

test_description='Test string list functionality'

. ./test-lib.sh

test_expect_success "test remove_duplicates" '
	test "x-" = "x$(test-tool string-list remove_duplicates -)" &&
	test "x" = "x$(test-tool string-list remove_duplicates "")" &&
	test a = "$(test-tool string-list remove_duplicates a)" &&
	test a = "$(test-tool string-list remove_duplicates a:a)" &&
	test a = "$(test-tool string-list remove_duplicates a:a:a:a:a)" &&
	test a:b = "$(test-tool string-list remove_duplicates a:b)" &&
	test a:b = "$(test-tool string-list remove_duplicates a:a:b)" &&
	test a:b = "$(test-tool string-list remove_duplicates a:b:b)" &&
	test a:b:c = "$(test-tool string-list remove_duplicates a:b:c)" &&
	test a:b:c = "$(test-tool string-list remove_duplicates a:a:b:c)" &&
	test a:b:c = "$(test-tool string-list remove_duplicates a:b:b:c)" &&
	test a:b:c = "$(test-tool string-list remove_duplicates a:b:c:c)" &&
	test a:b:c = "$(test-tool string-list remove_duplicates a:a:b:b:c:c)" &&
	test a:b:c = "$(test-tool string-list remove_duplicates a:a:a:b:b:b:c:c:c)"
'

test_done
