#!/bin/sh

test_description='subtree merge strategy'

. ./test-lib.sh

test_expect_success setup '

	s="1 2 3 4 5 6 7 8"
	for i in $s; do echo $i; done >hello &&
	git add hello &&
	git commit -m initial &&
	git checkout -b side &&
	echo >>hello world &&
	git add hello &&
	git commit -m second &&
	git checkout master &&
	for i in mundo $s; do echo $i; done >hello &&
	git add hello &&
	git commit -m master

'

test_expect_success 'subtree available and works like recursive' '

	git merge -s subtree side &&
	for i in mundo $s world; do echo $i; done >expect &&
	diff -u expect hello

'

test_done
