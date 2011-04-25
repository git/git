#!/bin/sh
#
# Copyright (c) 2007 Carlos Rica
#

test_description='git reset

Documented tests for git reset'

. ./test-lib.sh

test_expect_success 'creating initial files and commits' '
	test_tick &&
	echo "1st file" >first &&
	git add first &&
	git commit -m "create 1st file" &&

	echo "2nd file" >second &&
	git add second &&
	git commit -m "create 2nd file" &&

	echo "2nd line 1st file" >>first &&
	git commit -a -m "modify 1st file" &&

	git rm first &&
	git mv second secondfile &&
	git commit -a -m "remove 1st and rename 2nd" &&

	echo "1st line 2nd file" >secondfile &&
	echo "2nd line 2nd file" >>secondfile &&
	git commit -a -m "modify 2nd file"
'
# git log --pretty=oneline # to see those SHA1 involved

check_changes () {
	test "$(git rev-parse HEAD)" = "$1" &&
	git diff | test_cmp .diff_expect - &&
	git diff --cached | test_cmp .cached_expect - &&
	for FILE in *
	do
		echo $FILE':'
		cat $FILE || return
	done | test_cmp .cat_expect -
}

>.diff_expect
>.cached_expect
cat >.cat_expect <<EOF
secondfile:
1st line 2nd file
2nd line 2nd file
EOF

test_expect_success 'giving a non existing revision should fail' '
	test_must_fail git reset aaaaaa &&
	test_must_fail git reset --mixed aaaaaa &&
	test_must_fail git reset --soft aaaaaa &&
	test_must_fail git reset --hard aaaaaa &&
	check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc
'

test_expect_success 'reset --soft with unmerged index should fail' '
	touch .git/MERGE_HEAD &&
	echo "100644 44c5b5884550c17758737edcced463447b91d42b 1	un" |
		git update-index --index-info &&
	test_must_fail git reset --soft HEAD &&
	rm .git/MERGE_HEAD &&
	git rm --cached -- un
'

test_expect_success \
	'giving paths with options different than --mixed should fail' '
	test_must_fail git reset --soft -- first &&
	test_must_fail git reset --hard -- first &&
	test_must_fail git reset --soft HEAD^ -- first &&
	test_must_fail git reset --hard HEAD^ -- first &&
	check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc
'

test_expect_success 'giving unrecognized options should fail' '
	test_must_fail git reset --other &&
	test_must_fail git reset -o &&
	test_must_fail git reset --mixed --other &&
	test_must_fail git reset --mixed -o &&
	test_must_fail git reset --soft --other &&
	test_must_fail git reset --soft -o &&
	test_must_fail git reset --hard --other &&
	test_must_fail git reset --hard -o &&
	check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc
'

test_expect_success \
	'trying to do reset --soft with pending merge should fail' '
	git branch branch1 &&
	git branch branch2 &&

	git checkout branch1 &&
	echo "3rd line in branch1" >>secondfile &&
	git commit -a -m "change in branch1" &&

	git checkout branch2 &&
	echo "3rd line in branch2" >>secondfile &&
	git commit -a -m "change in branch2" &&

	test_must_fail git merge branch1 &&
	test_must_fail git reset --soft &&

	printf "1st line 2nd file\n2nd line 2nd file\n3rd line" >secondfile &&
	git commit -a -m "the change in branch2" &&

	git checkout master &&
	git branch -D branch1 branch2 &&
	check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc
'

test_expect_success \
	'trying to do reset --soft with pending checkout merge should fail' '
	git branch branch3 &&
	git branch branch4 &&

	git checkout branch3 &&
	echo "3rd line in branch3" >>secondfile &&
	git commit -a -m "line in branch3" &&

	git checkout branch4 &&
	echo "3rd line in branch4" >>secondfile &&

	git checkout -m branch3 &&
	test_must_fail git reset --soft &&

	printf "1st line 2nd file\n2nd line 2nd file\n3rd line" >secondfile &&
	git commit -a -m "the line in branch3" &&

	git checkout master &&
	git branch -D branch3 branch4 &&
	check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc
'

test_expect_success \
	'resetting to HEAD with no changes should succeed and do nothing' '
	git reset --hard &&
		check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc &&
	git reset --hard HEAD &&
		check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc &&
	git reset --soft &&
		check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc &&
	git reset --soft HEAD &&
		check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc &&
	git reset --mixed &&
		check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc &&
	git reset --mixed HEAD &&
		check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc &&
	git reset &&
		check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc &&
	git reset HEAD &&
		check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc
