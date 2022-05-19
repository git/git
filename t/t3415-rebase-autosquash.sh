#!/bin/sh

test_description='auto squash'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

test_expect_success setup '
	echo 0 >file0 &&
	git add . &&
	test_tick &&
	git cummit -m "initial cummit" &&
	echo 0 >file1 &&
	echo 2 >file2 &&
	git add . &&
	test_tick &&
	git cummit -m "first cummit" &&
	git tag first-cummit &&
	echo 3 >file3 &&
	git add . &&
	test_tick &&
	git cummit -m "second cummit" &&
	git tag base
'

test_auto_fixup () {
	no_squash= &&
	if test "x$1" = 'x!'
	then
		no_squash=true
		shift
	fi &&

	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git cummit -m "fixup! first" &&

	git tag $1 &&
	test_tick &&
	git rebase $2 -i HEAD^^^ &&
	git log --oneline >actual &&
	if test -n "$no_squash"
	then
		test_line_count = 4 actual
	else
		test_line_count = 3 actual &&
		git diff --exit-code $1 &&
		echo 1 >expect &&
		git cat-file blob HEAD^:file1 >actual &&
		test_cmp expect actual &&
		git cat-file commit HEAD^ >cummit &&
		grep first cummit >actual &&
		test_line_count = 1 actual
	fi
}

test_expect_success 'auto fixup (option)' '
	test_auto_fixup final-fixup-option --autosquash
'

test_expect_success 'auto fixup (config)' '
	git config rebase.autosquash true &&
	test_auto_fixup final-fixup-config-true &&
	test_auto_fixup ! fixup-config-true-no --no-autosquash &&
	git config rebase.autosquash false &&
	test_auto_fixup ! final-fixup-config-false
'

test_auto_squash () {
	no_squash= &&
	if test "x$1" = 'x!'
	then
		no_squash=true
		shift
	fi &&

	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git cummit -m "squash! first" -m "extra para for first" &&
	git tag $1 &&
	test_tick &&
	git rebase $2 -i HEAD^^^ &&
	git log --oneline >actual &&
	if test -n "$no_squash"
	then
		test_line_count = 4 actual
	else
		test_line_count = 3 actual &&
		git diff --exit-code $1 &&
		echo 1 >expect &&
		git cat-file blob HEAD^:file1 >actual &&
		test_cmp expect actual &&
		git cat-file commit HEAD^ >cummit &&
		grep first cummit >actual &&
		test_line_count = 2 actual
	fi
}

test_expect_success 'auto squash (option)' '
	test_auto_squash final-squash --autosquash
'

test_expect_success 'auto squash (config)' '
	git config rebase.autosquash true &&
	test_auto_squash final-squash-config-true &&
	test_auto_squash ! squash-config-true-no --no-autosquash &&
	git config rebase.autosquash false &&
	test_auto_squash ! final-squash-config-false
'

test_expect_success 'misspelled auto squash' '
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git cummit -m "squash! forst" &&
	git tag final-missquash &&
	test_tick &&
	git rebase --autosquash -i HEAD^^^ &&
	git log --oneline >actual &&
	test_line_count = 4 actual &&
	git diff --exit-code final-missquash &&
	git rev-list final-missquash...HEAD >list &&
	test_must_be_empty list
'

test_expect_success 'auto squash that matches 2 cummits' '
	git reset --hard base &&
	echo 4 >file4 &&
	git add file4 &&
	test_tick &&
	git cummit -m "first new cummit" &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git cummit -m "squash! first" -m "extra para for first" &&
	git tag final-multisquash &&
	test_tick &&
	git rebase --autosquash -i HEAD~4 &&
	git log --oneline >actual &&
	test_line_count = 4 actual &&
	git diff --exit-code final-multisquash &&
	echo 1 >expect &&
	git cat-file blob HEAD^^:file1 >actual &&
	test_cmp expect actual &&
	git cat-file commit HEAD^^ >cummit &&
	grep first cummit >actual &&
	test_line_count = 2 actual &&
	git cat-file commit HEAD >cummit &&
	grep first cummit >actual &&
	test_line_count = 1 actual
'

