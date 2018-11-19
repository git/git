#!/bin/sh

test_description='git rebase --continue tests'

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

set_fake_editor

test_expect_success 'setup' '
	test_commit "commit-new-file-F1" F1 1 &&
	test_commit "commit-new-file-F2" F2 2 &&

	git checkout -b topic HEAD^ &&
	test_commit "commit-new-file-F2-on-topic-branch" F2 22 &&

	git checkout master
'

test_expect_success 'interactive rebase --continue works with touched file' '
	rm -fr .git/rebase-* &&
	git reset --hard &&
	git checkout master &&

	FAKE_LINES="edit 1" git rebase -i HEAD^ &&
	test-tool chmtime =-60 F1 &&
	git rebase --continue
'

test_expect_success 'non-interactive rebase --continue works with touched file' '
	rm -fr .git/rebase-* &&
	git reset --hard &&
	git checkout master &&

	test_must_fail git rebase --onto master master topic &&
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
	test_when_finished "rm -fr test-bin funny.was.run" &&
	mkdir test-bin &&
	cat >test-bin/git-merge-funny <<-EOF &&
	#!$SHELL_PATH
	case "\$1" in --opt) ;; *) exit 2 ;; esac
	shift &&
	>funny.was.run &&
	exec git merge-recursive "\$@"
	EOF
	chmod +x test-bin/git-merge-funny &&
	(
		PATH=./test-bin$PATH_SEP$PATH &&
		test_must_fail git rebase -s funny -Xopt master topic
	) &&
	test -f funny.was.run &&
	rm funny.was.run &&
	echo "Resolved" >F2 &&
	git add F2 &&
	(
		PATH=./test-bin$PATH_SEP$PATH &&
		git rebase --continue
	) &&
	test -f funny.was.run
'

test_expect_success 'rebase -i --continue handles merge strategy and options' '
	rm -fr .git/rebase-* &&
	git reset --hard commit-new-file-F2-on-topic-branch &&
	test_commit "commit-new-file-F3-on-topic-branch-for-dash-i" F3 32 &&
	test_when_finished "rm -fr test-bin funny.was.run funny.args" &&
	mkdir test-bin &&
	cat >test-bin/git-merge-funny <<-EOF &&
	#!$SHELL_PATH
	echo "\$@" >>funny.args
	case "\$1" in --opt) ;; *) exit 2 ;; esac
	case "\$2" in --foo) ;; *) exit 2 ;; esac
	case "\$4" in --) ;; *) exit 2 ;; esac
	shift 2 &&
	>funny.was.run &&
	exec git merge-recursive "\$@"
	EOF
	chmod +x test-bin/git-merge-funny &&
	(
		PATH=./test-bin$PATH_SEP$PATH &&
		test_must_fail git rebase -i -s funny -Xopt -Xfoo master topic
	) &&
	test -f funny.was.run &&
	rm funny.was.run &&
	echo "Resolved" >F2 &&
	git add F2 &&
	(
		PATH=./test-bin$PATH_SEP$PATH &&
		git rebase --continue
	) &&
	test -f funny.was.run
'

