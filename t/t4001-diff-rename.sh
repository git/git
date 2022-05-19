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
	diff --but a/path0 b/path1
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
	diff --but a/path0 b/path0
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
	diff --but a/path1 b/path1
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
    'but update-index --add path0'

test_expect_success \
    'write that tree.' \
    'tree=$(but write-tree) && echo $tree'

sed -e 's/line/Line/' <path0 >path1
rm -f path0
test_expect_success \
    'renamed and edited the file.' \
    'but update-index --add --remove path0 path1'

test_expect_success \
    'but diff-index -p -M after rename and editing.' \
    'but diff-index -p -M $tree >current'


test_expect_success \
    'validate the output.' \
    'compare_diff_patch current expected'

test_expect_success 'test diff.renames=true' '
	but -c diff.renames=true diff --cached $tree >current &&
	compare_diff_patch current expected
'

test_expect_success 'test diff.renames=false' '
	but -c diff.renames=false diff --cached $tree >current &&
	compare_diff_patch current no-rename
'

test_expect_success 'test diff.renames unset' '
	but diff --cached $tree >current &&
	compare_diff_patch current expected
'

test_expect_success 'favour same basenames over different ones' '
	cp path1 another-path &&
	but add another-path &&
	but cummit -m 1 &&
	but rm path1 &&
	mkdir subdir &&
	but mv another-path subdir/path1 &&
	but status >out &&
	test_i18ngrep "renamed: .*path1 -> subdir/path1" out
'

test_expect_success 'test diff.renames=true for but status' '
	but -c diff.renames=true status >out &&
	test_i18ngrep "renamed: .*path1 -> subdir/path1" out
'

test_expect_success 'test diff.renames=false for but status' '
	but -c diff.renames=false status >out &&
	test_i18ngrep ! "renamed: .*path1 -> subdir/path1" out &&
	test_i18ngrep "new file: .*subdir/path1" out &&
	test_i18ngrep "deleted: .*[^/]path1" out
'

test_expect_success 'favour same basenames even with minor differences' '
	but show HEAD:path1 | sed "s/15/16/" > subdir/path1 &&
	but status >out &&
	test_i18ngrep "renamed: .*path1 -> subdir/path1" out
'

test_expect_success 'two files with same basename and same content' '
	but reset --hard &&
	mkdir -p dir/A dir/B &&
	cp path1 dir/A/file &&
	cp path1 dir/B/file &&
	but add dir &&
	but cummit -m 2 &&
	but mv dir other-dir &&
	but status >out &&
	test_i18ngrep "renamed: .*dir/A/file -> other-dir/A/file" out
'

test_expect_success 'setup for many rename source candidates' '
	but reset --hard &&
	for i in 0 1 2 3 4 5 6 7 8 9;
	do
		for j in 0 1 2 3 4 5 6 7 8 9;
		do
			echo "$i$j" >"path$i$j" || return 1
		done
	done &&
	but add "path??" &&
	test_tick &&
	but cummit -m "hundred" &&
	(cat path1 && echo new) >new-path &&
	echo old >>path1 &&
	but add new-path path1 &&
	but diff -l 4 -C -C --cached --name-status >actual 2>actual.err &&
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
	but add a/b/c &&
	but cummit -m "create a/b/c" &&
	mkdir -p c/b/ &&
	but mv a/b/c c/b/a &&
	but cummit -m "a/b/c -> c/b/a" &&
	but diff -M --summary HEAD^ HEAD >output &&
	test_i18ngrep " a/b/c => c/b/a " output &&
	but diff -M --stat HEAD^ HEAD >output &&
	test_i18ngrep " a/b/c => c/b/a " output
'

test_expect_success 'rename pretty print with common prefix' '
	mkdir -p c/d &&
	but mv c/b/a c/d/e &&
	but cummit -m "c/b/a -> c/d/e" &&
	but diff -M --summary HEAD^ HEAD >output &&
	test_i18ngrep " c/{b/a => d/e} " output &&
	but diff -M --stat HEAD^ HEAD >output &&
	test_i18ngrep " c/{b/a => d/e} " output
'

test_expect_success 'rename pretty print with common suffix' '
	mkdir d &&
	but mv c/d/e d/e &&
	but cummit -m "c/d/e -> d/e" &&
	but diff -M --summary HEAD^ HEAD >output &&
	test_i18ngrep " {c/d => d}/e " output &&
	but diff -M --stat HEAD^ HEAD >output &&
	test_i18ngrep " {c/d => d}/e " output
'

test_expect_success 'rename pretty print with common prefix and suffix' '
	mkdir d/f &&
	but mv d/e d/f/e &&
	but cummit -m "d/e -> d/f/e" &&
	but diff -M --summary HEAD^ HEAD >output &&
	test_i18ngrep " d/{ => f}/e " output &&
	but diff -M --stat HEAD^ HEAD >output &&
	test_i18ngrep " d/{ => f}/e " output
'

test_expect_success 'rename pretty print common prefix and suffix overlap' '
	mkdir d/f/f &&
	but mv d/f/e d/f/f/e &&
	but cummit -m "d/f/e d/f/f/e" &&
	but diff -M --summary HEAD^ HEAD >output &&
	test_i18ngrep " d/f/{ => f}/e " output &&
	but diff -M --stat HEAD^ HEAD >output &&
	test_i18ngrep " d/f/{ => f}/e " output
'

test_expect_success 'diff-tree -l0 defaults to a big rename limit, not zero' '
	test_write_lines line1 line2 line3 >myfile &&
	but add myfile &&
	but cummit -m x &&

	test_write_lines line1 line2 line4 >myotherfile &&
	but rm myfile &&
	but add myotherfile &&
	but cummit -m x &&

	but diff-tree -M -l0 HEAD HEAD^ >actual &&
	# Verify that a rename from myotherfile to myfile was detected
	grep "myotherfile.*myfile" actual
'

test_expect_success 'basename similarity vs best similarity' '
	mkdir subdir &&
	test_write_lines line1 line2 line3 line4 line5 \
			 line6 line7 line8 line9 line10 >subdir/file.txt &&
	but add subdir/file.txt &&
	but cummit -m "base txt" &&

	but rm subdir/file.txt &&
	test_write_lines line1 line2 line3 line4 line5 \
			  line6 line7 line8 >file.txt &&
	test_write_lines line1 line2 line3 line4 line5 \
			  line6 line7 line8 line9 >file.md &&
	but add file.txt file.md &&
	but cummit -a -m "rename" &&
	but diff-tree -r -M --name-status HEAD^ HEAD >actual &&
	# subdir/file.txt is 88% similar to file.md, 78% similar to file.txt,
	# but since same basenames are checked first...
	cat >expected <<-\EOF &&
	A	file.md
	R078	subdir/file.txt	file.txt
	EOF
	test_cmp expected actual
'

test_done
