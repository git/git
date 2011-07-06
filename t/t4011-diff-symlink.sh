#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='Test diff of symlinks.

'
. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh

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

test_expect_success SYMLINKS \
    'diff new symlink' \
    'ln -s xyzzy frotz &&
    git update-index &&
    tree=$(git write-tree) &&
    git update-index --add frotz &&
    GIT_DIFF_OPTS=--unified=0 git diff-index -M -p $tree > current &&
    compare_diff_patch current expected'

test_expect_success SYMLINKS \
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

test_expect_success SYMLINKS \
    'diff removed symlink' \
    'mv frotz frotz2 &&
    git diff-index -M -p $tree > current &&
    compare_diff_patch current expected'

cat > expected << EOF
diff --git a/frotz b/frotz
EOF

test_expect_success SYMLINKS \
    'diff identical, but newly created symlink' \
    'ln -s xyzzy frotz &&
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

test_expect_success SYMLINKS \
    'diff different symlink' \
    'rm frotz &&
    ln -s yxyyz frotz &&
    git diff-index -M -p $tree > current &&
    compare_diff_patch current expected'

test_expect_success SYMLINKS \
    'diff symlinks with non-existing targets' \
    'ln -s narf pinky &&
    ln -s take\ over brain &&
    test_must_fail git diff --no-index pinky brain > output 2> output.err &&
    grep narf output &&
    ! grep error output.err'

test_expect_success SYMLINKS 'setup symlinks with attributes' '
	echo "*.bin diff=bin" >>.gitattributes &&
	echo content >file.bin &&
	ln -s file.bin link.bin &&
	git add -N file.bin link.bin
'

cat >expect <<'EOF'
diff --git a/file.bin b/file.bin
index e69de29..d95f3ad 100644
Binary files a/file.bin and b/file.bin differ
diff --git a/link.bin b/link.bin
index e69de29..dce41ec 120000
--- a/link.bin
+++ b/link.bin
@@ -0,0 +1 @@
+file.bin
\ No newline at end of file
EOF
test_expect_success SYMLINKS 'symlinks do not respect userdiff config by path' '
	git config diff.bin.binary true &&
	git diff file.bin link.bin >actual &&
	test_cmp expect actual
'

test_done
