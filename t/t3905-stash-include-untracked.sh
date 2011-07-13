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
	git stash --include-untracked &&
	git diff-files --quiet &&
	git diff-index --cached --quiet HEAD
'

cat > expect <<EOF
?? expect
?? output
EOF

test_expect_success 'stash save --include-untracked cleaned the untracked files' '
	git status --porcelain > output
	test_cmp output expect
'

cat > expect.diff <<EOF
diff --git a/file2 b/file2
new file mode 100644
index 0000000..d00491f
--- /dev/null
+++ b/file2
@@ -0,0 +1 @@
+1
EOF
cat > expect.lstree <<EOF
file2
EOF

test_expect_success 'stash save --include-untracked stashed the untracked files' '
	test "!" -f file2 &&
	git diff HEAD..stash^3 -- file2 > output &&
	test_cmp output expect.diff &&
	git ls-tree --name-only stash^3: > output &&
	test_cmp output expect.lstree
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
?? expect
?? file2
?? output
EOF

test_expect_success 'stash pop after save --include-untracked leaves files untracked again' '
	git stash pop &&
	git status --porcelain > output
	test_cmp output expect
'

git clean --force --quiet

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
	git diff --cached > output &&
	test_cmp output expect
'

git reset > /dev/null

test_expect_success 'stash save --include-untracked -q is quiet' '
	echo 1 > file5 &&
	git stash save --include-untracked --quiet > output.out 2>&1 &&
	test ! -s output.out
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
	test ! -f file
'

cat > .gitignore <<EOF
.gitignore
ignored
EOF

test_expect_success 'stash save --include-untracked respects .gitignore' '
	echo ignored > ignored &&
	git stash -u &&
	test -s ignored &&
	test -s .gitignore
'

test_expect_success 'stash save -u can stash with only untracked files different' '
	echo 4 > file4 &&
	git stash -u
	test "!" -f file4
'

test_expect_success 'stash save --all does not respect .gitignore' '
	git stash -a &&
	test "!" -f ignored &&
	test "!" -f .gitignore
'

test_expect_success 'stash save --all is stash poppable' '
	git stash pop &&
	test -s ignored &&
	test -s .gitignore
'

test_done
