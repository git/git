#!/bin/sh

test_description='git apply handling criss-cross rename patch.'
. ./test-lib.sh

create_file() {
	cnt=0
	while test $cnt -le 100
	do
		cnt=$(($cnt + 1))
		echo "$2" >> "$1"
	done
}

test_expect_success 'setup' '
	create_file file1 "File1 contents" &&
	create_file file2 "File2 contents" &&
	create_file file3 "File3 contents" &&
	git add file1 file2 file3 &&
	git commit -m 1
'

test_expect_success 'criss-cross rename' '
	mv file1 tmp &&
	mv file2 file1 &&
	mv tmp file2 &&
	cp file1 file1-swapped &&
	cp file2 file2-swapped
'

test_expect_success 'diff -M -B' '
	git diff -M -B > diff &&
	git reset --hard

'

test_expect_success 'apply' '
	git apply diff &&
	test_cmp file1 file1-swapped &&
	test_cmp file2 file2-swapped
'

test_expect_success 'criss-cross rename' '
	git reset --hard &&
	mv file1 tmp &&
	mv file2 file1 &&
	mv file3 file2
	mv tmp file3 &&
	cp file1 file1-swapped &&
	cp file2 file2-swapped &&
	cp file3 file3-swapped
'

test_expect_success 'diff -M -B' '
	git diff -M -B > diff &&
	git reset --hard
'

test_expect_success 'apply' '
	git apply diff &&
	test_cmp file1 file1-swapped &&
	test_cmp file2 file2-swapped &&
	test_cmp file3 file3-swapped
'

test_done
