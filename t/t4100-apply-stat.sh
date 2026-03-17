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
test_done
