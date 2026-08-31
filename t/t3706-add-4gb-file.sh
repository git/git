#!/bin/sh
#
# Copyright (c) 2026 Johannes Schindelin
#

test_description='add 4GB+ objects'

. ./test-lib.sh

if ! test_have_prereq EXPENSIVE,SIZE_T_IS_64BIT
then
	skip_all='expensive 4GB blob test; enable on 64-bit with GIT_TEST_LONG=true'
	test_done
fi

size_4gb=4294967296

test_expect_success 'set up a 4GB file' '
	test_atexit "rm -f large" &&
	# genrandom takes only an unsigned long...
	test-tool genrandom 123 $(($size_4gb-1)) >large &&
	printf 1 >>large
'

test_expect_success 'add 4GB file' '
	git add large &&
	git cat-file -s :large >size-staged &&
	test $size_4gb = $(cat size-staged)
'
test_expect_success 'read 4GB loose object' '
	git -P show :large >read &&
	test_file_size read >size-read &&
	test $size_4gb = $(cat size-read)
'

test_done
