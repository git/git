#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Test rename detection in diff engine.

'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff.sh

test_expect_success 'setup' '
	cat >path0 <<-\EOF &&
	Line 1
	Line 2
	Line 3
	Line 4
	Line 5
	Line 6
	Line 7
	Line 8
	Line 9
	Line 10
	line 11
	Line 12
	Line 13
	Line 14
	Line 15
	EOF
	cat >expected <<-\EOF &&
	diff --git a/path0 b/path1
	rename from path0
	rename to path1
	--- a/path0
	+++ b/path1
	@@ -8,7 +8,7 @@ Line 7
	 Line 8
	 Line 9
	 Line 10
	-line 11
	+Line 11
	 Line 12
	 Line 13
	 Line 14
	EOF
	cat >no-rename <<-\EOF
	diff --git a/path0 b/path0
	deleted file mode 100644
	index fdbec44..0000000
	--- a/path0
	+++ /dev/null
	@@ -1,15 +0,0 @@
	-Line 1
	-Line 2
	-Line 3
	-Line 4
	-Line 5
	-Line 6
	-Line 7
	-Line 8
	-Line 9
	-Line 10
	-line 11
	-Line 12
	-Line 13
	-Line 14
	-Line 15
	diff --git a/path1 b/path1
	new file mode 100644
	index 0000000..752c50e
	--- /dev/null
	+++ b/path1
	@@ -0,0 +1,15 @@
	+Line 1
	+Line 2
	+Line 3
	+Line 4
	+Line 5
	+Line 6
	+Line 7
	+Line 8
	+Line 9
	+Line 10
	+Line 11
	+Line 12
	+Line 13
	+Line 14
	+Line 15
	EOF
'

test_expect_success \
    'update-index --add a file.' \
    'git update-index --add path0'

test_expect_success \
    'write that tree.' \
    'tree=$(git write-tree) && echo $tree'

sed -e 's/line/Line/' <path0 >path1
rm -f path0
test_expect_success \
    'renamed and edited the file.' \
    'git update-index --add --remove path0 path1'

test_expect_success \
    'git diff-index -p -M after rename and editing.' \
    'git diff-index -p -M $tree >current'


test_expect_success \
    'validate the output.' \
    'compare_diff_patch current expected'

test_expect_success 'test diff.renames=true' '
	git -c diff.renames=true diff --cached $tree >current &&
	compare_diff_patch current expected
'

test_expect_success 'test diff.renames=false' '
	git -c diff.renames=false diff --cached $tree >current &&
	compare_diff_patch current no-rename
'

test_expect_success 'test diff.renames unset' '
	git diff --cached $tree >current &&
	compare_diff_patch current expected
'

test_expect_success 'favour same basenames over different ones' '
	cp path1 another-path &&
	git add another-path &&
	git commit -m 1 &&
	git rm path1 &&
	mkdir subdir &&
	git mv another-path subdir/path1 &&
	git status >out &&
	test_i18ngrep "renamed: .*path1 -> subdir/path1" out
'

test_expect_success 'test diff.renames=true for git status' '
	git -c diff.renames=true status >out &&
	test_i18ngrep "renamed: .*path1 -> subdir/path1" out
'

test_expect_success 'test diff.renames=false for git status' '
	git -c diff.renames=false status >out &&
	test_i18ngrep ! "renamed: .*path1 -> subdir/path1" out &&
	test_i18ngrep "new file: .*subdir/path1" out &&
	test_i18ngrep "deleted: .*[^/]path1" out
'

test_expect_success 'favour same basenames even with minor differences' '
	git show HEAD:path1 | sed "s/15/16/" > subdir/path1 &&
	git status >out &&
	test_i18ngrep "renamed: .*path1 -> subdir/path1" out
'

test_expect_success 'two files with same basename and same content' '
	git reset --hard &&
	mkdir -p dir/A dir/B &&
	cp path1 dir/A/file &&
	cp path1 dir/B/file &&
	git add dir &&
	git commit -m 2 &&
	git mv dir other-dir &&
	git status >out &&
	test_i18ngrep "renamed: .*dir/A/file -> other-dir/A/file" out
'

