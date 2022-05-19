#!/bin/sh

test_description='Merge-recursive ours and theirs variants'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	test_write_lines 1 2 3 4 5 6 7 8 9 >file &&
	but add file &&
	cp file elif &&
	but cummit -m initial &&

	sed -e "s/1/one/" -e "s/9/nine/" >file <elif &&
	but cummit -a -m ours &&

	but checkout -b side HEAD^ &&

	sed -e "s/9/nueve/" >file <elif &&
	but cummit -a -m theirs &&

	but checkout main^0
'

test_expect_success 'plain recursive - should conflict' '
	but reset --hard main &&
	test_must_fail but merge -s recursive side &&
	grep nine file &&
	grep nueve file &&
	! grep 9 file &&
	grep one file &&
	! grep 1 file
'

test_expect_success 'recursive favouring theirs' '
	but reset --hard main &&
	but merge -s recursive -Xtheirs side &&
	! grep nine file &&
	grep nueve file &&
	! grep 9 file &&
	grep one file &&
	! grep 1 file
'

test_expect_success 'recursive favouring ours' '
	but reset --hard main &&
	but merge -s recursive -X ours side &&
	grep nine file &&
	! grep nueve file &&
	! grep 9 file &&
	grep one file &&
	! grep 1 file
'

test_expect_success 'binary file with -Xours/-Xtheirs' '
	echo file binary >.butattributes &&

	but reset --hard main &&
	but merge -s recursive -X theirs side &&
	but diff --exit-code side HEAD -- file &&

	but reset --hard main &&
	but merge -s recursive -X ours side &&
	but diff --exit-code main HEAD -- file
'

test_expect_success 'pull passes -X to underlying merge' '
	but reset --hard main && but pull --no-rebase -s recursive -Xours . side &&
	but reset --hard main && but pull --no-rebase -s recursive -X ours . side &&
	but reset --hard main && but pull --no-rebase -s recursive -Xtheirs . side &&
	but reset --hard main && but pull --no-rebase -s recursive -X theirs . side &&
	but reset --hard main && test_must_fail but pull --no-rebase -s recursive -X bork . side
'

test_expect_success SYMLINKS 'symlink with -Xours/-Xtheirs' '
	but reset --hard main &&
	but checkout -b two main &&
	ln -s target-zero link &&
	but add link &&
	but cummit -m "add link pointing to zero" &&

	ln -f -s target-two link &&
	but cummit -m "add link pointing to two" link &&

	but checkout -b one HEAD^ &&
	ln -f -s target-one link &&
	but cummit -m "add link pointing to one" link &&

	# we expect symbolic links not to resolve automatically, of course
	but checkout one^0 &&
	test_must_fail but merge -s recursive two &&

	# favor theirs to resolve to target-two?
	but reset --hard &&
	but checkout one^0 &&
	but merge -s recursive -X theirs two &&
	but diff --exit-code two HEAD link &&

	# favor ours to resolve to target-one?
	but reset --hard &&
	but checkout one^0 &&
	but merge -s recursive -X ours two &&
	but diff --exit-code one HEAD link

'

test_done
