#!/bin/sh

test_description='auto squash'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

test_expect_success setup '
	echo 0 >file0 &&
	but add . &&
	test_tick &&
	but cummit -m "initial cummit" &&
	echo 0 >file1 &&
	echo 2 >file2 &&
	but add . &&
	test_tick &&
	but cummit -m "first cummit" &&
	but tag first-cummit &&
	echo 3 >file3 &&
	but add . &&
	test_tick &&
	but cummit -m "second cummit" &&
	but tag base
'

test_auto_fixup () {
	no_squash= &&
	if test "x$1" = 'x!'
	then
		no_squash=true
		shift
	fi &&

	but reset --hard base &&
	echo 1 >file1 &&
	but add -u &&
	test_tick &&
	but cummit -m "fixup! first" &&

	but tag $1 &&
	test_tick &&
	but rebase $2 -i HEAD^^^ &&
	but log --oneline >actual &&
	if test -n "$no_squash"
	then
		test_line_count = 4 actual
	else
		test_line_count = 3 actual &&
		but diff --exit-code $1 &&
		echo 1 >expect &&
		but cat-file blob HEAD^:file1 >actual &&
		test_cmp expect actual &&
		but cat-file commit HEAD^ >cummit &&
		grep first cummit >actual &&
		test_line_count = 1 actual
	fi
}

test_expect_success 'auto fixup (option)' '
	test_auto_fixup final-fixup-option --autosquash
'

test_expect_success 'auto fixup (config)' '
	but config rebase.autosquash true &&
	test_auto_fixup final-fixup-config-true &&
	test_auto_fixup ! fixup-config-true-no --no-autosquash &&
	but config rebase.autosquash false &&
	test_auto_fixup ! final-fixup-config-false
'

test_auto_squash () {
	no_squash= &&
	if test "x$1" = 'x!'
	then
		no_squash=true
		shift
	fi &&

	but reset --hard base &&
	echo 1 >file1 &&
	but add -u &&
	test_tick &&
	but cummit -m "squash! first" -m "extra para for first" &&
	but tag $1 &&
	test_tick &&
	but rebase $2 -i HEAD^^^ &&
	but log --oneline >actual &&
	if test -n "$no_squash"
	then
		test_line_count = 4 actual
	else
		test_line_count = 3 actual &&
		but diff --exit-code $1 &&
		echo 1 >expect &&
		but cat-file blob HEAD^:file1 >actual &&
		test_cmp expect actual &&
		but cat-file commit HEAD^ >cummit &&
		grep first cummit >actual &&
		test_line_count = 2 actual
	fi
}

test_expect_success 'auto squash (option)' '
	test_auto_squash final-squash --autosquash
'

test_expect_success 'auto squash (config)' '
	but config rebase.autosquash true &&
	test_auto_squash final-squash-config-true &&
	test_auto_squash ! squash-config-true-no --no-autosquash &&
	but config rebase.autosquash false &&
	test_auto_squash ! final-squash-config-false
'

test_expect_success 'misspelled auto squash' '
	but reset --hard base &&
	echo 1 >file1 &&
	but add -u &&
	test_tick &&
	but cummit -m "squash! forst" &&
	but tag final-missquash &&
	test_tick &&
	but rebase --autosquash -i HEAD^^^ &&
	but log --oneline >actual &&
	test_line_count = 4 actual &&
	but diff --exit-code final-missquash &&
	but rev-list final-missquash...HEAD >list &&
	test_must_be_empty list
'

test_expect_success 'auto squash that matches 2 cummits' '
	but reset --hard base &&
	echo 4 >file4 &&
	but add file4 &&
	test_tick &&
	but cummit -m "first new cummit" &&
	echo 1 >file1 &&
	but add -u &&
	test_tick &&
	but cummit -m "squash! first" -m "extra para for first" &&
	but tag final-multisquash &&
	test_tick &&
	but rebase --autosquash -i HEAD~4 &&
	but log --oneline >actual &&
	test_line_count = 4 actual &&
	but diff --exit-code final-multisquash &&
	echo 1 >expect &&
	but cat-file blob HEAD^^:file1 >actual &&
	test_cmp expect actual &&
	but cat-file commit HEAD^^ >cummit &&
	grep first cummit >actual &&
	test_line_count = 2 actual &&
	but cat-file commit HEAD >cummit &&
	grep first cummit >actual &&
	test_line_count = 1 actual
'