test_expect_success 'auto squash that matches a cummit after the squash' '
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git cummit -m "squash! third" &&
	echo 4 >file4 &&
	git add file4 &&
	test_tick &&
	git cummit -m "third cummit" &&
	git tag final-presquash &&
	test_tick &&
	git rebase --autosquash -i HEAD~4 &&
	git log --oneline >actual &&
	test_line_count = 5 actual &&
	git diff --exit-code final-presquash &&
	echo 0 >expect &&
	git cat-file blob HEAD^^:file1 >actual &&
	test_cmp expect actual &&
	echo 1 >expect &&
	git cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	git cat-file commit HEAD >cummit &&
	grep third cummit >actual &&
	test_line_count = 1 actual &&
	git cat-file commit HEAD^ >cummit &&
	grep third cummit >actual &&
	test_line_count = 1 actual
'
test_expect_success 'auto squash that matches a sha1' '
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	oid=$(git rev-parse --short HEAD^) &&
	git cummit -m "squash! $oid" -m "extra para" &&
	git tag final-shasquash &&
	test_tick &&
	git rebase --autosquash -i HEAD^^^ &&
	git log --oneline >actual &&
	test_line_count = 3 actual &&
	git diff --exit-code final-shasquash &&
	echo 1 >expect &&
	git cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	git cat-file commit HEAD^ >cummit &&
	! grep "squash" cummit &&
	grep "^extra para" cummit >actual &&
	test_line_count = 1 actual
'

test_expect_success 'auto squash that matches longer sha1' '
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	oid=$(git rev-parse --short=11 HEAD^) &&
	git cummit -m "squash! $oid" -m "extra para" &&
	git tag final-longshasquash &&
	test_tick &&
	git rebase --autosquash -i HEAD^^^ &&
	git log --oneline >actual &&
	test_line_count = 3 actual &&
	git diff --exit-code final-longshasquash &&
	echo 1 >expect &&
	git cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	git cat-file commit HEAD^ >cummit &&
	! grep "squash" cummit &&
	grep "^extra para" cummit >actual &&
	test_line_count = 1 actual
'

test_auto_cummit_flags () {
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git cummit --$1 first-cummit -m "extra para for first" &&
	git tag final-cummit-$1 &&
	test_tick &&
	git rebase --autosquash -i HEAD^^^ &&
	git log --oneline >actual &&
	test_line_count = 3 actual &&
	git diff --exit-code final-cummit-$1 &&
	echo 1 >expect &&
	git cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	git cat-file commit HEAD^ >cummit &&
	grep first cummit >actual &&
	test_line_count = $2 actual
}

test_expect_success 'use cummit --fixup' '
	test_auto_cummit_flags fixup 1
'

test_expect_success 'use cummit --squash' '
	test_auto_cummit_flags squash 2
'

