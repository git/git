#!/bin/sh

test_description='auto squash'

. ./test-lib.sh

test_expect_success setup '
	echo 0 >file0 &&
	git add . &&
	test_tick &&
	git commit -m "initial commit" &&
	echo 0 >file1 &&
	echo 2 >file2 &&
	git add . &&
	test_tick &&
	git commit -m "first commit" &&
	echo 3 >file3 &&
	git add . &&
	test_tick &&
	git commit -m "second commit" &&
	git tag base
'

test_expect_success 'auto fixup' '
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git commit -m "fixup! first"

	git tag final-fixup &&
	test_tick &&
	git rebase --autosquash -i HEAD^^^ &&
	git log --oneline >actual &&
	test 3 = $(wc -l <actual) &&
	git diff --exit-code final-fixup &&
	test 1 = "$(git cat-file blob HEAD^:file1)" &&
	test 1 = $(git cat-file commit HEAD^ | grep first | wc -l)
'

test_expect_success 'auto squash' '
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git commit -m "squash! first"

	git tag final-squash &&
	test_tick &&
	git rebase --autosquash -i HEAD^^^ &&
	git log --oneline >actual &&
	test 3 = $(wc -l <actual) &&
	git diff --exit-code final-squash &&
	test 1 = "$(git cat-file blob HEAD^:file1)" &&
	test 2 = $(git cat-file commit HEAD^ | grep first | wc -l)
'

test_expect_success 'misspelled auto squash' '
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git commit -m "squash! forst"
	git tag final-missquash &&
	test_tick &&
	git rebase --autosquash -i HEAD^^^ &&
	git log --oneline >actual &&
	test 4 = $(wc -l <actual) &&
	git diff --exit-code final-missquash &&
	test 0 = $(git rev-list final-missquash...HEAD | wc -l)
'

test_done
