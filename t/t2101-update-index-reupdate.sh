#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='but update-index --again test.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'update-index --add' '
	echo hello world >file1 &&
	echo goodbye people >file2 &&
	but update-index --add file1 file2 &&
	but ls-files -s >current &&
	cat >expected <<-EOF &&
	100644 $(but hash-object file1) 0	file1
	100644 $(but hash-object file2) 0	file2
	EOF
	cmp current expected
'

test_expect_success 'update-index --again' '
	rm -f file1 &&
	echo hello everybody >file2 &&
	if but update-index --again
	then
		echo should have refused to remove file1
		exit 1
	else
		echo happy - failed as expected
	fi &&
	but ls-files -s >current &&
	cmp current expected
'

test_expect_success 'update-index --remove --again' '
	but update-index --remove --again &&
	but ls-files -s >current &&
	cat >expected <<-EOF &&
	100644 $(but hash-object file2) 0	file2
	EOF
	cmp current expected
'

test_expect_success 'first cummit' 'but cummit -m initial'

test_expect_success 'update-index again' '
	mkdir -p dir1 &&
	echo hello world >dir1/file3 &&
	echo goodbye people >file2 &&
	but update-index --add file2 dir1/file3 &&
	echo hello everybody >file2 &&
	echo happy >dir1/file3 &&
	but update-index --again &&
	but ls-files -s >current &&
	cat >expected <<-EOF &&
	100644 $(but hash-object dir1/file3) 0	dir1/file3
	100644 $(but hash-object file2) 0	file2
	EOF
	cmp current expected
'

file2=$(but hash-object file2)
test_expect_success 'update-index --update from subdir' '
	echo not so happy >file2 &&
	(cd dir1 &&
	cat ../file2 >file3 &&
	but update-index --again
	) &&
	but ls-files -s >current &&
	cat >expected <<-EOF &&
	100644 $(but hash-object dir1/file3) 0	dir1/file3
	100644 $file2 0	file2
	EOF
	test_cmp expected current
'

test_expect_success 'update-index --update with pathspec' '
	echo very happy >file2 &&
	cat file2 >dir1/file3 &&
	but update-index --again dir1/ &&
	but ls-files -s >current &&
	cat >expected <<-EOF &&
	100644 $(but hash-object dir1/file3) 0	dir1/file3
	100644 $file2 0	file2
	EOF
	cmp current expected
'

test_done
