#!/bin/sh

test_description="Test the svn importer's input handling routines.
"
. ./test-lib.sh

test_expect_success 'read greeting' '
	echo HELLO >expect &&
	test-line-buffer <<-\EOF >actual &&
	read 6
	HELLO
	EOF
	test_cmp expect actual
'

test_expect_success '0-length read, send along greeting' '
	echo HELLO >expect &&
	test-line-buffer <<-\EOF >actual &&
	read 0
	copy 6
	HELLO
	EOF
	test_cmp expect actual
'

test_expect_success 'buffer_read_string copes with null byte' '
	>expect &&
	q_to_nul <<-\EOF | test-line-buffer >actual &&
	read 2
	Q
	EOF
	test_cmp expect actual
'

test_expect_success 'skip, copy null byte' '
	echo Q | q_to_nul >expect &&
	q_to_nul <<-\EOF | test-line-buffer >actual &&
	skip 2
	Q
	copy 2
	Q
	EOF
	test_cmp expect actual
'

test_expect_success 'long reads are truncated' '
	echo foo >expect &&
	test-line-buffer <<-\EOF >actual &&
	read 5
	foo
	EOF
	test_cmp expect actual
'

test_expect_success 'long copies are truncated' '
	printf "%s\n" "" foo >expect &&
	test-line-buffer <<-\EOF >actual &&
	read 1

	copy 5
	foo
	EOF
	test_cmp expect actual
'

test_done
