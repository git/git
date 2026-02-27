#!/bin/sh

test_description='shifted diff groups re-diffing during histogram diff'

. ./test-lib.sh

test_expect_success 'shifted diff group should re-diff to minimize patch' '
	test_write_lines A x A A A x A A A >file1 &&
	test_write_lines A x A Z A x A A A >file2 &&

	file1_h=$(git rev-parse --short $(git hash-object file1)) &&
	file2_h=$(git rev-parse --short $(git hash-object file2)) &&

	cat >expect <<-EOF &&
	diff --git a/file1 b/file2
	index $file1_h..$file2_h 100644
	--- a/file1
	+++ b/file2
	@@ -1,7 +1,7 @@
	 A
	 x
	 A
	-A
	+Z
	 A
	 x
	 A
	EOF

	test_expect_code 1 git diff --no-index --histogram file1 file2 >output &&
	test_cmp expect output
'

test_expect_success 're-diff should preserve diff flags' '
	test_write_lines a b c a b c >file1 &&
	test_write_lines x " b" z a b c >file2 &&

	file1_h=$(git rev-parse --short $(git hash-object file1)) &&
	file2_h=$(git rev-parse --short $(git hash-object file2)) &&

	cat >expect <<-EOF &&
	diff --git a/file1 b/file2
	index $file1_h..$file2_h 100644
	--- a/file1
	+++ b/file2
	@@ -1,6 +1,6 @@
	-a
	-b
	-c
	+x
	+ b
	+z
	 a
	 b
	 c
	EOF

	test_expect_code 1 git diff --no-index --histogram file1 file2 >output &&
	test_cmp expect output &&

	cat >expect_iwhite <<-EOF &&
	diff --git a/file1 b/file2
	index $file1_h..$file2_h 100644
	--- a/file1
	+++ b/file2
	@@ -1,6 +1,6 @@
	-a
	+x
	  b
	-c
	+z
	 a
	 b
	 c
	EOF

	test_expect_code 1 git diff --no-index --histogram --ignore-all-space file1 file2 >output_iwhite &&
	test_cmp expect_iwhite output_iwhite
'

test_expect_success 'shifting on either side should trigger re-diff properly' '
	test_write_lines a b c a b c a b c >file1 &&
	test_write_lines a b c a1 a2 a3 b c1 a b c >file2 &&

	file1_h=$(git rev-parse --short $(git hash-object file1)) &&
	file2_h=$(git rev-parse --short $(git hash-object file2)) &&

	cat >expect1 <<-EOF &&
	diff --git a/file1 b/file2
	index $file1_h..$file2_h 100644
	--- a/file1
	+++ b/file2
	@@ -1,9 +1,11 @@
	 a
	 b
	 c
	-a
	+a1
	+a2
	+a3
	 b
	-c
	+c1
	 a
	 b
	 c
	EOF

	test_expect_code 1 git diff --no-index --histogram file1 file2 >output1 &&
	test_cmp expect1 output1 &&

	cat >expect2 <<-EOF &&
	diff --git a/file2 b/file1
	index $file2_h..$file1_h 100644
	--- a/file2
	+++ b/file1
	@@ -1,11 +1,9 @@
	 a
	 b
	 c
	-a1
	-a2
	-a3
	+a
	 b
	-c1
	+c
	 a
	 b
	 c
	EOF

	test_expect_code 1 git diff --no-index --histogram file2 file1 >output2 &&
	test_cmp expect2 output2
'

test_done
