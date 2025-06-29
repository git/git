#!/bin/sh
#
# Copyright (c) 2012 Michael Haggerty
#

test_description='Test string list functionality'

. ./test-lib.sh

test_expect_success "test filter_string_list" '
	test "x-" = "x$(test-tool string-list filter - y)" &&
	test "x-" = "x$(test-tool string-list filter no y)" &&
	test yes = "$(test-tool string-list filter yes y)" &&
	test yes = "$(test-tool string-list filter no:yes y)" &&
	test yes = "$(test-tool string-list filter yes:no y)" &&
	test y1:y2 = "$(test-tool string-list filter y1:y2 y)" &&
	test y2:y1 = "$(test-tool string-list filter y2:y1 y)" &&
	test "x-" = "x$(test-tool string-list filter x1:x2 y)"
'

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
