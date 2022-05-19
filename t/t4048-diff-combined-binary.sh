#!/bin/sh

test_description='combined and merge diff handle binary files and textconv'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup binary merge conflict' '
	echo oneQ1 | q_to_nul >binary &&
	but add binary &&
	but cummit -m one &&
	echo twoQ2 | q_to_nul >binary &&
	but cummit -a -m two &&
	two=$(but rev-parse --short HEAD:binary) &&
	but checkout -b branch-binary HEAD^ &&
	echo threeQ3 | q_to_nul >binary &&
	but cummit -a -m three &&
	three=$(but rev-parse --short HEAD:binary) &&
	test_must_fail but merge main &&
	echo resolvedQhooray | q_to_nul >binary &&
	but cummit -a -m resolved &&
	res=$(but rev-parse --short HEAD:binary)
'

cat >expect <<EOF
resolved

diff --but a/binary b/binary
index $three..$res 100644
Binary files a/binary and b/binary differ
resolved

diff --but a/binary b/binary
index $two..$res 100644
Binary files a/binary and b/binary differ
EOF
test_expect_success 'diff -m indicates binary-ness' '
	but show --format=%s -m >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
resolved

diff --combined binary
index $three,$two..$res
Binary files differ
EOF
test_expect_success 'diff -c indicates binary-ness' '
	but show --format=%s -c >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
resolved

diff --cc binary
index $three,$two..$res
Binary files differ
EOF
test_expect_success 'diff --cc indicates binary-ness' '
	but show --format=%s --cc >actual &&
	test_cmp expect actual
'

test_expect_success 'setup non-binary with binary attribute' '
	but checkout main &&
	test_cummit one text &&
	test_cummit two text &&
	two=$(but rev-parse --short HEAD:text) &&
	but checkout -b branch-text HEAD^ &&
	test_cummit three text &&
	three=$(but rev-parse --short HEAD:text) &&
	test_must_fail but merge main &&
	test_cummit resolved text &&
	res=$(but rev-parse --short HEAD:text) &&
	echo text -diff >.butattributes
'

cat >expect <<EOF
resolved

diff --but a/text b/text
index $three..$res 100644
Binary files a/text and b/text differ
resolved

diff --but a/text b/text
index $two..$res 100644
Binary files a/text and b/text differ
EOF
test_expect_success 'diff -m respects binary attribute' '
	but show --format=%s -m >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
resolved

diff --combined text
index $three,$two..$res
Binary files differ
EOF
test_expect_success 'diff -c respects binary attribute' '
	but show --format=%s -c >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
resolved

diff --cc text
index $three,$two..$res
Binary files differ
EOF
test_expect_success 'diff --cc respects binary attribute' '
	but show --format=%s --cc >actual &&
	test_cmp expect actual
'

test_expect_success 'setup textconv attribute' '
	echo "text diff=upcase" >.butattributes &&
	but config diff.upcase.textconv "tr a-z A-Z <"
'

cat >expect <<EOF
resolved

diff --but a/text b/text
index $three..$res 100644
--- a/text
+++ b/text
@@ -1 +1 @@
-THREE
+RESOLVED
resolved

diff --but a/text b/text
index $two..$res 100644
--- a/text
+++ b/text
@@ -1 +1 @@
-TWO
+RESOLVED
EOF
test_expect_success 'diff -m respects textconv attribute' '
	but show --format=%s -m >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
resolved

diff --combined text
index $three,$two..$res
--- a/text
+++ b/text
@@@ -1,1 -1,1 +1,1 @@@
- THREE
 -TWO
++RESOLVED
EOF
test_expect_success 'diff -c respects textconv attribute' '
	but show --format=%s -c >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
resolved

diff --cc text
index $three,$two..$res
--- a/text
+++ b/text
@@@ -1,1 -1,1 +1,1 @@@
- THREE
 -TWO
++RESOLVED
EOF
test_expect_success 'diff --cc respects textconv attribute' '
	but show --format=%s --cc >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
diff --combined text
index $three,$two..$res
--- a/text
+++ b/text
@@@ -1,1 -1,1 +1,1 @@@
- three
 -two
++resolved
EOF
test_expect_success 'diff-tree plumbing does not respect textconv' '
	but diff-tree HEAD -c -p >full &&
	tail -n +2 full >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
diff --cc text
index $three,$two..0000000
--- a/text
+++ b/text
@@@ -1,1 -1,1 +1,5 @@@
++<<<<<<< HEAD
 +THREE
++=======
+ TWO
++>>>>>>> MAIN
EOF
test_expect_success 'diff --cc respects textconv on worktree file' '
	but reset --hard HEAD^ &&
	test_must_fail but merge main &&
	but diff >actual &&
	test_cmp expect actual
'

test_done
