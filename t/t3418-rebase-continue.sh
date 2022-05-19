#!/bin/sh

test_description='but rebase --continue tests'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

set_fake_editor

test_expect_success 'setup' '
	test_cummit "cummit-new-file-F1" F1 1 &&
	test_cummit "cummit-new-file-F2" F2 2 &&

	but checkout -b topic HEAD^ &&
	test_cummit "cummit-new-file-F2-on-topic-branch" F2 22 &&

	but checkout main
'

test_expect_success 'merge based rebase --continue with works with touched file' '
	rm -fr .but/rebase-* &&
	but reset --hard &&
	but checkout main &&

	FAKE_LINES="edit 1" but rebase -i HEAD^ &&
	test-tool chmtime =-60 F1 &&
	but rebase --continue
'

test_expect_success 'merge based rebase --continue removes .but/MERGE_MSG' '
	but checkout -f --detach topic &&

	test_must_fail but rebase --onto main HEAD^ &&
	but read-tree --reset -u HEAD &&
	test_path_is_file .but/MERGE_MSG &&
	but rebase --continue &&
	test_path_is_missing .but/MERGE_MSG
'

test_expect_success 'apply based rebase --continue works with touched file' '
	rm -fr .but/rebase-* &&
	but reset --hard &&
	but checkout main &&

	test_must_fail but rebase --apply --onto main main topic &&
	echo "Resolved" >F2 &&
	but add F2 &&
	test-tool chmtime =-60 F1 &&
	but rebase --continue
'

test_expect_success 'rebase --continue can not be used with other options' '
	test_must_fail but rebase -v --continue &&
	test_must_fail but rebase --continue -v
'

test_expect_success 'rebase --continue remembers merge strategy and options' '
	rm -fr .but/rebase-* &&
	but reset --hard cummit-new-file-F2-on-topic-branch &&
	test_cummit "cummit-new-file-F3-on-topic-branch" F3 32 &&
	test_when_finished "rm -fr test-bin funny.was.run" &&
	mkdir test-bin &&
	cat >test-bin/but-merge-funny <<-EOF &&
	#!$SHELL_PATH
	case "\$1" in --opt) ;; *) exit 2 ;; esac
	shift &&
	>funny.was.run &&
	exec but merge-recursive "\$@"
	EOF
	chmod +x test-bin/but-merge-funny &&
	(
		PATH=./test-bin:$PATH &&
		test_must_fail but rebase -s funny -Xopt main topic
	) &&
	test -f funny.was.run &&
	rm funny.was.run &&
	echo "Resolved" >F2 &&
	but add F2 &&
	(
		PATH=./test-bin:$PATH &&
		but rebase --continue
	) &&
	test -f funny.was.run
'

test_expect_success 'rebase -i --continue handles merge strategy and options' '
	rm -fr .but/rebase-* &&
	but reset --hard cummit-new-file-F2-on-topic-branch &&
	test_cummit "cummit-new-file-F3-on-topic-branch-for-dash-i" F3 32 &&
	test_when_finished "rm -fr test-bin funny.was.run funny.args" &&
	mkdir test-bin &&
	cat >test-bin/but-merge-funny <<-EOF &&
	#!$SHELL_PATH
	echo "\$@" >>funny.args
	case "\$1" in --opt) ;; *) exit 2 ;; esac
	case "\$2" in --foo) ;; *) exit 2 ;; esac
	case "\$4" in --) ;; *) exit 2 ;; esac
	shift 2 &&
	>funny.was.run &&
	exec but merge-recursive "\$@"
	EOF
	chmod +x test-bin/but-merge-funny &&
	(
		PATH=./test-bin:$PATH &&
		test_must_fail but rebase -i -s funny -Xopt -Xfoo main topic
	) &&
	test -f funny.was.run &&
	rm funny.was.run &&
	echo "Resolved" >F2 &&
	but add F2 &&
	(
		PATH=./test-bin:$PATH &&
		but rebase --continue
	) &&
	test -f funny.was.run
'

