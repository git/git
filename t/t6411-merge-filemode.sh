#!/bin/sh

test_description='merge: handle file mode'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'set up mode change in one branch' '
	: >file1 &&
	but add file1 &&
	but cummit -m initial &&
	but checkout -b a1 main &&
	: >dummy &&
	but add dummy &&
	but cummit -m a &&
	but checkout -b b1 main &&
	test_chmod +x file1 &&
	but add file1 &&
	but cummit -m b1
'

do_one_mode () {
	strategy=$1
	us=$2
	them=$3
	test_expect_success "resolve single mode change ($strategy, $us)" '
		but checkout -f $us &&
		but merge -s $strategy $them &&
		but ls-files -s file1 | grep ^100755
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
	but reset --hard HEAD &&
	but checkout -b a2 main &&
	: >file2 &&
	H=$(but hash-object file2) &&
	test_chmod +x file2 &&
	but cummit -m a2 &&
	but checkout -b b2 main &&
	: >file2 &&
	but add file2 &&
	but cummit -m b2 &&
	cat >expect <<-EOF
	100755 $H 2	file2
	100644 $H 3	file2
	EOF
'

do_both_modes () {
	strategy=$1
	test_expect_success "detect conflict on double mode change ($strategy)" '
		but reset --hard &&
		but checkout -f a2 &&
		test_must_fail but merge -s $strategy b2 &&
		but ls-files -u >actual &&
		test_cmp expect actual &&
		but ls-files -s file2 | grep ^100755
	'

	test_expect_success FILEMODE "verify executable bit on file ($strategy)" '
		test -x file2
	'
}

# both sides are equivalent, so no need to run both ways
do_both_modes recursive
do_both_modes resolve

test_expect_success 'set up delete/modechange scenario' '
	but reset --hard &&
	but checkout -b deletion main &&
	but rm file1 &&
	but cummit -m deletion
'

do_delete_modechange () {
	strategy=$1
	us=$2
	them=$3
	test_expect_success "detect delete/modechange conflict ($strategy, $us)" '
		but reset --hard &&
		but checkout $us &&
		test_must_fail but merge -s $strategy $them
	'
}

do_delete_modechange recursive b1 deletion
do_delete_modechange recursive deletion b1
do_delete_modechange resolve b1 deletion
do_delete_modechange resolve deletion b1

test_done
