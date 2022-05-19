#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='but apply in reverse

'


TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '

	test_write_lines a b c d e f g h i j k l m n >file1 &&
	perl -pe "y/ijk/\\000\\001\\002/" <file1 >file2 &&

	but add file1 file2 &&
	but cummit -m initial &&
	but tag initial &&

	test_write_lines a b c g h i J K L m o n p q >file1 &&
	perl -pe "y/mon/\\000\\001\\002/" <file1 >file2 &&

	but cummit -a -m second &&
	but tag second &&

	but diff --binary initial second >patch

'

test_expect_success 'apply in forward' '

	T0=$(but rev-parse "second^{tree}") &&
	but reset --hard initial &&
	but apply --index --binary patch &&
	T1=$(but write-tree) &&
	test "$T0" = "$T1"
'

test_expect_success 'apply in reverse' '

	but reset --hard second &&
	but apply --reverse --binary --index patch &&
	but diff >diff &&
	test_must_be_empty diff

'

test_expect_success 'setup separate repository lacking postimage' '

	but archive --format=tar --prefix=initial/ initial | $TAR xf - &&
	(
		cd initial && but init && but add .
	) &&

	but archive --format=tar --prefix=second/ second | $TAR xf - &&
	(
		cd second && but init && but add .
	)

'

test_expect_success 'apply in forward without postimage' '

	T0=$(but rev-parse "second^{tree}") &&
	(
		cd initial &&
		but apply --index --binary ../patch &&
		T1=$(but write-tree) &&
		test "$T0" = "$T1"
	)
'

test_expect_success 'apply in reverse without postimage' '

	T0=$(but rev-parse "initial^{tree}") &&
	(
		cd second &&
		but apply --index --binary --reverse ../patch &&
		T1=$(but write-tree) &&
		test "$T0" = "$T1"
	)
'

test_expect_success 'reversing a whitespace introduction' '
	sed "s/a/a /" < file1 > file1.new &&
	mv file1.new file1 &&
	but diff | but apply --reverse --whitespace=error
'

test_done
