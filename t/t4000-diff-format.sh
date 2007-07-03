#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Test built-in diff output engine.

'
. ./test-lib.sh
. ../diff-lib.sh

echo >path0 'Line 1
Line 2
line 3'
cat path0 >path1
chmod +x path1

test_expect_success \
    'update-cache --add two files with and without +x.' \
    'git update-index --add path0 path1'

mv path0 path0-
sed -e 's/line/Line/' <path0- >path0
chmod +x path0
rm -f path1
test_expect_success \
    'git diff-files -p after editing work tree.' \
    'git diff-files -p >current'

# that's as far as it comes
if [ "$(git config --get core.filemode)" = false ]
then
	say 'filemode disabled on the filesystem'
	test_done
fi

cat >expected <<\EOF
diff --git a/path0 b/path0
old mode 100644
new mode 100755
--- a/path0
+++ b/path0
@@ -1,3 +1,3 @@
 Line 1
 Line 2
-line 3
+Line 3
diff --git a/path1 b/path1
deleted file mode 100755
--- a/path1
+++ /dev/null
@@ -1,3 +0,0 @@
-Line 1
-Line 2
-line 3
EOF

test_expect_success \
    'validate git diff-files -p output.' \
    'compare_diff_patch current expected'

test_done
