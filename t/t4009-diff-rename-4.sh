#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Same rename detection as t4003 but testing diff-raw -z.

'
. ./test-lib.sh
. ../diff-lib.sh ;# test-lib chdir's into trash

test_expect_success \
    'prepare reference tree' \
    'cat ../../COPYING >COPYING &&
     echo frotz >rezrov &&
    git update-index --add COPYING rezrov &&
    tree=$(git write-tree) &&
    echo $tree'

test_expect_success \
    'prepare work tree' \
    'sed -e 's/HOWEVER/However/' <COPYING >COPYING.1 &&
    sed -e 's/GPL/G.P.L/g' <COPYING >COPYING.2 &&
    rm -f COPYING &&
    git update-index --add --remove COPYING COPYING.?'

# tree has COPYING and rezrov.  work tree has COPYING.1 and COPYING.2,
# both are slightly edited, and unchanged rezrov.  We say COPYING.1
# and COPYING.2 are based on COPYING, and do not say anything about
# rezrov.

git diff-index -z -M $tree >current

cat >expected <<\EOF
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 0603b3238a076dc6c8022aedc6648fa523a17178 C1234
COPYING
COPYING.1
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 06c67961bbaed34a127f76d261f4c0bf73eda471 R1234
COPYING
COPYING.2
EOF

test_expect_success \
    'validate output from rename/copy detection (#1)' \
    'compare_diff_raw_z current expected'

################################################################

test_expect_success \
    'prepare work tree again' \
    'mv COPYING.2 COPYING &&
     git update-index --add --remove COPYING COPYING.1 COPYING.2'

# tree has COPYING and rezrov.  work tree has COPYING and COPYING.1,
# both are slightly edited, and unchanged rezrov.  We say COPYING.1
# is based on COPYING and COPYING is still there, and do not say anything
# about rezrov.

git diff-index -z -C $tree >current
cat >expected <<\EOF
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 06c67961bbaed34a127f76d261f4c0bf73eda471 M
COPYING
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 0603b3238a076dc6c8022aedc6648fa523a17178 C1234
COPYING
COPYING.1
EOF

test_expect_success \
    'validate output from rename/copy detection (#2)' \
    'compare_diff_raw_z current expected'

################################################################

# tree has COPYING and rezrov.  work tree has the same COPYING and
# copy-edited COPYING.1, and unchanged rezrov.  We should not say
# anything about rezrov nor COPYING, since the revised again diff-raw
# nows how to say Copy.

test_expect_success \
    'prepare work tree once again' \
    'cat ../../COPYING >COPYING &&
     git update-index --add --remove COPYING COPYING.1'

git diff-index -z -C --find-copies-harder $tree >current
cat >expected <<\EOF
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 0603b3238a076dc6c8022aedc6648fa523a17178 C1234
COPYING
COPYING.1
EOF

test_expect_success \
    'validate output from rename/copy detection (#3)' \
    'compare_diff_raw_z current expected'

test_done
