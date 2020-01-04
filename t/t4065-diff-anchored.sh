#!/bin/sh

test_description='anchored diff algorithm'

. ./test-lib.sh

test_expect_success '--anchored' '
	printf "a\nb\nc\n" >pre &&
	printf "c\na\nb\n" >post &&

	# normally, c is moved to produce the smallest diff
	test_expect_code 1 git diff --no-index pre post >diff &&
	grep "^+c" diff &&

	# with anchor, a is moved
	test_expect_code 1 git diff --no-index --anchored=c pre post >diff &&
	grep "^+a" diff
'

test_expect_success '--anchored multiple' '
	printf "a\nb\nc\nd\ne\nf\n" >pre &&
	printf "c\na\nb\nf\nd\ne\n" >post &&

	# with 1 anchor, c is not moved, but f is moved
	test_expect_code 1 git diff --no-index --anchored=c pre post >diff &&
	grep "^+a" diff && # a is moved instead of c
	grep "^+f" diff &&

	# with 2 anchors, c and f are not moved
	test_expect_code 1 git diff --no-index --anchored=c --anchored=f pre post >diff &&
	grep "^+a" diff &&
	grep "^+d" diff # d is moved instead of f
'

test_expect_success '--anchored with nonexistent line has no effect' '
	printf "a\nb\nc\n" >pre &&
	printf "c\na\nb\n" >post &&

	test_expect_code 1 git diff --no-index --anchored=x pre post >diff &&
	grep "^+c" diff
'

test_expect_success '--anchored with non-unique line has no effect' '
	printf "a\nb\nc\nd\ne\nc\n" >pre &&
	printf "c\na\nb\nc\nd\ne\n" >post &&

	test_expect_code 1 git diff --no-index --anchored=c pre post >diff &&
	grep "^+c" diff
'

test_expect_success 'diff still produced with impossible multiple --anchored' '
	printf "a\nb\nc\n" >pre &&
	printf "c\na\nb\n" >post &&

	test_expect_code 1 git diff --no-index --anchored=a --anchored=c pre post >diff &&
	mv post expected_post &&

	# Ensure that the diff is correct by applying it and then
	# comparing the result with the original
	git apply diff &&
	diff expected_post post
'

test_expect_success 'later algorithm arguments override earlier ones' '
	printf "a\nb\nc\n" >pre &&
	printf "c\na\nb\n" >post &&

	test_expect_code 1 git diff --no-index --patience --anchored=c pre post >diff &&
	grep "^+a" diff &&

	test_expect_code 1 git diff --no-index --anchored=c --patience pre post >diff &&
	grep "^+c" diff &&

	test_expect_code 1 git diff --no-index --histogram --anchored=c pre post >diff &&
	grep "^+a" diff &&

	test_expect_code 1 git diff --no-index --anchored=c --histogram pre post >diff &&
	grep "^+c" diff
'

test_expect_success '--anchored works with other commands like "git show"' '
	printf "a\nb\nc\n" >file &&
	git add file &&
	git commit -m foo &&
	printf "c\na\nb\n" >file &&
	git add file &&
	git commit -m foo &&

	# with anchor, a is moved
	git show --patience --anchored=c >diff &&
	grep "^+a" diff
'

test_done
