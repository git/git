#!/bin/sh
#
# Copyright (c) 2010 Bo Yang
#

test_description='Test --follow should always find copies hard in but log.

'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff.sh

echo >path0 'Line 1
Line 2
Line 3
'

test_expect_success \
    'add a file path0 and cummit.' \
    'but add path0 &&
     but cummit -m "Add path0"'

echo >path0 'New line 1
New line 2
New line 3
'
test_expect_success \
    'Change path0.' \
    'but add path0 &&
     but cummit -m "Change path0"'

cat <path0 >path1
test_expect_success \
    'copy path0 to path1.' \
    'but add path1 &&
     but cummit -m "Copy path1 from path0"'

test_expect_success \
    'find the copy path0 -> path1 harder' \
    'but log --follow --name-status --pretty="format:%s"  path1 > current'

cat >expected <<\EOF
Copy path1 from path0
C100	path0	path1

Change path0
M	path0

Add path0
A	path0
EOF

test_expect_success \
    'validate the output.' \
    'compare_diff_patch current expected'

test_done
