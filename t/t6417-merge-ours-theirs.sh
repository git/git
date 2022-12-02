#!/bin/sh

test_description='Merge-recursive ours and theirs variants'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	test_write_lines 1 2 3 4 5 6 7 8 9 >file &&
	git add file &&
	cp file elif &&
	git commit -m initial &&

	sed -e "s/1/one/" -e "s/9/nine/" >file <elif &&
	git commit -a -m ours &&

	git checkout -b side HEAD^ &&

	sed -e "s/9/nueve/" >file <elif &&
	git commit -a -m theirs &&

	git checkout main^0
'

test_expect_success 'plain recursive - should conflict' '
	git reset --hard main &&
	test_must_fail git merge -s recursive side &&
	grep nine file &&
	grep nueve file &&
	! grep 9 file &&
	grep one file &&
	! grep 1 file
'

test_expect_success 'recursive favouring theirs' '
	git reset --hard main &&
	git merge -s recursive -Xtheirs side &&
	! grep nine file &&
	grep nueve file &&
	! grep 9 file &&
	grep one file &&
	! grep 1 file
'

test_expect_success 'recursive favouring ours' '
	git reset --hard main &&
	git merge -s recursive -X ours side &&
	grep nine file &&
	! grep nueve file &&
	! grep 9 file &&
	grep one file &&
	! grep 1 file
'

test_expect_success 'binary file with -Xours/-Xtheirs' '
	echo file binary >.gitattributes &&

	git reset --hard main &&
	git merge -s recursive -X theirs side &&
	git diff --exit-code side HEAD -- file &&

	git reset --hard main &&
	git merge -s recursive -X ours side &&
	git diff --exit-code main HEAD -- file
'

test_expect_success 'pull passes -X to underlying merge' '
	git reset --hard main && git pull --no-rebase -s recursive -Xours . side &&
	git reset --hard main && git pull --no-rebase -s recursive -X ours . side &&
	git reset --hard main && git pull --no-rebase -s recursive -Xtheirs . side &&
	git reset --hard main && git pull --no-rebase -s recursive -X theirs . side &&
	git reset --hard main && test_must_fail git pull --no-rebase -s recursive -X bork . side
'

test_expect_success SYMLINKS 'symlink with -Xours/-Xtheirs' '
	git reset --hard main &&
	git checkout -b two main &&
	ln -s target-zero link &&
	git add link &&
	git commit -m "add link pointing to zero" &&

	ln -f -s target-two link &&
	git commit -m "add link pointing to two" link &&

	git checkout -b one HEAD^ &&
	ln -f -s target-one link &&
	git commit -m "add link pointing to one" link &&

	# we expect symbolic links not to resolve automatically, of course
	git checkout one^0 &&
	test_must_fail git merge -s recursive two &&

	# favor theirs to resolve to target-two?
	git reset --hard &&
	git checkout one^0 &&
	git merge -s recursive -X theirs two &&
	git diff --exit-code two HEAD link &&

	# favor ours to resolve to target-one?
	git reset --hard &&
	git checkout one^0 &&
	git merge -s recursive -X ours two &&
	git diff --exit-code one HEAD link

'

test_done
