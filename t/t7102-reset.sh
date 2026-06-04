#!/bin/sh
#
# Copyright (c) 2007 Carlos Rica
#

test_description='git reset

Documented tests for git reset'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

if test_have_prereq ICONV
then
	commit_msg () {
		# String "modify 2nd file (changed)" partly in German
		# (translated with Google Translate),
		# encoded in UTF-8, used as a commit log message below.
		msg="modify 2nd file (ge\303\244ndert)\n"
		if test -n "$1"
		then
			printf "$msg" | iconv -f utf-8 -t "$1"
		else
			printf "$msg"
		fi
	}

	# Tested non-UTF-8 encoding
	test_encoding="ISO8859-1"
else
	commit_msg () {
		echo "modify 2nd file (geandert)"
	}

	# Tested non-UTF-8 encoding
	test_encoding="UTF-8"
fi

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
	head5p2=$(git rev-parse --verify HEAD) &&
	head5p2f=$(git rev-parse --short HEAD:first) &&

	git rm first &&
	git mv second secondfile &&
	git commit -a -m "remove 1st and rename 2nd" &&
	head5p1=$(git rev-parse --verify HEAD) &&
	head5p1s=$(git rev-parse --short HEAD:secondfile) &&

	echo "1st line 2nd file" >secondfile &&
	echo "2nd line 2nd file" >>secondfile &&
	# "git commit -m" would break MinGW, as Windows refuse to pass
	# $test_encoding encoded parameter to git.
	commit_msg $test_encoding | git -c "i18n.commitEncoding=$test_encoding" commit -a -F - &&
	head5=$(git rev-parse --verify HEAD) &&
	head5s=$(git rev-parse --short HEAD:secondfile) &&
	head5sl=$(git rev-parse HEAD:secondfile)
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

# no negated form for various type of resets
for opt in soft mixed hard merge keep
do
	test_expect_success "no 'git reset --no-$opt'" '
		test_when_finished "rm -f err" &&
		test_must_fail git reset --no-$opt 2>err &&
		grep "error: unknown option .no-$opt." err
	'
done

test_expect_success 'reset --hard message' '
	hex=$(git log -1 --format="%h") &&
	git reset --hard >.actual &&
	echo HEAD is now at $hex $(commit_msg) >.expected &&
	test_cmp .expected .actual
'

test_expect_success 'reset --hard message (ISO8859-1 logoutputencoding)' '
	hex=$(git log -1 --format="%h") &&
	git -c "i18n.logOutputEncoding=$test_encoding" reset --hard >.actual &&
	echo HEAD is now at $hex $(commit_msg $test_encoding) >.expected &&
	test_cmp .expected .actual
'

test_expect_success 'giving a non existing revision should fail' '
	>.diff_expect &&
	>.cached_expect &&
	cat >.cat_expect <<-\EOF &&
	secondfile:
	1st line 2nd file
	2nd line 2nd file
	EOF

	test_must_fail git reset aaaaaa &&
	test_must_fail git reset --mixed aaaaaa &&
	test_must_fail git reset --soft aaaaaa &&
	test_must_fail git reset --hard aaaaaa &&
	check_changes $head5
'

test_expect_success 'reset --soft with unmerged index should fail' '
	touch .git/MERGE_HEAD &&
	echo "100644 $head5sl 1	un" |
		git update-index --index-info &&
	test_must_fail git reset --soft HEAD &&
	rm .git/MERGE_HEAD &&
	git rm --cached -- un
'

test_expect_success 'giving paths with options different than --mixed should fail' '
	test_must_fail git reset --soft -- first &&
	test_must_fail git reset --hard -- first &&
	test_must_fail git reset --soft HEAD^ -- first &&
	test_must_fail git reset --hard HEAD^ -- first &&
	check_changes $head5
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
	check_changes $head5
'

test_expect_success 'trying to do reset --soft with pending merge should fail' '
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

	git checkout main &&
	git branch -D branch1 branch2 &&
	check_changes $head5
'

test_expect_success 'trying to do reset --soft with pending checkout merge should fail' '
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

	git checkout main &&
	git branch -D branch3 branch4 &&
	check_changes $head5
'