'

>.diff_expect
cat >.cached_expect <<EOF
diff --git a/secondfile b/secondfile
index 1bbba79..44c5b58 100644
--- a/secondfile
+++ b/secondfile
@@ -1 +1,2 @@
-2nd file
+1st line 2nd file
+2nd line 2nd file
EOF
cat >.cat_expect <<EOF
secondfile:
1st line 2nd file
2nd line 2nd file
EOF
test_expect_success '--soft reset only should show changes in diff --cached' '
	git reset --soft HEAD^ &&
	check_changes d1a4bc3abce4829628ae2dcb0d60ef3d1a78b1c4 &&
	test "$(git rev-parse ORIG_HEAD)" = \
			3ec39651e7f44ea531a5de18a9fa791c0fd370fc
'

>.diff_expect
>.cached_expect
cat >.cat_expect <<EOF
secondfile:
1st line 2nd file
2nd line 2nd file
3rd line 2nd file
EOF
test_expect_success \
	'changing files and redo the last commit should succeed' '
	echo "3rd line 2nd file" >>secondfile &&
	git commit -a -C ORIG_HEAD &&
	check_changes 3d3b7be011a58ca0c179ae45d94e6c83c0b0cd0d &&
	test "$(git rev-parse ORIG_HEAD)" = \
			3ec39651e7f44ea531a5de18a9fa791c0fd370fc
'

>.diff_expect
>.cached_expect
cat >.cat_expect <<EOF
first:
1st file
2nd line 1st file
second:
2nd file
EOF
test_expect_success \
	'--hard reset should change the files and undo commits permanently' '
	git reset --hard HEAD~2 &&
	check_changes ddaefe00f1da16864591c61fdc7adb5d7cd6b74e &&
	test "$(git rev-parse ORIG_HEAD)" = \
			3d3b7be011a58ca0c179ae45d94e6c83c0b0cd0d
'

>.diff_expect
cat >.cached_expect <<EOF
diff --git a/first b/first
deleted file mode 100644
index 8206c22..0000000
--- a/first
+++ /dev/null
@@ -1,2 +0,0 @@
-1st file
-2nd line 1st file
diff --git a/second b/second
deleted file mode 100644
index 1bbba79..0000000
--- a/second
+++ /dev/null
@@ -1 +0,0 @@
-2nd file
diff --git a/secondfile b/secondfile
new file mode 100644
index 0000000..44c5b58
--- /dev/null
+++ b/secondfile
@@ -0,0 +1,2 @@
+1st line 2nd file
+2nd line 2nd file
EOF
cat >.cat_expect <<EOF
secondfile:
1st line 2nd file
2nd line 2nd file
EOF
test_expect_success \
	'redoing changes adding them without commit them should succeed' '
	git rm first &&
	git mv second secondfile &&

	echo "1st line 2nd file" >secondfile &&
	echo "2nd line 2nd file" >>secondfile &&
	git add secondfile &&
	check_changes ddaefe00f1da16864591c61fdc7adb5d7cd6b74e
'

cat >.diff_expect <<EOF
diff --git a/first b/first
deleted file mode 100644
index 8206c22..0000000
--- a/first
+++ /dev/null
@@ -1,2 +0,0 @@
-1st file
-2nd line 1st file
diff --git a/second b/second
deleted file mode 100644
index 1bbba79..0000000
--- a/second
+++ /dev/null
@@ -1 +0,0 @@
-2nd file
EOF
>.cached_expect
cat >.cat_expect <<EOF
secondfile:
1st line 2nd file
2nd line 2nd file
EOF
test_expect_success '--mixed reset to HEAD should unadd the files' '
	git reset &&
	check_changes ddaefe00f1da16864591c61fdc7adb5d7cd6b74e &&
	test "$(git rev-parse ORIG_HEAD)" = \
			ddaefe00f1da16864591c61fdc7adb5d7cd6b74e
'

>.diff_expect
>.cached_expect
cat >.cat_expect <<EOF
secondfile:
1st line 2nd file
2nd line 2nd file
EOF
test_expect_success 'redoing the last two commits should succeed' '
	git add secondfile &&
	git reset --hard ddaefe00f1da16864591c61fdc7adb5d7cd6b74e &&

	git rm first &&
	git mv second secondfile &&
	git commit -a -m "remove 1st and rename 2nd" &&

	echo "1st line 2nd file" >secondfile &&
	echo "2nd line 2nd file" >>secondfile &&
	git commit -a -m "modify 2nd file" &&
	check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc
