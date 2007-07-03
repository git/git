#!/bin/sh
#
# Copyright (c) 2007 Johannes Sixt
#

test_description='merging symlinks on filesystem w/o symlink support.

This tests that git-merge-recursive writes merge results as plain files
if core.symlinks is false.'

. ./test-lib.sh

test_expect_success \
'setup' '
git config core.symlinks false &&
> file &&
git add file &&
git-commit -m initial &&
git branch b-symlink &&
git branch b-file &&
l=$(echo -n file | git-hash-object -t blob -w --stdin) &&
echo "120000 $l	symlink" | git update-index --index-info &&
git-commit -m master &&
git-checkout b-symlink &&
l=$(echo -n file-different | git-hash-object -t blob -w --stdin) &&
echo "120000 $l	symlink" | git update-index --index-info &&
git-commit -m b-symlink &&
git-checkout b-file &&
echo plain-file > symlink &&
git add symlink &&
git-commit -m b-file'

test_expect_failure \
'merge master into b-symlink, which has a different symbolic link' '
! git-checkout b-symlink ||
git-merge master'

test_expect_success \
'the merge result must be a file' '
test -f symlink'

test_expect_failure \
'merge master into b-file, which has a file instead of a symbolic link' '
! (git-reset --hard &&
git-checkout b-file) ||
git-merge master'

test_expect_success \
'the merge result must be a file' '
test -f symlink'

test_expect_failure \
'merge b-file, which has a file instead of a symbolic link, into master' '
! (git-reset --hard &&
git-checkout master) ||
git-merge b-file'

test_expect_success \
'the merge result must be a file' '
test -f symlink'

test_done