test_expect_success 'resetting to HEAD with no changes should succeed and do nothing' '
	git reset --hard &&
		check_changes $head5 &&
	git reset --hard HEAD &&
		check_changes $head5 &&
	git reset --soft &&
		check_changes $head5 &&
	git reset --soft HEAD &&
		check_changes $head5 &&
	git reset --mixed &&
		check_changes $head5 &&
	git reset --mixed HEAD &&
		check_changes $head5 &&
	git reset &&
		check_changes $head5 &&
	git reset HEAD &&
		check_changes $head5
'

test_expect_success '--soft reset only should show changes in diff --cached' '
	>.diff_expect &&
	cat >.cached_expect <<-EOF &&
	diff --git a/secondfile b/secondfile
	index $head5p1s..$head5s 100644
	--- a/secondfile
	+++ b/secondfile
	@@ -1 +1,2 @@
	-2nd file
	+1st line 2nd file
	+2nd line 2nd file
	EOF
	cat >.cat_expect <<-\EOF &&
	secondfile:
	1st line 2nd file
	2nd line 2nd file
	EOF
	git reset --soft HEAD^ &&
	check_changes $head5p1 &&
	test "$(git rev-parse ORIG_HEAD)" = \
			$head5
'

test_expect_success 'changing files and redo the last commit should succeed' '
	>.diff_expect &&
	>.cached_expect &&
	cat >.cat_expect <<-\EOF &&
	secondfile:
	1st line 2nd file
	2nd line 2nd file
	3rd line 2nd file
	EOF
	echo "3rd line 2nd file" >>secondfile &&
	git commit -a -C ORIG_HEAD &&
	head4=$(git rev-parse --verify HEAD) &&
	check_changes $head4 &&
	test "$(git rev-parse ORIG_HEAD)" = \
			$head5
'

test_expect_success '--hard reset should change the files and undo commits permanently' '
	>.diff_expect &&
	>.cached_expect &&
	cat >.cat_expect <<-\EOF &&
	first:
	1st file
	2nd line 1st file
	second:
	2nd file
	EOF
	git reset --hard HEAD~2 &&
	check_changes $head5p2 &&
	test "$(git rev-parse ORIG_HEAD)" = \
			$head4
'

test_expect_success 'redoing changes adding them without commit them should succeed' '
	>.diff_expect &&
	cat >.cached_expect <<-EOF &&
	diff --git a/first b/first
	deleted file mode 100644
	index $head5p2f..0000000
	--- a/first
	+++ /dev/null
	@@ -1,2 +0,0 @@
	-1st file
	-2nd line 1st file
	diff --git a/second b/second
	deleted file mode 100644
	index $head5p1s..0000000
	--- a/second
	+++ /dev/null
	@@ -1 +0,0 @@
	-2nd file
	diff --git a/secondfile b/secondfile
	new file mode 100644
	index 0000000..$head5s
	--- /dev/null
	+++ b/secondfile
	@@ -0,0 +1,2 @@
	+1st line 2nd file
	+2nd line 2nd file
	EOF
	cat >.cat_expect <<-\EOF &&
	secondfile:
	1st line 2nd file
	2nd line 2nd file
	EOF
	git rm first &&
	git mv second secondfile &&

	echo "1st line 2nd file" >secondfile &&
	echo "2nd line 2nd file" >>secondfile &&
	git add secondfile &&
	check_changes $head5p2
'

test_expect_success '--mixed reset to HEAD should unadd the files' '
	cat >.diff_expect <<-EOF &&
	diff --git a/first b/first
	deleted file mode 100644
	index $head5p2f..0000000
	--- a/first
	+++ /dev/null
	@@ -1,2 +0,0 @@
	-1st file
	-2nd line 1st file
	diff --git a/second b/second
	deleted file mode 100644
	index $head5p1s..0000000
	--- a/second
	+++ /dev/null
	@@ -1 +0,0 @@
	-2nd file
	EOF
	>.cached_expect &&
	cat >.cat_expect <<-\EOF &&
	secondfile:
	1st line 2nd file
	2nd line 2nd file
	EOF
	git reset &&
	check_changes $head5p2 &&
	test "$(git rev-parse ORIG_HEAD)" = $head5p2
