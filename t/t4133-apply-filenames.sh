#!/bin/sh
#
# Copyright (c) 2010 Andreas Gruenbacher
#

test_description='git apply filename consistency check'


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
	test_grep "inconsistent new filename" err &&
	test_must_fail git apply bad2.patch 2>err &&
	test_grep "inconsistent old filename" err
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
	test_grep "lacks filename information" err
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
	test_grep "lacks filename information" err
'

reject_empty_git_header () {
	file=$1 &&
	line=$2 &&
	shift 2 &&
	test_must_fail git apply --check "$@" "$file" 2>err &&
	test_grep "corrupt patch: empty \"diff --git\" header at $file:$line" err
}

test_expect_success 'reject a new file with a blank line after "diff --git"' '
	cat >blank_line.diff <<-\EOF &&
	diff --git a/f b/f

	new file mode 100644
	index 0000000..d00491f
	--- /dev/null
	+++ b/f
	@@ -0,0 +1 @@
	+1
	EOF
	reject_empty_git_header blank_line.diff 1 &&
	test_must_fail git apply blank_line.diff &&
	test_path_is_missing f
'

test_expect_success 'reject an indented "new file mode" line' '
	cat >indented_mode.diff <<-\EOF &&
	diff --git a/f b/f
	 new file mode 100644
	index 0000000..d00491f
	--- /dev/null
	+++ b/f
	@@ -0,0 +1 @@
	+1
	EOF
	reject_empty_git_header indented_mode.diff 1
'

test_expect_success 'reject a deletion with a blank line after "diff --git"' '
	cat >blank_line_delete.diff <<-\EOF &&
	diff --git a/f b/f

	deleted file mode 100644
	index d00491f..0000000
	--- a/f
	+++ /dev/null
	@@ -1 +0,0 @@
	-1
	EOF
	reject_empty_git_header blank_line_delete.diff 1 &&
	reject_empty_git_header blank_line_delete.diff 1 -R
'

test_expect_success 'reject a modification with a blank line after "diff --git"' '
	test_when_finished "rm -f g" &&
	echo 1 >g &&
	cat >blank_line_modify.diff <<-\EOF &&
	diff --git a/g b/g

	old mode 100644
	new mode 100755
	index d00491f..0cfbf08
	--- a/g
	+++ b/g
	@@ -1 +1 @@
	-1
	+2
	EOF
	reject_empty_git_header blank_line_modify.diff 1 &&
	echo 1 >expect &&
	test_cmp expect g
'

test_expect_success 'name the empty "diff --git" line in the error' '
	cat >later_line.diff <<-\EOF &&
	some text before the patch

	diff --git a/f b/f
	some text after it
	--- /dev/null
	+++ b/f
	@@ -0,0 +1 @@
	+1
	EOF
	reject_empty_git_header later_line.diff 3
'

test_expect_success 'reject a "diff --git" line followed by another' '
	test_when_finished "rm -f g" &&
	echo 1 >g &&
	cat >empty_section.diff <<-\EOF &&
	diff --git a/g b/g
	diff --git a/g b/g
	index d00491f..0cfbf08 100644
	--- a/g
	+++ b/g
	@@ -1 +1 @@
	-1
	+2
	EOF
	reject_empty_git_header empty_section.diff 1 &&
	echo 1 >expect &&
	test_cmp expect g
'

test_done
