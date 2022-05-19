#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='but checkout-index -u test.

With -u flag, but checkout-index internally runs the equivalent of
but update-index --refresh on the checked out entry.'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success \
'preparation' '
echo frotz >path0 &&
but update-index --add path0 &&
t=$(but write-tree)'

test_expect_success \
'without -u, but checkout-index smudges stat information.' '
rm -f path0 &&
but read-tree $t &&
but checkout-index -f -a &&
test_must_fail but diff-files --exit-code'

test_expect_success \
'with -u, but checkout-index picks up stat information from new files.' '
rm -f path0 &&
but read-tree $t &&
but checkout-index -u -f -a &&
but diff-files --exit-code'

test_done
