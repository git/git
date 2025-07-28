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
	git commit -m "fixup! first" &&

	git tag $1 &&
	test_tick &&
	git rebase $2 HEAD^^^ &&
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
		git cat-file commit HEAD^ >commit &&
		grep first commit >actual &&
		test_line_count = 1 actual
	fi
}

test_expect_success 'auto fixup (option)' '
	test_auto_fixup fixup-option --autosquash &&
	test_auto_fixup fixup-option-i "--autosquash -i"
'

test_expect_success 'auto fixup (config true)' '
	git config rebase.autosquash true &&
	test_auto_fixup ! fixup-config-true &&
	test_auto_fixup fixup-config-true-i -i &&
	test_auto_fixup ! fixup-config-true-no --no-autosquash &&
	test_auto_fixup ! fixup-config-true-i-no "-i --no-autosquash"
'

test_expect_success 'auto fixup (config false)' '
	git config rebase.autosquash false &&
	test_auto_fixup ! fixup-config-false &&
	test_auto_fixup ! fixup-config-false-i -i &&
	test_auto_fixup fixup-config-false-yes --autosquash &&
	test_auto_fixup fixup-config-false-i-yes "-i --autosquash"
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
	git commit -m "squash! first" -m "extra para for first" &&
	git tag $1 &&
	test_tick &&
	git rebase $2 HEAD^^^ &&
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
		git cat-file commit HEAD^ >commit &&
		grep first commit >actual &&
		test_line_count = 2 actual
	fi
}

test_expect_success 'auto squash (option)' '
	test_auto_squash squash-option --autosquash &&
	test_auto_squash squash-option-i "--autosquash -i"
'

test_expect_success 'auto squash (config true)' '
	git config rebase.autosquash true &&
	test_auto_squash ! squash-config-true &&
	test_auto_squash squash-config-true-i -i &&
	test_auto_squash ! squash-config-true-no --no-autosquash &&
	test_auto_squash ! squash-config-true-i-no "-i --no-autosquash"
'

test_expect_success 'auto squash (config false)' '
	git config rebase.autosquash false &&
	test_auto_squash ! squash-config-false &&
	test_auto_squash ! squash-config-false-i -i &&
	test_auto_squash squash-config-false-yes --autosquash &&
	test_auto_squash squash-config-false-i-yes "-i --autosquash"
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
	git rev-list final-missquash...HEAD >list &&
	test_must_be_empty list
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
	git commit -m "squash! first" -m "extra para for first" &&
	git tag final-multisquash &&
	test_tick &&
	git rebase --autosquash -i HEAD~4 &&
	git log --oneline >actual &&
	test_line_count = 4 actual &&
	git diff --exit-code final-multisquash &&
	echo 1 >expect &&
	git cat-file blob HEAD^^:file1 >actual &&
	test_cmp expect actual &&
	git cat-file commit HEAD^^ >commit &&
	grep first commit >actual &&
	test_line_count = 2 actual &&
	git cat-file commit HEAD >commit &&
	grep first commit >actual &&
	test_line_count = 1 actual
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
	echo 0 >expect &&
	git cat-file blob HEAD^^:file1 >actual &&
	test_cmp expect actual &&
	echo 1 >expect &&
	git cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	git cat-file commit HEAD >commit &&
	grep third commit >actual &&
	test_line_count = 1 actual &&
	git cat-file commit HEAD^ >commit &&
	grep third commit >actual &&
	test_line_count = 1 actual
'
test_expect_success 'auto squash that matches a sha1' '
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	oid=$(git rev-parse --short HEAD^) &&
	git commit -m "squash! $oid" -m "extra para" &&
	git tag final-shasquash &&
	test_tick &&
	git rebase --autosquash -i HEAD^^^ &&
	git log --oneline >actual &&
	test_line_count = 3 actual &&
	git diff --exit-code final-shasquash &&
	echo 1 >expect &&
	git cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	git cat-file commit HEAD^ >commit &&
	! grep "squash" commit &&
	grep "^extra para" commit >actual &&
	test_line_count = 1 actual
'

test_expect_success 'auto squash that matches longer sha1' '
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	oid=$(git rev-parse --short=11 HEAD^) &&
	git commit -m "squash! $oid" -m "extra para" &&
	git tag final-longshasquash &&
	test_tick &&
	git rebase --autosquash -i HEAD^^^ &&
	git log --oneline >actual &&
	test_line_count = 3 actual &&
	git diff --exit-code final-longshasquash &&
	echo 1 >expect &&
	git cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	git cat-file commit HEAD^ >commit &&
	! grep "squash" commit &&
	grep "^extra para" commit >actual &&
	test_line_count = 1 actual
'

test_expect_success 'auto squash of fixup commit that matches branch name which points back to fixup commit' '
	git reset --hard base &&
	git commit --allow-empty -m "fixup! self-cycle" &&
	git branch self-cycle &&
	GIT_SEQUENCE_EDITOR="cat >tmp" git rebase --autosquash -i HEAD^^ &&
	sed -ne "/^[^#]/{s/[0-9a-f]\{7,\}/HASH/g;p;}" tmp >actual &&
	cat <<-EOF >expect &&
	pick HASH # second commit
	pick HASH # fixup! self-cycle # empty
	EOF
	test_cmp expect actual
