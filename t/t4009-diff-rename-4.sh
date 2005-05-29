#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Same rename detection as t4003 but testing diff-raw -z.

'
. ./test-lib.sh

_x40='[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]'
_x40="$_x40$_x40$_x40$_x40$_x40$_x40$_x40$_x40"
sanitize_diff_raw='/^:/s/ '"$_x40"' '"$_x40"' \([A-Z]\)[0-9]*$/ X X \1#/'
compare_diff_raw () {
    # When heuristics are improved, the score numbers would change.
    # Ignore them while comparing.
    # Also we do not check SHA1 hash generation in this test, which
    # is a job for t0000-basic.sh

    tr '\0' '\012' <"$1" | sed -e "$sanitize_diff_raw" >.tmp-1
    tr '\0' '\012' <"$2" | sed -e "$sanitize_diff_raw" >.tmp-2
    diff -u .tmp-1 .tmp-2 && rm -f .tmp-1 .tmp-2
}

compare_diff_patch () {
    # When heuristics are improved, the score numbers would change.
    # Ignore them while comparing.
    sed -e '/^similarity index [0-9]*%$/d' <"$1" >.tmp-1
    sed -e '/^similarity index [0-9]*%$/d' <"$2" >.tmp-2
    diff -u .tmp-1 .tmp-2 && rm -f .tmp-1 .tmp-2
}

test_expect_success \
    'prepare reference tree' \
    'cat ../../COPYING >COPYING &&
     echo frotz >rezrov &&
    git-update-cache --add COPYING rezrov &&
    tree=$(git-write-tree) &&
    echo $tree'

test_expect_success \
    'prepare work tree' \
    'sed -e 's/HOWEVER/However/' <COPYING >COPYING.1 &&
    sed -e 's/GPL/G.P.L/g' <COPYING >COPYING.2 &&
    rm -f COPYING &&
    git-update-cache --add --remove COPYING COPYING.?'

# tree has COPYING and rezrov.  work tree has COPYING.1 and COPYING.2,
# both are slightly edited, and unchanged rezrov.  We say COPYING.1
# and COPYING.2 are based on COPYING, and do not say anything about
# rezrov.

git-diff-cache -z -M $tree >current

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
    'compare_diff_raw current expected'

# make sure diff-helper can grok it.
mv current diff-raw
GIT_DIFF_OPTS=--unified=0 git-diff-helper -z <diff-raw >current
cat >expected <<\EOF
diff --git a/COPYING b/COPYING.1
copy from COPYING
copy to COPYING.1
--- a/COPYING
+++ b/COPYING.1
@@ -6 +6 @@
- HOWEVER, in order to allow a migration to GPLv3 if that seems like
+ However, in order to allow a migration to GPLv3 if that seems like
diff --git a/COPYING b/COPYING.2
rename old COPYING
rename new COPYING.2
--- a/COPYING
+++ b/COPYING.2
@@ -2 +2 @@
- Note that the only valid version of the GPL as far as this project
+ Note that the only valid version of the G.P.L as far as this project
@@ -6 +6 @@
- HOWEVER, in order to allow a migration to GPLv3 if that seems like
+ HOWEVER, in order to allow a migration to G.P.Lv3 if that seems like
@@ -12 +12 @@
-	This file is licensed under the GPL v2, or a later version
+	This file is licensed under the G.P.L v2, or a later version
EOF

test_expect_success \
    'validate output from diff-helper (#1)' \
    'compare_diff_patch current expected'

################################################################

test_expect_success \
    'prepare work tree again' \
    'mv COPYING.2 COPYING &&
     git-update-cache --add --remove COPYING COPYING.1 COPYING.2'

# tree has COPYING and rezrov.  work tree has COPYING and COPYING.1,
# both are slightly edited, and unchanged rezrov.  We say COPYING.1
# is based on COPYING and COPYING is still there, and do not say anything
# about rezrov.

git-diff-cache -z -C $tree >current
cat >expected <<\EOF
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 06c67961bbaed34a127f76d261f4c0bf73eda471 M
COPYING
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 0603b3238a076dc6c8022aedc6648fa523a17178 C1234
COPYING
COPYING.1
EOF

test_expect_success \
    'validate output from rename/copy detection (#2)' \
    'compare_diff_raw current expected'

# make sure diff-helper can grok it.
mv current diff-raw
GIT_DIFF_OPTS=--unified=0 git-diff-helper -z <diff-raw >current
cat >expected <<\EOF
diff --git a/COPYING b/COPYING
--- a/COPYING
+++ b/COPYING
@@ -2 +2 @@
- Note that the only valid version of the GPL as far as this project
+ Note that the only valid version of the G.P.L as far as this project
@@ -6 +6 @@
- HOWEVER, in order to allow a migration to GPLv3 if that seems like
+ HOWEVER, in order to allow a migration to G.P.Lv3 if that seems like
@@ -12 +12 @@
-	This file is licensed under the GPL v2, or a later version
+	This file is licensed under the G.P.L v2, or a later version
diff --git a/COPYING b/COPYING.1
copy from COPYING
copy to COPYING.1
--- a/COPYING
+++ b/COPYING.1
@@ -6 +6 @@
- HOWEVER, in order to allow a migration to GPLv3 if that seems like
+ However, in order to allow a migration to GPLv3 if that seems like
EOF

test_expect_success \
    'validate output from diff-helper (#2)' \
    'compare_diff_patch current expected'

################################################################

# tree has COPYING and rezrov.  work tree has the same COPYING and
# copy-edited COPYING.1, and unchanged rezrov.  We should not say
# anything about rezrov nor COPYING, since the revised again diff-raw
# nows how to say Copy.

test_expect_success \
    'prepare work tree once again' \
    'cat ../../COPYING >COPYING &&
     git-update-cache --add --remove COPYING COPYING.1'

git-diff-cache -z -C $tree >current
cat >expected <<\EOF
:100644 100644 6ff87c4664981e4397625791c8ea3bbb5f2279a3 0603b3238a076dc6c8022aedc6648fa523a17178 C1234
COPYING
COPYING.1
EOF

test_expect_success \
    'validate output from rename/copy detection (#3)' \
    'compare_diff_raw current expected'

# make sure diff-helper can grok it.
mv current diff-raw
GIT_DIFF_OPTS=--unified=0 git-diff-helper -z <diff-raw >current
cat >expected <<\EOF
diff --git a/COPYING b/COPYING.1
copy from COPYING
copy to COPYING.1
--- a/COPYING
+++ b/COPYING.1
@@ -6 +6 @@
- HOWEVER, in order to allow a migration to GPLv3 if that seems like
+ However, in order to allow a migration to GPLv3 if that seems like
EOF

test_expect_success \
    'validate output from diff-helper (#3)' \
    'compare_diff_patch current expected'

test_done
