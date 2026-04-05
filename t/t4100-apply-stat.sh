#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git apply --stat --summary test, with --recount

'

. ./test-lib.sh

UNC='s/^\(@@ -[1-9][0-9]*\),[0-9]* \(+[1-9][0-9]*\),[0-9]* @@/\1,999 \2,999 @@/'

num=0
while read title
do
	num=$(( $num + 1 ))
	test_expect_success "$title" '
		git apply --stat --summary \
			<"$TEST_DIRECTORY/t4100/t-apply-$num.patch" >current &&
		test_cmp "$TEST_DIRECTORY"/t4100/t-apply-$num.expect current
	'

	test_expect_success "$title with recount" '
		sed -e "$UNC" <"$TEST_DIRECTORY/t4100/t-apply-$num.patch" |
		git apply --recount --stat --summary >current &&
		test_cmp "$TEST_DIRECTORY"/t4100/t-apply-$num.expect current
	'
done <<\EOF
rename
copy
rewrite
mode
non git (1)
non git (2)
non git (3)
incomplete (1)
incomplete (2)
EOF

test_expect_success 'applying a hunk header which overflows fails' '
	cat >patch <<-\EOF &&
	diff -u a/file b/file
	--- a/file
	+++ b/file
	@@ -98765432109876543210 +98765432109876543210 @@
	-a
	+b
	EOF
	test_must_fail git apply patch 2>err &&
	echo "error: corrupt patch at patch:4" >expect &&
	test_cmp expect err
'

test_expect_success 'applying a hunk header which overflows from stdin fails' '
	cat >patch <<-\EOF &&
	diff -u a/file b/file
	--- a/file
	+++ b/file
	@@ -98765432109876543210 +98765432109876543210 @@
	-a
	+b
	EOF
	test_must_fail git apply <patch 2>err &&
	echo "error: corrupt patch at <stdin>:4" >expect &&
	test_cmp expect err
'

test_expect_success 'applying multiple patches reports the corrupted input' '
	cat >good.patch <<-\EOF &&
	diff -u a/file b/file
	--- a/file
	+++ b/file
	@@ -1 +1 @@
	-a
	+b
	EOF
	cat >bad.patch <<-\EOF &&
	diff -u a/file b/file
	--- a/file
	+++ b/file
	@@ -98765432109876543210 +98765432109876543210 @@
	-a
	+b
	EOF
	test_must_fail git apply --stat --summary good.patch bad.patch 2>err &&
	echo "error: corrupt patch at bad.patch:4" >expect &&
	test_cmp expect err
'

test_expect_success 'applying a patch without a header reports the input' '
	cat >fragment.patch <<-\EOF &&
	@@ -1 +1 @@
	-a
	+b
	EOF
	test_must_fail git apply fragment.patch 2>err &&
	echo "error: patch fragment without header at fragment.patch:1: @@ -1 +1 @@" >expect &&
	test_cmp expect err
'

test_expect_success 'applying a patch with a missing filename reports the input' '
	cat >missing.patch <<-\EOF &&
	diff --git a/f b/f
	index 7898192..6178079 100644
	--- a/f
	@@ -1 +1 @@
	-a
	+b
	EOF
	test_must_fail git apply missing.patch 2>err &&
	echo "error: git diff header lacks filename information at missing.patch:4" >expect &&
	test_cmp expect err
'

test_expect_success 'applying a patch with an invalid mode reports the input' '
	cat >mode.patch <<-\EOF &&
	diff --git a/f b/f
	old mode 10x644
	EOF
	test_must_fail git apply mode.patch 2>err &&
	cat >expect <<-\EOF &&
	error: invalid mode at mode.patch:2: 10x644

	EOF
	test_cmp expect err
'

test_expect_success 'applying a patch with only garbage reports the input' '
	cat >garbage.patch <<-\EOF &&
	diff --git a/f b/f
	--- a/f
	+++ b/f
	this is garbage
	EOF
	test_must_fail git apply garbage.patch 2>err &&
	echo "error: patch with only garbage at garbage.patch:4" >expect &&
	test_cmp expect err
'
test_done
