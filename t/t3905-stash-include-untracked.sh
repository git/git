#!/bin/sh
#
# Copyright (c) 2011 David Caldwell
#

test_description='Test git stash --include-untracked'

. ./test-lib.sh

test_expect_success 'stash save --include-untracked some dirty working directory' '
	echo 1 > file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	echo 2 > file &&
	git add file &&
	echo 3 > file &&
	test_tick &&
	echo 1 > file2 &&
	echo 1 > HEAD &&
	mkdir untracked &&
	echo untracked >untracked/untracked &&
	git stash --include-untracked &&
	git diff-files --quiet &&
	git diff-index --cached --quiet HEAD
'

cat > expect <<EOF
?? actual
?? expect
EOF

test_expect_success 'stash save --include-untracked cleaned the untracked files' '
	git status --porcelain >actual &&
	test_cmp expect actual
'

cat > expect.diff <<EOF
diff --git a/HEAD b/HEAD
new file mode 100644
index 0000000..d00491f
--- /dev/null
+++ b/HEAD
@@ -0,0 +1 @@
+1
diff --git a/file2 b/file2
new file mode 100644
index 0000000..d00491f
--- /dev/null
+++ b/file2
@@ -0,0 +1 @@
+1
diff --git a/untracked/untracked b/untracked/untracked
new file mode 100644
index 0000000..5a72eb2
--- /dev/null
+++ b/untracked/untracked
@@ -0,0 +1 @@
+untracked
EOF
cat > expect.lstree <<EOF
HEAD
file2
untracked
EOF

test_expect_success 'stash save --include-untracked stashed the untracked files' '
	test_path_is_missing file2 &&
	test_path_is_missing untracked &&
	test_path_is_missing HEAD &&
	git diff HEAD stash^3 -- HEAD file2 untracked >actual &&
	test_cmp expect.diff actual &&
	git ls-tree --name-only stash^3: >actual &&
	test_cmp expect.lstree actual
'
test_expect_success 'stash save --patch --include-untracked fails' '
	test_must_fail git stash --patch --include-untracked
'

test_expect_success 'stash save --patch --all fails' '
	test_must_fail git stash --patch --all
'

git clean --force --quiet

cat > expect <<EOF
 M file
?? HEAD
?? actual
?? expect
?? file2
?? untracked/
EOF

test_expect_success 'stash pop after save --include-untracked leaves files untracked again' '
	git stash pop &&
	git status --porcelain >actual &&
	test_cmp expect actual &&
	test "1" = "$(cat file2)" &&
	test untracked = "$(cat untracked/untracked)"
'

git clean --force --quiet -d

test_expect_success 'stash save -u dirty index' '
	echo 4 > file3 &&
	git add file3 &&
	test_tick &&
	git stash -u
'

cat > expect <<EOF
diff --git a/file3 b/file3
new file mode 100644
index 0000000..b8626c4
--- /dev/null
+++ b/file3
@@ -0,0 +1 @@
+4
EOF

test_expect_success 'stash save --include-untracked dirty index got stashed' '
	git stash pop --index &&
	git diff --cached >actual &&
	test_cmp expect actual
'

git reset > /dev/null

# Must direct output somewhere where it won't be considered an untracked file
test_expect_success 'stash save --include-untracked -q is quiet' '
	echo 1 > file5 &&
	git stash save --include-untracked --quiet > .git/stash-output.out 2>&1 &&
	test_line_count = 0 .git/stash-output.out &&
	rm -f .git/stash-output.out
'

test_expect_success 'stash save --include-untracked removed files' '
	rm -f file &&
	git stash save --include-untracked &&
	echo 1 > expect &&
	test_cmp file expect
'

rm -f expect

test_expect_success 'stash save --include-untracked removed files got stashed' '
	git stash pop &&
	test_path_is_missing file
'

cat > .gitignore <<EOF
.gitignore
ignored
ignored.d/
EOF

test_expect_success 'stash save --include-untracked respects .gitignore' '
	echo ignored > ignored &&
	mkdir ignored.d &&
	echo ignored >ignored.d/untracked &&
	git stash -u &&
	test -s ignored &&
	test -s ignored.d/untracked &&
	test -s .gitignore
'

test_expect_success 'stash save -u can stash with only untracked files different' '
	echo 4 > file4 &&
	git stash -u &&
	test_path_is_missing file4
'

test_expect_success 'stash save --all does not respect .gitignore' '
	git stash -a &&
	test_path_is_missing ignored &&
	test_path_is_missing ignored.d &&
	test_path_is_missing .gitignore
'

test_expect_success 'stash save --all is stash poppable' '
	git stash pop &&
	test -s ignored &&
	test -s ignored.d/untracked &&
	test -s .gitignore
'

test_done
