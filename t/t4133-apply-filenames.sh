#!/bin/sh
#
# Copyright (c) 2010 Andreas Gruenbacher
#

test_description='git apply filename consistency check'


TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	cat > bad1.patch <<EOF &&
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
	test_must_fail git apply bad1.patch 2>err &&
	test_i18ngrep "inconsistent new filename" err &&
	test_must_fail git apply bad2.patch 2>err &&
	test_i18ngrep "inconsistent old filename" err
'

test_expect_success 'apply diff with new filename missing from headers' '
	cat >missing_new_filename.diff <<-\EOF &&
	diff --git a/f b/f
	index 0000000..d00491f
	--- a/f
	@@ -0,0 +1 @@
	+1
	EOF
	test_must_fail git apply missing_new_filename.diff 2>err &&
	test_i18ngrep "lacks filename information" err
'

test_expect_success 'apply diff with old filename missing from headers' '
	cat >missing_old_filename.diff <<-\EOF &&
	diff --git a/f b/f
	index d00491f..0000000
	+++ b/f
	@@ -1 +0,0 @@
	-1
	EOF
	test_must_fail git apply missing_old_filename.diff 2>err &&
	test_i18ngrep "lacks filename information" err
'

test_done
