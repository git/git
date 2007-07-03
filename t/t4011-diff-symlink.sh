#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='Test diff of symlinks.

'
. ./test-lib.sh
. ../diff-lib.sh

cat > expected << EOF
diff --git a/frotz b/frotz
new file mode 120000
index 0000000..7c465af
--- /dev/null
+++ b/frotz
@@ -0,0 +1 @@
+xyzzy
\ No newline at end of file
EOF

test_expect_success \
    'diff new symlink' \
    'ln -s xyzzy frotz &&
    git update-index &&
    tree=$(git write-tree) &&
    git update-index --add frotz &&
    GIT_DIFF_OPTS=--unified=0 git diff-index -M -p $tree > current &&
    compare_diff_patch current expected'

test_expect_success \
    'diff unchanged symlink' \
    'tree=$(git write-tree) &&
    git update-index frotz &&
    test -z "$(git diff-index --name-only $tree)"'

cat > expected << EOF
diff --git a/frotz b/frotz
deleted file mode 120000
index 7c465af..0000000
--- a/frotz
+++ /dev/null
@@ -1 +0,0 @@
-xyzzy
\ No newline at end of file
EOF

test_expect_success \
    'diff removed symlink' \
    'rm frotz &&
    git diff-index -M -p $tree > current &&
    compare_diff_patch current expected'

cat > expected << EOF
diff --git a/frotz b/frotz
EOF

test_expect_success \
    'diff identical, but newly created symlink' \
    'sleep 3 &&
    ln -s xyzzy frotz &&
    git diff-index -M -p $tree > current &&
    compare_diff_patch current expected'

cat > expected << EOF
diff --git a/frotz b/frotz
index 7c465af..df1db54 120000
--- a/frotz
+++ b/frotz
@@ -1 +1 @@
-xyzzy
\ No newline at end of file
+yxyyz
\ No newline at end of file
EOF

test_expect_success \
    'diff different symlink' \
    'rm frotz &&
    ln -s yxyyz frotz &&
    git diff-index -M -p $tree > current &&
    compare_diff_patch current expected'

test_done