test_expect_success 'auto squash that matches a cummit after the squash' '
	but reset --hard base &&
	echo 1 >file1 &&
	but add -u &&
	test_tick &&
	but cummit -m "squash! third" &&
	echo 4 >file4 &&
	but add file4 &&
	test_tick &&
	but cummit -m "third cummit" &&
	but tag final-presquash &&
	test_tick &&
	but rebase --autosquash -i HEAD~4 &&
	but log --oneline >actual &&
	test_line_count = 5 actual &&
	but diff --exit-code final-presquash &&
	echo 0 >expect &&
	but cat-file blob HEAD^^:file1 >actual &&
	test_cmp expect actual &&
	echo 1 >expect &&
	but cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	but cat-file commit HEAD >cummit &&
	grep third cummit >actual &&
	test_line_count = 1 actual &&
	but cat-file commit HEAD^ >cummit &&
	grep third cummit >actual &&
	test_line_count = 1 actual
'
test_expect_success 'auto squash that matches a sha1' '
	but reset --hard base &&
	echo 1 >file1 &&
	but add -u &&
	test_tick &&
	oid=$(but rev-parse --short HEAD^) &&
	but cummit -m "squash! $oid" -m "extra para" &&
	but tag final-shasquash &&
	test_tick &&
	but rebase --autosquash -i HEAD^^^ &&
	but log --oneline >actual &&
	test_line_count = 3 actual &&
	but diff --exit-code final-shasquash &&
	echo 1 >expect &&
	but cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	but cat-file commit HEAD^ >cummit &&
	! grep "squash" cummit &&
	grep "^extra para" cummit >actual &&
	test_line_count = 1 actual
'

test_expect_success 'auto squash that matches longer sha1' '
	but reset --hard base &&
	echo 1 >file1 &&
	but add -u &&
	test_tick &&
	oid=$(but rev-parse --short=11 HEAD^) &&
	but cummit -m "squash! $oid" -m "extra para" &&
	but tag final-longshasquash &&
	test_tick &&
	but rebase --autosquash -i HEAD^^^ &&
	but log --oneline >actual &&
	test_line_count = 3 actual &&
	but diff --exit-code final-longshasquash &&
	echo 1 >expect &&
	but cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	but cat-file commit HEAD^ >cummit &&
	! grep "squash" cummit &&
	grep "^extra para" cummit >actual &&
	test_line_count = 1 actual
'

