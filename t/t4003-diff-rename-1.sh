#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='More rename detection

'
. ./test-lib.sh

test_expect_success \
    'prepare reference tree' \
    'cat ../../COPYING >COPYING &&
    git-update-cache --add COPYING &&
    tree=$(git-write-tree) &&
    echo $tree'

test_expect_success \
    'prepare work tree' \
    'sed -e 's/HOWEVER/However/' <COPYING >COPYING.1 &&
    sed -e 's/GPL/G.P.L/g' <COPYING >COPYING.2 &&
    rm -f COPYING &&
    git-update-cache --add --remove COPYING COPYING.?'

# tree has COPYING.  work tree has COPYING.1 and COPYING.2,
# both are slightly edited.  So we say you copy-and-edit one,
# and rename-and-edit the other.

GIT_DIFF_OPTS=-u0 git-diff-cache -M $tree |
sed -e 's/\([0-9][0-9]*\)/#/g' >current &&
cat >expected <<\EOF
diff --git a/COPYING b/COPYING.#
similarity index #%
copy from COPYING
copy to COPYING.#
--- a/COPYING
+++ b/COPYING.#
@@ -# +# @@
- HOWEVER, in order to allow a migration to GPLv# if that seems like
+ However, in order to allow a migration to GPLv# if that seems like
diff --git a/COPYING b/COPYING.#
similarity index #%
rename old COPYING
rename new COPYING.#
--- a/COPYING
+++ b/COPYING.#
@@ -# +# @@
- Note that the only valid version of the GPL as far as this project
+ Note that the only valid version of the G.P.L as far as this project
@@ -# +# @@
- HOWEVER, in order to allow a migration to GPLv# if that seems like
+ HOWEVER, in order to allow a migration to G.P.Lv# if that seems like
@@ -# +# @@
-	This file is licensed under the GPL v#, or a later version
+	This file is licensed under the G.P.L v#, or a later version
EOF

test_expect_success \
    'validate output from rename/copy detection' \
    'diff -u current expected'

test_expect_success \
    'prepare work tree again' \
    'mv COPYING.2 COPYING &&
     git-update-cache --add --remove COPYING COPYING.1 COPYING.2'

# tree has COPYING.  work tree has COPYING and COPYING.1,
# both are slightly edited.  So we say you edited one,
# and copy-and-edit the other.

GIT_DIFF_OPTS=-u0 git-diff-cache -C $tree |
sed -e 's/\([0-9][0-9]*\)/#/g' >current
cat >expected <<\EOF
diff --git a/COPYING b/COPYING.#
similarity index #%
copy from COPYING
copy to COPYING.#
--- a/COPYING
+++ b/COPYING.#
@@ -# +# @@
- HOWEVER, in order to allow a migration to GPLv# if that seems like
+ However, in order to allow a migration to GPLv# if that seems like
diff --git a/COPYING b/COPYING
--- a/COPYING
+++ b/COPYING
@@ -# +# @@
- Note that the only valid version of the GPL as far as this project
+ Note that the only valid version of the G.P.L as far as this project
@@ -# +# @@
- HOWEVER, in order to allow a migration to GPLv# if that seems like
+ HOWEVER, in order to allow a migration to G.P.Lv# if that seems like
@@ -# +# @@
-	This file is licensed under the GPL v#, or a later version
+	This file is licensed under the G.P.L v#, or a later version
EOF

test_expect_success \
    'validate output from rename/copy detection' \
    'diff -u current expected'

test_expect_success \
    'prepare work tree once again' \
    'cat ../../COPYING >COPYING &&
     git-update-cache --add --remove COPYING COPYING.1'

# tree has COPYING.  work tree has the same COPYING and COPYING.1,
# but COPYING is not edited.  We say you copy-and-edit COPYING.1;
# this is only possible because -C mode now reports the unmodified
# file to the diff-core.

GIT_DIFF_OPTS=-u0 git-diff-cache -C $tree |
sed -e 's/\([0-9][0-9]*\)/#/g' >current
cat >expected <<\EOF
diff --git a/COPYING b/COPYING.#
similarity index #%
copy from COPYING
copy to COPYING.#
--- a/COPYING
+++ b/COPYING.#
@@ -# +# @@
- HOWEVER, in order to allow a migration to GPLv# if that seems like
+ However, in order to allow a migration to GPLv# if that seems like
EOF

test_expect_success \
    'validate output from rename/copy detection' \
    'diff -u current expected'

test_done
