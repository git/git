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
	test-chmtime =-60 F1 &&
	git rebase --continue
'

test_expect_success 'non-interactive rebase --continue works with touched file' '
	rm -fr .git/rebase-* &&
	git reset --hard &&
	git checkout master &&

	test_must_fail git rebase --onto master master topic &&
	echo "Resolved" >F2 &&
	git add F2 &&
	test-chmtime =-60 F1 &&
	git rebase --continue
'

test_expect_success 'non-interactive rebase --continue with rerere enabled' '
	test_config rerere.enabled true &&
	test_when_finished "test_might_fail git rebase --abort" &&
	git reset --hard commit-new-file-F2-on-topic-branch &&
	git checkout master &&
	rm -fr .git/rebase-* &&

	test_must_fail git rebase --onto master master topic &&
	echo "Resolved" >F2 &&
	git add F2 &&
	cp F2 F2.expected &&
	git rebase --continue &&

	git reset --hard commit-new-file-F2-on-topic-branch &&
	git checkout master &&
	test_must_fail git rebase --onto master master topic &&
	test_cmp F2.expected F2
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
		PATH=./test-bin$PATH_SEP$PATH
		test_must_fail git rebase -s funny -Xopt master topic
	) &&
	test -f funny.was.run &&
	rm funny.was.run &&
	echo "Resolved" >F2 &&
	git add F2 &&
	(
		PATH=./test-bin$PATH_SEP$PATH
		git rebase --continue
	) &&
	test -f funny.was.run
'

test_expect_success 'rebase --continue remembers --rerere-autoupdate' '
	rm -fr .git/rebase-* &&
	git reset --hard commit-new-file-F3-on-topic-branch &&
	git checkout master &&
	test_commit "commit-new-file-F3" F3 3 &&
	git config rerere.enabled true &&
	test_must_fail git rebase -m master topic &&
	echo "Resolved" >F2 &&
	git add F2 &&
	test_must_fail git rebase --continue &&
	echo "Resolved" >F3 &&
	git add F3 &&
	git rebase --continue &&
	git reset --hard topic@{1} &&
	test_must_fail git rebase -m --rerere-autoupdate master &&
	test "$(cat F2)" = "Resolved" &&
	test_must_fail git rebase --continue &&
	test "$(cat F3)" = "Resolved" &&
	git rebase --continue
'

test_done