'

test_expect_success 'redoing the last two commits should succeed' '
	>.diff_expect &&
	>.cached_expect &&
	cat >.cat_expect <<-\EOF &&
	secondfile:
	1st line 2nd file
	2nd line 2nd file
	EOF
	git add secondfile &&
	git reset --hard $head5p2 &&
	git rm first &&
	git mv second secondfile &&
	git commit -a -m "remove 1st and rename 2nd" &&

	echo "1st line 2nd file" >secondfile &&
	echo "2nd line 2nd file" >>secondfile &&
	# "git commit -m" would break MinGW, as Windows refuse to pass
	# $test_encoding encoded parameter to git.
	commit_msg $test_encoding | git -c "i18n.commitEncoding=$test_encoding" commit -a -F - &&
	check_changes $head5
'

test_expect_success '--hard reset to HEAD should clear a failed merge' '
	>.diff_expect &&
	>.cached_expect &&
	cat >.cat_expect <<-\EOF &&
	secondfile:
	1st line 2nd file
	2nd line 2nd file
	3rd line in branch2
	EOF
	git branch branch1 &&
	git branch branch2 &&

	git checkout branch1 &&
	echo "3rd line in branch1" >>secondfile &&
	git commit -a -m "change in branch1" &&

	git checkout branch2 &&
	echo "3rd line in branch2" >>secondfile &&
	git commit -a -m "change in branch2" &&
	head3=$(git rev-parse --verify HEAD) &&

	test_must_fail git pull . branch1 &&
	git reset --hard &&
	check_changes $head3
'

test_expect_success '--hard reset to ORIG_HEAD should clear a fast-forward merge' '
	>.diff_expect &&
	>.cached_expect &&
	cat >.cat_expect <<-\EOF &&
	secondfile:
	1st line 2nd file
	2nd line 2nd file
	EOF
	git reset --hard HEAD^ &&
	check_changes $head5 &&

	git pull . branch1 &&
	git reset --hard ORIG_HEAD &&
	check_changes $head5 &&

	git checkout main &&
	git branch -D branch1 branch2 &&
	check_changes $head5
'

test_expect_success 'test --mixed <paths>' '
	echo 1 >file1 &&
	echo 2 >file2 &&
	git add file1 file2 &&
	test_tick &&
	git commit -m files &&
	before1=$(git rev-parse --short HEAD:file1) &&
	before2=$(git rev-parse --short HEAD:file2) &&
	git rm file2 &&
	echo 3 >file3 &&
	echo 4 >file4 &&
	echo 5 >file1 &&
	after1=$(git rev-parse --short $(git hash-object file1)) &&
	after4=$(git rev-parse --short $(git hash-object file4)) &&
	git add file1 file3 file4 &&
	git reset HEAD -- file1 file2 file3 &&
	test_must_fail git diff --quiet &&
	git diff >output &&

	cat >expect <<-EOF &&
	diff --git a/file1 b/file1
	index $before1..$after1 100644
	--- a/file1
	+++ b/file1
	@@ -1 +1 @@
	-1
	+5
	diff --git a/file2 b/file2
	deleted file mode 100644
	index $before2..0000000
	--- a/file2
	+++ /dev/null
	@@ -1 +0,0 @@
	-2
	EOF

	test_cmp expect output &&
	git diff --cached >output &&

	cat >cached_expect <<-EOF &&
	diff --git a/file4 b/file4
	new file mode 100644
	index 0000000..$after4
	--- /dev/null
	+++ b/file4
	@@ -0,0 +1 @@
	+4
	EOF

	test_cmp cached_expect output
'

test_expect_success 'test resetting the index at give paths' '
	mkdir sub &&
	>sub/file1 &&
	>sub/file2 &&
	git update-index --add sub/file1 sub/file2 &&
	T=$(git write-tree) &&
	git reset HEAD sub/file2 &&
	test_must_fail git diff --quiet &&
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