test_expect_success 'setup for many rename source candidates' '
	git reset --hard &&
	for i in 0 1 2 3 4 5 6 7 8 9;
	do
		for j in 0 1 2 3 4 5 6 7 8 9;
		do
			echo "$i$j" >"path$i$j" || return 1
		done
	done &&
	git add "path??" &&
	test_tick &&
	git commit -m "hundred" &&
	(cat path1 && echo new) >new-path &&
	echo old >>path1 &&
	git add new-path path1 &&
	git diff -l 4 -C -C --cached --name-status >actual 2>actual.err &&
	sed -e "s/^\([CM]\)[0-9]*	/\1	/" actual >actual.munged &&
	cat >expect <<-EOF &&
	C	path1	new-path
	M	path1
	EOF
	test_cmp expect actual.munged &&
	grep warning actual.err
'

test_expect_success 'rename pretty print with nothing in common' '
	mkdir -p a/b/ &&
	: >a/b/c &&
	git add a/b/c &&
	git commit -m "create a/b/c" &&
	mkdir -p c/b/ &&
	git mv a/b/c c/b/a &&
	git commit -m "a/b/c -> c/b/a" &&
	git diff -M --summary HEAD^ HEAD >output &&
	test_i18ngrep " a/b/c => c/b/a " output &&
	git diff -M --stat HEAD^ HEAD >output &&
	test_i18ngrep " a/b/c => c/b/a " output
'

test_expect_success 'rename pretty print with common prefix' '
	mkdir -p c/d &&
	git mv c/b/a c/d/e &&
	git commit -m "c/b/a -> c/d/e" &&
	git diff -M --summary HEAD^ HEAD >output &&
	test_i18ngrep " c/{b/a => d/e} " output &&
	git diff -M --stat HEAD^ HEAD >output &&
	test_i18ngrep " c/{b/a => d/e} " output
'

test_expect_success 'rename pretty print with common suffix' '
	mkdir d &&
	git mv c/d/e d/e &&
	git commit -m "c/d/e -> d/e" &&
	git diff -M --summary HEAD^ HEAD >output &&
	test_i18ngrep " {c/d => d}/e " output &&
	git diff -M --stat HEAD^ HEAD >output &&
	test_i18ngrep " {c/d => d}/e " output
'

test_expect_success 'rename pretty print with common prefix and suffix' '
	mkdir d/f &&
	git mv d/e d/f/e &&
	git commit -m "d/e -> d/f/e" &&
	git diff -M --summary HEAD^ HEAD >output &&
	test_i18ngrep " d/{ => f}/e " output &&
	git diff -M --stat HEAD^ HEAD >output &&
	test_i18ngrep " d/{ => f}/e " output
'

test_expect_success 'rename pretty print common prefix and suffix overlap' '
	mkdir d/f/f &&
	git mv d/f/e d/f/f/e &&
	git commit -m "d/f/e d/f/f/e" &&
	git diff -M --summary HEAD^ HEAD >output &&
	test_i18ngrep " d/f/{ => f}/e " output &&
	git diff -M --stat HEAD^ HEAD >output &&
	test_i18ngrep " d/f/{ => f}/e " output
'

test_expect_success 'diff-tree -l0 defaults to a big rename limit, not zero' '
	test_write_lines line1 line2 line3 >myfile &&
	git add myfile &&
	git commit -m x &&

	test_write_lines line1 line2 line4 >myotherfile &&
	git rm myfile &&
	git add myotherfile &&
	git commit -m x &&

	git diff-tree -M -l0 HEAD HEAD^ >actual &&
	# Verify that a rename from myotherfile to myfile was detected
	grep "myotherfile.*myfile" actual
'

test_expect_success 'basename similarity vs best similarity' '
	mkdir subdir &&
	test_write_lines line1 line2 line3 line4 line5 \
			 line6 line7 line8 line9 line10 >subdir/file.txt &&
	git add subdir/file.txt &&
	git commit -m "base txt" &&

	git rm subdir/file.txt &&
	test_write_lines line1 line2 line3 line4 line5 \
			  line6 line7 line8 >file.txt &&
	test_write_lines line1 line2 line3 line4 line5 \
			  line6 line7 line8 line9 >file.md &&
	git add file.txt file.md &&
	git commit -a -m "rename" &&
	git diff-tree -r -M --name-status HEAD^ HEAD >actual &&
	# subdir/file.txt is 88% similar to file.md, 78% similar to file.txt,
	# but since same basenames are checked first...
	cat >expected <<-\EOF &&
	A	file.md
	R078	subdir/file.txt	file.txt
	EOF
	test_cmp expected actual
'

test_done
