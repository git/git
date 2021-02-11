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

test_done
