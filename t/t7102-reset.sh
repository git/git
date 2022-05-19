#!/bin/sh
#
# Copyright (c) 2007 Carlos Rica
#

test_description='but reset

Documented tests for but reset'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

cummit_msg () {
	# String "modify 2nd file (changed)" partly in German
	# (translated with Google Translate),
	# encoded in UTF-8, used as a cummit log message below.
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

test_expect_success 'creating initial files and cummits' '
	test_tick &&
	echo "1st file" >first &&
	but add first &&
	but cummit -m "create 1st file" &&

	echo "2nd file" >second &&
	but add second &&
	but cummit -m "create 2nd file" &&

	echo "2nd line 1st file" >>first &&
	but cummit -a -m "modify 1st file" &&
	head5p2=$(but rev-parse --verify HEAD) &&
	head5p2f=$(but rev-parse --short HEAD:first) &&

	but rm first &&
	but mv second secondfile &&
	but cummit -a -m "remove 1st and rename 2nd" &&
	head5p1=$(but rev-parse --verify HEAD) &&
	head5p1s=$(but rev-parse --short HEAD:secondfile) &&

	echo "1st line 2nd file" >secondfile &&
	echo "2nd line 2nd file" >>secondfile &&
	# "but cummit -m" would break MinGW, as Windows refuse to pass
	# $test_encoding encoded parameter to but.
	cummit_msg $test_encoding | but -c "i18n.cummitEncoding=$test_encoding" cummit -a -F - &&
	head5=$(but rev-parse --verify HEAD) &&
	head5s=$(but rev-parse --short HEAD:secondfile) &&
	head5sl=$(but rev-parse HEAD:secondfile)
'
# but log --pretty=oneline # to see those SHA1 involved

check_changes () {
	test "$(but rev-parse HEAD)" = "$1" &&
	but diff | test_cmp .diff_expect - &&
	but diff --cached | test_cmp .cached_expect - &&
	for FILE in *
	do
		echo $FILE':'
		cat $FILE || return
	done | test_cmp .cat_expect -
}

test_expect_success 'reset --hard message' '
	hex=$(but log -1 --format="%h") &&
	but reset --hard >.actual &&
	echo HEAD is now at $hex $(cummit_msg) >.expected &&
	test_cmp .expected .actual
'

test_expect_success 'reset --hard message (ISO8859-1 logoutputencoding)' '
	hex=$(but log -1 --format="%h") &&
	but -c "i18n.logOutputEncoding=$test_encoding" reset --hard >.actual &&
	echo HEAD is now at $hex $(cummit_msg $test_encoding) >.expected &&
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

	test_must_fail but reset aaaaaa &&
	test_must_fail but reset --mixed aaaaaa &&
	test_must_fail but reset --soft aaaaaa &&
	test_must_fail but reset --hard aaaaaa &&
	check_changes $head5
'

test_expect_success 'reset --soft with unmerged index should fail' '
	touch .but/MERGE_HEAD &&
	echo "100644 $head5sl 1	un" |
		but update-index --index-info &&
	test_must_fail but reset --soft HEAD &&
	rm .but/MERGE_HEAD &&
	but rm --cached -- un
'

test_expect_success 'giving paths with options different than --mixed should fail' '
	test_must_fail but reset --soft -- first &&
	test_must_fail but reset --hard -- first &&
	test_must_fail but reset --soft HEAD^ -- first &&
	test_must_fail but reset --hard HEAD^ -- first &&
	check_changes $head5
'

test_expect_success 'giving unrecognized options should fail' '
	test_must_fail but reset --other &&
	test_must_fail but reset -o &&
	test_must_fail but reset --mixed --other &&
	test_must_fail but reset --mixed -o &&
	test_must_fail but reset --soft --other &&
	test_must_fail but reset --soft -o &&
	test_must_fail but reset --hard --other &&
	test_must_fail but reset --hard -o &&
	check_changes $head5
'

test_expect_success 'trying to do reset --soft with pending merge should fail' '
	but branch branch1 &&
	but branch branch2 &&

	but checkout branch1 &&
	echo "3rd line in branch1" >>secondfile &&
	but cummit -a -m "change in branch1" &&

	but checkout branch2 &&
	echo "3rd line in branch2" >>secondfile &&
	but cummit -a -m "change in branch2" &&

	test_must_fail but merge branch1 &&
	test_must_fail but reset --soft &&

	printf "1st line 2nd file\n2nd line 2nd file\n3rd line" >secondfile &&
	but cummit -a -m "the change in branch2" &&

	but checkout main &&
	but branch -D branch1 branch2 &&
	check_changes $head5
'

test_expect_success 'trying to do reset --soft with pending checkout merge should fail' '
	but branch branch3 &&
	but branch branch4 &&

	but checkout branch3 &&
	echo "3rd line in branch3" >>secondfile &&
	but cummit -a -m "line in branch3" &&

	but checkout branch4 &&
	echo "3rd line in branch4" >>secondfile &&

	but checkout -m branch3 &&
	test_must_fail but reset --soft &&

	printf "1st line 2nd file\n2nd line 2nd file\n3rd line" >secondfile &&
	but cummit -a -m "the line in branch3" &&

	but checkout main &&
	but branch -D branch3 branch4 &&
	check_changes $head5
'

test_expect_success 'resetting to HEAD with no changes should succeed and do nothing' '
	but reset --hard &&
		check_changes $head5 &&
	but reset --hard HEAD &&
		check_changes $head5 &&
	but reset --soft &&
		check_changes $head5 &&
	but reset --soft HEAD &&
		check_changes $head5 &&
	but reset --mixed &&
		check_changes $head5 &&
	but reset --mixed HEAD &&
		check_changes $head5 &&
	but reset &&
		check_changes $head5 &&
	but reset HEAD &&
		check_changes $head5
'

test_expect_success '--soft reset only should show changes in diff --cached' '
	>.diff_expect &&
	cat >.cached_expect <<-EOF &&
	diff --but a/secondfile b/secondfile
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
	but reset --soft HEAD^ &&
	check_changes $head5p1 &&
	test "$(but rev-parse ORIG_HEAD)" = \
			$head5
'

test_expect_success 'changing files and redo the last cummit should succeed' '
	>.diff_expect &&
	>.cached_expect &&
	cat >.cat_expect <<-\EOF &&
	secondfile:
	1st line 2nd file
	2nd line 2nd file
	3rd line 2nd file
	EOF
	echo "3rd line 2nd file" >>secondfile &&
	but cummit -a -C ORIG_HEAD &&
	head4=$(but rev-parse --verify HEAD) &&
	check_changes $head4 &&
	test "$(but rev-parse ORIG_HEAD)" = \
			$head5
'

test_expect_success '--hard reset should change the files and undo cummits permanently' '
	>.diff_expect &&
	>.cached_expect &&
	cat >.cat_expect <<-\EOF &&
	first:
	1st file
	2nd line 1st file
	second:
	2nd file
	EOF
	but reset --hard HEAD~2 &&
	check_changes $head5p2 &&
	test "$(but rev-parse ORIG_HEAD)" = \
			$head4
'

test_expect_success 'redoing changes adding them without cummit them should succeed' '
	>.diff_expect &&
	cat >.cached_expect <<-EOF &&
	diff --but a/first b/first
	deleted file mode 100644
	index $head5p2f..0000000
	--- a/first
	+++ /dev/null
	@@ -1,2 +0,0 @@
	-1st file
	-2nd line 1st file
	diff --but a/second b/second
	deleted file mode 100644
	index $head5p1s..0000000
	--- a/second
	+++ /dev/null
	@@ -1 +0,0 @@
	-2nd file
	diff --but a/secondfile b/secondfile
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
	but rm first &&
	but mv second secondfile &&

	echo "1st line 2nd file" >secondfile &&
	echo "2nd line 2nd file" >>secondfile &&
	but add secondfile &&
	check_changes $head5p2
'

test_expect_success '--mixed reset to HEAD should unadd the files' '
	cat >.diff_expect <<-EOF &&
	diff --but a/first b/first
	deleted file mode 100644
	index $head5p2f..0000000
	--- a/first
	+++ /dev/null
	@@ -1,2 +0,0 @@
	-1st file
	-2nd line 1st file
	diff --but a/second b/second
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
	but reset &&
	check_changes $head5p2 &&
	test "$(but rev-parse ORIG_HEAD)" = $head5p2
'

test_expect_success 'redoing the last two cummits should succeed' '
	>.diff_expect &&
	>.cached_expect &&
	cat >.cat_expect <<-\EOF &&
	secondfile:
	1st line 2nd file
	2nd line 2nd file
	EOF
	but add secondfile &&
	but reset --hard $head5p2 &&
	but rm first &&
	but mv second secondfile &&
	but cummit -a -m "remove 1st and rename 2nd" &&

	echo "1st line 2nd file" >secondfile &&
	echo "2nd line 2nd file" >>secondfile &&
	# "but cummit -m" would break MinGW, as Windows refuse to pass
	# $test_encoding encoded parameter to but.
	cummit_msg $test_encoding | but -c "i18n.cummitEncoding=$test_encoding" cummit -a -F - &&
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
	but branch branch1 &&
	but branch branch2 &&

	but checkout branch1 &&
	echo "3rd line in branch1" >>secondfile &&
	but cummit -a -m "change in branch1" &&

	but checkout branch2 &&
	echo "3rd line in branch2" >>secondfile &&
	but cummit -a -m "change in branch2" &&
	head3=$(but rev-parse --verify HEAD) &&

	test_must_fail but pull . branch1 &&
	but reset --hard &&
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
	but reset --hard HEAD^ &&
	check_changes $head5 &&

	but pull . branch1 &&
	but reset --hard ORIG_HEAD &&
	check_changes $head5 &&

	but checkout main &&
	but branch -D branch1 branch2 &&
	check_changes $head5
'

test_expect_success 'test --mixed <paths>' '
	echo 1 >file1 &&
	echo 2 >file2 &&
	but add file1 file2 &&
	test_tick &&
	but cummit -m files &&
	before1=$(but rev-parse --short HEAD:file1) &&
	before2=$(but rev-parse --short HEAD:file2) &&
	but rm file2 &&
	echo 3 >file3 &&
	echo 4 >file4 &&
	echo 5 >file1 &&
	after1=$(but rev-parse --short $(but hash-object file1)) &&
	after4=$(but rev-parse --short $(but hash-object file4)) &&
	but add file1 file3 file4 &&
	but reset HEAD -- file1 file2 file3 &&
	test_must_fail but diff --quiet &&
	but diff >output &&

	cat >expect <<-EOF &&
	diff --but a/file1 b/file1
	index $before1..$after1 100644
	--- a/file1
	+++ b/file1
	@@ -1 +1 @@
	-1
	+5
	diff --but a/file2 b/file2
	deleted file mode 100644
	index $before2..0000000
	--- a/file2
	+++ /dev/null
	@@ -1 +0,0 @@
	-2
	EOF

	test_cmp expect output &&
	but diff --cached >output &&

	cat >cached_expect <<-EOF &&
	diff --but a/file4 b/file4
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
	but update-index --add sub/file1 sub/file2 &&
	T=$(but write-tree) &&
	but reset HEAD sub/file2 &&
	test_must_fail but diff --quiet &&
	U=$(but write-tree) &&
	echo "$T" &&
	echo "$U" &&
	test_must_fail but diff-index --cached --exit-code "$T" &&
	test "$T" != "$U"
'

test_expect_success 'resetting an unmodified path is a no-op' '
	but reset --hard &&
	but reset -- file1 &&
	but diff-files --exit-code &&
	but diff-index --cached --exit-code HEAD
'

test_reset_refreshes_index () {

	# To test whether the index is refreshed in `but reset --mixed` with
	# the given options, create a scenario where we clearly see different
	# results depending on whether the refresh occurred or not.

	# Step 0: start with a clean index
	but reset --hard HEAD &&

	# Step 1: remove file2, but only in the index (no change to worktree)
	but rm --cached file2 &&

	# Step 2: reset index & leave worktree unchanged from HEAD
	but $1 reset $2 --mixed HEAD &&

	# Step 3: verify whether the index is refreshed by checking whether
	# file2 still has staged changes in the index differing from HEAD (if
	# the refresh occurred, there should be no such changes)
	but diff-files >output.log &&
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
	but add file2 &&
	but update-index --skip-worktree file2 &&
	but reset --mixed HEAD >output &&
	test_must_be_empty output &&

	cat >expect <<-\EOF &&
	Unstaged changes after reset:
	M	file2
	EOF
	but update-index --no-skip-worktree file2 &&
	but add file2 &&
	but reset --mixed HEAD >output &&
	test_cmp expect output
'

test_expect_success 'resetting specific path that is unmerged' '
	but rm --cached file2 &&
	F1=$(but rev-parse HEAD:file1) &&
	F2=$(but rev-parse HEAD:file2) &&
	F3=$(but rev-parse HEAD:secondfile) &&
	{
		echo "100644 $F1 1	file2" &&
		echo "100644 $F2 2	file2" &&
		echo "100644 $F3 3	file2"
	} | but update-index --index-info &&
	but ls-files -u &&
	but reset HEAD file2 &&
	test_must_fail but diff --quiet &&
	but diff-index --exit-code --cached HEAD
'

test_expect_success 'disambiguation (1)' '
	but reset --hard &&
	>secondfile &&
	but add secondfile &&
	but reset secondfile &&
	test_must_fail but diff --quiet -- secondfile &&
	test -z "$(but diff --cached --name-only)" &&
	test -f secondfile &&
	test_must_be_empty secondfile
'

test_expect_success 'disambiguation (2)' '
	but reset --hard &&
	>secondfile &&
	but add secondfile &&
	rm -f secondfile &&
	test_must_fail but reset secondfile &&
	test -n "$(but diff --cached --name-only -- secondfile)" &&
	test ! -f secondfile
'

test_expect_success 'disambiguation (3)' '
	but reset --hard &&
	>secondfile &&
	but add secondfile &&
	rm -f secondfile &&
	but reset HEAD secondfile &&
	test_must_fail but diff --quiet &&
	test -z "$(but diff --cached --name-only)" &&
	test ! -f secondfile
'

test_expect_success 'disambiguation (4)' '
	but reset --hard &&
	>secondfile &&
	but add secondfile &&
	rm -f secondfile &&
	but reset -- secondfile &&
	test_must_fail but diff --quiet &&
	test -z "$(but diff --cached --name-only)" &&
	test ! -f secondfile
'

test_expect_success 'reset with paths accepts tree' '
	# for simpler tests, drop last cummit containing added files
	but reset --hard HEAD^ &&
	but reset HEAD^^{tree} -- . &&
	but diff --cached HEAD^ --exit-code &&
	but diff HEAD --exit-code
'

test_expect_success 'reset -N keeps removed files as intent-to-add' '
	echo new-file >new-file &&
	but add new-file &&
	but reset -N HEAD &&

	tree=$(but write-tree) &&
	but ls-tree $tree new-file >actual &&
	test_must_be_empty actual &&

	but diff --name-only >actual &&
	echo new-file >expect &&
	test_cmp expect actual
'

test_expect_success 'reset --mixed sets up work tree' '
	but init mixed_worktree &&
	(
		cd mixed_worktree &&
		test_cummit dummy
	) &&
	but --but-dir=mixed_worktree/.but --work-tree=mixed_worktree reset >actual &&
	test_must_be_empty actual
'

test_done
