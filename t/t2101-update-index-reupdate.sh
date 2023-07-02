#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='git update-index --again test.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'update-index --add' '
	echo hello world >file1 &&
	echo goodbye people >file2 &&
	git update-index --add file1 file2 &&
	git ls-files -s >current &&
	cat >expected <<-EOF &&
	100644 $(git hash-object file1) 0	file1
	100644 $(git hash-object file2) 0	file2
	EOF
	cmp current expected
'

test_expect_success 'update-index --again' '
	rm -f file1 &&
	echo hello everybody >file2 &&
	if git update-index --again
	then
		echo should have refused to remove file1
		exit 1
	else
		echo happy - failed as expected
	fi &&
	git ls-files -s >current &&
	cmp current expected
'

test_expect_success 'update-index --remove --again' '
	git update-index --remove --again &&
	git ls-files -s >current &&
	cat >expected <<-EOF &&
	100644 $(git hash-object file2) 0	file2
	EOF
	cmp current expected
'

test_expect_success 'first commit' 'git commit -m initial'

test_expect_success 'update-index again' '
	mkdir -p dir1 &&
	echo hello world >dir1/file3 &&
	echo goodbye people >file2 &&
	git update-index --add file2 dir1/file3 &&
	echo hello everybody >file2 &&
	echo happy >dir1/file3 &&
	git update-index --again &&
	git ls-files -s >current &&
	cat >expected <<-EOF &&
	100644 $(git hash-object dir1/file3) 0	dir1/file3
	100644 $(git hash-object file2) 0	file2
	EOF
	cmp current expected
'

file2=$(git hash-object file2)
test_expect_success 'update-index --update from subdir' '
	echo not so happy >file2 &&
	(cd dir1 &&
	cat ../file2 >file3 &&
	git update-index --again
	) &&
	git ls-files -s >current &&
	cat >expected <<-EOF &&
	100644 $(git hash-object dir1/file3) 0	dir1/file3
	100644 $file2 0	file2
	EOF
	test_cmp expected current
'

test_expect_success 'update-index --update with pathspec' '
	echo very happy >file2 &&
	cat file2 >dir1/file3 &&
	git update-index --again dir1/ &&
	git ls-files -s >current &&
	cat >expected <<-EOF &&
	100644 $(git hash-object dir1/file3) 0	dir1/file3
	100644 $file2 0	file2
	EOF
	cmp current expected
'

test_done