test_reset_refreshes_index () {

	# To test whether the index is refreshed in `git reset --mixed` with
	# the given options, create a scenario where we clearly see different
	# results depending on whether the refresh occurred or not.

	# Step 0: start with a clean index
	git reset --hard HEAD &&

	# Step 1: remove file2, but only in the index (no change to worktree)
	git rm --cached file2 &&

	# Step 2: reset index & leave worktree unchanged from HEAD
	git $1 reset $2 --mixed HEAD &&

	# Step 3: verify whether the index is refreshed by checking whether
	# file2 still has staged changes in the index differing from HEAD (if
	# the refresh occurred, there should be no such changes)
	git diff-files >output.log &&
	test_must_be_empty output.log
}

test_expect_success '--mixed refreshes the index' '
	# Verify default behavior (without --[no-]refresh or reset.refresh)
	test_reset_refreshes_index &&

	# With --quiet
	test_reset_refreshes_index "" --quiet
'

test_expect_success '--mixed --[no-]refresh sets refresh behavior' '
	# Verify that --[no-]refresh controls index refresh
	test_reset_refreshes_index "" --refresh &&
	! test_reset_refreshes_index "" --no-refresh
'

test_expect_success '--mixed preserves skip-worktree' '
	echo 123 >>file2 &&
	git add file2 &&
	git update-index --skip-worktree file2 &&
	git reset --mixed HEAD >output &&
	test_must_be_empty output &&

	cat >expect <<-\EOF &&
	Unstaged changes after reset:
	M	file2
	EOF
	git update-index --no-skip-worktree file2 &&
	git add file2 &&
	git reset --mixed HEAD >output &&
	test_cmp expect output
'

test_expect_success 'resetting specific path that is unmerged' '
	git rm --cached file2 &&
	F1=$(git rev-parse HEAD:file1) &&
	F2=$(git rev-parse HEAD:file2) &&
	F3=$(git rev-parse HEAD:secondfile) &&
	{
		echo "100644 $F1 1	file2" &&
		echo "100644 $F2 2	file2" &&
		echo "100644 $F3 3	file2"
	} | git update-index --index-info &&
	git ls-files -u &&
	git reset HEAD file2 &&
	test_must_fail git diff --quiet &&
	git diff-index --exit-code --cached HEAD
'

test_expect_success 'disambiguation (1)' '
	git reset --hard &&
	>secondfile &&
	git add secondfile &&
	git reset secondfile &&
	test_must_fail git diff --quiet -- secondfile &&
	test -z "$(git diff --cached --name-only)" &&
	test -f secondfile &&
	test_must_be_empty secondfile
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
	git reset HEAD secondfile &&
	test_must_fail git diff --quiet &&
	test -z "$(git diff --cached --name-only)" &&
	test ! -f secondfile
'

test_expect_success 'disambiguation (4)' '
	git reset --hard &&
	>secondfile &&
	git add secondfile &&
	rm -f secondfile &&
	git reset -- secondfile &&
	test_must_fail git diff --quiet &&
	test -z "$(git diff --cached --name-only)" &&
	test ! -f secondfile
'

test_expect_success 'reset with paths accepts tree' '
	# for simpler tests, drop last commit containing added files
	git reset --hard HEAD^ &&
	git reset HEAD^^{tree} -- . &&
	git diff --cached HEAD^ --exit-code &&
	git diff HEAD --exit-code
'

test_expect_success 'reset -N keeps removed files as intent-to-add' '
	echo new-file >new-file &&
	git add new-file &&
	git reset -N HEAD &&

	tree=$(git write-tree) &&
	git ls-tree $tree new-file >actual &&
	test_must_be_empty actual &&

	git diff --name-only >actual &&
	echo new-file >expect &&
	test_cmp expect actual
'

test_expect_success 'reset --mixed sets up work tree' '
	git init mixed_worktree &&
	(
		cd mixed_worktree &&
		test_commit dummy
	) &&
	git --git-dir=mixed_worktree/.git --work-tree=mixed_worktree reset >actual &&
	test_must_be_empty actual
'

test_expect_success 'reset handles --end-of-options' '
	git update-ref refs/heads/--foo HEAD^ &&
	git log -1 --format=%s refs/heads/--foo >expect &&
	git reset --hard --end-of-options --foo &&
	git log -1 --format=%s HEAD >actual &&
	test_cmp expect actual
'

test_done
