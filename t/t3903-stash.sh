#!/bin/sh
#
# Copyright (c) 2007 Johannes E Schindelin
#

test_description='Test git-stash'

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

test_done
