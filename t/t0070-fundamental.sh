#!/bin/sh

test_description='check that the most basic functions work


Verify wrappers and compatibility functions.
'

. ./test-lib.sh

test_expect_success 'character classes (isspace, isalpha etc.)' '
	test-ctype
'

test_done
