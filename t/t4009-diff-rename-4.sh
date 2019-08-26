#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Same rename detection as t4003 but testing diff-raw -z.

'
. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh ;# test-lib chdir's into trash

test_expect_success \
    'prepare reference tree' \
    'cat "$TEST_DIRECTORY"/diff-lib/COPYING >COPYING &&
     echo frotz >rezrov &&
    git update-index --add COPYING rezrov &&
    orig=$(git hash-object COPYING) &&
    tree=$(git write-tree) &&
    echo $tree'

test_expect_success \
    'prepare work tree' \
    'sed -e 's/HOWEVER/However/' <COPYING >COPYING.1 &&
    sed -e 's/GPL/G.P.L/g' <COPYING >COPYING.2 &&
    rm -f COPYING &&
    c1=$(git hash-object COPYING.1) &&
    c2=$(git hash-object COPYING.2) &&
    git update-index --add --remove COPYING COPYING.?'

# tree has COPYING and rezrov.  work tree has COPYING.1 and COPYING.2,
# both are slightly edited, and unchanged rezrov.  We say COPYING.1
# and COPYING.2 are based on COPYING, and do not say anything about
# rezrov.

git diff-index -z -C $tree >current

cat >expected <<EOF
:100644 100644 $orig $c1 C1234
COPYING
COPYING.1
:100644 100644 $orig $c2 R1234
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
cat >expected <<EOF
:100644 100644 $orig $c2 M
COPYING
:100644 100644 $orig $c1 C1234
COPYING
COPYING.1
EOF

test_expect_success \
    'validate output from rename/copy detection (#2)' \
    'compare_diff_raw_z current expected'

################################################################

# tree has COPYING and rezrov.  work tree has the same COPYING and
# copy-edited COPYING.1, and unchanged rezrov.  We should not say
# anything about rezrov or COPYING, since the revised again diff-raw
# nows how to say Copy.

test_expect_success \
    'prepare work tree once again' \
    'cat "$TEST_DIRECTORY"/diff-lib/COPYING >COPYING &&
     git update-index --add --remove COPYING COPYING.1'

git diff-index -z -C --find-copies-harder $tree >current
cat >expected <<EOF
:100644 100644 $orig $c1 C1234
COPYING
COPYING.1
EOF

test_expect_success \
    'validate output from rename/copy detection (#3)' \
    'compare_diff_raw_z current expected'

test_done