test_auto_fixup_fixup () {
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git cummit -m "$1! first" -m "extra para for first" &&
	echo 2 >file1 &&
	git add -u &&
	test_tick &&
	git cummit -m "$1! $2! first" -m "second extra para for first" &&
	git tag "final-$1-$2" &&
	test_tick &&
	(
		set_cat_todo_editor &&
		test_must_fail git rebase --autosquash -i HEAD^^^^ >actual &&
		head=$(git rev-parse --short HEAD) &&
		parent1=$(git rev-parse --short HEAD^) &&
		parent2=$(git rev-parse --short HEAD^^) &&
		parent3=$(git rev-parse --short HEAD^^^) &&
		cat >expected <<-EOF &&
		pick $parent3 first cummit
		$1 $parent1 $1! first
		$1 $head $1! $2! first
		pick $parent2 second cummit
		EOF
		test_cmp expected actual
	) &&
	git rebase --autosquash -i HEAD^^^^ &&
	git log --oneline >actual &&
	test_line_count = 3 actual
	git diff --exit-code "final-$1-$2" &&
	echo 2 >expect &&
	git cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	git cat-file commit HEAD^ >cummit &&
	grep first cummit >actual &&
	if test "$1" = "fixup"
	then
		test_line_count = 1 actual
	elif test "$1" = "squash"
	then
		test_line_count = 3 actual
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

test_expect_success 'autosquash with custom inst format' '
	git reset --hard base &&
	git config --add rebase.instructionFormat "[%an @ %ar] %s"  &&
	echo 2 >file1 &&
	git add -u &&
	test_tick &&
	oid=$(git rev-parse --short HEAD^) &&
	git cummit -m "squash! $oid" -m "extra para for first" &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	subject=$(git log -n 1 --format=%s HEAD~2) &&
	git cummit -m "squash! $subject" -m "second extra para for first" &&
	git tag final-squash-instFmt &&
	test_tick &&
	git rebase --autosquash -i HEAD~4 &&
	git log --oneline >actual &&
	test_line_count = 3 actual &&
	git diff --exit-code final-squash-instFmt &&
	echo 1 >expect &&
	git cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	git cat-file commit HEAD^ >cummit &&
	! grep "squash" cummit &&
	grep first cummit >actual &&
	test_line_count = 3 actual
'

test_expect_success 'autosquash with empty custom instructionFormat' '
	git reset --hard base &&
	test_cummit empty-instructionFormat-test &&
	(
		set_cat_todo_editor &&
		test_must_fail git -c rebase.instructionFormat= \
			rebase --autosquash  --force-rebase -i HEAD^ >actual &&
		git log -1 --format="pick %h %s" >expect &&
		test_cmp expect actual
	)
'

set_backup_editor () {
	write_script backup-editor.sh <<-\EOF
	cp "$1" .git/backup-"$(basename "$1")"
	EOF
	test_set_editor "$PWD/backup-editor.sh"
}

test_expect_success 'autosquash with multiple empty patches' '
	test_tick &&
	git cummit --allow-empty -m "empty" &&
	test_tick &&
	git cummit --allow-empty -m "empty2" &&
	test_tick &&
	>fixup &&
	git add fixup &&
	git cummit --fixup HEAD^^ &&
	(
		set_backup_editor &&
		GIT_USE_REBASE_HELPER=false \
		git rebase -i --force-rebase --autosquash HEAD~4 &&
		grep empty2 .git/backup-git-rebase-todo
	)
'

test_expect_success 'extra spaces after fixup!' '
	base=$(git rev-parse HEAD) &&
	test_cummit to-fixup &&
	git cummit --allow-empty -m "fixup!  to-fixup" &&
	git rebase -i --autosquash --keep-empty HEAD~2 &&
	parent=$(git rev-parse HEAD^) &&
	test $base = $parent
'

test_expect_success 'wrapped original subject' '
	if test -d .git/rebase-merge; then git rebase --abort; fi &&
	base=$(git rev-parse HEAD) &&
	echo "wrapped subject" >wrapped &&
	git add wrapped &&
	test_tick &&
	git cummit --allow-empty -m "$(printf "To\nfixup")" &&
	test_tick &&
	git cummit --allow-empty -m "fixup! To fixup" &&
	git rebase -i --autosquash --keep-empty HEAD~2 &&
	parent=$(git rev-parse HEAD^) &&
	test $base = $parent
'

test_expect_success 'abort last squash' '
	test_when_finished "test_might_fail git rebase --abort" &&
	test_when_finished "git checkout main" &&

	git checkout -b some-squashes &&
	git cummit --allow-empty -m first &&
	git cummit --allow-empty --squash HEAD &&
	git cummit --allow-empty -m second &&
	git cummit --allow-empty --squash HEAD &&

	test_must_fail git -c core.editor="grep -q ^pick" \
		rebase -ki --autosquash HEAD~4 &&
	: do not finish the squash, but resolve it manually &&
	git cummit --allow-empty --amend -m edited-first &&
	git rebase --skip &&
	git show >actual &&
	! grep first actual
'

test_expect_success 'fixup a fixup' '
	echo 0to-fixup >file0 &&
	test_tick &&
	git cummit -m "to-fixup" file0 &&
	test_tick &&
	git cummit --squash HEAD -m X --allow-empty &&
	test_tick &&
	git cummit --squash HEAD^ -m Y --allow-empty &&
	test_tick &&
	git cummit -m "squash! $(git rev-parse HEAD^)" -m Z --allow-empty &&
	test_tick &&
	git cummit -m "squash! $(git rev-parse HEAD^^)" -m W --allow-empty &&
	git rebase -ki --autosquash HEAD~5 &&
	test XZWY = $(git show | tr -cd W-Z)
'

test_expect_success 'fixup does not clean up cummit message' '
	oneline="#818" &&
	git cummit --allow-empty -m "$oneline" &&
	git cummit --fixup HEAD --allow-empty &&
	git -c cummit.cleanup=strip rebase -ki --autosquash HEAD~2 &&
	test "$oneline" = "$(git show -s --format=%s)"
'

test_done
