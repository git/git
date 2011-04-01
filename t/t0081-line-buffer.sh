#!/bin/sh

test_description="Test the svn importer's input handling routines.

These tests provide some simple checks that the line_buffer API
behaves as advertised.

While at it, check that input of newlines and null bytes are handled
correctly.
"
. ./test-lib.sh

test_expect_success 'hello world' '
	echo ">HELLO" >expect &&
	test-line-buffer <<-\EOF >actual &&
	binary 6
	HELLO
	EOF
	test_cmp expect actual
'

test_expect_success '0-length read, send along greeting' '
	echo ">HELLO" >expect &&
	test-line-buffer <<-\EOF >actual &&
	binary 0
	copy 6
	HELLO
	EOF
	test_cmp expect actual
'

test_expect_success NOT_MINGW 'read from file descriptor' '
	rm -f input &&
	echo hello >expect &&
	echo hello >input &&
	echo copy 6 |
	test-line-buffer "&4" 4<input >actual &&
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

test_expect_success 'read null byte' '
	echo ">QhelloQ" | q_to_nul >expect &&
	q_to_nul <<-\EOF | test-line-buffer >actual &&
	binary 8
	QhelloQ
	EOF
	test_cmp expect actual
'

test_expect_success 'long reads are truncated' '
	echo ">foo" >expect &&
	test-line-buffer <<-\EOF >actual &&
	binary 5
	foo
	EOF
	test_cmp expect actual
'

test_expect_success 'long copies are truncated' '
	printf "%s\n" ">" foo >expect &&
	test-line-buffer <<-\EOF >actual &&
	binary 1

	copy 5
	foo
	EOF
	test_cmp expect actual
'

test_expect_success 'long binary reads are truncated' '
	echo ">foo" >expect &&
	test-line-buffer <<-\EOF >actual &&
	binary 5
	foo
	EOF
	test_cmp expect actual
'

test_done
