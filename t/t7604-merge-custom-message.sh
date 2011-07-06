#!/bin/sh

test_description='git merge

Testing merge when using a custom message for the merge commit.'

. ./test-lib.sh

test_expect_success 'setup' '
	echo c0 > c0.c &&
	git add c0.c &&
	git commit -m c0 &&
	git tag c0 &&
	echo c1 > c1.c &&
	git add c1.c &&
	git commit -m c1 &&
	git tag c1 &&
	git reset --hard c0 &&
	echo c2 > c2.c &&
	git add c2.c &&
	git commit -m c2 &&
	git tag c2
'


test_expect_success 'merge c2 with a custom message' '
	git reset --hard c1 &&
	echo >expected "custom message" &&
	git merge -m "custom message" c2 &&
	git cat-file commit HEAD | sed -e "1,/^$/d" >actual &&
	test_cmp expected actual
'

test_done
