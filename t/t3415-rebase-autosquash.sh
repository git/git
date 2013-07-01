#!/bin/sh

test_description='auto squash'

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

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
	git tag first-commit &&
	echo 3 >file3 &&
	git add . &&
	test_tick &&
	git commit -m "second commit" &&
	git tag base
'

test_auto_fixup () {
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git commit -m "fixup! first" &&

	git tag $1 &&
	test_tick &&
	git rebase $2 -i HEAD^^^ &&
	git log --oneline >actual &&
	test_line_count = 3 actual &&
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

test_auto_squash () {
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git commit -m "squash! first" &&

	git tag $1 &&
	test_tick &&
	git rebase $2 -i HEAD^^^ &&
	git log --oneline >actual &&
	test_line_count = 3 actual &&
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
	test_line_count = 4 actual &&
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
	test_line_count = 4 actual &&
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
	test_line_count = 5 actual &&
	git diff --exit-code final-presquash &&
	test 0 = "$(git cat-file blob HEAD^^:file1)" &&
	test 1 = "$(git cat-file blob HEAD^:file1)" &&
	test 1 = $(git cat-file commit HEAD | grep third | wc -l) &&
	test 1 = $(git cat-file commit HEAD^ | grep third | wc -l)
'
test_expect_success 'auto squash that matches a sha1' '
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git commit -m "squash! $(git rev-parse --short HEAD^)" &&
	git tag final-shasquash &&
	test_tick &&
	git rebase --autosquash -i HEAD^^^ &&
	git log --oneline >actual &&
	test_line_count = 3 actual &&
	git diff --exit-code final-shasquash &&
	test 1 = "$(git cat-file blob HEAD^:file1)" &&
	test 1 = $(git cat-file commit HEAD^ | grep squash | wc -l)
'

test_expect_success 'auto squash that matches longer sha1' '
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git commit -m "squash! $(git rev-parse --short=11 HEAD^)" &&
	git tag final-longshasquash &&
	test_tick &&
	git rebase --autosquash -i HEAD^^^ &&
	git log --oneline >actual &&
	test_line_count = 3 actual &&
	git diff --exit-code final-longshasquash &&
	test 1 = "$(git cat-file blob HEAD^:file1)" &&
	test 1 = $(git cat-file commit HEAD^ | grep squash | wc -l)
'

test_auto_commit_flags () {
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git commit --$1 first-commit &&
	git tag final-commit-$1 &&
	test_tick &&
	git rebase --autosquash -i HEAD^^^ &&
	git log --oneline >actual &&
	test_line_count = 3 actual &&
	git diff --exit-code final-commit-$1 &&
	test 1 = "$(git cat-file blob HEAD^:file1)" &&
	test $2 = $(git cat-file commit HEAD^ | grep first | wc -l)
}

test_expect_success 'use commit --fixup' '
	test_auto_commit_flags fixup 1
'

test_expect_success 'use commit --squash' '
	test_auto_commit_flags squash 2
'

test_auto_fixup_fixup () {
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git commit -m "$1! first" &&
	echo 2 >file1 &&
	git add -u &&
	test_tick &&
	git commit -m "$1! $2! first" &&
	git tag "final-$1-$2" &&
	test_tick &&
	(
		set_cat_todo_editor &&
		test_must_fail git rebase --autosquash -i HEAD^^^^ >actual &&
		cat >expected <<-EOF &&
		pick $(git rev-parse --short HEAD^^^) first commit
		$1 $(git rev-parse --short HEAD^) $1! first
		$1 $(git rev-parse --short HEAD) $1! $2! first
		pick $(git rev-parse --short HEAD^^) second commit
		EOF
		test_cmp expected actual
	) &&
	git rebase --autosquash -i HEAD^^^^ &&
	git log --oneline >actual &&
	test_line_count = 3 actual
	git diff --exit-code "final-$1-$2" &&
	test 2 = "$(git cat-file blob HEAD^:file1)" &&
	if test "$1" = "fixup"
	then
		test 1 = $(git cat-file commit HEAD^ | grep first | wc -l)
	elif test "$1" = "squash"
	then
		test 3 = $(git cat-file commit HEAD^ | grep first | wc -l)
	else
		false
	fi
}

test_expect_success 'fixup! fixup!' '
	test_auto_fixup_fixup fixup fixup
'

test_expect_success 'fixup! squash!' '
	test_auto_fixup_fixup fixup squash
'

test_expect_success 'squash! squash!' '
	test_auto_fixup_fixup squash squash
'

test_expect_success 'squash! fixup!' '
	test_auto_fixup_fixup squash fixup
'

test_done
