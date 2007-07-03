#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git apply in reverse

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
	git tag second &&

	git diff --binary initial second >patch

'

test_expect_success 'apply in forward' '

	T0=`git rev-parse "second^{tree}"` &&
	git reset --hard initial &&
	git apply --index --binary patch &&
	T1=`git write-tree` &&
	test "$T0" = "$T1"
'

test_expect_success 'apply in reverse' '

	git reset --hard second &&
	git apply --reverse --binary --index patch &&
	git diff >diff &&
	git diff /dev/null diff

'

test_expect_success 'setup separate repository lacking postimage' '

	git tar-tree initial initial | tar xf - &&
	(
		cd initial && git init && git add .
	) &&

	git tar-tree second second | tar xf - &&
	(
		cd second && git init && git add .
	)

'

test_expect_success 'apply in forward without postimage' '

	T0=`git rev-parse "second^{tree}"` &&
	(
		cd initial &&
		git apply --index --binary ../patch &&
		T1=`git write-tree` &&
		test "$T0" = "$T1"
	)
'

test_expect_success 'apply in reverse without postimage' '

	T0=`git rev-parse "initial^{tree}"` &&
	(
		cd second &&
		git apply --index --binary --reverse ../patch &&
		T1=`git write-tree` &&
		test "$T0" = "$T1"
	)
'

test_done
