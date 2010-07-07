#!/bin/sh

test_description='add -i basic tests'
. ./test-lib.sh

if ! test_have_prereq PERL; then
	skip_all='skipping git add -i tests, perl not available'
	test_done
fi

test_expect_success 'setup (initial)' '
	echo content >file &&
	git add file &&
	echo more >>file &&
	echo lines >>file
'
test_expect_success 'status works (initial)' '
	git add -i </dev/null >output &&
	grep "+1/-0 *+2/-0 file" output
'
cat >expected <<EOF
new file mode 100644
index 0000000..d95f3ad
--- /dev/null
+++ b/file
@@ -0,0 +1 @@
+content
EOF
test_expect_success 'diff works (initial)' '
	(echo d; echo 1) | git add -i >output &&
	sed -ne "/new file/,/content/p" <output >diff &&
	test_cmp expected diff
'
test_expect_success 'revert works (initial)' '
	git add file &&
	(echo r; echo 1) | git add -i &&
	git ls-files >output &&
	! grep . output
'

test_expect_success 'setup (commit)' '
	echo baseline >file &&
	git add file &&
	git commit -m commit &&
	echo content >>file &&
	git add file &&
	echo more >>file &&
	echo lines >>file
'
test_expect_success 'status works (commit)' '
	git add -i </dev/null >output &&
	grep "+1/-0 *+2/-0 file" output
'
cat >expected <<EOF
index 180b47c..b6f2c08 100644
--- a/file
+++ b/file
@@ -1 +1,2 @@
 baseline
+content
EOF
test_expect_success 'diff works (commit)' '
	(echo d; echo 1) | git add -i >output &&
	sed -ne "/^index/,/content/p" <output >diff &&
	test_cmp expected diff
'
test_expect_success 'revert works (commit)' '
	git add file &&
	(echo r; echo 1) | git add -i &&
	git add -i </dev/null >output &&
	grep "unchanged *+3/-0 file" output
'

cat >expected <<EOF
EOF
cat >fake_editor.sh <<EOF
EOF
chmod a+x fake_editor.sh
test_set_editor "$(pwd)/fake_editor.sh"
test_expect_success 'dummy edit works' '
	(echo e; echo a) | git add -p &&
	git diff > diff &&
	test_cmp expected diff
'

cat >patch <<EOF
@@ -1,1 +1,4 @@
 this
+patch
-doesn't
 apply
EOF
echo "#!$SHELL_PATH" >fake_editor.sh
cat >>fake_editor.sh <<\EOF
mv -f "$1" oldpatch &&
mv -f patch "$1"
EOF
chmod a+x fake_editor.sh
test_set_editor "$(pwd)/fake_editor.sh"
test_expect_success 'bad edit rejected' '
	git reset &&
	(echo e; echo n; echo d) | git add -p >output &&
	grep "hunk does not apply" output
'

cat >patch <<EOF
this patch
is garbage
EOF
test_expect_success 'garbage edit rejected' '
	git reset &&
	(echo e; echo n; echo d) | git add -p >output &&
	grep "hunk does not apply" output
'

cat >patch <<EOF
@@ -1,0 +1,0 @@
 baseline
+content
+newcontent
+lines
EOF
cat >expected <<EOF
diff --git a/file b/file
index b5dd6c9..f910ae9 100644
--- a/file
+++ b/file
@@ -1,4 +1,4 @@
 baseline
 content
-newcontent
+more
 lines
EOF
test_expect_success 'real edit works' '
	(echo e; echo n; echo d) | git add -p &&
	git diff >output &&
	test_cmp expected output
'

test_expect_success 'skip files similarly as commit -a' '
	git reset &&
	echo file >.gitignore &&
	echo changed >file &&
	echo y | git add -p file &&
	git diff >output &&
	git reset &&
	git commit -am commit &&
	git diff >expected &&
	test_cmp expected output &&
	git reset --hard HEAD^
'
rm -f .gitignore

if test "$(git config --bool core.filemode)" = false
then
	say '# skipping filemode tests (filesystem does not properly support modes)'
else
	test_set_prereq FILEMODE
fi

test_expect_success FILEMODE 'patch does not affect mode' '
	git reset --hard &&
	echo content >>file &&
	chmod +x file &&
	printf "n\\ny\\n" | git add -p &&
	git show :file | grep content &&
	git diff file | grep "new mode"
'

test_expect_success FILEMODE 'stage mode but not hunk' '
	git reset --hard &&
	echo content >>file &&
	chmod +x file &&
	printf "y\\nn\\n" | git add -p &&
	git diff --cached file | grep "new mode" &&
	git diff          file | grep "+content"
'


test_expect_success FILEMODE 'stage mode and hunk' '
	git reset --hard &&
	echo content >>file &&
	chmod +x file &&
	printf "y\\ny\\n" | git add -p &&
	git diff --cached file | grep "new mode" &&
	git diff --cached file | grep "+content" &&
	test -z "$(git diff file)"
'

# end of tests disabled when filemode is not usable

test_expect_success 'setup again' '
	git reset --hard &&
	test_chmod +x file &&
	echo content >>file
'

# Write the patch file with a new line at the top and bottom
cat >patch <<EOF
index 180b47c..b6f2c08 100644
--- a/file
+++ b/file
@@ -1,2 +1,4 @@
+firstline
 baseline
 content
+lastline
EOF
# Expected output, similar to the patch but w/ diff at the top
cat >expected <<EOF
diff --git a/file b/file
index b6f2c08..61b9053 100755
--- a/file
+++ b/file
@@ -1,2 +1,4 @@
+firstline
 baseline
 content
+lastline
EOF
# Test splitting the first patch, then adding both
test_expect_success 'add first line works' '
	git commit -am "clear local changes" &&
	git apply patch &&
	(echo s; echo y; echo y) | git add -p file &&
	git diff --cached > diff &&
	test_cmp expected diff
'

cat >expected <<EOF
diff --git a/non-empty b/non-empty
deleted file mode 100644
index d95f3ad..0000000
--- a/non-empty
+++ /dev/null
@@ -1 +0,0 @@
-content
EOF
test_expect_success 'deleting a non-empty file' '
	git reset --hard &&
	echo content >non-empty &&
	git add non-empty &&
	git commit -m non-empty &&
	rm non-empty &&
	echo y | git add -p non-empty &&
	git diff --cached >diff &&
	test_cmp expected diff
'

cat >expected <<EOF
diff --git a/empty b/empty
deleted file mode 100644
index e69de29..0000000
EOF

test_expect_success 'deleting an empty file' '
	git reset --hard &&
	> empty &&
	git add empty &&
	git commit -m empty &&
	rm empty &&
	echo y | git add -p empty &&
	git diff --cached >diff &&
	test_cmp expected diff
'

test_done
