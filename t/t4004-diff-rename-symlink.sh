#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='More rename detection tests.

The rename detection logic should be able to detect pure rename or
copy of symbolic links, but should not produce rename/copy followed
by an edit for them.
'
. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh

if ! test_have_prereq SYMLINKS
then
	skip_all='Symbolic links not supported, skipping tests.'
	test_done
fi

test_expect_success \
    'prepare reference tree' \
    'echo xyzzy | tr -d '\\\\'012 >yomin &&
     ln -s xyzzy frotz &&
    git update-index --add frotz yomin &&
    tree=$(git write-tree) &&
    echo $tree'

test_expect_success \
    'prepare work tree' \
    'mv frotz rezrov &&
     rm -f yomin &&
     ln -s xyzzy nitfol &&
     ln -s xzzzy bozbar &&
    git update-index --add --remove frotz rezrov nitfol bozbar yomin'

# tree has frotz pointing at xyzzy, and yomin that contains xyzzy to
# confuse things.  work tree has rezrov (xyzzy) nitfol (xyzzy) and
# bozbar (xzzzy).
# rezrov and nitfol are rename/copy of frotz and bozbar should be
# a new creation.

GIT_DIFF_OPTS=--unified=0 git diff-index -M -p $tree >current
cat >expected <<\EOF
diff --git a/bozbar b/bozbar
new file mode 120000
--- /dev/null
+++ b/bozbar
@@ -0,0 +1 @@
+xzzzy
\ No newline at end of file
diff --git a/frotz b/nitfol
similarity index 100%
copy from frotz
copy to nitfol
diff --git a/frotz b/rezrov
similarity index 100%
rename from frotz
rename to rezrov
diff --git a/yomin b/yomin
deleted file mode 100644
--- a/yomin
+++ /dev/null
@@ -1 +0,0 @@
-xyzzy
\ No newline at end of file
EOF

test_expect_success \
    'validate diff output' \
    'compare_diff_patch current expected'

test_done
