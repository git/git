#!/bin/sh

test_description='git rebase --continue tests'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

set_fake_editor

test_expect_success 'setup' '
	test_commit "commit-new-file-F1" F1 1 &&
	test_commit "commit-new-file-F2" F2 2 &&

	git checkout -b topic HEAD^ &&
	test_commit "commit-new-file-F2-on-topic-branch" F2 22 &&

	git checkout main
'

test_expect_success 'merge based rebase --continue with works with touched file' '
	rm -fr .git/rebase-* &&
	git reset --hard &&
	git checkout main &&

	FAKE_LINES="edit 1" git rebase -i HEAD^ &&
	test-tool chmtime =-60 F1 &&
	git rebase --continue
'

test_expect_success 'merge based rebase --continue removes .git/MERGE_MSG' '
	git checkout -f --detach topic &&

	test_must_fail git rebase --onto main HEAD^ &&
	git read-tree --reset -u HEAD &&
	test_path_is_file .git/MERGE_MSG &&
	git rebase --continue &&
	test_path_is_missing .git/MERGE_MSG
'

test_expect_success 'apply based rebase --continue works with touched file' '
	rm -fr .git/rebase-* &&
	git reset --hard &&
	git checkout main &&

	test_must_fail git rebase --apply --onto main main topic &&
	echo "Resolved" >F2 &&
	git add F2 &&
	test-tool chmtime =-60 F1 &&
	git rebase --continue
'

test_expect_success 'rebase --continue can not be used with other options' '
	test_must_fail git rebase -v --continue &&
	test_must_fail git rebase --continue -v
'

