#!/bin/sh
#
# Copyright (c) 2007 Johannes Sixt
#

test_description='merging symlinks on filesystem w/o symlink support.

This tests that but merge-recursive writes merge results as plain files
if core.symlinks is false.'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	but config core.symlinks false &&
	>file &&
	but add file &&
	but cummit -m initial &&
	but branch b-symlink &&
	but branch b-file &&
	l=$(printf file | but hash-object -t blob -w --stdin) &&
	echo "120000 $l	symlink" | but update-index --index-info &&
	but cummit -m main &&
	but checkout b-symlink &&
	l=$(printf file-different | but hash-object -t blob -w --stdin) &&
	echo "120000 $l	symlink" | but update-index --index-info &&
	but cummit -m b-symlink &&
	but checkout b-file &&
	echo plain-file >symlink &&
	but add symlink &&
	but cummit -m b-file
'

test_expect_success 'merge main into b-symlink, which has a different symbolic link' '
	but checkout b-symlink &&
	test_must_fail but merge main
'

test_expect_success 'the merge result must be a file' '
	test_path_is_file symlink
'

test_expect_success 'merge main into b-file, which has a file instead of a symbolic link' '
	but reset --hard &&
	but checkout b-file &&
	test_must_fail but merge main
'

test_expect_success 'the merge result must be a file' '
	test_path_is_file symlink
'

test_expect_success 'merge b-file, which has a file instead of a symbolic link, into main' '
	but reset --hard &&
	but checkout main &&
	test_must_fail but merge b-file
'

test_expect_success 'the merge result must be a file' '
	test_path_is_file symlink
'

test_done
