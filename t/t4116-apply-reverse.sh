#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git apply in reverse

'


. ./test-lib.sh

test_expect_success setup '

	test_write_lines a b c d e f g h i j k l m n >file1 &&
	perl -pe "y/ijk/\\000\\001\\002/" <file1 >file2 &&

	git add file1 file2 &&
	git commit -m initial &&
	git tag initial &&

	test_write_lines a b c g h i J K L m o n p q >file1 &&
	perl -pe "y/mon/\\000\\001\\002/" <file1 >file2 &&

	git commit -a -m second &&
	git tag second &&

	git diff --binary initial second >patch

'

test_expect_success 'apply in forward' '

	T0=$(git rev-parse "second^{tree}") &&
	git reset --hard initial &&
	git apply --index --binary patch &&
	T1=$(git write-tree) &&
	test "$T0" = "$T1"
'

test_expect_success 'apply in reverse' '

	git reset --hard second &&
	git apply --reverse --binary --index patch &&
	git diff >diff &&
	test_must_be_empty diff

'

test_expect_success 'setup separate repository lacking postimage' '

	git archive --format=tar --prefix=initial/ initial | $TAR xf - &&
	(
		cd initial && git init && git add .
	) &&

	git archive --format=tar --prefix=second/ second | $TAR xf - &&
	(
		cd second && git init && git add .
	)

'

test_expect_success 'apply in forward without postimage' '

	T0=$(git rev-parse "second^{tree}") &&
	(
		cd initial &&
		git apply --index --binary ../patch &&
		T1=$(git write-tree) &&
		test "$T0" = "$T1"
	)
'

test_expect_success 'apply in reverse without postimage' '

	T0=$(git rev-parse "initial^{tree}") &&
	(
		cd second &&
		git apply --index --binary --reverse ../patch &&
		T1=$(git write-tree) &&
		test "$T0" = "$T1"
	)
'

test_expect_success 'reversing a whitespace introduction' '
	sed "s/a/a /" < file1 > file1.new &&
	mv file1.new file1 &&
	git diff | git apply --reverse --whitespace=error
'

test_done
