#!/bin/sh

test_description='merge with sparse files'

. ./test-lib.sh

# test_file $filename $content
test_file () {
	echo "$2" > "$1" &&
	but add "$1"
}

# test_cummit_this $message_and_tag
test_cummit_this () {
	but cummit -m "$1" &&
	but tag "$1"
}

test_expect_success 'setup' '
	test_file checked-out init &&
	test_file modify_delete modify_delete_init &&
	test_cummit_this init &&
	test_file modify_delete modify_delete_theirs &&
	test_cummit_this theirs &&
	but reset --hard init &&
	but rm modify_delete &&
	test_cummit_this ours &&
	but config core.sparseCheckout true &&
	echo "/checked-out" >.but/info/sparse-checkout &&
	but reset --hard &&
	test_must_fail but merge theirs
'

test_expect_success 'reset --hard works after the conflict' '
	but reset --hard
'

test_expect_success 'is reset properly' '
	but status --porcelain -- modify_delete >out &&
	test_must_be_empty out &&
	test_path_is_missing modify_delete
'

test_expect_success 'setup: conflict back' '
	test_must_fail but merge theirs
'

test_expect_success 'Merge abort works after the conflict' '
	but merge --abort
'

test_expect_success 'is aborted properly' '
	but status --porcelain -- modify_delete >out &&
	test_must_be_empty out &&
	test_path_is_missing modify_delete
'

test_done
