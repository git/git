#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Rename interaction with pathspec.

'
. ./test-lib.sh

_x40='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x40$_x40$_x40$_x40$_x40$_x40$_x40$_x40"
sanitize_diff_raw='s/ \('"$_x40"'\) \1 \([CR]\)[0-9]*	/ \1 \1 \2#	/'
compare_diff_raw () {
    # When heuristics are improved, the score numbers would change.
    # Ignore them while comparing.
    # Also we do not check SHA1 hash generation in this test, which
    # is a job for t0000-basic.sh

    sed -e "$sanitize_diff_raw" <"$1" >.tmp-1
    sed -e "$sanitize_diff_raw" <"$2" >.tmp-2
    diff -u .tmp-1 .tmp-2 && rm -f .tmp-1 .tmp-2
}

test_expect_success \
    'prepare reference tree' \
    'mkdir path0 path1 &&
     cp ../../COPYING path0/COPYING &&
     git-update-cache --add path0/COPYING &&
    tree=$(git-write-tree) &&
    echo $tree'

test_expect_success \
    'prepare work tree' \
    'cp path0/COPYING path1/COPYING &&
     git-update-cache --add --remove path0/COPYING path1/COPYING'

# In the tree, there is only path0/COPYING.  In the cache, path0 and
# path1 both have COPYING and the latter is a copy of path0/COPYING.
# Comparing the full tree with cache should tell us so.

git-diff-cache -C $tree >current

cat >expected <<\EOF
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 6ff87c4664981e4397625791c8ea3bbb5f2279a3 C100	path0/COPYING	path1/COPYING
EOF

test_expect_success \
    'validate the result' \
    'compare_diff_raw current expected'

# In the tree, there is only path0/COPYING.  In the cache, path0 and
# path1 both have COPYING and the latter is a copy of path0/COPYING.
# When we omit output from path0 it should still be able to tell us
# that path1/COPYING is result from a copy from path0/COPYING, not
# rename, which would imply path0/COPYING is now gone.

git-diff-cache -C $tree path1 >current

cat >expected <<\EOF
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 6ff87c4664981e4397625791c8ea3bbb5f2279a3 C100	path0/COPYING	path1/COPYING
EOF

test_expect_success \
    'validate the result' \
    'compare_diff_raw current expected'

test_expect_success \
    'tweak work tree' \
    'rm -f path0/COPYING &&
     git-update-cache --remove path0/COPYING'

# In the tree, there is only path0/COPYING.  In the cache, path0 does
# not have COPYING anymore and path1 has COPYING which is a copy of
# path0/COPYING.  Showing the full tree with cache should tell us about
# the rename.

git-diff-cache -C $tree >current

cat >expected <<\EOF
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 6ff87c4664981e4397625791c8ea3bbb5f2279a3 R100	path0/COPYING	path1/COPYING
EOF

test_expect_success \
    'validate the result' \
    'compare_diff_raw current expected'

# In the tree, there is only path0/COPYING.  In the cache, path0 does
# not have COPYING anymore and path1 has COPYING which is a copy of
# path0/COPYING.  Even if we restrict the output to path1, it still
# should show us the rename.

git-diff-cache -C $tree path1 >current

cat >expected <<\EOF
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 6ff87c4664981e4397625791c8ea3bbb5f2279a3 R100	path0/COPYING	path1/COPYING
EOF

test_expect_success \
    'validate the result' \
    'compare_diff_raw current expected'

test_done