test_expect_success 'rebase -r passes merge strategy options correctly' '
	rm -fr .but/rebase-* &&
	but reset --hard cummit-new-file-F3-on-topic-branch &&
	test_cummit merge-theirs &&
	but reset --hard HEAD^ &&
	test_cummit some-other-cummit &&
	test_tick &&
	but merge --no-ff merge-theirs &&
	FAKE_LINES="1 3 edit 4 5 7 8 9" but rebase -i -f -r -m \
		-s recursive --strategy-option=theirs HEAD~2 &&
	test_cummit force-change-ours &&
	but rebase --continue
'

test_expect_success '--skip after failed fixup cleans cummit message' '
	test_when_finished "test_might_fail but rebase --abort" &&
	but checkout -b with-conflicting-fixup &&
	test_cummit wants-fixup &&
	test_cummit "fixup! wants-fixup" wants-fixup.t 1 wants-fixup-1 &&
	test_cummit "fixup! wants-fixup" wants-fixup.t 2 wants-fixup-2 &&
	test_cummit "fixup! wants-fixup" wants-fixup.t 3 wants-fixup-3 &&
	test_must_fail env FAKE_LINES="1 fixup 2 squash 4" \
		but rebase -i HEAD~4 &&

	: now there is a conflict, and comments in the cummit message &&
	but show HEAD >out &&
	grep "fixup! wants-fixup" out &&

	: skip and continue &&
	echo "cp \"\$1\" .but/copy.txt" | write_script copy-editor.sh &&
	(test_set_editor "$PWD/copy-editor.sh" && but rebase --skip) &&

	: the user should not have had to edit the cummit message &&
	test_path_is_missing .but/copy.txt &&

	: now the comments in the cummit message should have been cleaned up &&
	but show HEAD >out &&
	! grep "fixup! wants-fixup" out &&

	: now, let us ensure that "squash" is handled correctly &&
	but reset --hard wants-fixup-3 &&
	test_must_fail env FAKE_LINES="1 squash 4 squash 2 squash 4" \
		but rebase -i HEAD~4 &&

	: the first squash failed, but there are two more in the chain &&
	(test_set_editor "$PWD/copy-editor.sh" &&
	 test_must_fail but rebase --skip) &&

	: not the final squash, no need to edit the cummit message &&
	test_path_is_missing .but/copy.txt &&

	: The first squash was skipped, therefore: &&
	but show HEAD >out &&
	test_i18ngrep "# This is a combination of 2 cummits" out &&
	test_i18ngrep "# This is the cummit message #2:" out &&

	(test_set_editor "$PWD/copy-editor.sh" && but rebase --skip) &&
	but show HEAD >out &&
	test_i18ngrep ! "# This is a combination" out &&

	: Final squash failed, but there was still a squash &&
	test_i18ngrep "# This is a combination of 2 cummits" .but/copy.txt &&
	test_i18ngrep "# This is the cummit message #2:" .but/copy.txt
'

test_expect_success 'setup rerere database' '
	rm -fr .but/rebase-* &&
	but reset --hard cummit-new-file-F3-on-topic-branch &&
	but checkout main &&
	test_cummit "cummit-new-file-F3" F3 3 &&
	test_config rerere.enabled true &&
	but update-ref refs/heads/topic cummit-new-file-F3-on-topic-branch &&
	test_must_fail but rebase -m main topic &&
	echo "Resolved" >F2 &&
	cp F2 expected-F2 &&
	but add F2 &&
	test_must_fail but rebase --continue &&
	echo "Resolved" >F3 &&
	cp F3 expected-F3 &&
	but add F3 &&
	but rebase --continue &&
	but reset --hard topic@{1}
'

prepare () {
	rm -fr .but/rebase-* &&
	but reset --hard cummit-new-file-F3-on-topic-branch &&
	but checkout main &&
	test_config rerere.enabled true
}

