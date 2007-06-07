#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='git-apply with new style GNU diff with empty context

'

. ./test-lib.sh

test_expect_success setup '
	{
		echo; echo;
		echo A; echo B; echo C;
		echo;
	} >file1 &&
	cat file1 >file1.orig &&
	{
		cat file1 &&
		echo Q | tr -d "\\012"
	} >file2 &&
	cat file2 >file2.orig
	git add file1 file2 &&
	sed -e "/^B/d" <file1.orig >file1 &&
	sed -e "/^[BQ]/d" <file2.orig >file2 &&
	echo Q | tr -d "\\012" >>file2 &&
	cat file1 >file1.mods &&
	cat file2 >file2.mods &&
	git diff |
	sed -e "s/^ \$//" >diff.output
'

test_expect_success 'apply --numstat' '

	git apply --numstat diff.output >actual &&
	{
		echo "0	1	file1" &&
		echo "0	1	file2"
	} >expect &&
	git diff expect actual

'

test_expect_success 'apply --apply' '

	cat file1.orig >file1 &&
	cat file2.orig >file2 &&
	git update-index file1 file2 &&
	git apply --index diff.output &&
	git diff file1.mods file1 &&
	git diff file2.mods file2
'

test_done
