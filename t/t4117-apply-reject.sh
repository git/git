#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git apply with rejects

'

. ./test-lib.sh

test_expect_success setup '
	test_write_lines 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 >file1 &&
	cat file1 >saved.file1 &&
	git update-index --add file1 &&
	git commit -m initial &&

	test_write_lines 1 2 A B 4 5 6 7 8 9 10 11 12 C 13 14 15 16 17 18 19 20 D 21 >file1 &&
	git diff >patch.1 &&
	cat file1 >clean &&

	test_write_lines 1 E 2 3 4 5 6 7 8 9 10 11 12 C 13 14 15 16 17 18 19 20 F 21 >expected &&

	mv file1 file2 &&
	git update-index --add --remove file1 file2 &&
	git diff -M HEAD >patch.2 &&

	rm -f file1 file2 &&
	mv saved.file1 file1 &&
	git update-index --add --remove file1 file2 &&

	test_write_lines 1 E 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 F 21 >file1 &&

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

	test_must_fail git apply patch.1 &&
	test_cmp file1 saved.file1
'

test_expect_success 'apply without --reject should fail' '

	test_must_fail git apply --verbose patch.1 &&
	test_cmp file1 saved.file1
'

test_expect_success 'apply with --reject should fail but update the file' '

	cat saved.file1 >file1 &&
	rm -f file1.rej file2.rej &&

	test_must_fail git apply --reject patch.1 &&
	test_cmp expected file1 &&

	test_path_is_file file1.rej &&
	test_path_is_missing file2.rej
'

test_expect_success 'apply with --reject should fail but update the file' '

	cat saved.file1 >file1 &&
	rm -f file1.rej file2.rej file2 &&

	test_must_fail git apply --reject patch.2 >rejects &&
	test_path_is_missing file1 &&
	test_cmp expected file2 &&

	test_path_is_file file2.rej &&
	test_path_is_missing file1.rej

'

test_expect_success 'the same test with --verbose' '

	cat saved.file1 >file1 &&
	rm -f file1.rej file2.rej file2 &&

	test_must_fail git apply --reject --verbose patch.2 >rejects &&
	test_path_is_missing file1 &&
	test_cmp expected file2 &&

	test_path_is_file file2.rej &&
	test_path_is_missing file1.rej

'

test_expect_success 'apply cleanly with --verbose' '

	git cat-file -p HEAD:file1 >file1 &&
	rm -f file?.rej file2 &&

	git apply --verbose patch.1 &&

	test_cmp file1 clean
'

test_done
