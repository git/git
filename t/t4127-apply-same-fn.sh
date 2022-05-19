#!/bin/sh

test_description='apply same filename'


TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

modify () {
	sed -e "$1" < "$2" > "$2".x &&
	mv "$2".x "$2"
}

test_expect_success setup '
	test_write_lines a b c d e f g h i j k l m >same_fn &&
	cp same_fn other_fn &&
	but add same_fn other_fn &&
	but cummit -m initial
'
test_expect_success 'apply same filename with independent changes' '
	modify "s/^d/z/" same_fn &&
	but diff > patch0 &&
	but add same_fn &&
	modify "s/^i/y/" same_fn &&
	but diff >> patch0 &&
	cp same_fn same_fn2 &&
	but reset --hard &&
	but apply patch0 &&
	test_cmp same_fn same_fn2
'

test_expect_success 'apply same filename with overlapping changes' '
	but reset --hard &&

	# Store same_fn so that we can check apply -R in next test
	cp same_fn same_fn1 &&

	modify "s/^d/z/" same_fn &&
	but diff > patch0 &&
	but add same_fn &&
	modify "s/^e/y/" same_fn &&
	but diff >> patch0 &&
	cp same_fn same_fn2 &&
	but reset --hard &&
	but apply patch0 &&
	test_cmp same_fn same_fn2
'

test_expect_success 'apply same filename with overlapping changes, in reverse' '
	but apply -R patch0 &&
	test_cmp same_fn same_fn1
'

test_expect_success 'apply same new filename after rename' '
	but reset --hard &&
	but mv same_fn new_fn &&
	modify "s/^d/z/" new_fn &&
	but add new_fn &&
	but diff -M --cached > patch1 &&
	modify "s/^e/y/" new_fn &&
	but diff >> patch1 &&
	cp new_fn new_fn2 &&
	but reset --hard &&
	but apply --index patch1 &&
	test_cmp new_fn new_fn2
'

test_expect_success 'apply same old filename after rename -- should fail.' '
	but reset --hard &&
	but mv same_fn new_fn &&
	modify "s/^d/z/" new_fn &&
	but add new_fn &&
	but diff -M --cached > patch1 &&
	but mv new_fn same_fn &&
	modify "s/^e/y/" same_fn &&
	but diff >> patch1 &&
	but reset --hard &&
	test_must_fail but apply patch1
'

test_expect_success 'apply A->B (rename), C->A (rename), A->A -- should pass.' '
	but reset --hard &&
	but mv same_fn new_fn &&
	modify "s/^d/z/" new_fn &&
	but add new_fn &&
	but diff -M --cached > patch1 &&
	but cummit -m "a rename" &&
	but mv other_fn same_fn &&
	modify "s/^e/y/" same_fn &&
	but add same_fn &&
	but diff -M --cached >> patch1 &&
	modify "s/^g/x/" same_fn &&
	but diff >> patch1 &&
	but reset --hard HEAD^ &&
	but apply patch1
'

test_done
