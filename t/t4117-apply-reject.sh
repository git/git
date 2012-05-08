#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git apply with rejects

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
	cat file1 >clean &&

	for i in 1 E 2 3 4 5 6 7 8 9 10 11 12 C 13 14 15 16 17 18 19 20 F 21
	do
		echo $i
	done >expected &&

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

test_expect_success 'apply --reject is incompatible with --3way' '
	test_when_finished "cat saved.file1 >file1" &&
	git diff >patch.0 &&
	git checkout file1 &&
	test_must_fail git apply --reject --3way patch.0 &&
	git diff --exit-code
'

test_expect_success 'apply without --reject should fail' '

	if git apply patch.1
	then
		echo "Eh? Why?"
		exit 1
	fi

	test_cmp file1 saved.file1
'

test_expect_success 'apply without --reject should fail' '

	if git apply --verbose patch.1
	then
		echo "Eh? Why?"
		exit 1
	fi

	test_cmp file1 saved.file1
'

test_expect_success 'apply with --reject should fail but update the file' '

	cat saved.file1 >file1 &&
	rm -f file1.rej file2.rej &&

	if git apply --reject patch.1
	then
		echo "succeeds with --reject?"
		exit 1
	fi

	test_cmp file1 expected &&

	cat file1.rej &&

	if test -f file2.rej
	then
		echo "file2 should not have been touched"
		exit 1
	fi
'

test_expect_success 'apply with --reject should fail but update the file' '

	cat saved.file1 >file1 &&
	rm -f file1.rej file2.rej file2 &&

	if git apply --reject patch.2 >rejects
	then
		echo "succeeds with --reject?"
		exit 1
	fi

	test -f file1 && {
		echo "file1 still exists?"
		exit 1
	}
	test_cmp file2 expected &&

	cat file2.rej &&

	if test -f file1.rej
	then
		echo "file2 should not have been touched"
		exit 1
	fi

'

test_expect_success 'the same test with --verbose' '

	cat saved.file1 >file1 &&
	rm -f file1.rej file2.rej file2 &&

	if git apply --reject --verbose patch.2 >rejects
	then
		echo "succeeds with --reject?"
		exit 1
	fi

	test -f file1 && {
		echo "file1 still exists?"
		exit 1
	}
	test_cmp file2 expected &&

	cat file2.rej &&

	if test -f file1.rej
	then
		echo "file2 should not have been touched"
		exit 1
	fi

'

test_expect_success 'apply cleanly with --verbose' '

	git cat-file -p HEAD:file1 >file1 &&
	rm -f file?.rej file2 &&

	git apply --verbose patch.1 &&

	test_cmp file1 clean
'

test_done
