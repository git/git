#!/bin/sh

test_description="Test the svn importer's input handling routines.

These tests exercise the line_buffer library, but their real purpose
is to check the assumptions that library makes of the platform's input
routines.  Processes engaged in bi-directional communication would
hang if fread or fgets is too greedy.

While at it, check that input of newlines and null bytes are handled
correctly.
"
. ./test-lib.sh

test -n "$GIT_REMOTE_SVN_TEST_BIG_FILES" && test_set_prereq EXPENSIVE

generate_tens_of_lines () {
	tens=$1 &&
	line=$2 &&

	i=0 &&
	while test $i -lt "$tens"
	do
		for j in a b c d e f g h i j
		do
			echo "$line"
		done &&
		: $((i = $i + 1)) ||
		return
	done
}

long_read_test () {
	: each line is 10 bytes, including newline &&
	line=abcdefghi &&
	echo "$line" >expect &&

	if ! test_declared_prereq PIPE
	then
		echo >&4 "long_read_test: need to declare PIPE prerequisite"
		return 127
	fi &&
	tens_of_lines=$(($1 / 100 + 1)) &&
	lines=$(($tens_of_lines * 10)) &&
	readsize=$((($lines - 1) * 10 + 3)) &&
	copysize=7 &&
	rm -f input &&
	mkfifo input &&
	{
		(
			generate_tens_of_lines $tens_of_lines "$line" &&
			exec sleep 100
		) >input &
	} &&
	test-line-buffer input <<-EOF >output &&
	binary $readsize
	copy $copysize
	EOF
	kill $! &&
	test_line_count = $lines output &&
	tail -n 1 <output >actual &&
	test_cmp expect actual
}

test_expect_success 'setup: have pipes?' '
      rm -f frob &&
      if mkfifo frob
      then
		test_set_prereq PIPE
      fi
'

test_expect_success 'hello world' '
	echo ">HELLO" >expect &&
	test-line-buffer <<-\EOF >actual &&
	binary 6
	HELLO
	EOF
	test_cmp expect actual
'

test_expect_success PIPE '0-length read, no input available' '
	printf ">" >expect &&
	rm -f input &&
	mkfifo input &&
	{
		sleep 100 >input &
	} &&
	test-line-buffer input <<-\EOF >actual &&
	binary 0
	copy 0
	EOF
	kill $! &&
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

test_expect_success PIPE '1-byte read, no input available' '
	printf ">%s" ab >expect &&
	rm -f input &&
	mkfifo input &&
	{
		(
			printf "%s" a &&
			printf "%s" b &&
			exec sleep 100
		) >input &
	} &&
	test-line-buffer input <<-\EOF >actual &&
	binary 1
	copy 1
	EOF
	kill $! &&
	test_cmp expect actual
'

test_expect_success PIPE 'long read (around 8192 bytes)' '
	long_read_test 8192
'

test_expect_success PIPE,EXPENSIVE 'longer read (around 65536 bytes)' '
	long_read_test 65536
'

test_expect_success 'read from file descriptor' '
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