test_auto_cummit_flags () {
	but reset --hard base &&
	echo 1 >file1 &&
	but add -u &&
	test_tick &&
	but cummit --$1 first-cummit -m "extra para for first" &&
	but tag final-cummit-$1 &&
	test_tick &&
	but rebase --autosquash -i HEAD^^^ &&
	but log --oneline >actual &&
	test_line_count = 3 actual &&
	but diff --exit-code final-cummit-$1 &&
	echo 1 >expect &&
	but cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	but cat-file commit HEAD^ >cummit &&
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
	but reset --hard base &&
	echo 1 >file1 &&
	but add -u &&
	test_tick &&
	but cummit -m "$1! first" -m "extra para for first" &&
	echo 2 >file1 &&
	but add -u &&
	test_tick &&
	but cummit -m "$1! $2! first" -m "second extra para for first" &&
	but tag "final-$1-$2" &&
	test_tick &&
	(
		set_cat_todo_editor &&
		test_must_fail but rebase --autosquash -i HEAD^^^^ >actual &&
		head=$(but rev-parse --short HEAD) &&
		parent1=$(but rev-parse --short HEAD^) &&
		parent2=$(but rev-parse --short HEAD^^) &&
		parent3=$(but rev-parse --short HEAD^^^) &&
		cat >expected <<-EOF &&
		pick $parent3 first cummit
		$1 $parent1 $1! first
		$1 $head $1! $2! first
		pick $parent2 second cummit
		EOF
		test_cmp expected actual
	) &&
	but rebase --autosquash -i HEAD^^^^ &&
	but log --oneline >actual &&
	test_line_count = 3 actual
	but diff --exit-code "final-$1-$2" &&
	echo 2 >expect &&
	but cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	but cat-file commit HEAD^ >cummit &&
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
	but reset --hard base &&
	but config --add rebase.instructionFormat "[%an @ %ar] %s"  &&
	echo 2 >file1 &&
	but add -u &&
	test_tick &&
	oid=$(but rev-parse --short HEAD^) &&
	but cummit -m "squash! $oid" -m "extra para for first" &&
	echo 1 >file1 &&
	but add -u &&
	test_tick &&
	subject=$(but log -n 1 --format=%s HEAD~2) &&
	but cummit -m "squash! $subject" -m "second extra para for first" &&
	but tag final-squash-instFmt &&
	test_tick &&
	but rebase --autosquash -i HEAD~4 &&
	but log --oneline >actual &&
	test_line_count = 3 actual &&
	but diff --exit-code final-squash-instFmt &&
	echo 1 >expect &&
	but cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	but cat-file commit HEAD^ >cummit &&
	! grep "squash" cummit &&
	grep first cummit >actual &&
	test_line_count = 3 actual
'

test_expect_success 'autosquash with empty custom instructionFormat' '
	but reset --hard base &&
	test_cummit empty-instructionFormat-test &&
	(
		set_cat_todo_editor &&
		test_must_fail but -c rebase.instructionFormat= \
			rebase --autosquash  --force-rebase -i HEAD^ >actual &&
		but log -1 --format="pick %h %s" >expect &&
		test_cmp expect actual
	)
'

set_backup_editor () {
	write_script backup-editor.sh <<-\EOF
	cp "$1" .but/backup-"$(basename "$1")"
	EOF
	test_set_editor "$PWD/backup-editor.sh"
}

test_expect_success 'autosquash with multiple empty patches' '
	test_tick &&
	but cummit --allow-empty -m "empty" &&
	test_tick &&
	but cummit --allow-empty -m "empty2" &&
	test_tick &&
	>fixup &&
	but add fixup &&
	but cummit --fixup HEAD^^ &&
	(
		set_backup_editor &&
		BUT_USE_REBASE_HELPER=false \
		but rebase -i --force-rebase --autosquash HEAD~4 &&
		grep empty2 .but/backup-but-rebase-todo
	)
'

test_expect_success 'extra spaces after fixup!' '
	base=$(but rev-parse HEAD) &&
	test_cummit to-fixup &&
	but cummit --allow-empty -m "fixup!  to-fixup" &&
	but rebase -i --autosquash --keep-empty HEAD~2 &&
	parent=$(but rev-parse HEAD^) &&
	test $base = $parent
'

test_expect_success 'wrapped original subject' '
	if test -d .but/rebase-merge; then but rebase --abort; fi &&
	base=$(but rev-parse HEAD) &&
	echo "wrapped subject" >wrapped &&
	but add wrapped &&
	test_tick &&
	but cummit --allow-empty -m "$(printf "To\nfixup")" &&
	test_tick &&
	but cummit --allow-empty -m "fixup! To fixup" &&
	but rebase -i --autosquash --keep-empty HEAD~2 &&
	parent=$(but rev-parse HEAD^) &&
	test $base = $parent
'

test_expect_success 'abort last squash' '
	test_when_finished "test_might_fail but rebase --abort" &&
	test_when_finished "but checkout main" &&

	but checkout -b some-squashes &&
	but cummit --allow-empty -m first &&
	but cummit --allow-empty --squash HEAD &&
	but cummit --allow-empty -m second &&
	but cummit --allow-empty --squash HEAD &&

	test_must_fail but -c core.editor="grep -q ^pick" \
		rebase -ki --autosquash HEAD~4 &&
	: do not finish the squash, but resolve it manually &&
	but cummit --allow-empty --amend -m edited-first &&
	but rebase --skip &&
	but show >actual &&
	! grep first actual
'

test_expect_success 'fixup a fixup' '
	echo 0to-fixup >file0 &&
	test_tick &&
	but cummit -m "to-fixup" file0 &&
	test_tick &&
	but cummit --squash HEAD -m X --allow-empty &&
	test_tick &&
	but cummit --squash HEAD^ -m Y --allow-empty &&
	test_tick &&
	but cummit -m "squash! $(but rev-parse HEAD^)" -m Z --allow-empty &&
	test_tick &&
	but cummit -m "squash! $(but rev-parse HEAD^^)" -m W --allow-empty &&
	but rebase -ki --autosquash HEAD~5 &&
	test XZWY = $(but show | tr -cd W-Z)
'

test_expect_success 'fixup does not clean up cummit message' '
	oneline="#818" &&
	but cummit --allow-empty -m "$oneline" &&
	but cummit --fixup HEAD --allow-empty &&
	but -c cummit.cleanup=strip rebase -ki --autosquash HEAD~2 &&
	test "$oneline" = "$(but show -s --format=%s)"
'

test_done