'

test_auto_commit_flags () {
	git reset --hard base &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	git commit --$1 first-commit -m "extra para for first" &&
	git tag final-commit-$1 &&
	test_tick &&
	git rebase --autosquash -i HEAD^^^ &&
	git log --oneline >actual &&
	test_line_count = 3 actual &&
	git diff --exit-code final-commit-$1 &&
	echo 1 >expect &&
	git cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	git cat-file commit HEAD^ >commit &&
	grep first commit >actual &&
	test_line_count = $2 actual
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
	git commit -m "$1! first" -m "extra para for first" &&
	echo 2 >file1 &&
	git add -u &&
	test_tick &&
	git commit -m "$1! $2! first" -m "second extra para for first" &&
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
		pick $parent3 # first commit
		$1 $parent1 # $1! first
		$1 $head # $1! $2! first
		pick $parent2 # second commit
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
	git cat-file commit HEAD^ >commit &&
	grep first commit >actual &&
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
	git commit -m "squash! $oid" -m "extra para for first" &&
	echo 1 >file1 &&
	git add -u &&
	test_tick &&
	subject=$(git log -n 1 --format=%s HEAD~2) &&
	git commit -m "squash! $subject" -m "second extra para for first" &&
	git tag final-squash-instFmt &&
	test_tick &&
	git rebase --autosquash -i HEAD~4 &&
	git log --oneline >actual &&
	test_line_count = 3 actual &&
	git diff --exit-code final-squash-instFmt &&
	echo 1 >expect &&
	git cat-file blob HEAD^:file1 >actual &&
	test_cmp expect actual &&
	git cat-file commit HEAD^ >commit &&
	! grep "squash" commit &&
	grep first commit >actual &&
	test_line_count = 3 actual
'

test_expect_success 'autosquash with empty custom instructionFormat' '
	git reset --hard base &&
	test_commit empty-instructionFormat-test &&
	(
		set_cat_todo_editor &&
		test_must_fail git -c rebase.instructionFormat= \
			rebase --autosquash  --force-rebase -i HEAD^ >actual &&
		git log -1 --format="pick %h # %s" >expect &&
		test_cmp expect actual
	)
'

test_expect_success 'autosquash with invalid custom instructionFormat' '
	git reset --hard base &&
	test_commit invalid-instructionFormat-test &&
	(
		test_must_fail git -c rebase.instructionFormat=blah \
			rebase --autosquash  --force-rebase -i HEAD^ &&
		test_path_is_missing .git/rebase-merge
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
	git commit --allow-empty -m "empty" &&
	test_tick &&
	git commit --allow-empty -m "empty2" &&
	test_tick &&
	>fixup &&
	git add fixup &&
	git commit --fixup HEAD^^ &&
	(
		set_backup_editor &&
		GIT_USE_REBASE_HELPER=false \
		git rebase -i --force-rebase --autosquash HEAD~4 &&
		grep empty2 .git/backup-git-rebase-todo
	)
'

test_expect_success 'extra spaces after fixup!' '
	base=$(git rev-parse HEAD) &&
	test_commit to-fixup &&
	git commit --allow-empty -m "fixup!  to-fixup" &&
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
	git commit --allow-empty -m "$(printf "To\nfixup")" &&
	test_tick &&
	git commit --allow-empty -m "fixup! To fixup" &&
	git rebase -i --autosquash --keep-empty HEAD~2 &&
	parent=$(git rev-parse HEAD^) &&
	test $base = $parent
'

test_expect_success 'abort last squash' '
	test_when_finished "test_might_fail git rebase --abort" &&
	test_when_finished "git checkout main" &&

	git checkout -b some-squashes &&
	git commit --allow-empty -m first &&
	git commit --allow-empty --squash HEAD &&
	git commit --allow-empty -m second &&
	git commit --allow-empty --squash HEAD &&

	test_must_fail git -c core.editor="grep -q ^pick" \
		rebase -ki --autosquash HEAD~4 &&
	: do not finish the squash, but resolve it manually &&
	git commit --allow-empty --amend -m edited-first &&
	git rebase --skip &&
	git show >actual &&
	! grep first actual
'

test_expect_success 'fixup a fixup' '
	echo 0to-fixup >file0 &&
	test_tick &&
	git commit -m "to-fixup" file0 &&
	test_tick &&
	git commit --squash HEAD -m X --allow-empty &&
	test_tick &&
	git commit --squash HEAD^ -m Y --allow-empty &&
	test_tick &&
	git commit -m "squash! $(git rev-parse HEAD^)" -m Z --allow-empty &&
	test_tick &&
	git commit -m "squash! $(git rev-parse HEAD^^)" -m W --allow-empty &&
	git rebase -ki --autosquash HEAD~5 &&
	test XZWY = $(git show | tr -cd W-Z)
'

test_expect_success 'fixup does not clean up commit message' '
	oneline="#818" &&
	git commit --allow-empty -m "$oneline" &&
	git commit --fixup HEAD --allow-empty &&
	git -c commit.cleanup=strip rebase -ki --autosquash HEAD~2 &&
	test "$oneline" = "$(git show -s --format=%s)"
'

test_done
