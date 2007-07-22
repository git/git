#!/bin/sh
#
# Copyright (c) 2007 Johannes Sixt
#

test_description='git checkout-index on filesystem w/o symlinks test.

This tests that git checkout-index creates a symbolic link as a plain
file if core.symlinks is false.'

. ./test-lib.sh

test_expect_success \
'preparation' '
git config core.symlinks false &&
l=$(echo -n file | git-hash-object -t blob -w --stdin) &&
echo "120000 $l	symlink" | git update-index --index-info'

test_expect_success \
'the checked-out symlink must be a file' '
git checkout-index symlink &&
test -f symlink'

test_expect_success \
'the file must be the blob we added during the setup' '
test "$(git-hash-object -t blob symlink)" = $l'

test_done
