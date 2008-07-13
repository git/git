#!/bin/sh

test_description='add -i basic tests'
. ./test-lib.sh

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

if test "$(git config --bool core.filemode)" = false
then
    say 'skipping filemode tests (filesystem does not properly support modes)'
else

test_expect_success 'patch does not affect mode' '
	git reset --hard &&
	echo content >>file &&
	chmod +x file &&
	printf "n\\ny\\n" | git add -p &&
	git show :file | grep content &&
	git diff file | grep "new mode"
'

test_expect_success 'stage mode but not hunk' '
	git reset --hard &&
	echo content >>file &&
	chmod +x file &&
	printf "y\\nn\\n" | git add -p &&
	git diff --cached file | grep "new mode" &&
	git diff          file | grep "+content"
'

fi
# end of tests disabled when filemode is not usable

test_done
