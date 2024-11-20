#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Test built-in diff output engine.

We happen to know that all diff plumbing and diff Porcelain share the
same command line parser, so testing one should be sufficient; pick
diff-files as a representative.
'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff.sh

echo >path0 'Line 1
Line 2
line 3'
cat path0 >path1
chmod +x path1
mkdir path2
>path2/path3

test_expect_success 'update-index --add two files with and without +x.' '
	git update-index --add path0 path1 path2/path3
'

mv path0 path0-
sed -e 's/line/Line/' <path0- >path0
chmod +x path0
rm -f path1
test_expect_success 'git diff-files -p after editing work tree.' '
	git diff-files -p >actual
'

# that's as far as it comes
if [ "$(git config --get core.filemode)" = false ]
then
	say 'filemode disabled on the filesystem'
	test_done
fi

cat >expected <<\EOF
diff --git a/path0 b/path0
old mode 100644
new mode 100755
--- a/path0
+++ b/path0
@@ -1,3 +1,3 @@
 Line 1
 Line 2
-line 3
+Line 3
diff --git a/path1 b/path1
deleted file mode 100755
--- a/path1
+++ /dev/null
@@ -1,3 +0,0 @@
-Line 1
-Line 2
-line 3
EOF

test_expect_success 'validate git diff-files -p output.' '
	compare_diff_patch expected actual
'

test_expect_success 'git diff-files -s after editing work tree' '
	git diff-files -s >actual 2>err &&
	test_must_be_empty actual &&
	test_must_be_empty err
'

test_expect_success 'git diff-files --no-patch as synonym for -s' '
	git diff-files --no-patch >actual 2>err &&
	test_must_be_empty actual &&
	test_must_be_empty err
'

test_expect_success 'git diff-files --no-patch --patch shows the patch' '
	git diff-files --no-patch --patch >actual &&
	compare_diff_patch expected actual
'

test_expect_success 'git diff-files --no-patch --patch-with-raw shows the patch and raw data' '
	git diff-files --no-patch --patch-with-raw >actual &&
	grep -q "^:100644 100755 .* $ZERO_OID M	path0\$" actual &&
	tail -n +4 actual >actual-patch &&
	compare_diff_patch expected actual-patch
'

test_expect_success 'git diff-files --patch --no-patch does not show the patch' '
	git diff-files --patch --no-patch >actual 2>err &&
	test_must_be_empty actual &&
	test_must_be_empty err
'


# Smudge path2/path3 so that dirstat has something to show
date >path2/path3

for format in stat raw numstat shortstat summary \
	dirstat cumulative dirstat-by-file \
	patch-with-raw patch-with-stat compact-summary
do
	test_expect_success "--no-patch in 'git diff-files --no-patch --$format' is a no-op" '
		git diff-files --no-patch "--$format" >actual &&
		git diff-files "--$format" >expect &&
		test_cmp expect actual
	'

	test_expect_success "--no-patch clears all previous ones" '
		git diff-files --$format -s -p >actual &&
		git diff-files -p >expect &&
		test_cmp expect actual
	'

	test_expect_success "--no-patch in 'git diff --no-patch --$format' is a no-op" '
		git diff --no-patch "--$format" >actual &&
		git diff "--$format" >expect &&
		test_cmp expect actual
	'
done

test_done
