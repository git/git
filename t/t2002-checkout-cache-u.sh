#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git-checkout-cache -u test.

With -u flag, git-checkout-cache internally runs the equivalent of
git-update-cache --refresh on the checked out entry.'

. ./test-lib.sh

test_expect_success \
'preparation' '
echo frotz >path0 &&
git-update-cache --add path0 &&
t=$(git-write-tree)'

test_expect_failure \
'without -u, git-checkout-cache smudges stat information.' '
rm -f path0 &&
git-read-tree $t &&
git-checkout-cache -f -a &&
git-diff-files | diff - /dev/null'

test_expect_success \
'with -u, git-checkout-cache picks up stat information from new files.' '
rm -f path0 &&
git-read-tree $t &&
git-checkout-cache -u -f -a &&
git-diff-files | diff - /dev/null'
