#!/bin/sh

test_description='Merge-recursive ours and theirs variants'
. ./test-lib.sh

test_expect_success setup '
	for i in 1 2 3 4 5 6 7 8 9
	do
		echo "$i"
	done >file &&
	git add file &&
	cp file elif &&
	git commit -m initial &&

	sed -e "s/1/one/" -e "s/9/nine/" >file <elif &&
	git commit -a -m ours &&

	git checkout -b side HEAD^ &&

	sed -e "s/9/nueve/" >file <elif &&
	git commit -a -m theirs &&

	git checkout master^0
'

test_expect_success 'plain recursive - should conflict' '
	git reset --hard master &&
	test_must_fail git merge -s recursive side &&
	grep nine file &&
	grep nueve file &&
	! grep 9 file &&
	grep one file &&
	! grep 1 file
'

test_expect_success 'recursive favouring theirs' '
	git reset --hard master &&
	git merge -s recursive -Xtheirs side &&
	! grep nine file &&
	grep nueve file &&
	! grep 9 file &&
	grep one file &&
	! grep 1 file
'

test_expect_success 'recursive favouring ours' '
	git reset --hard master &&
	git merge -s recursive -X ours side &&
	grep nine file &&
	! grep nueve file &&
	! grep 9 file &&
	grep one file &&
	! grep 1 file
'

test_expect_success 'binary file with -Xours/-Xtheirs' '
	echo file binary >.gitattributes &&

	git reset --hard master &&
	git merge -s recursive -X theirs side &&
	git diff --exit-code side HEAD -- file &&

	git reset --hard master &&
	git merge -s recursive -X ours side &&
	git diff --exit-code master HEAD -- file
'

test_expect_success 'pull passes -X to underlying merge' '
	git reset --hard master && git pull -s recursive -Xours . side &&
	git reset --hard master && git pull -s recursive -X ours . side &&
	git reset --hard master && git pull -s recursive -Xtheirs . side &&
	git reset --hard master && git pull -s recursive -X theirs . side &&
	git reset --hard master && test_must_fail git pull -s recursive -X bork . side
'

test_done
