#!/bin/sh

test_description="Test the svn importer's input handling routines.
"
. ./test-lib.sh

test_expect_success 'read greeting' '
	echo HELLO >expect &&
	test-line-buffer <<-\EOF >actual &&
	read 5
	HELLO
	EOF
	test_cmp expect actual
'

test_expect_success '0-length read, send along greeting' '
	printf "%s\n" "" HELLO >expect &&
	test-line-buffer <<-\EOF >actual &&
	read 0

	copy 5
	HELLO
	EOF
	test_cmp expect actual
'

test_expect_success 'buffer_read_string copes with trailing null byte' '
	echo >expect &&
	q_to_nul <<-\EOF | test-line-buffer >actual &&
	read 1
	Q
	EOF
	test_cmp expect actual
'

test_expect_success '0-length read, copy null byte' '
	printf "%s\n" "" Q | q_to_nul >expect &&
	q_to_nul <<-\EOF | test-line-buffer >actual &&
	read 0

	copy 1
	Q
	EOF
	test_cmp expect actual
'

test_expect_success 'long reads are truncated' '
	printf "%s\n" foo "" >expect &&
	test-line-buffer <<-\EOF >actual &&
	read 5
	foo
	EOF
	test_cmp expect actual
'

test_expect_success 'long copies are truncated' '
	printf "%s\n" "" foo >expect &&
	test-line-buffer <<-\EOF >actual &&
	read 0

	copy 5
	foo
	EOF
	test_cmp expect actual
'

test_done