test_rerere_autoupdate () {
	action=$1 &&
	test_expect_success "rebase $action --continue remembers --rerere-autoupdate" '
		prepare &&
		test_must_fail but rebase $action --rerere-autoupdate main topic &&
		test_cmp expected-F2 F2 &&
		but diff-files --quiet &&
		test_must_fail but rebase --continue &&
		test_cmp expected-F3 F3 &&
		but diff-files --quiet &&
		but rebase --continue
	'

	test_expect_success "rebase $action --continue honors rerere.autoUpdate" '
		prepare &&
		test_config rerere.autoupdate true &&
		test_must_fail but rebase $action main topic &&
		test_cmp expected-F2 F2 &&
		but diff-files --quiet &&
		test_must_fail but rebase --continue &&
		test_cmp expected-F3 F3 &&
		but diff-files --quiet &&
		but rebase --continue
	'

	test_expect_success "rebase $action --continue remembers --no-rerere-autoupdate" '
		prepare &&
		test_config rerere.autoupdate true &&
		test_must_fail but rebase $action --no-rerere-autoupdate main topic &&
		test_cmp expected-F2 F2 &&
		test_must_fail but diff-files --quiet &&
		but add F2 &&
		test_must_fail but rebase --continue &&
		test_cmp expected-F3 F3 &&
		test_must_fail but diff-files --quiet &&
		but add F3 &&
		but rebase --continue
	'
}

test_rerere_autoupdate --apply
test_rerere_autoupdate -m
GIT_SEQUENCE_EDITOR=: && export GIT_SEQUENCE_EDITOR
test_rerere_autoupdate -i
unset GIT_SEQUENCE_EDITOR

test_expect_success 'the todo command "break" works' '
	rm -f execed &&
	FAKE_LINES="break b exec_>execed" but rebase -i HEAD &&
	test_path_is_missing execed &&
	but rebase --continue &&
	test_path_is_missing execed &&
	but rebase --continue &&
	test_path_is_file execed
'

test_expect_success '--reschedule-failed-exec' '
	test_when_finished "but rebase --abort" &&
	test_must_fail but rebase -x false --reschedule-failed-exec HEAD^ &&
	grep "^exec false" .but/rebase-merge/but-rebase-todo &&
	but rebase --abort &&
	test_must_fail but -c rebase.rescheduleFailedExec=true \
		rebase -x false HEAD^ 2>err &&
	grep "^exec false" .but/rebase-merge/but-rebase-todo &&
	test_i18ngrep "has been rescheduled" err
'

test_expect_success 'rebase.rescheduleFailedExec only affects `rebase -i`' '
	test_config rebase.rescheduleFailedExec true &&
	test_must_fail but rebase -x false HEAD^ &&
	grep "^exec false" .but/rebase-merge/but-rebase-todo &&
	but rebase --abort &&
	but rebase HEAD^
'

test_expect_success 'rebase.rescheduleFailedExec=true & --no-reschedule-failed-exec' '
	test_when_finished "but rebase --abort" &&
	test_config rebase.rescheduleFailedExec true &&
	test_must_fail but rebase -x false --no-reschedule-failed-exec HEAD~2 &&
	test_must_fail but rebase --continue 2>err &&
	! grep "has been rescheduled" err
'

test_expect_success 'new rebase.rescheduleFailedExec=true setting in an ongoing rebase is ignored' '
	test_when_finished "but rebase --abort" &&
	test_must_fail but rebase -x false HEAD~2 &&
	test_config rebase.rescheduleFailedExec true &&
	test_must_fail but rebase --continue 2>err &&
	! grep "has been rescheduled" err
'

test_expect_success 'there is no --no-reschedule-failed-exec in an ongoing rebase' '
	test_when_finished "but rebase --abort" &&
	test_must_fail but rebase -x false HEAD~2 &&
	test_expect_code 129 but rebase --continue --no-reschedule-failed-exec &&
	test_expect_code 129 but rebase --edit-todo --no-reschedule-failed-exec
'

test_orig_head_helper () {
	test_when_finished 'but rebase --abort &&
		but checkout topic &&
		but reset --hard cummit-new-file-F2-on-topic-branch' &&
	but update-ref -d ORIG_HEAD &&
	test_must_fail but rebase "$@" &&
	test_cmp_rev ORIG_HEAD cummit-new-file-F2-on-topic-branch
}

test_orig_head () {
	type=$1
	test_expect_success "rebase $type sets ORIG_HEAD correctly" '
		but checkout topic &&
		but reset --hard cummit-new-file-F2-on-topic-branch &&
		test_orig_head_helper $type main
	'

	test_expect_success "rebase $type <upstream> <branch> sets ORIG_HEAD correctly" '
		but checkout main &&
		test_orig_head_helper $type main topic
	'
}

test_orig_head --apply
test_orig_head --merge

test_done
