#!/bin/sh

test_description='check that the most basic functions work


Verify wrappers and compatibility functions.
'

. ./test-lib.sh

test_expect_success 'character classes (isspace, isalpha etc.)' '
	test-ctype
'

test_expect_success 'mktemp to nonexistent directory prints filename' '
	test_must_fail test-mktemp doesnotexist/testXXXXXX 2>err &&
	grep "doesnotexist/test" err
'

test_expect_success POSIXPERM 'mktemp to unwritable directory prints filename' '
	mkdir cannotwrite &&
	chmod -w cannotwrite &&
	test_when_finished "chmod +w cannotwrite" &&
	test_must_fail test-mktemp cannotwrite/testXXXXXX 2>err &&
	grep "cannotwrite/test" err
'

test_done
