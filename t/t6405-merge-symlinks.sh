#!/bin/sh
#
# Copyright (c) 2007 Johannes Sixt
#

test_description='merging symlinks on filesystem w/o symlink support.

This tests that git merge-recursive writes merge results as plain files
if core.symlinks is false.'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	git config core.symlinks false &&
	>file &&
	git add file &&
	git commit -m initial &&
	git branch b-symlink &&
	git branch b-file &&
	l=$(printf file | git hash-object -t blob -w --stdin) &&
	echo "120000 $l	symlink" | git update-index --index-info &&
	git commit -m main &&
	git checkout b-symlink &&
	l=$(printf file-different | git hash-object -t blob -w --stdin) &&
	echo "120000 $l	symlink" | git update-index --index-info &&
	git commit -m b-symlink &&
	git checkout b-file &&
	echo plain-file >symlink &&
	git add symlink &&
	git commit -m b-file
'

test_expect_success 'merge main into b-symlink, which has a different symbolic link' '
	git checkout b-symlink &&
	test_must_fail git merge main
'

test_expect_success 'the merge result must be a file' '
	test_path_is_file symlink
'

test_expect_success 'merge main into b-file, which has a file instead of a symbolic link' '
	git reset --hard &&
	git checkout b-file &&
	test_must_fail git merge main
'

test_expect_success 'the merge result must be a file' '
	test_path_is_file symlink
'

test_expect_success 'merge b-file, which has a file instead of a symbolic link, into main' '
	git reset --hard &&
	git checkout main &&
	test_must_fail git merge b-file
'

test_expect_success 'the merge result must be a file' '
	test_path_is_file symlink
'

test_done
