#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git-apply in reverse

'

. ./test-lib.sh

test_expect_success setup '

	for i in a b c d e f g h i j k l m n; do echo $i; done >file1 &&
	tr "[ijk]" '\''[\0\1\2]'\'' <file1 >file2 &&

	git add file1 file2 &&
	git commit -m initial &&
	git tag initial &&

	for i in a b c g h i J K L m o n p q; do echo $i; done >file1 &&
	tr "[mon]" '\''[\0\1\2]'\'' <file1 >file2 &&

	git commit -a -m second &&

	git diff --binary -R initial >patch

'

test_expect_success 'apply in forward' '

	git apply --index --binary patch &&
	git diff initial >diff &&
	diff -u /dev/null diff

'

test_expect_success 'apply in reverse' '

	git apply --reverse --binary --index patch &&
	git diff >diff &&
	diff -u /dev/null diff

'

test_done
