#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git-apply with rejects

'

. ./test-lib.sh

test_expect_success setup '
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21
	do
		echo $i
	done >file1 &&
	cat file1 >saved.file1 &&
	git update-index --add file1 &&
	git commit -m initial &&

	for i in 1 2 A B 4 5 6 7 8 9 10 11 12 C 13 14 15 16 17 18 19 20 D 21
	do
		echo $i
	done >file1 &&
	git diff >patch.1 &&

	mv file1 file2 &&
	git update-index --add --remove file1 file2 &&
	git diff -M HEAD >patch.2 &&

	rm -f file1 file2 &&
	mv saved.file1 file1 &&
	git update-index --add --remove file1 file2 &&

	for i in 1 E 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 F 21
	do
		echo $i
	done >file1 &&

	cat file1 >saved.file1
'

test_expect_success 'apply without --reject should fail' '

	if git apply patch.1
	then
		echo "Eh? Why?"
		exit 1
	fi

	diff -u file1 saved.file1
'

test_expect_success 'apply with --reject should fail but update the file' '

	cat saved.file1 >file1

	if git apply --reject patch.1 >rejects
	then
		echo "succeeds with --reject?"
		exit 1
	fi
	cat rejects
	for i in 1 E 2 3 4 5 6 7 8 9 10 11 12 C 13 14 15 16 17 18 19 20 F 21
	do
		echo $i
	done >expected.file1 &&

	diff -u file1 expected.file1
'

test_expect_success 'apply with --reject should fail but update the file' '

	cat saved.file1 >file1

	if git apply --reject patch.2 >rejects
	then
		echo "succeeds with --reject?"
		exit 1
	fi

	cat rejects

	for i in 1 E 2 3 4 5 6 7 8 9 10 11 12 C 13 14 15 16 17 18 19 20 F 21
	do
		echo $i
	done >expected.file2 &&

	test -f file1 && {
		echo "file1 still exists?"
		exit 1
	}
	diff -u file2 expected.file2
'

test_done
