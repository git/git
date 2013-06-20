#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Break and then rename

We have two very different files, file0 and file1, registered in a tree.

We update file1 so drastically that it is more similar to file0, and
then remove file0.  With -B, changes to file1 should be broken into
separate delete and create, resulting in removal of file0, removal of
original file1 and creation of completely rewritten file1.

Further, with -B and -M together, these three modifications should
turn into rename-edit of file0 into file1.

Starting from the same two files in the tree, we swap file0 and file1.
With -B, this should be detected as two complete rewrites, resulting in
four changes in total.

Further, with -B and -M together, these should turn into two renames.
'
. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh ;# test-lib chdir's into trash

test_expect_success \
    setup \
    'cat "$TEST_DIRECTORY"/../README >file0 &&
     cat "$TEST_DIRECTORY"/../COPYING >file1 &&
    git update-index --add file0 file1 &&
    tree=$(git write-tree) &&
    echo "$tree"'

test_expect_success \
    'change file1 with copy-edit of file0 and remove file0' \
    'sed -e "s/git/GIT/" file0 >file1 &&
     rm -f file0 &&
    git update-index --remove file0 file1'

test_expect_success \
    'run diff with -B' \
    'git diff-index -B --cached "$tree" >current'

cat >expected <<\EOF
:100644 000000 f5deac7be59e7eeab8657fd9ae706fd6a57daed2 0000000000000000000000000000000000000000 D	file0
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 11e331465a89c394dc25c780de230043750c1ec8 M100	file1
EOF

test_expect_success \
    'validate result of -B (#1)' \
    'compare_diff_raw expected current'

test_expect_success \
    'run diff with -B and -M' \
    'git diff-index -B -M "$tree" >current'

cat >expected <<\EOF
:100644 100644 f5deac7be59e7eeab8657fd9ae706fd6a57daed2 08bb2fb671deff4c03a4d4a0a1315dff98d5732c R100	file0	file1
EOF

test_expect_success \
    'validate result of -B -M (#2)' \
    'compare_diff_raw expected current'

test_expect_success \
    'swap file0 and file1' \
    'rm -f file0 file1 &&
     git read-tree -m $tree &&
     git checkout-index -f -u -a &&
     mv file0 tmp &&
     mv file1 file0 &&
     mv tmp file1 &&
     git update-index file0 file1'

test_expect_success \
    'run diff with -B' \
    'git diff-index -B "$tree" >current'

cat >expected <<\EOF
:100644 100644 f5deac7be59e7eeab8657fd9ae706fd6a57daed2 6ff87c4664981e4397625791c8ea3bbb5f2279a3 M100	file0
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 f5deac7be59e7eeab8657fd9ae706fd6a57daed2 M100	file1
EOF

test_expect_success \
    'validate result of -B (#3)' \
    'compare_diff_raw expected current'

test_expect_success \
    'run diff with -B and -M' \
    'git diff-index -B -M "$tree" >current'

cat >expected <<\EOF
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 6ff87c4664981e4397625791c8ea3bbb5f2279a3 R100	file1	file0
:100644 100644 f5deac7be59e7eeab8657fd9ae706fd6a57daed2 f5deac7be59e7eeab8657fd9ae706fd6a57daed2 R100	file0	file1
EOF

test_expect_success \
    'validate result of -B -M (#4)' \
    'compare_diff_raw expected current'

test_expect_success \
    'make file0 into something completely different' \
    'rm -f file0 &&
     test_ln_s_add frotz file0 &&
     git update-index file1'

test_expect_success \
    'run diff with -B' \
    'git diff-index -B "$tree" >current'

cat >expected <<\EOF
:100644 120000 f5deac7be59e7eeab8657fd9ae706fd6a57daed2 67be421f88824578857624f7b3dc75e99a8a1481 T	file0
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 f5deac7be59e7eeab8657fd9ae706fd6a57daed2 M100	file1
EOF

test_expect_success \
    'validate result of -B (#5)' \
    'compare_diff_raw expected current'

test_expect_success \
    'run diff with -B -M' \
    'git diff-index -B -M "$tree" >current'

# file0 changed from regular to symlink.  file1 is very close to the preimage of file0.
# because we break file0, file1 can become a rename of it.
cat >expected <<\EOF
:100644 120000 f5deac7be59e7eeab8657fd9ae706fd6a57daed2 67be421f88824578857624f7b3dc75e99a8a1481 T	file0
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 f5deac7be59e7eeab8657fd9ae706fd6a57daed2 R	file0	file1
EOF

test_expect_success \
    'validate result of -B -M (#6)' \
    'compare_diff_raw expected current'

test_expect_success \
    'run diff with -M' \
    'git diff-index -M "$tree" >current'

# This should not mistake file0 as the copy source of new file1
# due to type differences.
cat >expected <<\EOF
:100644 120000 f5deac7be59e7eeab8657fd9ae706fd6a57daed2 67be421f88824578857624f7b3dc75e99a8a1481 T	file0
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 f5deac7be59e7eeab8657fd9ae706fd6a57daed2 M	file1
EOF

test_expect_success \
    'validate result of -M (#7)' \
    'compare_diff_raw expected current'

test_expect_success \
    'file1 edited to look like file0 and file0 rename-edited to file2' \
    'rm -f file0 file1 &&
     git read-tree -m $tree &&
     git checkout-index -f -u -a &&
     sed -e "s/git/GIT/" file0 >file1 &&
     sed -e "s/git/GET/" file0 >file2 &&
     rm -f file0 &&
     git update-index --add --remove file0 file1 file2'

test_expect_success \
    'run diff with -B' \
    'git diff-index -B "$tree" >current'

cat >expected <<\EOF
:100644 000000 f5deac7be59e7eeab8657fd9ae706fd6a57daed2 0000000000000000000000000000000000000000 D	file0
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 08bb2fb671deff4c03a4d4a0a1315dff98d5732c M100	file1
:000000 100644 0000000000000000000000000000000000000000 f5deac7be59e7eeab8657fd9ae706fd6a57daed2 A	file2
EOF

test_expect_success \
    'validate result of -B (#8)' \
    'compare_diff_raw expected current'

test_expect_success \
    'run diff with -B -C' \
    'git diff-index -B -C "$tree" >current'

cat >expected <<\EOF
:100644 100644 f5deac7be59e7eeab8657fd9ae706fd6a57daed2 08bb2fb671deff4c03a4d4a0a1315dff98d5732c C095	file0	file1
:100644 100644 f5deac7be59e7eeab8657fd9ae706fd6a57daed2 59f832e5c8b3f7e486be15ad0cd3e95ba9af8998 R095	file0	file2
EOF

test_expect_success \
    'validate result of -B -M (#9)' \
    'compare_diff_raw expected current'

test_done
