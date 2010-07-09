#!/bin/sh
#
# Copyright (c) 2010 Andreas Gruenbacher
#

test_description='git apply filename consistency check'

. ./test-lib.sh

test_expect_success setup '
	cat > bad1.patch <<EOF
diff --git a/f b/f
new file mode 100644
index 0000000..d00491f
--- /dev/null
+++ b/f-blah
@@ -0,0 +1 @@
+1
EOF
	cat > bad2.patch <<EOF
diff --git a/f b/f
deleted file mode 100644
index d00491f..0000000
--- b/f-blah
+++ /dev/null
@@ -1 +0,0 @@
-1
EOF
'

test_expect_success 'apply diff with inconsistent filenames in headers' '
	test_must_fail git apply bad1.patch 2>err
	grep "inconsistent new filename" err
	test_must_fail git apply bad2.patch 2>err
	grep "inconsistent old filename" err
'

test_done
