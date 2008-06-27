#!/bin/sh

test_description='apply same filename'

. ./test-lib.sh

test_expect_success setup '
	for i in a b c d e f g h i j k l m
	do
		echo $i
	done >same_fn &&
	cp same_fn other_fn &&
	git add same_fn other_fn &&
	git commit -m initial
'
test_expect_success 'apply same filename with independent changes' '
	sed -i -e "s/^d/z/" same_fn &&
	git diff > patch0 &&
	git add same_fn &&
	sed -i -e "s/^i/y/" same_fn &&
	git diff >> patch0 &&
	cp same_fn same_fn2 &&
	git reset --hard &&
	git-apply patch0 &&
	diff same_fn same_fn2
'

test_expect_success 'apply same filename with overlapping changes' '
	git reset --hard
	sed -i -e "s/^d/z/" same_fn &&
	git diff > patch0 &&
	git add same_fn &&
	sed -i -e "s/^e/y/" same_fn &&
	git diff >> patch0 &&
	cp same_fn same_fn2 &&
	git reset --hard &&
	git-apply patch0 &&
	diff same_fn same_fn2
'

test_expect_success 'apply same new filename after rename' '
	git reset --hard
	git mv same_fn new_fn
	sed -i -e "s/^d/z/" new_fn &&
	git add new_fn &&
	git diff -M --cached > patch1 &&
	sed -i -e "s/^e/y/" new_fn &&
	git diff >> patch1 &&
	cp new_fn new_fn2 &&
	git reset --hard &&
	git apply --index patch1 &&
	diff new_fn new_fn2
'

test_expect_success 'apply same old filename after rename -- should fail.' '
	git reset --hard
	git mv same_fn new_fn
	sed -i -e "s/^d/z/" new_fn &&
	git add new_fn &&
	git diff -M --cached > patch1 &&
	git mv new_fn same_fn
	sed -i -e "s/^e/y/" same_fn &&
	git diff >> patch1 &&
	git reset --hard &&
	test_must_fail git apply patch1
'

test_expect_success 'apply A->B (rename), C->A (rename), A->A -- should pass.' '
	git reset --hard
	git mv same_fn new_fn
	sed -i -e "s/^d/z/" new_fn &&
	git add new_fn &&
	git diff -M --cached > patch1 &&
	git commit -m "a rename" &&
	git mv other_fn same_fn
	sed -i -e "s/^e/y/" same_fn &&
	git add same_fn &&
	git diff -M --cached >> patch1 &&
	sed -i -e "s/^g/x/" same_fn &&
	git diff >> patch1 &&
	git reset --hard HEAD^ &&
	git apply patch1
'

test_done
