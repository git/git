#!/bin/sh

test_description='merge: handle file mode'
. ./test-lib.sh

test_expect_success 'set up mode change in one branch' '
	: >file1 &&
	git add file1 &&
	git commit -m initial &&
	git checkout -b a1 master &&
	: >dummy &&
	git add dummy &&
	git commit -m a &&
	git checkout -b b1 master &&
	test_chmod +x file1 &&
	git add file1 &&
	git commit -m b1
'

do_one_mode () {
	strategy=$1
	us=$2
	them=$3
	test_expect_success "resolve single mode change ($strategy, $us)" '
		git checkout -f $us &&
		git merge -s $strategy $them &&
		git ls-files -s file1 | grep ^100755
	'

	test_expect_success FILEMODE "verify executable bit on file ($strategy, $us)" '
		test -x file1
	'
}

do_one_mode recursive a1 b1
do_one_mode recursive b1 a1
do_one_mode resolve a1 b1
do_one_mode resolve b1 a1

test_expect_success 'set up mode change in both branches' '
	git reset --hard HEAD &&
	git checkout -b a2 master &&
	: >file2 &&
	H=$(git hash-object file2) &&
	test_chmod +x file2 &&
	git commit -m a2 &&
	git checkout -b b2 master &&
	: >file2 &&
	git add file2 &&
	git commit -m b2 &&
	{
		echo "100755 $H 2	file2"
		echo "100644 $H 3	file2"
	} >expect
'

do_both_modes () {
	strategy=$1
	test_expect_success "detect conflict on double mode change ($strategy)" '
		git reset --hard &&
		git checkout -f a2 &&
		test_must_fail git merge -s $strategy b2 &&
		git ls-files -u >actual &&
		test_cmp actual expect &&
		git ls-files -s file2 | grep ^100755
	'

	test_expect_success FILEMODE "verify executable bit on file ($strategy)" '
		test -x file2
	'
}

# both sides are equivalent, so no need to run both ways
do_both_modes recursive
do_both_modes resolve

test_expect_success 'set up delete/modechange scenario' '
	git reset --hard &&
	git checkout -b deletion master &&
	git rm file1 &&
	git commit -m deletion
'

do_delete_modechange () {
	strategy=$1
	us=$2
	them=$3
	test_expect_success "detect delete/modechange conflict ($strategy, $us)" '
		git reset --hard &&
		git checkout $us &&
		test_must_fail git merge -s $strategy $them
	'
}

do_delete_modechange recursive b1 deletion
do_delete_modechange recursive deletion b1
do_delete_modechange resolve b1 deletion
do_delete_modechange resolve deletion b1

test_done
