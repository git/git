#!/bin/sh
#
# Copyright (c) 2007 Johannes Sixt
#

test_description='git update-index on filesystem w/o symlinks test.

This tests that git update-index keeps the symbolic link property
even if a plain file is in the working tree if core.symlinks is false.'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success \
'preparation' '
git config core.symlinks false &&
l=$(printf file | git hash-object -t blob -w --stdin) &&
echo "120000 $l	symlink" | git update-index --index-info'

test_expect_success \
'modify the symbolic link' '
printf new-file > symlink &&
git update-index symlink'

test_expect_success \
'the index entry must still be a symbolic link' '
case "$(git ls-files --stage --cached symlink)" in
120000" "*symlink) echo pass;;
*) echo fail; git ls-files --stage --cached symlink; false;;
esac'

test_done