'

>.diff_expect
>.cached_expect
cat >.cat_expect <<EOF
secondfile:
1st line 2nd file
2nd line 2nd file
3rd line in branch2
EOF
test_expect_success '--hard reset to HEAD should clear a failed merge' '
	git branch branch1 &&
	git branch branch2 &&

	git checkout branch1 &&
	echo "3rd line in branch1" >>secondfile &&
	git commit -a -m "change in branch1" &&

	git checkout branch2 &&
	echo "3rd line in branch2" >>secondfile &&
	git commit -a -m "change in branch2" &&

	test_must_fail git pull . branch1 &&
	git reset --hard &&
	check_changes 77abb337073fb4369a7ad69ff6f5ec0e4d6b54bb
'

>.diff_expect
>.cached_expect
cat >.cat_expect <<EOF
secondfile:
1st line 2nd file
2nd line 2nd file
EOF
test_expect_success \
	'--hard reset to ORIG_HEAD should clear a fast-forward merge' '
	git reset --hard HEAD^ &&
	check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc &&

	git pull . branch1 &&
	git reset --hard ORIG_HEAD &&
	check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc &&

	git checkout master &&
	git branch -D branch1 branch2 &&
	check_changes 3ec39651e7f44ea531a5de18a9fa791c0fd370fc
'

cat > expect << EOF
diff --git a/file1 b/file1
index d00491f..7ed6ff8 100644
--- a/file1
+++ b/file1
@@ -1 +1 @@
-1
+5
diff --git a/file2 b/file2
deleted file mode 100644
index 0cfbf08..0000000
--- a/file2
+++ /dev/null
@@ -1 +0,0 @@
-2
EOF
cat > cached_expect << EOF
diff --git a/file4 b/file4
new file mode 100644
index 0000000..b8626c4
--- /dev/null
+++ b/file4
@@ -0,0 +1 @@
+4
EOF
test_expect_success 'test --mixed <paths>' '
	echo 1 > file1 &&
	echo 2 > file2 &&
	git add file1 file2 &&
	test_tick &&
	git commit -m files &&
	git rm file2 &&
	echo 3 > file3 &&
	echo 4 > file4 &&
	echo 5 > file1 &&
	git add file1 file3 file4 &&
	test_must_fail git reset HEAD -- file1 file2 file3 &&
	git diff > output &&
	test_cmp output expect &&
	git diff --cached > output &&
	test_cmp output cached_expect
'

test_expect_success 'test resetting the index at give paths' '

	mkdir sub &&
	>sub/file1 &&
	>sub/file2 &&
	git update-index --add sub/file1 sub/file2 &&
	T=$(git write-tree) &&
	test_must_fail git reset HEAD sub/file2 &&
	U=$(git write-tree) &&
	echo "$T" &&
	echo "$U" &&
	test_must_fail git diff-index --cached --exit-code "$T" &&
	test "$T" != "$U"

'

test_expect_success 'resetting an unmodified path is a no-op' '
	git reset --hard &&
	git reset -- file1 &&
	git diff-files --exit-code &&
	git diff-index --cached --exit-code HEAD
'

cat > expect << EOF
Unstaged changes after reset:
M	file2
EOF

test_expect_success '--mixed refreshes the index' '
	echo 123 >> file2 &&
	git reset --mixed HEAD > output &&
	test_i18ncmp expect output
'

test_expect_success 'disambiguation (1)' '

	git reset --hard &&
	>secondfile &&
	git add secondfile &&
	test_must_fail git reset secondfile &&
	test -z "$(git diff --cached --name-only)" &&
	test -f secondfile &&
	test ! -s secondfile

'

test_expect_success 'disambiguation (2)' '

	git reset --hard &&
	>secondfile &&
	git add secondfile &&
	rm -f secondfile &&
	test_must_fail git reset secondfile &&
	test -n "$(git diff --cached --name-only -- secondfile)" &&
	test ! -f secondfile

'

test_expect_success 'disambiguation (3)' '

	git reset --hard &&
	>secondfile &&
	git add secondfile &&
	rm -f secondfile &&
	test_must_fail git reset HEAD secondfile &&
	test -z "$(git diff --cached --name-only)" &&
	test ! -f secondfile

'

test_expect_success 'disambiguation (4)' '

	git reset --hard &&
	>secondfile &&
	git add secondfile &&
	rm -f secondfile &&
	test_must_fail git reset -- secondfile &&
	test -z "$(git diff --cached --name-only)" &&
	test ! -f secondfile
'

test_done
