#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Rename interaction with pathspec.

'
. ./test-lib.sh
. ../diff-lib.sh ;# test-lib chdir's into trash

test_expect_success \
    'prepare reference tree' \
    'mkdir path0 path1 &&
     cp ../../COPYING path0/COPYING &&
     git update-index --add path0/COPYING &&
    tree=$(git write-tree) &&
    echo $tree'

test_expect_success \
    'prepare work tree' \
    'cp path0/COPYING path1/COPYING &&
     git update-index --add --remove path0/COPYING path1/COPYING'

# In the tree, there is only path0/COPYING.  In the cache, path0 and
# path1 both have COPYING and the latter is a copy of path0/COPYING.
# Comparing the full tree with cache should tell us so.

git diff-index -C --find-copies-harder $tree >current

cat >expected <<\EOF
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 6ff87c4664981e4397625791c8ea3bbb5f2279a3 C100	path0/COPYING	path1/COPYING
EOF

test_expect_success \
    'validate the result (#1)' \
    'compare_diff_raw current expected'

# In the tree, there is only path0/COPYING.  In the cache, path0 and
# path1 both have COPYING and the latter is a copy of path0/COPYING.
# However when we say we care only about path1, we should just see
# path1/COPYING suddenly appearing from nowhere, not detected as
# a copy from path0/COPYING.

git diff-index -C $tree path1 >current

cat >expected <<\EOF
:000000 100644 0000000000000000000000000000000000000000 6ff87c4664981e4397625791c8ea3bbb5f2279a3 A	path1/COPYING
EOF

test_expect_success \
    'validate the result (#2)' \
    'compare_diff_raw current expected'

test_expect_success \
    'tweak work tree' \
    'rm -f path0/COPYING &&
     git update-index --remove path0/COPYING'

# In the tree, there is only path0/COPYING.  In the cache, path0 does
# not have COPYING anymore and path1 has COPYING which is a copy of
# path0/COPYING.  Showing the full tree with cache should tell us about
# the rename.

git diff-index -C $tree >current

cat >expected <<\EOF
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 6ff87c4664981e4397625791c8ea3bbb5f2279a3 R100	path0/COPYING	path1/COPYING
EOF

test_expect_success \
    'validate the result (#3)' \
    'compare_diff_raw current expected'

# In the tree, there is only path0/COPYING.  In the cache, path0 does
# not have COPYING anymore and path1 has COPYING which is a copy of
# path0/COPYING.  When we say we care only about path1, we should just
# see path1/COPYING appearing from nowhere.

git diff-index -C $tree path1 >current

cat >expected <<\EOF
:000000 100644 0000000000000000000000000000000000000000 6ff87c4664981e4397625791c8ea3bbb5f2279a3 A	path1/COPYING
EOF

test_expect_success \
    'validate the result (#4)' \
    'compare_diff_raw current expected'

test_done
