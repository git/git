#!/bin/sh
#
# Copyright (c) 2007 Johannes E Schindelin
#

test_description='Test git stash'

. ./test-lib.sh

test_expect_success 'stash some dirty working directory' '
	echo 1 > file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	echo 2 > file &&
	git add file &&
	echo 3 > file &&
	test_tick &&
	git stash &&
	git diff-files --quiet &&
	git diff-index --cached --quiet HEAD
'

cat > expect << EOF
diff --git a/file b/file
index 0cfbf08..00750ed 100644
--- a/file
+++ b/file
@@ -1 +1 @@
-2
+3
EOF

test_expect_success 'parents of stash' '
	test $(git rev-parse stash^) = $(git rev-parse HEAD) &&
	git diff stash^2..stash > output &&
	test_cmp output expect
'

test_expect_success 'apply needs clean working directory' '
	echo 4 > other-file &&
	git add other-file &&
	echo 5 > other-file &&
	test_must_fail git stash apply
'

test_expect_success 'apply stashed changes' '
	git add other-file &&
	test_tick &&
	git commit -m other-file &&
	git stash apply &&
	test 3 = $(cat file) &&
	test 1 = $(git show :file) &&
	test 1 = $(git show HEAD:file)
'

test_expect_success 'apply stashed changes (including index)' '
	git reset --hard HEAD^ &&
	echo 6 > other-file &&
	git add other-file &&
	test_tick &&
	git commit -m other-file &&
	git stash apply --index &&
	test 3 = $(cat file) &&
	test 2 = $(git show :file) &&
	test 1 = $(git show HEAD:file)
'

test_expect_success 'unstashing in a subdirectory' '
	git reset --hard HEAD &&
	mkdir subdir &&
	cd subdir &&
	git stash apply &&
	cd ..
'

test_expect_success 'drop top stash' '
	git reset --hard &&
	git stash list > stashlist1 &&
	echo 7 > file &&
	git stash &&
	git stash drop &&
	git stash list > stashlist2 &&
	diff stashlist1 stashlist2 &&
	git stash apply &&
	test 3 = $(cat file) &&
	test 1 = $(git show :file) &&
	test 1 = $(git show HEAD:file)
'

test_expect_success 'drop middle stash' '
	git reset --hard &&
	echo 8 > file &&
	git stash &&
	echo 9 > file &&
	git stash &&
	git stash drop stash@{1} &&
	test 2 = $(git stash list | wc -l) &&
	git stash apply &&
	test 9 = $(cat file) &&
	test 1 = $(git show :file) &&
	test 1 = $(git show HEAD:file) &&
	git reset --hard &&
	git stash drop &&
	git stash apply &&
	test 3 = $(cat file) &&
	test 1 = $(git show :file) &&
	test 1 = $(git show HEAD:file)
'

test_expect_success 'stash pop' '
	git reset --hard &&
	git stash pop &&
	test 3 = $(cat file) &&
	test 1 = $(git show :file) &&
	test 1 = $(git show HEAD:file) &&
	test 0 = $(git stash list | wc -l)
'

cat > expect << EOF
diff --git a/file2 b/file2
new file mode 100644
index 0000000..1fe912c
--- /dev/null
+++ b/file2
@@ -0,0 +1 @@
+bar2
EOF

cat > expect1 << EOF
diff --git a/file b/file
index 257cc56..5716ca5 100644
--- a/file
+++ b/file
@@ -1 +1 @@
-foo
+bar
EOF

cat > expect2 << EOF
diff --git a/file b/file
index 7601807..5716ca5 100644
--- a/file
+++ b/file
@@ -1 +1 @@
-baz
+bar
diff --git a/file2 b/file2
new file mode 100644
index 0000000..1fe912c
--- /dev/null
+++ b/file2
@@ -0,0 +1 @@
+bar2
EOF

test_expect_success 'stash branch' '
	echo foo > file &&
	git commit file -m first
	echo bar > file &&
	echo bar2 > file2 &&
	git add file2 &&
	git stash &&
	echo baz > file &&
	git commit file -m second &&
	git stash branch stashbranch &&
	test refs/heads/stashbranch = $(git symbolic-ref HEAD) &&
	test $(git rev-parse HEAD) = $(git rev-parse master^) &&
	git diff --cached > output &&
	test_cmp output expect &&
	git diff > output &&
	test_cmp output expect1 &&
	git add file &&
	git commit -m alternate\ second &&
	git diff master..stashbranch > output &&
	test_cmp output expect2 &&
	test 0 = $(git stash list | wc -l)
'

test_expect_success 'apply -q is quiet' '
	echo foo > file &&
	git stash &&
	git stash apply -q > output.out 2>&1 &&
	test ! -s output.out
'

test_expect_success 'save -q is quiet' '
	git stash save --quiet > output.out 2>&1 &&
	test ! -s output.out
'

test_expect_success 'pop -q is quiet' '
	git stash pop -q > output.out 2>&1 &&
	test ! -s output.out
'

test_expect_success 'pop -q --index works and is quiet' '
	echo foo > file &&
	git add file &&
	git stash save --quiet &&
	git stash pop -q --index > output.out 2>&1 &&
	test foo = "$(git show :file)" &&
	test ! -s output.out
'

test_expect_success 'drop -q is quiet' '
	git stash &&
	git stash drop -q > output.out 2>&1 &&
	test ! -s output.out
'

test_expect_success 'stash -k' '
	echo bar3 > file &&
	echo bar4 > file2 &&
	git add file2 &&
	git stash -k &&
	test bar,bar4 = $(cat file),$(cat file2)
'

test_expect_success 'stash --invalid-option' '
	echo bar5 > file &&
	echo bar6 > file2 &&
	git add file2 &&
	test_must_fail git stash --invalid-option &&
	test_must_fail git stash save --invalid-option &&
	test bar5,bar6 = $(cat file),$(cat file2) &&
	git stash -- -message-starting-with-dash &&
	test bar,bar2 = $(cat file),$(cat file2)
'

test_done
