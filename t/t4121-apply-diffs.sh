#!/bin/sh

test_description='git apply for contextually independent diffs'
. ./test-lib.sh

echo '1
2
3
4
5
6
7
8' >file

test_expect_success 'setup' \
	'git add file &&
	git commit -q -m 1 &&
	git checkout -b test &&
	mv file file.tmp &&
	echo 0 >file &&
	cat file.tmp >>file &&
	rm file.tmp &&
	git commit -a -q -m 2 &&
	echo 9 >>file &&
	git commit -a -q -m 3 &&
	git checkout master'

test_expect_success \
	'check if contextually independent diffs for the same file apply' \
	'( git diff test~2 test~1 && git diff test~1 test~0 )| git apply'

test_done