test_expect_success 'rebase --continue remembers merge strategy and options' '
	rm -fr .git/rebase-* &&
	git reset --hard commit-new-file-F2-on-topic-branch &&
	test_commit "commit-new-file-F3-on-topic-branch" F3 32 &&
	test_when_finished "rm -fr test-bin" &&
	mkdir test-bin &&

	write_script test-bin/git-merge-funny <<-\EOF &&
	printf "[%s]\n" $# "$1" "$2" "$3" "$5" >actual
	shift 3 &&
	exec git merge-recursive "$@"
	EOF

	cat >expect <<-\EOF &&
	[7]
	[--option=arg with space]
	[--op"tion\]
	[--new
	line ]
	[--]
	EOF

	rm -f actual &&
	(
		PATH=./test-bin$PATH_SEP$PATH &&
		test_must_fail git rebase -s funny -X"option=arg with space" \
				-Xop\"tion\\ -X"new${LF}line " main topic
	) &&
	test_cmp expect actual &&
	rm actual &&
	echo "Resolved" >F2 &&
	git add F2 &&
	(
		PATH=./test-bin$PATH_SEP$PATH &&
		git rebase --continue
	) &&
	test_cmp expect actual
'

test_expect_success 'rebase -r passes merge strategy options correctly' '
	rm -fr .git/rebase-* &&
	git reset --hard commit-new-file-F3-on-topic-branch &&
	test_commit merge-theirs &&
	git reset --hard HEAD^ &&
	test_commit some-other-commit &&
	test_tick &&
	git merge --no-ff merge-theirs &&
	FAKE_LINES="1 3 edit 4 5 7 8 9" git rebase -i -f -r -m \
		-s recursive --strategy-option=theirs HEAD~2 &&
	test_commit force-change-ours &&
	git rebase --continue
'

test_expect_success '--skip after failed fixup cleans commit message' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout -b with-conflicting-fixup &&
	test_commit wants-fixup &&
	test_commit "fixup 1" wants-fixup.t 1 wants-fixup-1 &&
	test_commit "fixup 2" wants-fixup.t 2 wants-fixup-2 &&
	test_commit "fixup 3" wants-fixup.t 3 wants-fixup-3 &&
	test_must_fail env FAKE_LINES="1 fixup 2 squash 4" \
		git rebase -i HEAD~4 &&

	: now there is a conflict, and comments in the commit message &&
	test_commit_message HEAD <<-\EOF &&
	# This is a combination of 2 commits.
	# This is the 1st commit message:

	wants-fixup

	# The commit message #2 will be skipped:

	# fixup 1
	EOF

	: skip and continue &&
	echo "cp \"\$1\" .git/copy.txt" | write_script copy-editor.sh &&
	(test_set_editor "$PWD/copy-editor.sh" && git rebase --skip) &&

	: the user should not have had to edit the commit message &&
	test_path_is_missing .git/copy.txt &&

	: now the comments in the commit message should have been cleaned up &&
	test_commit_message HEAD -m wants-fixup &&

	: now, let us ensure that "squash" is handled correctly &&
	git reset --hard wants-fixup-3 &&
	test_must_fail env FAKE_LINES="1 squash 2 squash 1 squash 3 squash 1" \
		git rebase -i HEAD~4 &&

	: the second squash failed, but there are two more in the chain &&
	(test_set_editor "$PWD/copy-editor.sh" &&
	 test_must_fail git rebase --skip) &&

	: not the final squash, no need to edit the commit message &&
	test_path_is_missing .git/copy.txt &&

	: The first and third squashes succeeded, therefore: &&
	test_commit_message HEAD <<-\EOF &&
	# This is a combination of 3 commits.
	# This is the 1st commit message:

	wants-fixup

	# This is the commit message #2:

	fixup 1

	# This is the commit message #3:

	fixup 2
	EOF

	(test_set_editor "$PWD/copy-editor.sh" && git rebase --skip) &&
	test_commit_message HEAD <<-\EOF &&
	wants-fixup

	fixup 1

	fixup 2
	EOF

	: Final squash failed, but there was still a squash &&
	head -n1 .git/copy.txt >first-line &&
	test_grep "# This is a combination of 3 commits" first-line &&
	test_grep "# This is the commit message #3:" .git/copy.txt
'

test_expect_success 'setup rerere database' '
	rm -fr .git/rebase-* &&
	git reset --hard commit-new-file-F3-on-topic-branch &&
	git checkout main &&
	test_commit "commit-new-file-F3" F3 3 &&
	test_config rerere.enabled true &&
	git update-ref refs/heads/topic commit-new-file-F3-on-topic-branch &&
	test_must_fail git rebase -m main topic &&
	echo "Resolved" >F2 &&
	cp F2 expected-F2 &&
	git add F2 &&
	test_must_fail git rebase --continue &&
	echo "Resolved" >F3 &&
	cp F3 expected-F3 &&
	git add F3 &&
	git rebase --continue &&
	git reset --hard topic@{1}
'

prepare () {
	rm -fr .git/rebase-* &&
	git reset --hard commit-new-file-F3-on-topic-branch &&
	git checkout main &&
	test_config rerere.enabled true
}

test_rerere_autoupdate () {
	action=$1 &&
	test_expect_success "rebase $action --continue remembers --rerere-autoupdate" '
		prepare &&
		test_must_fail git rebase $action --rerere-autoupdate main topic &&
		test_cmp expected-F2 F2 &&
		git diff-files --quiet &&
		test_must_fail git rebase --continue &&
		test_cmp expected-F3 F3 &&
		git diff-files --quiet &&
		git rebase --continue
	'

	test_expect_success "rebase $action --continue honors rerere.autoUpdate" '
		prepare &&
		test_config rerere.autoupdate true &&
		test_must_fail git rebase $action main topic &&
		test_cmp expected-F2 F2 &&
		git diff-files --quiet &&
		test_must_fail git rebase --continue &&
		test_cmp expected-F3 F3 &&
		git diff-files --quiet &&
		git rebase --continue
	'

	test_expect_success "rebase $action --continue remembers --no-rerere-autoupdate" '
		prepare &&
		test_config rerere.autoupdate true &&
		test_must_fail git rebase $action --no-rerere-autoupdate main topic &&
		test_cmp expected-F2 F2 &&
		test_must_fail git diff-files --quiet &&
		git add F2 &&
		test_must_fail git rebase --continue &&
		test_cmp expected-F3 F3 &&
		test_must_fail git diff-files --quiet &&
		git add F3 &&
		git rebase --continue
	'
}

test_rerere_autoupdate --apply
test_rerere_autoupdate -m
GIT_SEQUENCE_EDITOR=: && export GIT_SEQUENCE_EDITOR
test_rerere_autoupdate -i
unset GIT_SEQUENCE_EDITOR

test_expect_success 'the todo command "break" works' '
	rm -f execed &&
	FAKE_LINES="break b exec_>execed" git rebase -i HEAD &&
	test_path_is_missing execed &&
	git rebase --continue &&
	test_path_is_missing execed &&
	git rebase --continue &&
	test_path_is_file execed
'

test_expect_success 'patch file is removed before break command' '
	test_when_finished "git rebase --abort" &&
	cat >todo <<-\EOF &&
	pick commit-new-file-F2-on-topic-branch
	break
	EOF

	(
		set_replace_editor todo &&
		test_must_fail git rebase -i --onto commit-new-file-F2 HEAD
	) &&
	test_path_is_file .git/rebase-merge/patch &&
	echo 22>F2 &&
	git add F2 &&
	git rebase --continue &&
	test_path_is_missing .git/rebase-merge/patch
'

test_expect_success '--reschedule-failed-exec' '
	test_when_finished "git rebase --abort" &&
	test_must_fail git rebase -x false --reschedule-failed-exec HEAD^ &&
	grep "^exec false" .git/rebase-merge/git-rebase-todo &&
	git rebase --abort &&
	test_must_fail git -c rebase.rescheduleFailedExec=true \
		rebase -x false HEAD^ 2>err &&
	grep "^exec false" .git/rebase-merge/git-rebase-todo &&
	test_grep "has been rescheduled" err
'

test_expect_success 'rebase.rescheduleFailedExec only affects `rebase -i`' '
	test_config rebase.rescheduleFailedExec true &&
	test_must_fail git rebase -x false HEAD^ &&
	grep "^exec false" .git/rebase-merge/git-rebase-todo &&
	git rebase --abort &&
	git rebase HEAD^
'

test_expect_success 'rebase.rescheduleFailedExec=true & --no-reschedule-failed-exec' '
	test_when_finished "git rebase --abort" &&
	test_config rebase.rescheduleFailedExec true &&
	test_must_fail git rebase -x false --no-reschedule-failed-exec HEAD~2 &&
	test_must_fail git rebase --continue 2>err &&
	! grep "has been rescheduled" err
'

test_expect_success 'new rebase.rescheduleFailedExec=true setting in an ongoing rebase is ignored' '
	test_when_finished "git rebase --abort" &&
	test_must_fail git rebase -x false HEAD~2 &&
	test_config rebase.rescheduleFailedExec true &&
	test_must_fail git rebase --continue 2>err &&
	! grep "has been rescheduled" err
'

test_expect_success 'there is no --no-reschedule-failed-exec in an ongoing rebase' '
	test_when_finished "git rebase --abort" &&
	test_must_fail git rebase -x false HEAD~2 &&
	test_expect_code 129 git rebase --continue --no-reschedule-failed-exec &&
	test_expect_code 129 git rebase --edit-todo --no-reschedule-failed-exec
'

test_orig_head_helper () {
	test_when_finished 'git rebase --abort &&
		git checkout topic &&
		git reset --hard commit-new-file-F2-on-topic-branch' &&
	git update-ref -d ORIG_HEAD &&
	test_must_fail git rebase "$@" &&
	test_cmp_rev ORIG_HEAD commit-new-file-F2-on-topic-branch
}

test_orig_head () {
	type=$1
	test_expect_success "rebase $type sets ORIG_HEAD correctly" '
		git checkout topic &&
		git reset --hard commit-new-file-F2-on-topic-branch &&
		test_orig_head_helper $type main
	'

	test_expect_success "rebase $type <upstream> <branch> sets ORIG_HEAD correctly" '
		git checkout main &&
		test_orig_head_helper $type main topic
	'
}

test_orig_head --apply
test_orig_head --merge

test_done
