#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Test rename detection in diff engine.

'
. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh

echo >path0 'Line 1
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
cat >expected <<\EOF
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

test_expect_success \
    'validate the output.' \
    'compare_diff_patch current expected'

test_expect_success 'favour same basenames over different ones' '
	cp path1 another-path &&
	git add another-path &&
	git commit -m 1 &&
	git rm path1 &&
	mkdir subdir &&
	git mv another-path subdir/path1 &&
	git status | test_i18ngrep "renamed: .*path1 -> subdir/path1"'

test_expect_success 'favour same basenames even with minor differences' '
	git show HEAD:path1 | sed "s/15/16/" > subdir/path1 &&
	git status | test_i18ngrep "renamed: .*path1 -> subdir/path1"'

test_expect_success 'setup for many rename source candidates' '
	git reset --hard &&
	for i in 0 1 2 3 4 5 6 7 8 9;
	do
		for j in 0 1 2 3 4 5 6 7 8 9;
		do
			echo "$i$j" >"path$i$j"
		done
	done &&
	git add "path??" &&
	test_tick &&
	git commit -m "hundred" &&
	(cat path1; echo new) >new-path &&
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

test_done
