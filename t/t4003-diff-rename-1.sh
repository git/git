#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='More rename detection

'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff.sh ;# test-lib chdir's into trash

test_expect_success \
    'prepare reference tree' \
    'COPYING_test_data >COPYING &&
     echo frotz >rezrov &&
    but update-index --add COPYING rezrov &&
    tree=$(but write-tree) &&
    echo $tree'

test_expect_success \
    'prepare work tree' \
    'sed -e 's/HOWEVER/However/' <COPYING >COPYING.1 &&
    sed -e 's/GPL/G.P.L/g' <COPYING >COPYING.2 &&
    rm -f COPYING &&
    but update-index --add --remove COPYING COPYING.?'

# tree has COPYING and rezrov.  work tree has COPYING.1 and COPYING.2,
# both are slightly edited, and unchanged rezrov.  So we say you
# copy-and-edit one, and rename-and-edit the other.  We do not say
# anything about rezrov.

BUT_DIFF_OPTS=--unified=0 but diff-index -C -p $tree >current
cat >expected <<\EOF
diff --but a/COPYING b/COPYING.1
copy from COPYING
copy to COPYING.1
--- a/COPYING
+++ b/COPYING.1
@@ -6 +6 @@
- HOWEVER, in order to allow a migration to GPLv3 if that seems like
+ However, in order to allow a migration to GPLv3 if that seems like
diff --but a/COPYING b/COPYING.2
rename from COPYING
rename to COPYING.2
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
    'validate output from rename/copy detection (#1)' \
    'compare_diff_patch current expected'

test_expect_success \
    'prepare work tree again' \
    'mv COPYING.2 COPYING &&
     but update-index --add --remove COPYING COPYING.1 COPYING.2'

# tree has COPYING and rezrov.  work tree has COPYING and COPYING.1,
# both are slightly edited, and unchanged rezrov.  So we say you
# edited one, and copy-and-edit the other.  We do not say
# anything about rezrov.

BUT_DIFF_OPTS=--unified=0 but diff-index -C -p $tree >current
cat >expected <<\EOF
diff --but a/COPYING b/COPYING
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
diff --but a/COPYING b/COPYING.1
copy from COPYING
copy to COPYING.1
--- a/COPYING
+++ b/COPYING.1
@@ -6 +6 @@
- HOWEVER, in order to allow a migration to GPLv3 if that seems like
+ However, in order to allow a migration to GPLv3 if that seems like
EOF

test_expect_success \
    'validate output from rename/copy detection (#2)' \
    'compare_diff_patch current expected'

test_expect_success \
    'prepare work tree once again' \
    'COPYING_test_data >COPYING &&
     but update-index --add --remove COPYING COPYING.1'

# tree has COPYING and rezrov.  work tree has COPYING and COPYING.1,
# but COPYING is not edited.  We say you copy-and-edit COPYING.1; this
# is only possible because -C mode now reports the unmodified file to
# the diff-core.  Unchanged rezrov, although being fed to
# but diff-index as well, should not be mentioned.

BUT_DIFF_OPTS=--unified=0 \
    but diff-index -C --find-copies-harder -p $tree >current
cat >expected <<\EOF
diff --but a/COPYING b/COPYING.1
copy from COPYING
copy to COPYING.1
--- a/COPYING
+++ b/COPYING.1
@@ -6 +6 @@
- HOWEVER, in order to allow a migration to GPLv3 if that seems like
+ However, in order to allow a migration to GPLv3 if that seems like
EOF

test_expect_success \
    'validate output from rename/copy detection (#3)' \
    'compare_diff_patch current expected'

test_done