test_expect_success REBASE_P 'rebase passes merge strategy options correctly' '
	rm -fr .git/rebase-* &&
	git reset --hard commit-new-file-F3-on-topic-branch &&
	test_commit theirs-to-merge &&
	git reset --hard HEAD^ &&
	test_commit some-commit &&
	test_tick &&
	git merge --no-ff theirs-to-merge &&
	FAKE_LINES="1 edit 2 3" git rebase -i -f -p -m \
		-s recursive --strategy-option=theirs HEAD~2 &&
	test_commit force-change &&
	git rebase --continue
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
	test_commit "fixup! wants-fixup" wants-fixup.t 1 wants-fixup-1 &&
	test_commit "fixup! wants-fixup" wants-fixup.t 2 wants-fixup-2 &&
	test_commit "fixup! wants-fixup" wants-fixup.t 3 wants-fixup-3 &&
	test_must_fail env FAKE_LINES="1 fixup 2 squash 4" \
		git rebase -i HEAD~4 &&

	: now there is a conflict, and comments in the commit message &&
	git show HEAD >out &&
	grep "fixup! wants-fixup" out &&

	: skip and continue &&
	echo "cp \"\$1\" .git/copy.txt" | write_script copy-editor.sh &&
	(test_set_editor "$PWD/copy-editor.sh" && git rebase --skip) &&

	: the user should not have had to edit the commit message &&
	test_path_is_missing .git/copy.txt &&

	: now the comments in the commit message should have been cleaned up &&
	git show HEAD >out &&
	! grep "fixup! wants-fixup" out &&

	: now, let us ensure that "squash" is handled correctly &&
	git reset --hard wants-fixup-3 &&
	test_must_fail env FAKE_LINES="1 squash 4 squash 2 squash 4" \
		git rebase -i HEAD~4 &&

	: the first squash failed, but there are two more in the chain &&
	(test_set_editor "$PWD/copy-editor.sh" &&
	 test_must_fail git rebase --skip) &&

	: not the final squash, no need to edit the commit message &&
	test_path_is_missing .git/copy.txt &&

	: The first squash was skipped, therefore: &&
	git show HEAD >out &&
	test_i18ngrep "# This is a combination of 2 commits" out &&
	test_i18ngrep "# This is the commit message #2:" out &&

	(test_set_editor "$PWD/copy-editor.sh" && git rebase --skip) &&
	git show HEAD >out &&
	test_i18ngrep ! "# This is a combination" out &&

	: Final squash failed, but there was still a squash &&
	test_i18ngrep "# This is a combination of 2 commits" .git/copy.txt &&
	test_i18ngrep "# This is the commit message #2:" .git/copy.txt
'

test_expect_success 'setup rerere database' '
	rm -fr .git/rebase-* &&
	git reset --hard commit-new-file-F3-on-topic-branch &&
	git checkout master &&
	test_commit "commit-new-file-F3" F3 3 &&
	test_config rerere.enabled true &&
	git update-ref refs/heads/topic commit-new-file-F3-on-topic-branch &&
	test_must_fail git rebase -m master topic &&
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
	git checkout master &&
	test_config rerere.enabled true
}

test_rerere_autoupdate () {
	action=$1 &&
	test_expect_success "rebase $action --continue remembers --rerere-autoupdate" '
		prepare &&
		test_must_fail git rebase $action --rerere-autoupdate master topic &&
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
		test_must_fail git rebase $action master topic &&
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
		test_must_fail git rebase $action --no-rerere-autoupdate master topic &&
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

test_rerere_autoupdate
test_rerere_autoupdate -m
GIT_SEQUENCE_EDITOR=: && export GIT_SEQUENCE_EDITOR
test_rerere_autoupdate -i
test_have_prereq !REBASE_P || test_rerere_autoupdate --preserve-merges
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

test_expect_success '--reschedule-failed-exec' '
	test_when_finished "git rebase --abort" &&
	test_must_fail git rebase -x false --reschedule-failed-exec HEAD^ &&
	grep "^exec false" .git/rebase-merge/git-rebase-todo &&
	git rebase --abort &&
	test_must_fail git -c rebase.rescheduleFailedExec=true \
		rebase -x false HEAD^ 2>err &&
	grep "^exec false" .git/rebase-merge/git-rebase-todo &&
	test_i18ngrep "has been rescheduled" err
'

test_expect_success 'rebase.reschedulefailedexec only affects `rebase -i`' '
	test_config rebase.reschedulefailedexec true &&
	test_must_fail git rebase -x false HEAD^ &&
	grep "^exec false" .git/rebase-merge/git-rebase-todo &&
	git rebase --abort &&
	git rebase HEAD^
'

test_done
