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

test_auto_fixup() {
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git commit -m "fixup! first" &&

	git tag $1 &&
	test_tick &&
	git rebase $2 -i HEAD^^^ &&
	git log --oneline >actual &&
	test 3 = $(wc -l <actual) &&
	git diff --exit-code $1 &&
	test 1 = "$(git cat-file blob HEAD^:file1)" &&
	test 1 = $(git cat-file commit HEAD^ | grep first | wc -l)
}

test_expect_success 'auto fixup (option)' '
	test_auto_fixup final-fixup-option --autosquash
'

test_expect_success 'auto fixup (config)' '
	git config rebase.autosquash true &&
	test_auto_fixup final-fixup-config-true &&
	test_must_fail test_auto_fixup fixup-config-true-no --no-autosquash &&
	git config rebase.autosquash false &&
	test_must_fail test_auto_fixup final-fixup-config-false
'

test_auto_squash() {
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git commit -m "squash! first" &&

	git tag $1 &&
	test_tick &&
	git rebase $2 -i HEAD^^^ &&
	git log --oneline >actual &&
	test 3 = $(wc -l <actual) &&
	git diff --exit-code $1 &&
	test 1 = "$(git cat-file blob HEAD^:file1)" &&
	test 2 = $(git cat-file commit HEAD^ | grep first | wc -l)
}

test_expect_success 'auto squash (option)' '
	test_auto_squash final-squash --autosquash
'

test_expect_success 'auto squash (config)' '
	git config rebase.autosquash true &&
	test_auto_squash final-squash-config-true &&
	test_must_fail test_auto_squash squash-config-true-no --no-autosquash &&
	git config rebase.autosquash false &&
	test_must_fail test_auto_squash final-squash-config-false
'

test_expect_success 'misspelled auto squash' '
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git commit -m "squash! forst" &&
	git tag final-missquash &&
	test_tick &&
	git rebase --autosquash -i HEAD^^^ &&
	git log --oneline >actual &&
	test 4 = $(wc -l <actual) &&
	git diff --exit-code final-missquash &&
	test 0 = $(git rev-list final-missquash...HEAD | wc -l)
'

test_expect_success 'auto squash that matches 2 commits' '
	git reset --hard base &&
	echo 4 >file4 &&
	git add file4 &&
	test_tick &&
	git commit -m "first new commit" &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git commit -m "squash! first" &&
	git tag final-multisquash &&
	test_tick &&
	git rebase --autosquash -i HEAD~4 &&
	git log --oneline >actual &&
	test 4 = $(wc -l <actual) &&
	git diff --exit-code final-multisquash &&
	test 1 = "$(git cat-file blob HEAD^^:file1)" &&
	test 2 = $(git cat-file commit HEAD^^ | grep first | wc -l) &&
	test 1 = $(git cat-file commit HEAD | grep first | wc -l)
'

test_expect_success 'auto squash that matches a commit after the squash' '
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git commit -m "squash! third" &&
	echo 4 >file4 &&
	git add file4 &&
	test_tick &&
	git commit -m "third commit" &&
	git tag final-presquash &&
	test_tick &&
	git rebase --autosquash -i HEAD~4 &&
	git log --oneline >actual &&
	test 5 = $(wc -l <actual) &&
	git diff --exit-code final-presquash &&
	test 0 = "$(git cat-file blob HEAD^^:file1)" &&
	test 1 = "$(git cat-file blob HEAD^:file1)" &&
	test 1 = $(git cat-file commit HEAD | grep third | wc -l) &&
	test 1 = $(git cat-file commit HEAD^ | grep third | wc -l)
'

test_done
