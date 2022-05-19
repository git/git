#!/bin/sh
#
# Copyright (c) 2007 Johannes Sixt
#

test_description='but checkout-index on filesystem w/o symlinks test.

This tests that but checkout-index creates a symbolic link as a plain
file if core.symlinks is false.'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success \
'preparation' '
but config core.symlinks false &&
l=$(printf file | but hash-object -t blob -w --stdin) &&
echo "120000 $l	symlink" | but update-index --index-info'

test_expect_success \
'the checked-out symlink must be a file' '
but checkout-index symlink &&
test -f symlink'

test_expect_success \
'the file must be the blob we added during the setup' '
test "$(but hash-object -t blob symlink)" = $l'

test_done
