#!/bin/sh

test_description='merge with sparse files'

. ./test-lib.sh

# test_file $filename $content
test_file () {
	echo "$2" > "$1" &&
	git add "$1"
}

# test_commit_this $message_and_tag
test_commit_this () {
	git commit -m "$1" &&
	git tag "$1"
}

test_expect_success 'setup' '
	test_file checked-out init &&
	test_file modify_delete modify_delete_init &&
	test_commit_this init &&
	test_file modify_delete modify_delete_theirs &&
	test_commit_this theirs &&
	git reset --hard init &&
	git rm modify_delete &&
	test_commit_this ours &&
	git config core.sparseCheckout true &&
	echo "/checked-out" >.git/info/sparse-checkout &&
	git reset --hard &&
	test_must_fail git merge theirs
'

test_expect_success 'reset --hard works after the conflict' '
	git reset --hard
'

test_expect_success 'is reset properly' '
	git status --porcelain -- modify_delete >out &&
	test_must_be_empty out &&
	test_path_is_missing modify_delete
'

test_expect_success 'setup: conflict back' '
	test_must_fail git merge theirs
'

test_expect_success 'Merge abort works after the conflict' '
	git merge --abort
'

test_expect_success 'is aborted properly' '
	git status --porcelain -- modify_delete >out &&
	test_must_be_empty out &&
	test_path_is_missing modify_delete
'

test_done
