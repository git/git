#!/bin/sh
#
# Copyright (c) 2006 Catalin Marinas
#

test_description='git-apply trying to add an ending line.

'
. ./test-lib.sh

# setup

cat >test-patch <<\EOF
diff --git a/file b/file
--- a/file
+++ b/file
@@ -1,2 +1,3 @@
 a
 b
+c
EOF

echo 'a' >file
echo 'b' >>file
echo 'c' >>file

test_expect_success setup \
    'git-update-index --add file'

# test

test_expect_failure apply \
    'git-apply --index test-patch'

test_done
