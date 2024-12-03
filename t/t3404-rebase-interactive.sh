#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='git rebase interactive

This test runs git rebase "interactively", by faking an edit, and verifies
that the result still makes sense.

Initial setup:

     one - two - three - four (conflict-branch)
   /
 A - B - C - D - E            (primary)
 | \
 |   F - G - H                (branch1)
 |     \
 |\      I                    (branch2)
 | \
 |   J - K - L - M            (no-conflict-branch)
  \
    N - O - P                 (no-ff-branch)

 where A, B, D and G all touch file1, and one, two, three, four all
 touch file "conflict".
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

test_expect_success 'setup' '
	git switch -C primary &&
	test_commit A file1 &&
	test_commit B file1 &&
	test_commit C file2 &&
	test_commit D file1 &&
	test_commit E file3 &&
	git checkout -b branch1 A &&
	test_commit F file4 &&
	test_commit G file1 &&
	test_commit H file5 &&
	git checkout -b branch2 F &&
	test_commit I file6 &&
	git checkout -b conflict-branch A &&
	test_commit one conflict &&
	test_commit two conflict &&
	test_commit three conflict &&
	test_commit four conflict &&
	git checkout -b no-conflict-branch A &&
	test_commit J fileJ &&
	test_commit K fileK &&
	test_commit L fileL &&
	test_commit M fileM &&
	git checkout -b no-ff-branch A &&
	test_commit N fileN &&
	test_commit O fileO &&
	test_commit P fileP
'

# "exec" commands are run with the user shell by default, but this may
# be non-POSIX. For example, if SHELL=zsh then ">file" doesn't work
# to create a file. Unsetting SHELL avoids such non-portable behavior
# in tests. It must be exported for it to take effect where needed.
SHELL=
export SHELL

test_expect_success 'rebase --keep-empty' '
	git checkout -b emptybranch primary &&
	git commit --allow-empty -m "empty" &&
	git rebase --keep-empty -i HEAD~2 &&
	git log --oneline >actual &&
	test_line_count = 6 actual
'

test_expect_success 'rebase -i with empty todo list' '
	cat >expect <<-\EOF &&
	error: nothing to do
	EOF
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="#" \
			git rebase -i HEAD^ >output 2>&1
	) &&
	tail -n 1 output >actual &&  # Ignore output about changing todo list
	test_cmp expect actual
'

test_expect_success 'rebase -i with the exec command' '
	git checkout primary &&
	(
	set_fake_editor &&
	FAKE_LINES="1 exec_>touch-one
		2 exec_>touch-two exec_false exec_>touch-three
		3 4 exec_>\"touch-file__name_with_spaces\";_>touch-after-semicolon 5" &&
	export FAKE_LINES &&
	test_must_fail git rebase -i A
	) &&
	test_path_is_file touch-one &&
	test_path_is_file touch-two &&
	# Missing because we should have stopped by now.
	test_path_is_missing touch-three &&
	test_cmp_rev C HEAD &&
	git rebase --continue &&
	test_path_is_file touch-three &&
	test_path_is_file "touch-file  name with spaces" &&
	test_path_is_file touch-after-semicolon &&
	test_cmp_rev primary HEAD &&
	rm -f touch-*
'

test_expect_success 'rebase -i with the exec command runs from tree root' '
	git checkout primary &&
	mkdir subdir && (cd subdir &&
	set_fake_editor &&
	FAKE_LINES="1 exec_>touch-subdir" \
		git rebase -i HEAD^
	) &&
	test_path_is_file touch-subdir &&
	rm -fr subdir
'

test_expect_success 'rebase -i with exec allows git commands in subdirs' '
	test_when_finished "rm -rf subdir" &&
	test_when_finished "git rebase --abort ||:" &&
	git checkout primary &&
	mkdir subdir && (cd subdir &&
	set_fake_editor &&
	FAKE_LINES="1 x_cd_subdir_&&_git_rev-parse_--is-inside-work-tree" \
		git rebase -i HEAD^
	)
'

test_expect_success 'rebase -i sets work tree properly' '
	test_when_finished "rm -rf subdir" &&
	test_when_finished "test_might_fail git rebase --abort" &&
	mkdir subdir &&
	git rebase -x "(cd subdir && git rev-parse --show-toplevel)" HEAD^ \
		>actual &&
	! grep "/subdir$" actual
'

test_expect_success 'rebase -i with the exec command checks tree cleanness' '
	git checkout primary &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="exec_echo_foo_>file1 1" \
			git rebase -i HEAD^
	) &&
	test_cmp_rev primary^ HEAD &&
	git reset --hard &&
	git rebase --continue
'

test_expect_success 'cherry-pick works with rebase --exec' '
	test_when_finished "git cherry-pick --abort; \
			    git rebase --abort; \
			    git checkout primary" &&
	echo "exec git cherry-pick G" >todo &&
	(
		set_replace_editor todo &&
		test_must_fail git rebase -i D D
	) &&
	test_cmp_rev G CHERRY_PICK_HEAD
'

test_expect_success 'rebase -x with empty command fails' '
	test_when_finished "git rebase --abort ||:" &&
	test_must_fail env git rebase -x "" @ 2>actual &&
	test_write_lines "error: empty exec command" >expected &&
	test_cmp expected actual &&
	test_must_fail env git rebase -x " " @ 2>actual &&
	test_cmp expected actual
'

test_expect_success 'rebase -x with newline in command fails' '
	test_when_finished "git rebase --abort ||:" &&
	test_must_fail env git rebase -x "a${LF}b" @ 2>actual &&
	test_write_lines "error: exec commands cannot contain newlines" \
			 >expected &&
	test_cmp expected actual
'

test_expect_success 'rebase -i with exec of inexistent command' '
	git checkout primary &&
	test_when_finished "git rebase --abort" &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="exec_this-command-does-not-exist 1" \
			git rebase -i HEAD^ >actual 2>&1
	) &&
	! grep "Maybe git-rebase is broken" actual
'

test_expect_success 'implicit interactive rebase does not invoke sequence editor' '
	test_when_finished "git rebase --abort ||:" &&
	GIT_SEQUENCE_EDITOR="echo bad >" git rebase -x"echo one" @^
'

test_expect_success 'no changes are a nop' '
	git checkout branch2 &&
	git rebase -i F &&
	test "$(git symbolic-ref -q HEAD)" = "refs/heads/branch2" &&
	test_cmp_rev I HEAD
'

test_expect_success 'test the [branch] option' '
	git checkout -b dead-end &&
	git rm file6 &&
	git commit -m "stop here" &&
	git rebase -i F branch2 &&
	test "$(git symbolic-ref -q HEAD)" = "refs/heads/branch2" &&
	test_cmp_rev I branch2 &&
	test_cmp_rev I HEAD
'

test_expect_success 'test --onto <branch>' '
	git checkout -b test-onto branch2 &&
	git rebase -i --onto branch1 F &&
	test "$(git symbolic-ref -q HEAD)" = "refs/heads/test-onto" &&
	test_cmp_rev HEAD^ branch1 &&
	test_cmp_rev I branch2
'

test_expect_success 'rebase on top of a non-conflicting commit' '
	git checkout branch1 &&
	git tag original-branch1 &&
	git rebase -i branch2 &&
	test file6 = $(git diff --name-only original-branch1) &&
	test "$(git symbolic-ref -q HEAD)" = "refs/heads/branch1" &&
	test_cmp_rev I branch2 &&
	test_cmp_rev I HEAD~2
'

test_expect_success 'reflog for the branch shows state before rebase' '
	test_cmp_rev branch1@{1} original-branch1
'

test_expect_success 'reflog for the branch shows correct finish message' '
	printf "rebase (finish): refs/heads/branch1 onto %s\n" \
		"$(git rev-parse branch2)" >expected &&
	git log -g --pretty=%gs -1 refs/heads/branch1 >actual &&
	test_cmp expected actual
'

test_expect_success 'exchange two commits' '
	(
		set_fake_editor &&
		FAKE_LINES="2 1" git rebase -i HEAD~2
	) &&
	test H = $(git cat-file commit HEAD^ | sed -ne \$p) &&
	test G = $(git cat-file commit HEAD | sed -ne \$p) &&
	blob1=$(git rev-parse --short HEAD^:file1) &&
	blob2=$(git rev-parse --short HEAD:file1) &&
	commit=$(git rev-parse --short HEAD)
'

test_expect_success 'stop on conflicting pick' '
	cat >expect <<-EOF &&
	diff --git a/file1 b/file1
	index $blob1..$blob2 100644
	--- a/file1
	+++ b/file1
	@@ -1 +1 @@
	-A
	+G
	EOF
	cat >expect2 <<-EOF &&
	<<<<<<< HEAD
	D
	=======
	G
	>>>>>>> $commit (G)
	EOF
	git tag new-branch1 &&
	test_must_fail git rebase -i primary &&
	test "$(git rev-parse HEAD~3)" = "$(git rev-parse primary)" &&
	test_cmp expect .git/rebase-merge/patch &&
	test_cmp expect2 file1 &&
	test "$(git diff --name-status |
		sed -n -e "/^U/s/^U[^a-z]*//p")" = file1 &&
	grep -v "^#" <.git/rebase-merge/done >actual &&
	test_line_count = 4 actual &&
	test 0 = $(grep -c "^[^#]" <.git/rebase-merge/git-rebase-todo)
'

test_expect_success 'show conflicted patch' '
	GIT_TRACE=1 git rebase --show-current-patch >/dev/null 2>stderr &&
	grep "show.*REBASE_HEAD" stderr &&
	# the original stopped-sha1 is abbreviated
	stopped_sha1="$(git rev-parse $(cat ".git/rebase-merge/stopped-sha"))" &&
	test "$(git rev-parse REBASE_HEAD)" = "$stopped_sha1"
'

test_expect_success 'abort' '
	git rebase --abort &&
	test_cmp_rev new-branch1 HEAD &&
	test "$(git symbolic-ref -q HEAD)" = "refs/heads/branch1" &&
	test_path_is_missing .git/rebase-merge
'

test_expect_success 'abort with error when new base cannot be checked out' '
	git rm --cached file1 &&
	git commit -m "remove file in base" &&
	test_must_fail git rebase -i primary > output 2>&1 &&
	test_grep "The following untracked working tree files would be overwritten by checkout:" \
		output &&
	test_grep "file1" output &&
	test_path_is_missing .git/rebase-merge &&
	rm file1 &&
	git reset --hard HEAD^
'

test_expect_success 'retain authorship' '
	echo A > file7 &&
	git add file7 &&
	test_tick &&
	GIT_AUTHOR_NAME="Twerp Snog" git commit -m "different author" &&
	git tag twerp &&
	git rebase -i --onto primary HEAD^ &&
	git show HEAD >actual &&
	grep "^Author: Twerp Snog" actual
'

test_expect_success 'retain authorship w/ conflicts' '
	oGIT_AUTHOR_NAME=$GIT_AUTHOR_NAME &&
	test_when_finished "GIT_AUTHOR_NAME=\$oGIT_AUTHOR_NAME" &&

	git reset --hard twerp &&
	test_commit a conflict a conflict-a &&
	git reset --hard twerp &&

	GIT_AUTHOR_NAME=AttributeMe &&
	export GIT_AUTHOR_NAME &&
	test_commit b conflict b conflict-b &&
	GIT_AUTHOR_NAME=$oGIT_AUTHOR_NAME &&

	test_must_fail git rebase -i conflict-a &&
	echo resolved >conflict &&
	git add conflict &&
	git rebase --continue &&
	test_cmp_rev conflict-a^0 HEAD^ &&
	git show >out &&
	grep AttributeMe out
'

test_expect_success 'squash' '
	git reset --hard twerp &&
	echo B > file7 &&
	test_tick &&
	GIT_AUTHOR_NAME="Nitfol" git commit -m "nitfol" file7 &&
	echo "******************************" &&
	(
		set_fake_editor &&
		FAKE_LINES="1 squash 2" EXPECT_HEADER_COUNT=2 \
			git rebase -i --onto primary HEAD~2
	) &&
	test B = $(cat file7) &&
	test_cmp_rev HEAD^ primary
'

test_expect_success 'retain authorship when squashing' '
	git show HEAD >actual &&
	grep "^Author: Twerp Snog" actual
'

test_expect_success '--continue tries to commit' '
	git reset --hard D &&
	test_tick &&
	(
		set_fake_editor &&
		test_must_fail git rebase -i --onto new-branch1 HEAD^ &&
		echo resolved > file1 &&
		git add file1 &&
		FAKE_COMMIT_MESSAGE="chouette!" git rebase --continue
	) &&
	test_cmp_rev HEAD^ new-branch1 &&
	git show HEAD >actual &&
	grep chouette actual
'

test_expect_success 'verbose flag is heeded, even after --continue' '
	git reset --hard primary@{1} &&
	test_tick &&
	test_must_fail git rebase -v -i --onto new-branch1 HEAD^ &&
	echo resolved > file1 &&
	git add file1 &&
	git rebase --continue > output &&
	grep "^ file1 | 2 +-$" output
'

test_expect_success 'multi-squash only fires up editor once' '
	base=$(git rev-parse HEAD~4) &&
	(
		set_fake_editor &&
		FAKE_COMMIT_AMEND="ONCE" \
			FAKE_LINES="1 squash 2 squash 3 squash 4" \
			EXPECT_HEADER_COUNT=4 \
			git rebase -i $base
	) &&
	test $base = $(git rev-parse HEAD^) &&
	git show >output &&
	grep ONCE output >actual &&
	test_line_count = 1 actual
'

test_expect_success 'multi-fixup does not fire up editor' '
	git checkout -b multi-fixup E &&
	base=$(git rev-parse HEAD~4) &&
	(
		set_fake_editor &&
		FAKE_COMMIT_AMEND="NEVER" \
			FAKE_LINES="1 fixup 2 fixup 3 fixup 4" \
			git rebase -i $base
	) &&
	test $base = $(git rev-parse HEAD^) &&
	git show >output &&
	! grep NEVER output &&
	git checkout @{-1} &&
	git branch -D multi-fixup
'

test_expect_success 'commit message used after conflict' '
	git checkout -b conflict-fixup conflict-branch &&
	base=$(git rev-parse HEAD~4) &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 fixup 3 fixup 4" \
			git rebase -i $base &&
		echo three > conflict &&
		git add conflict &&
		FAKE_COMMIT_AMEND="ONCE" EXPECT_HEADER_COUNT=2 \
			git rebase --continue
	) &&
	test $base = $(git rev-parse HEAD^) &&
	git show >output &&
	grep ONCE output >actual &&
	test_line_count = 1 actual &&
	git checkout @{-1} &&
	git branch -D conflict-fixup
'

test_expect_success 'commit message retained after conflict' '
	git checkout -b conflict-squash conflict-branch &&
	base=$(git rev-parse HEAD~4) &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 fixup 3 squash 4" \
			git rebase -i $base &&
		echo three > conflict &&
		git add conflict &&
		FAKE_COMMIT_AMEND="TWICE" EXPECT_HEADER_COUNT=2 \
			git rebase --continue
	) &&
	test $base = $(git rev-parse HEAD^) &&
	git show >output &&
	grep TWICE output >actual &&
	test_line_count = 2 actual &&
	git checkout @{-1} &&
	git branch -D conflict-squash
'

test_expect_success 'squash and fixup generate correct log messages' '
	cat >expect-squash-fixup <<-\EOF &&
	B

	D

	ONCE
	EOF
	git checkout -b squash-fixup E &&
	base=$(git rev-parse HEAD~4) &&
	(
		set_fake_editor &&
		FAKE_COMMIT_AMEND="ONCE" \
			FAKE_LINES="1 fixup 2 squash 3 fixup 4" \
			EXPECT_HEADER_COUNT=4 \
			git rebase -i $base
	) &&
	git cat-file commit HEAD | sed -e 1,/^\$/d > actual-squash-fixup &&
	test_cmp expect-squash-fixup actual-squash-fixup &&
	git cat-file commit HEAD@{2} >actual &&
	grep "^# This is a combination of 3 commits\." actual &&
	git cat-file commit HEAD@{3} >actual &&
	grep "^# This is a combination of 2 commits\." actual  &&
	git checkout @{-1} &&
	git branch -D squash-fixup
'

test_expect_success 'squash ignores comments' '
	git checkout -b skip-comments E &&
	base=$(git rev-parse HEAD~4) &&
	(
		set_fake_editor &&
		FAKE_COMMIT_AMEND="ONCE" \
			FAKE_LINES="# 1 # squash 2 # squash 3 # squash 4 #" \
			EXPECT_HEADER_COUNT=4 \
			git rebase -i $base
	) &&
	test $base = $(git rev-parse HEAD^) &&
	git show >output &&
	grep ONCE output >actual &&
	test_line_count = 1 actual &&
	git checkout @{-1} &&
	git branch -D skip-comments
'

test_expect_success 'squash ignores blank lines' '
	git checkout -b skip-blank-lines E &&
	base=$(git rev-parse HEAD~4) &&
	(
		set_fake_editor &&
		FAKE_COMMIT_AMEND="ONCE" \
			FAKE_LINES="> 1 > squash 2 > squash 3 > squash 4 >" \
			EXPECT_HEADER_COUNT=4 \
			git rebase -i $base
	) &&
	test $base = $(git rev-parse HEAD^) &&
	git show >output &&
	grep ONCE output >actual &&
	test_line_count = 1 actual &&
	git checkout @{-1} &&
	git branch -D skip-blank-lines
'

test_expect_success 'squash works as expected' '
	git checkout -b squash-works no-conflict-branch &&
	one=$(git rev-parse HEAD~3) &&
	(
		set_fake_editor &&
		FAKE_LINES="1 s 3 2" EXPECT_HEADER_COUNT=2 git rebase -i HEAD~3
	) &&
	test $one = $(git rev-parse HEAD~2)
'

test_expect_success 'interrupted squash works as expected' '
	git checkout -b interrupted-squash conflict-branch &&
	one=$(git rev-parse HEAD~3) &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 squash 3 2" \
			git rebase -i HEAD~3
	) &&
	test_write_lines one two four > conflict &&
	git add conflict &&
	test_must_fail git rebase --continue &&
	echo resolved > conflict &&
	git add conflict &&
	git rebase --continue &&
	test $one = $(git rev-parse HEAD~2)
'

test_expect_success 'interrupted squash works as expected (case 2)' '
	git checkout -b interrupted-squash2 conflict-branch &&
	one=$(git rev-parse HEAD~3) &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="3 squash 1 2" \
			git rebase -i HEAD~3
	) &&
	test_write_lines one four > conflict &&
	git add conflict &&
	test_must_fail git rebase --continue &&
	test_write_lines one two four > conflict &&
	git add conflict &&
	test_must_fail git rebase --continue &&
	echo resolved > conflict &&
	git add conflict &&
	git rebase --continue &&
	test $one = $(git rev-parse HEAD~2)
'

test_expect_success '--continue tries to commit, even for "edit"' '
	echo unrelated > file7 &&
	git add file7 &&
	test_tick &&
	git commit -m "unrelated change" &&
	parent=$(git rev-parse HEAD^) &&
	test_tick &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1" git rebase -i HEAD^ &&
		echo edited > file7 &&
		git add file7 &&
		FAKE_COMMIT_MESSAGE="chouette!" git rebase --continue
	) &&
	test edited = $(git show HEAD:file7) &&
	git show HEAD >actual &&
	grep chouette actual &&
	test $parent = $(git rev-parse HEAD^)
'

test_expect_success 'aborted --continue does not squash commits after "edit"' '
	old=$(git rev-parse HEAD) &&
	test_tick &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1" git rebase -i HEAD^ &&
		echo "edited again" > file7 &&
		git add file7 &&
		test_must_fail env FAKE_COMMIT_MESSAGE=" " git rebase --continue
	) &&
	test $old = $(git rev-parse HEAD) &&
	git rebase --abort
'

test_expect_success 'auto-amend only edited commits after "edit"' '
	test_tick &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1" git rebase -i HEAD^ &&
		echo "edited again" > file7 &&
		git add file7 &&
		FAKE_COMMIT_MESSAGE="edited file7 again" git commit &&
		echo "and again" > file7 &&
		git add file7 &&
		test_tick &&
		test_must_fail env FAKE_COMMIT_MESSAGE="and again" \
			git rebase --continue
	) &&
	git rebase --abort
'

test_expect_success 'clean error after failed "exec"' '
	test_tick &&
	test_when_finished "git rebase --abort || :" &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 exec_false" git rebase -i HEAD^
	) &&
	echo "edited again" > file7 &&
	git add file7 &&
	test_must_fail git rebase --continue 2>error &&
	test_grep "you have staged changes in your working tree" error &&
	test_grep ! "could not open.*for reading" error
'

test_expect_success 'rebase a detached HEAD' '
	grandparent=$(git rev-parse HEAD~2) &&
	git checkout $(git rev-parse HEAD) &&
	test_tick &&
	(
		set_fake_editor &&
		FAKE_LINES="2 1" git rebase -i HEAD~2
	) &&
	test $grandparent = $(git rev-parse HEAD~2)
'

test_expect_success 'rebase a commit violating pre-commit' '
	test_hook pre-commit <<-\EOF &&
	test -z "$(git diff --cached --check)"
	EOF
	echo "monde! " >> file1 &&
	test_tick &&
	test_must_fail git commit -m doesnt-verify file1 &&
	git commit -m doesnt-verify --no-verify file1 &&
	test_tick &&
	(
		set_fake_editor &&
		FAKE_LINES=2 git rebase -i HEAD~2
	)
'

test_expect_success 'rebase with a file named HEAD in worktree' '
	git reset --hard &&
	git checkout -b branch3 A &&

	(
		GIT_AUTHOR_NAME="Squashed Away" &&
		export GIT_AUTHOR_NAME &&
		>HEAD &&
		git add HEAD &&
		git commit -m "Add head" &&
		>BODY &&
		git add BODY &&
		git commit -m "Add body"
	) &&

	(
		set_fake_editor &&
		FAKE_LINES="1 squash 2" git rebase -i @{-1}
	) &&
	test "$(git show -s --pretty=format:%an)" = "Squashed Away"

'

test_expect_success 'do "noop" when there is nothing to cherry-pick' '

	git checkout -b branch4 HEAD &&
	GIT_EDITOR=: git commit --amend \
		--author="Somebody else <somebody@else.com>" &&
	test $(git rev-parse branch3) != $(git rev-parse branch4) &&
	git rebase -i branch3 &&
	test_cmp_rev branch3 branch4

'

test_expect_success 'submodule rebase setup' '
	git checkout A &&
	mkdir sub &&
	(
		cd sub && git init && >elif &&
		git add elif && git commit -m "submodule initial"
	) &&
	echo 1 >file1 &&
	git add file1 sub &&
	test_tick &&
	git commit -m "One" &&
	echo 2 >file1 &&
	test_tick &&
	git commit -a -m "Two" &&
	(
		cd sub && echo 3 >elif &&
		git commit -a -m "submodule second"
	) &&
	test_tick &&
	git commit -a -m "Three changes submodule"
'

test_expect_success 'submodule rebase -i' '
	(
		set_fake_editor &&
		FAKE_LINES="1 squash 2 3" git rebase -i A
	)
'

test_expect_success 'submodule conflict setup' '
	git tag submodule-base &&
	git checkout HEAD^ &&
	(
		cd sub && git checkout HEAD^ && echo 4 >elif &&
		git add elif && git commit -m "submodule conflict"
	) &&
	git add sub &&
	test_tick &&
	git commit -m "Conflict in submodule" &&
	git tag submodule-topic
'

test_expect_success 'rebase -i continue with only submodule staged' '
	test_must_fail git rebase -i submodule-base &&
	git add sub &&
	git rebase --continue &&
	test $(git rev-parse submodule-base) != $(git rev-parse HEAD)
'

test_expect_success 'rebase -i continue with unstaged submodule' '
	git checkout submodule-topic &&
	git reset --hard &&
	test_must_fail git rebase -i submodule-base &&
	git reset &&
	git rebase --continue &&
	test_cmp_rev submodule-base HEAD
'

test_expect_success 'avoid unnecessary reset' '
	git checkout primary &&
	git reset --hard &&
	test-tool chmtime =123456789 file3 &&
	git update-index --refresh &&
	HEAD=$(git rev-parse HEAD) &&
	git rebase -i HEAD~4 &&
	test $HEAD = $(git rev-parse HEAD) &&
	MTIME=$(test-tool chmtime --get file3) &&
	test 123456789 = $MTIME
'

test_expect_success 'reword' '
	git checkout -b reword-branch primary &&
	(
		set_fake_editor &&
		FAKE_LINES="1 2 3 reword 4" FAKE_COMMIT_MESSAGE="E changed" \
			git rebase -i A &&
		git show HEAD >actual &&
		grep "E changed" actual &&
		test $(git rev-parse primary) != $(git rev-parse HEAD) &&
		test_cmp_rev primary^ HEAD^ &&
		FAKE_LINES="1 2 reword 3 4" FAKE_COMMIT_MESSAGE="D changed" \
			git rebase -i A &&
		git show HEAD^ >actual &&
		grep "D changed" actual &&
		FAKE_LINES="reword 1 2 3 4" FAKE_COMMIT_MESSAGE="B changed" \
			git rebase -i A &&
		git show HEAD~3 >actual &&
		grep "B changed" actual &&
		FAKE_LINES="1 r 2 pick 3 p 4" FAKE_COMMIT_MESSAGE="C changed" \
			git rebase -i A
	) &&
	git show HEAD~2 >actual &&
	grep "C changed" actual
'

test_expect_success 'no uncommitted changes when rewording and the todo list is reloaded' '
	git checkout E &&
	test_when_finished "git checkout @{-1}" &&
	(
		set_fake_editor &&
		GIT_SEQUENCE_EDITOR="\"$PWD/fake-editor.sh\"" &&
		export GIT_SEQUENCE_EDITOR &&
		set_reword_editor &&
		FAKE_LINES="reword 1 reword 2" git rebase -i C
	) &&
	check_reworded_commits D E
'

test_expect_success 'rebase -i can copy notes' '
	git config notes.rewrite.rebase true &&
	git config notes.rewriteRef "refs/notes/*" &&
	test_commit n1 &&
	test_commit n2 &&
	test_commit n3 &&
	git notes add -m"a note" n3 &&
	git rebase -i --onto n1 n2 &&
	test "a note" = "$(git notes show HEAD)"
'

test_expect_success 'rebase -i can copy notes over a fixup' '
	cat >expect <<-\EOF &&
	an earlier note

	a note
	EOF
	git reset --hard n3 &&
	git notes add -m"an earlier note" n2 &&
	(
		set_fake_editor &&
		GIT_NOTES_REWRITE_MODE=concatenate FAKE_LINES="1 f 2" \
			git rebase -i n1
	) &&
	git notes show > output &&
	test_cmp expect output
'

test_expect_success 'rebase while detaching HEAD' '
	git symbolic-ref HEAD &&
	grandparent=$(git rev-parse HEAD~2) &&
	test_tick &&
	(
		set_fake_editor &&
		FAKE_LINES="2 1" git rebase -i HEAD~2 HEAD^0
	) &&
	test $grandparent = $(git rev-parse HEAD~2) &&
	test_must_fail git symbolic-ref HEAD
'

test_tick # Ensure that the rebased commits get a different timestamp.
test_expect_success 'always cherry-pick with --no-ff' '
	git checkout no-ff-branch &&
	git tag original-no-ff-branch &&
	git rebase -i --no-ff A &&
	for p in 0 1 2
	do
		test ! $(git rev-parse HEAD~$p) = $(git rev-parse original-no-ff-branch~$p) &&
		git diff HEAD~$p original-no-ff-branch~$p > out &&
		test_must_be_empty out || return 1
	done &&
	test_cmp_rev HEAD~3 original-no-ff-branch~3 &&
	git diff HEAD~3 original-no-ff-branch~3 > out &&
	test_must_be_empty out
'

test_expect_success 'set up commits with funny messages' '
	git checkout -b funny A &&
	echo >>file1 &&
	test_tick &&
	git commit -a -m "end with slash\\" &&
	echo >>file1 &&
	test_tick &&
	git commit -a -m "something (\000) that looks like octal" &&
	echo >>file1 &&
	test_tick &&
	git commit -a -m "something (\n) that looks like a newline" &&
	echo >>file1 &&
	test_tick &&
	git commit -a -m "another commit"
'

test_expect_success 'rebase-i history with funny messages' '
	git rev-list A..funny >expect &&
	test_tick &&
	(
		set_fake_editor &&
		FAKE_LINES="1 2 3 4" git rebase -i A
	) &&
	git rev-list A.. >actual &&
	test_cmp expect actual
'

test_expect_success 'prepare for rebase -i --exec' '
	git checkout primary &&
	git checkout -b execute &&
	test_commit one_exec main.txt one_exec &&
	test_commit two_exec main.txt two_exec &&
	test_commit three_exec main.txt three_exec
'

test_expect_success 'running "git rebase -i --exec git show HEAD"' '
	(
		set_fake_editor &&
		git rebase -i --exec "git show HEAD" HEAD~2 >actual &&
		FAKE_LINES="1 exec_git_show_HEAD 2 exec_git_show_HEAD" &&
		export FAKE_LINES &&
		git rebase -i HEAD~2 >expect
	) &&
	sed -e "1,9d" expect >expected &&
	test_cmp expected actual
'

test_expect_success 'running "git rebase --exec git show HEAD -i"' '
	git reset --hard execute &&
	(
		set_fake_editor &&
		git rebase --exec "git show HEAD" -i HEAD~2 >actual &&
		FAKE_LINES="1 exec_git_show_HEAD 2 exec_git_show_HEAD" &&
		export FAKE_LINES &&
		git rebase -i HEAD~2 >expect
	) &&
	sed -e "1,9d" expect >expected &&
	test_cmp expected actual
'

test_expect_success 'running "git rebase -ix git show HEAD"' '
	git reset --hard execute &&
	(
		set_fake_editor &&
		git rebase -ix "git show HEAD" HEAD~2 >actual &&
		FAKE_LINES="1 exec_git_show_HEAD 2 exec_git_show_HEAD" &&
		export FAKE_LINES &&
		git rebase -i HEAD~2 >expect
	) &&
	sed -e "1,9d" expect >expected &&
	test_cmp expected actual
'


test_expect_success 'rebase -ix with several <CMD>' '
	git reset --hard execute &&
	(
		set_fake_editor &&
		git rebase -ix "git show HEAD; pwd" HEAD~2 >actual &&
		FAKE_LINES="1 exec_git_show_HEAD;_pwd 2 exec_git_show_HEAD;_pwd" &&
		export FAKE_LINES &&
		git rebase -i HEAD~2 >expect
	) &&
	sed -e "1,9d" expect >expected &&
	test_cmp expected actual
'

test_expect_success 'rebase -ix with several instances of --exec' '
	git reset --hard execute &&
	(
		set_fake_editor &&
		git rebase -i --exec "git show HEAD" --exec "pwd" HEAD~2 >actual &&
		FAKE_LINES="1 exec_git_show_HEAD exec_pwd 2
				exec_git_show_HEAD exec_pwd" &&
		export FAKE_LINES &&
		git rebase -i HEAD~2 >expect
	) &&
	sed -e "1,11d" expect >expected &&
	test_cmp expected actual
'

test_expect_success 'rebase -ix with --autosquash' '
	git reset --hard execute &&
	git checkout -b autosquash &&
	echo second >second.txt &&
	git add second.txt &&
	git commit -m "fixup! two_exec" &&
	echo bis >bis.txt &&
	git add bis.txt &&
	git commit -m "fixup! two_exec" &&
	git checkout -b autosquash_actual &&
	git rebase -i --exec "git show HEAD" --autosquash HEAD~4 >actual &&
	git checkout autosquash &&
	(
		set_fake_editor &&
		git checkout -b autosquash_expected &&
		FAKE_LINES="1 fixup 3 fixup 4 exec_git_show_HEAD 2 exec_git_show_HEAD" &&
		export FAKE_LINES &&
		git rebase -i HEAD~4 >expect
	) &&
	sed -e "1,13d" expect >expected &&
	test_cmp expected actual
'

test_expect_success 'rebase --exec works without -i ' '
	git reset --hard execute &&
	rm -rf exec_output &&
	EDITOR="echo >invoked_editor" git rebase --exec "echo a line >>exec_output"  HEAD~2 2>actual &&
	test_grep  "Successfully rebased and updated" actual &&
	test_line_count = 2 exec_output &&
	test_path_is_missing invoked_editor
'

test_expect_success 'rebase -i --exec without <CMD>' '
	git reset --hard execute &&
	test_must_fail git rebase -i --exec 2>actual &&
	test_grep "requires a value" actual &&
	git checkout primary
'

test_expect_success 'rebase -i --root re-order and drop commits' '
	git checkout E &&
	(
		set_fake_editor &&
		FAKE_LINES="3 1 2 5" git rebase -i --root
	) &&
	test E = $(git cat-file commit HEAD | sed -ne \$p) &&
	test B = $(git cat-file commit HEAD^ | sed -ne \$p) &&
	test A = $(git cat-file commit HEAD^^ | sed -ne \$p) &&
	test C = $(git cat-file commit HEAD^^^ | sed -ne \$p) &&
	test 0 = $(git cat-file commit HEAD^^^ | grep -c ^parent\ )
'

test_expect_success 'rebase -i --root retain root commit author and message' '
	git checkout A &&
	echo B >file7 &&
	git add file7 &&
	GIT_AUTHOR_NAME="Twerp Snog" git commit -m "different author" &&
	(
		set_fake_editor &&
		FAKE_LINES="2" git rebase -i --root
	) &&
	git cat-file commit HEAD >output &&
	grep -q "^author Twerp Snog" output &&
	git cat-file commit HEAD >actual &&
	grep -q "^different author$" actual
'

test_expect_success 'rebase -i --root temporary sentinel commit' '
	git checkout B &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="2" git rebase -i --root
	) &&
	git cat-file commit HEAD >actual &&
	grep "^tree $EMPTY_TREE" actual &&
	git rebase --abort
'

test_expect_success 'rebase -i --root fixup root commit' '
	git checkout B &&
	(
		set_fake_editor &&
		FAKE_LINES="1 fixup 2" git rebase -i --root
	) &&
	test A = $(git cat-file commit HEAD | sed -ne \$p) &&
	test B = $(git show HEAD:file1) &&
	test 0 = $(git cat-file commit HEAD | grep -c ^parent\ )
'

test_expect_success 'rebase -i --root reword original root commit' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout -b reword-original-root-branch primary &&
	(
		set_fake_editor &&
		FAKE_LINES="reword 1 2" FAKE_COMMIT_MESSAGE="A changed" \
			git rebase -i --root
	) &&
	git show HEAD^ >actual &&
	grep "A changed" actual &&
	test -z "$(git show -s --format=%p HEAD^)"
'

test_expect_success 'rebase -i --root reword new root commit' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout -b reword-now-root-branch primary &&
	(
		set_fake_editor &&
		FAKE_LINES="reword 3 1" FAKE_COMMIT_MESSAGE="C changed" \
		git rebase -i --root
	) &&
	git show HEAD^ >actual &&
	grep "C changed" actual &&
	test -z "$(git show -s --format=%p HEAD^)"
'

test_expect_success 'rebase -i --root when root has untracked file conflict' '
	test_when_finished "reset_rebase" &&
	git checkout -b failing-root-pick A &&
	echo x >file2 &&
	git rm file1 &&
	git commit -m "remove file 1 add file 2" &&
	echo z >file1 &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 2" git rebase -i --root
	) &&
	rm file1 &&
	git rebase --continue &&
	test "$(git log -1 --format=%B)" = "remove file 1 add file 2" &&
	test "$(git rev-list --count HEAD)" = 2
'

test_expect_success 'rebase -i --root reword root when root has untracked file conflict' '
	test_when_finished "reset_rebase" &&
	echo z>file1 &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="reword 1 2" \
			FAKE_COMMIT_MESSAGE="Modified A" git rebase -i --root &&
		rm file1 &&
		FAKE_COMMIT_MESSAGE="Reworded A" git rebase --continue
	) &&
	test "$(git log -1 --format=%B HEAD^)" = "Reworded A" &&
	test "$(git rev-list --count HEAD)" = 2
'

test_expect_success 'rebase --edit-todo does not work on non-interactive rebase' '
	git checkout reword-original-root-branch &&
	git reset --hard &&
	git checkout conflict-branch &&
	(
		set_fake_editor &&
		test_must_fail git rebase -f --apply --onto HEAD~2 HEAD~ &&
		test_must_fail git rebase --edit-todo
	) &&
	git rebase --abort
'

test_expect_success 'rebase --edit-todo can be used to modify todo' '
	git reset --hard &&
	git checkout no-conflict-branch^0 &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1 2 3" git rebase -i HEAD~3 &&
		FAKE_LINES="2 1" git rebase --edit-todo &&
		git rebase --continue
	) &&
	test M = $(git cat-file commit HEAD^ | sed -ne \$p) &&
	test L = $(git cat-file commit HEAD | sed -ne \$p)
'

test_expect_success 'rebase -i produces readable reflog' '
	git reset --hard &&
	git branch -f branch-reflog-test H &&
	git rebase -i --onto I F branch-reflog-test &&
	cat >expect <<-\EOF &&
	rebase (finish): returning to refs/heads/branch-reflog-test
	rebase (pick): H
	rebase (pick): G
	rebase (start): checkout I
	EOF
	git reflog -n4 HEAD |
	sed "s/[^:]*: //" >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase -i respects core.commentchar' '
	git reset --hard &&
	git checkout E^0 &&
	test_config core.commentchar "\\" &&
	write_script remove-all-but-first.sh <<-\EOF &&
	sed -e "2,\$s/^/\\\\/" "$1" >"$1.tmp" &&
	mv "$1.tmp" "$1"
	EOF
	(
		test_set_editor "$(pwd)/remove-all-but-first.sh" &&
		git rebase -i B
	) &&
	test B = $(git cat-file commit HEAD^ | sed -ne \$p)
'

test_expect_success 'rebase -i respects core.commentchar=auto' '
	test_config core.commentchar auto &&
	write_script copy-edit-script.sh <<-\EOF &&
	cp "$1" edit-script
	EOF
	test_when_finished "git rebase --abort || :" &&
	(
		test_set_editor "$(pwd)/copy-edit-script.sh" &&
		git rebase -i HEAD^
	) &&
	test -z "$(grep -ve "^#" -e "^\$" -e "^pick" edit-script)"
'

test_expect_success 'rebase -i, with <onto> and <upstream> specified as :/quuxery' '
	test_when_finished "git branch -D torebase" &&
	git checkout -b torebase branch1 &&
	upstream=$(git rev-parse ":/J") &&
	onto=$(git rev-parse ":/A") &&
	git rebase --onto $onto $upstream &&
	git reset --hard branch1 &&
	git rebase --onto ":/A" ":/J" &&
	git checkout branch1
'

test_expect_success 'rebase -i with --strategy and -X' '
	git checkout -b conflict-merge-use-theirs conflict-branch &&
	git reset --hard HEAD^ &&
	echo five >conflict &&
	echo Z >file1 &&
	git commit -a -m "one file conflict" &&
	EDITOR=true git rebase -i --strategy=recursive -Xours conflict-branch &&
	test $(git show conflict-branch:conflict) = $(cat conflict) &&
	test $(cat file1) = Z
'

test_expect_success 'interrupted rebase -i with --strategy and -X' '
	git checkout -b conflict-merge-use-theirs-interrupted conflict-branch &&
	git reset --hard HEAD^ &&
	>breakpoint &&
	git add breakpoint &&
	git commit -m "breakpoint for interactive mode" &&
	echo five >conflict &&
	echo Z >file1 &&
	git commit -a -m "one file conflict" &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1 2" git rebase -i --strategy=recursive \
			-Xours conflict-branch
	) &&
	git rebase --continue &&
	test $(git show conflict-branch:conflict) = $(cat conflict) &&
	test $(cat file1) = Z
'

test_expect_success 'rebase -i error on commits with \ in message' '
	current_head=$(git rev-parse HEAD) &&
	test_when_finished "git rebase --abort; git reset --hard $current_head; rm -f error" &&
	test_commit TO-REMOVE will-conflict old-content &&
	test_commit "\temp" will-conflict new-content dummy &&
	test_must_fail env EDITOR=true git rebase -i HEAD^ --onto HEAD^^ 2>error &&
	test_expect_code 1 grep  "	emp" error
'

test_expect_success 'short commit ID setup' '
	test_when_finished "git checkout primary" &&
	git checkout --orphan collide &&
	git rm -rf . &&
	(
	unset test_tick &&
	test_commit collide1 collide &&
	test_commit --notick collide2 collide &&
	test_commit --notick collide3 collide
	)
'

if test -n "$GIT_TEST_FIND_COLLIDER"
then
	author="$(unset test_tick; test_tick; git var GIT_AUTHOR_IDENT)"
	committer="$(unset test_tick; test_tick; git var GIT_COMMITTER_IDENT)"
	blob="$(git rev-parse collide2:collide)"
	from="$(git rev-parse collide1^0)"
	repl="commit refs/heads/collider-&\\n"
	repl="${repl}author $author\\ncommitter $committer\\n"
	repl="${repl}data <<EOF\\ncollide2 &\\nEOF\\n"
	repl="${repl}from $from\\nM 100644 $blob collide\\n"
	test_seq 1 32768 | sed "s|.*|$repl|" >script &&
	git fast-import <script &&
	git pack-refs &&
	git for-each-ref >refs &&
	grep "^$(test_oid t3404_collision)" <refs >matches &&
	cat matches &&
	test_line_count -gt 2 matches || {
		echo "Could not find a collider" >&2
		exit 1
	}
fi

test_expect_success 'short commit ID collide' '
	test_oid_cache <<-EOF &&
	# collision-related constants
	t3404_collision	sha1:6bcd
	t3404_collision	sha256:0161
	t3404_collider	sha1:ac4f2ee
	t3404_collider	sha256:16697
	EOF
	test_when_finished "reset_rebase && git checkout primary" &&
	git checkout collide &&
	colliding_id=$(test_oid t3404_collision) &&
	hexsz=$(test_oid hexsz) &&
	test $colliding_id = "$(git rev-parse HEAD | cut -c 1-4)" &&
	test_config core.abbrev 4 &&
	(
		unset test_tick &&
		test_tick &&
		set_fake_editor &&
		FAKE_COMMIT_MESSAGE="collide2 $(test_oid t3404_collider)" \
		FAKE_LINES="reword 1 break 2" git rebase -i HEAD~2 &&
		test $colliding_id = "$(git rev-parse HEAD | cut -c 1-4)" &&
		grep "^pick $colliding_id " \
			.git/rebase-merge/git-rebase-todo.tmp &&
		grep -E "^pick [0-9a-f]{$hexsz}" \
			.git/rebase-merge/git-rebase-todo &&
		grep -E "^pick [0-9a-f]{$hexsz}" \
			.git/rebase-merge/git-rebase-todo.backup &&
		git rebase --continue
	) &&
	collide2="$(git rev-parse HEAD~1 | cut -c 1-4)" &&
	collide3="$(git rev-parse collide3 | cut -c 1-4)" &&
	test "$collide2" = "$collide3"
'

test_expect_success 'respect core.abbrev' '
	git config core.abbrev 12 &&
	(
		set_cat_todo_editor &&
		test_must_fail git rebase -i HEAD~4 >todo-list
	) &&
	test 4 = $(grep -c -E "pick [0-9a-f]{12,}" todo-list)
'

test_expect_success 'todo count' '
	write_script dump-raw.sh <<-\EOF &&
		cat "$1"
	EOF
	(
		test_set_editor "$(pwd)/dump-raw.sh" &&
		git rebase -i HEAD~4 >actual
	) &&
	test_grep "^# Rebase ..* onto ..* ([0-9]" actual
'

test_expect_success 'rebase -i commits that overwrite untracked files (pick)' '
	git checkout --force A &&
	git clean -f &&
	cat >todo <<-EOF &&
	exec >file2
	pick $(git rev-parse B) B
	pick $(git rev-parse C) C
	pick $(git rev-parse D) D
	exec cat .git/rebase-merge/done >actual
	EOF
	(
		set_replace_editor todo &&
		test_must_fail git rebase -i A
	) &&
	test_cmp_rev HEAD B &&
	test_cmp_rev REBASE_HEAD C &&
	head -n3 todo >expect &&
	test_cmp expect .git/rebase-merge/done &&
	rm file2 &&
	test_path_is_missing .git/rebase-merge/patch &&
	echo changed >file1 &&
	git add file1 &&
	test_must_fail git rebase --continue 2>err &&
	grep "error: you have staged changes in your working tree" err &&
	git reset --hard HEAD &&
	git rebase --continue &&
	test_cmp_rev HEAD D &&
	tail -n3 todo >>expect &&
	test_cmp expect actual
'

test_expect_success 'rebase -i commits that overwrite untracked files (squash)' '
	git checkout --force branch2 &&
	git clean -f &&
	git tag original-branch2 &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1 squash 2" git rebase -i A
	) &&
	test_cmp_rev HEAD F &&
	test_path_is_missing file6 &&
	>file6 &&
	test_must_fail git rebase --continue &&
	test_cmp_rev HEAD F &&
	test_cmp_rev REBASE_HEAD I &&
	rm file6 &&
	test_path_is_missing .git/rebase-merge/patch &&
	echo changed >file1 &&
	git add file1 &&
	test_must_fail git rebase --continue 2>err &&
	grep "error: you have staged changes in your working tree" err &&
	git reset --hard HEAD &&
	git rebase --continue &&
	test $(git cat-file commit HEAD | sed -ne \$p) = I &&
	git reset --hard original-branch2
'

test_expect_success 'rebase -i commits that overwrite untracked files (no ff)' '
	git checkout --force branch2 &&
	git clean -f &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1 2" git rebase -i --no-ff A
	) &&
	test $(git cat-file commit HEAD | sed -ne \$p) = F &&
	test_path_is_missing file6 &&
	>file6 &&
	test_must_fail git rebase --continue &&
	test $(git cat-file commit HEAD | sed -ne \$p) = F &&
	test_cmp_rev REBASE_HEAD I &&
	rm file6 &&
	test_path_is_missing .git/rebase-merge/patch &&
	echo changed >file1 &&
	git add file1 &&
	test_must_fail git rebase --continue 2>err &&
	grep "error: you have staged changes in your working tree" err &&
	git reset --hard HEAD &&
	git rebase --continue &&
	test $(git cat-file commit HEAD | sed -ne \$p) = I
'

test_expect_success 'rebase --continue removes CHERRY_PICK_HEAD' '
	git checkout -b commit-to-skip &&
	for double in X 3 1
	do
		test_seq 5 | sed "s/$double/&&/" >seq &&
		git add seq &&
		test_tick &&
		git commit -m seq-$double || return 1
	done &&
	git tag seq-onto &&
	git reset --hard HEAD~2 &&
	git cherry-pick seq-onto &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES= git rebase -i seq-onto
	) &&
	test -d .git/rebase-merge &&
	git rebase --continue &&
	git diff --exit-code seq-onto &&
	test ! -d .git/rebase-merge &&
	test ! -f .git/CHERRY_PICK_HEAD
'

rebase_setup_and_clean () {
	test_when_finished "
		git checkout primary &&
		test_might_fail git branch -D $1 &&
		test_might_fail git rebase --abort
	" &&
	git checkout -b $1 ${2:-primary}
}

test_expect_success 'drop' '
	rebase_setup_and_clean drop-test &&
	(
		set_fake_editor &&
		FAKE_LINES="1 drop 2 3 d 4 5" git rebase -i --root
	) &&
	test E = $(git cat-file commit HEAD | sed -ne \$p) &&
	test C = $(git cat-file commit HEAD^ | sed -ne \$p) &&
	test A = $(git cat-file commit HEAD^^ | sed -ne \$p)
'

test_expect_success 'rebase -i respects rebase.missingCommitsCheck = ignore' '
	test_config rebase.missingCommitsCheck ignore &&
	rebase_setup_and_clean missing-commit &&
	(
		set_fake_editor &&
		FAKE_LINES="1 2 3 4" git rebase -i --root 2>actual
	) &&
	test D = $(git cat-file commit HEAD | sed -ne \$p) &&
	test_grep \
		"Successfully rebased and updated refs/heads/missing-commit" \
		actual
'

test_expect_success 'rebase -i respects rebase.missingCommitsCheck = warn' '
	cat >expect <<-EOF &&
	Warning: some commits may have been dropped accidentally.
	Dropped commits (newer to older):
	 - $(git rev-list --pretty=oneline --abbrev-commit -1 primary)
	To avoid this message, use "drop" to explicitly remove a commit.
	EOF
	test_config rebase.missingCommitsCheck warn &&
	rebase_setup_and_clean missing-commit &&
	(
		set_fake_editor &&
		FAKE_LINES="1 2 3 4" git rebase -i --root 2>actual.2
	) &&
	head -n4 actual.2 >actual &&
	test_cmp expect actual &&
	test D = $(git cat-file commit HEAD | sed -ne \$p)
'

test_expect_success 'rebase -i respects rebase.missingCommitsCheck = error' '
	cat >expect <<-EOF &&
	Warning: some commits may have been dropped accidentally.
	Dropped commits (newer to older):
	 - $(git rev-list --pretty=oneline --abbrev-commit -1 primary)
	 - $(git rev-list --pretty=oneline --abbrev-commit -1 primary~2)
	To avoid this message, use "drop" to explicitly remove a commit.

	Use '\''git config rebase.missingCommitsCheck'\'' to change the level of warnings.
	The possible behaviours are: ignore, warn, error.

	You can fix this with '\''git rebase --edit-todo'\'' and then run '\''git rebase --continue'\''.
	Or you can abort the rebase with '\''git rebase --abort'\''.
	EOF
	test_config rebase.missingCommitsCheck error &&
	rebase_setup_and_clean missing-commit &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 2 4" \
			git rebase -i --root 2>actual &&
		test_cmp expect actual &&
		cp .git/rebase-merge/git-rebase-todo.backup \
			.git/rebase-merge/git-rebase-todo &&
		FAKE_LINES="1 2 drop 3 4 drop 5" git rebase --edit-todo
	) &&
	git rebase --continue &&
	test D = $(git cat-file commit HEAD | sed -ne \$p) &&
	test B = $(git cat-file commit HEAD^ | sed -ne \$p)
'

test_expect_success 'rebase --edit-todo respects rebase.missingCommitsCheck = ignore' '
	test_config rebase.missingCommitsCheck ignore &&
	rebase_setup_and_clean missing-commit &&
	(
		set_fake_editor &&
		FAKE_LINES="break 1 2 3 4 5" git rebase -i --root &&
		FAKE_LINES="1 2 3 4" git rebase --edit-todo &&
		git rebase --continue 2>actual
	) &&
	test D = $(git cat-file commit HEAD | sed -ne \$p) &&
	test_grep \
		"Successfully rebased and updated refs/heads/missing-commit" \
		actual
'

test_expect_success 'rebase --edit-todo respects rebase.missingCommitsCheck = warn' '
	cat >expect <<-EOF &&
	error: invalid command '\''pickled'\''
	error: invalid line 1: pickled $(git rev-list --pretty=oneline --abbrev-commit -1 primary~4)
	Warning: some commits may have been dropped accidentally.
	Dropped commits (newer to older):
	 - $(git rev-list --pretty=oneline --abbrev-commit -1 primary)
	 - $(git rev-list --pretty=oneline --abbrev-commit -1 primary~4)
	To avoid this message, use "drop" to explicitly remove a commit.
	EOF
	head -n5 expect >expect.2 &&
	tail -n1 expect >>expect.2 &&
	tail -n4 expect.2 >expect.3 &&
	test_config rebase.missingCommitsCheck warn &&
	rebase_setup_and_clean missing-commit &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="bad 1 2 3 4 5" \
			git rebase -i --root &&
		cp .git/rebase-merge/git-rebase-todo.backup orig &&
		FAKE_LINES="2 3 4" git rebase --edit-todo 2>actual.2 &&
		head -n7 actual.2 >actual &&
		test_cmp expect actual &&
		cp orig .git/rebase-merge/git-rebase-todo &&
		FAKE_LINES="1 2 3 4" git rebase --edit-todo 2>actual.2 &&
		head -n4 actual.2 >actual &&
		test_cmp expect.3 actual &&
		git rebase --continue 2>actual
	) &&
	test D = $(git cat-file commit HEAD | sed -ne \$p) &&
	test_grep \
		"Successfully rebased and updated refs/heads/missing-commit" \
		actual
'

test_expect_success 'rebase --edit-todo respects rebase.missingCommitsCheck = error' '
	cat >expect <<-EOF &&
	error: invalid command '\''pickled'\''
	error: invalid line 1: pickled $(git rev-list --pretty=oneline --abbrev-commit -1 primary~4)
	Warning: some commits may have been dropped accidentally.
	Dropped commits (newer to older):
	 - $(git rev-list --pretty=oneline --abbrev-commit -1 primary)
	 - $(git rev-list --pretty=oneline --abbrev-commit -1 primary~4)
	To avoid this message, use "drop" to explicitly remove a commit.

	Use '\''git config rebase.missingCommitsCheck'\'' to change the level of warnings.
	The possible behaviours are: ignore, warn, error.

	You can fix this with '\''git rebase --edit-todo'\'' and then run '\''git rebase --continue'\''.
	Or you can abort the rebase with '\''git rebase --abort'\''.
	EOF
	tail -n11 expect >expect.2 &&
	head -n3 expect.2 >expect.3 &&
	tail -n7 expect.2 >>expect.3 &&
	test_config rebase.missingCommitsCheck error &&
	rebase_setup_and_clean missing-commit &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="bad 1 2 3 4 5" \
			git rebase -i --root &&
		cp .git/rebase-merge/git-rebase-todo.backup orig &&
		test_must_fail env FAKE_LINES="2 3 4" \
			git rebase --edit-todo 2>actual &&
		test_cmp expect actual &&
		test_must_fail git rebase --continue 2>actual &&
		test_cmp expect.2 actual &&
		test_must_fail git rebase --edit-todo &&
		cp orig .git/rebase-merge/git-rebase-todo &&
		test_must_fail env FAKE_LINES="1 2 3 4" \
			git rebase --edit-todo 2>actual &&
		test_cmp expect.3 actual &&
		test_must_fail git rebase --continue 2>actual &&
		test_cmp expect.3 actual &&
		cp orig .git/rebase-merge/git-rebase-todo &&
		FAKE_LINES="1 2 3 4 drop 5" git rebase --edit-todo &&
		git rebase --continue 2>actual
	) &&
	test D = $(git cat-file commit HEAD | sed -ne \$p) &&
	test_grep \
		"Successfully rebased and updated refs/heads/missing-commit" \
		actual
'

test_expect_success 'rebase.missingCommitsCheck = error after resolving conflicts' '
	test_config rebase.missingCommitsCheck error &&
	(
		set_fake_editor &&
		FAKE_LINES="drop 1 break 2 3 4" git rebase -i A E
	) &&
	git rebase --edit-todo &&
	test_must_fail git rebase --continue &&
	echo x >file1 &&
	git add file1 &&
	git rebase --continue
'

test_expect_success 'rebase.missingCommitsCheck = error when editing for a second time' '
	test_config rebase.missingCommitsCheck error &&
	(
		set_fake_editor &&
		FAKE_LINES="1 break 2 3" git rebase -i A D &&
		cp .git/rebase-merge/git-rebase-todo todo &&
		test_must_fail env FAKE_LINES=2 git rebase --edit-todo &&
		GIT_SEQUENCE_EDITOR="cp todo" git rebase --edit-todo &&
		git rebase --continue
	)
'

test_expect_success 'respects rebase.abbreviateCommands with fixup, squash and exec' '
	rebase_setup_and_clean abbrevcmd &&
	test_commit "first" file1.txt "first line" first &&
	test_commit "second" file1.txt "another line" second &&
	test_commit "fixup! first" file2.txt "first line again" first_fixup &&
	test_commit "squash! second" file1.txt "another line here" second_squash &&
	cat >expected <<-EOF &&
	p $(git rev-list --abbrev-commit -1 first) first
	f $(git rev-list --abbrev-commit -1 first_fixup) fixup! first
	x git show HEAD
	p $(git rev-list --abbrev-commit -1 second) second
	s $(git rev-list --abbrev-commit -1 second_squash) squash! second
	x git show HEAD
	EOF
	git checkout abbrevcmd &&
	test_config rebase.abbreviateCommands true &&
	(
		set_cat_todo_editor &&
		test_must_fail git rebase -i --exec "git show HEAD" \
			--autosquash primary >actual
	) &&
	test_cmp expected actual
'

test_expect_success 'static check of bad command' '
	rebase_setup_and_clean bad-cmd &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 2 3 bad 4 5" \
		git rebase -i --root 2>actual &&
		test_grep "pickled $(git rev-list --oneline -1 primary~1)" \
				actual &&
		test_grep "You can fix this with .git rebase --edit-todo.." \
				actual &&
		FAKE_LINES="1 2 3 drop 4 5" git rebase --edit-todo
	) &&
	git rebase --continue &&
	test E = $(git cat-file commit HEAD | sed -ne \$p) &&
	test C = $(git cat-file commit HEAD^ | sed -ne \$p)
'

test_expect_success 'the first command cannot be a fixup' '
	rebase_setup_and_clean fixup-first &&

	cat >orig <<-EOF &&
	fixup $(git log -1 --format="%h %s" B)
	pick $(git log -1 --format="%h %s" C)
	EOF

	(
		set_replace_editor orig &&
		test_must_fail git rebase -i A 2>actual
	) &&
	grep "cannot .fixup. without a previous commit" actual &&
	grep "You can fix this with .git rebase --edit-todo.." actual &&
	# verify that the todo list has not been truncated
	grep -v "^#" .git/rebase-merge/git-rebase-todo >actual &&
	test_cmp orig actual &&

	test_must_fail git rebase --edit-todo 2>actual &&
	grep "cannot .fixup. without a previous commit" actual &&
	grep "You can fix this with .git rebase --edit-todo.." actual &&
	# verify that the todo list has not been truncated
	grep -v "^#" .git/rebase-merge/git-rebase-todo >actual &&
	test_cmp orig actual
'

test_expect_success 'tabs and spaces are accepted in the todolist' '
	rebase_setup_and_clean indented-comment &&
	write_script add-indent.sh <<-\EOF &&
	(
		# Turn single spaces into space/tab mix
		sed "1s/ /	/g; 2s/ /  /g; 3s/ / 	/g" "$1"
		printf "\n\t# comment\n #more\n\t # comment\n"
	) >"$1.new"
	mv "$1.new" "$1"
	EOF
	(
		test_set_editor "$(pwd)/add-indent.sh" &&
		git rebase -i HEAD^^^
	) &&
	test E = $(git cat-file commit HEAD | sed -ne \$p)
'

test_expect_success 'static check of bad SHA-1' '
	rebase_setup_and_clean bad-sha &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 2 edit fakesha 3 4 5 #" \
			git rebase -i --root 2>actual &&
			test_grep "edit XXXXXXX False commit" actual &&
			test_grep "You can fix this with .git rebase --edit-todo.." \
					actual &&
		FAKE_LINES="1 2 4 5 6" git rebase --edit-todo
	) &&
	git rebase --continue &&
	test E = $(git cat-file commit HEAD | sed -ne \$p)
'

test_expect_success 'editor saves as CR/LF' '
	git checkout -b with-crlf &&
	write_script add-crs.sh <<-\EOF &&
	sed -e "s/\$/Q/" <"$1" | tr Q "\\015" >"$1".new &&
	mv -f "$1".new "$1"
	EOF
	(
		test_set_editor "$(pwd)/add-crs.sh" &&
		git rebase -i HEAD^
	)
'

test_expect_success 'rebase -i --gpg-sign=<key-id>' '
	test_when_finished "test_might_fail git rebase --abort" &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1" git rebase -i --gpg-sign="\"S I Gner\"" \
			HEAD^ >out 2>err
	) &&
	test_grep "$SQ-S\"S I Gner\"$SQ" err
'

test_expect_success 'rebase -i --gpg-sign=<key-id> overrides commit.gpgSign' '
	test_when_finished "test_might_fail git rebase --abort" &&
	test_config commit.gpgsign true &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1" git rebase -i --gpg-sign="\"S I Gner\"" \
			HEAD^ >out 2>err
	) &&
	test_grep "$SQ-S\"S I Gner\"$SQ" err
'

test_expect_success 'valid author header after --root swap' '
	rebase_setup_and_clean author-header no-conflict-branch &&
	git commit --amend --author="Au ${SQ}thor <author@example.com>" --no-edit &&
	git cat-file commit HEAD | grep ^author >expected &&
	(
		set_fake_editor &&
		FAKE_LINES="5 1" git rebase -i --root
	) &&
	git cat-file commit HEAD^ | grep ^author >actual &&
	test_cmp expected actual
'

test_expect_success 'valid author header when author contains single quote' '
	rebase_setup_and_clean author-header no-conflict-branch &&
	git commit --amend --author="Au ${SQ}thor <author@example.com>" --no-edit &&
	git cat-file commit HEAD | grep ^author >expected &&
	(
		set_fake_editor &&
		FAKE_LINES="2" git rebase -i HEAD~2
	) &&
	git cat-file commit HEAD | grep ^author >actual &&
	test_cmp expected actual
'

test_expect_success 'post-commit hook is called' '
	>actual &&
	test_hook post-commit <<-\EOS &&
	git rev-parse HEAD >>actual
	EOS
	(
		set_fake_editor &&
		FAKE_LINES="edit 4 1 reword 2 fixup 3" git rebase -i A E &&
		echo x>file3 &&
		git add file3 &&
		FAKE_COMMIT_MESSAGE=edited git rebase --continue
	) &&
	git rev-parse HEAD@{5} HEAD@{4} HEAD@{3} HEAD@{2} HEAD@{1} HEAD \
		>expect &&
	test_cmp expect actual
'

test_expect_success 'correct error message for partial commit after empty pick' '
	test_when_finished "git rebase --abort" &&
	(
		set_fake_editor &&
		FAKE_LINES="2 1 1" &&
		export FAKE_LINES &&
		test_must_fail git rebase -i A D
	) &&
	echo x >file1 &&
	test_must_fail git commit file1 2>err &&
	test_grep "cannot do a partial commit during a rebase." err
'

test_expect_success 'correct error message for commit --amend after empty pick' '
	test_when_finished "git rebase --abort" &&
	(
		set_fake_editor &&
		FAKE_LINES="1 1" &&
		export FAKE_LINES &&
		test_must_fail git rebase -i A D
	) &&
	echo x>file1 &&
	test_must_fail git commit -a --amend 2>err &&
	test_grep "middle of a rebase -- cannot amend." err
'

test_expect_success 'todo has correct onto hash' '
	GIT_SEQUENCE_EDITOR=cat git rebase -i no-conflict-branch~4 no-conflict-branch >actual &&
	onto=$(git rev-parse --short HEAD~4) &&
	test_grep "^# Rebase ..* onto $onto" actual
'

test_expect_success 'ORIG_HEAD is updated correctly' '
	test_when_finished "git checkout primary && git branch -D test-orig-head" &&
	git checkout -b test-orig-head A &&
	git commit --allow-empty -m A1 &&
	git commit --allow-empty -m A2 &&
	git commit --allow-empty -m A3 &&
	git commit --allow-empty -m A4 &&
	git rebase primary &&
	test_cmp_rev ORIG_HEAD test-orig-head@{1}
'

test_expect_success '--update-refs adds label and update-ref commands' '
	git checkout -b update-refs no-conflict-branch &&
	git branch -f base HEAD~4 &&
	git branch -f first HEAD~3 &&
	git branch -f second HEAD~3 &&
	git branch -f third HEAD~1 &&
	git commit --allow-empty --fixup=third &&
	git branch -f is-not-reordered &&
	git commit --allow-empty --fixup=HEAD~4 &&
	git branch -f shared-tip &&
	(
		set_cat_todo_editor &&

		cat >expect <<-EOF &&
		pick $(git log -1 --format=%h J) J
		fixup $(git log -1 --format=%h update-refs) fixup! J # empty
		update-ref refs/heads/second
		update-ref refs/heads/first
		pick $(git log -1 --format=%h K) K
		pick $(git log -1 --format=%h L) L
		fixup $(git log -1 --format=%h is-not-reordered) fixup! L # empty
		update-ref refs/heads/third
		pick $(git log -1 --format=%h M) M
		update-ref refs/heads/no-conflict-branch
		update-ref refs/heads/is-not-reordered
		update-ref refs/heads/shared-tip
		EOF

		test_must_fail git rebase -i --autosquash --update-refs primary >todo &&
		test_cmp expect todo &&

		test_must_fail git -c rebase.autosquash=true \
				   -c rebase.updaterefs=true \
				   rebase -i primary >todo &&

		test_cmp expect todo
	)
'

test_expect_success '--update-refs adds commands with --rebase-merges' '
	git checkout -b update-refs-with-merge no-conflict-branch &&
	git branch -f base HEAD~4 &&
	git branch -f first HEAD~3 &&
	git branch -f second HEAD~3 &&
	git branch -f third HEAD~1 &&
	git merge -m merge branch2 &&
	git branch -f merge-branch &&
	git commit --fixup=third --allow-empty &&
	(
		set_cat_todo_editor &&

		cat >expect <<-EOF &&
		label onto
		reset onto
		pick $(git log -1 --format=%h branch2~1) F
		pick $(git log -1 --format=%h branch2) I
		update-ref refs/heads/branch2
		label branch2
		reset onto
		pick $(git log -1 --format=%h refs/heads/second) J
		update-ref refs/heads/second
		update-ref refs/heads/first
		pick $(git log -1 --format=%h refs/heads/third~1) K
		pick $(git log -1 --format=%h refs/heads/third) L
		fixup $(git log -1 --format=%h update-refs-with-merge) fixup! L # empty
		update-ref refs/heads/third
		pick $(git log -1 --format=%h HEAD~2) M
		update-ref refs/heads/no-conflict-branch
		merge -C $(git log -1 --format=%h HEAD~1) branch2 # merge
		update-ref refs/heads/merge-branch
		EOF

		test_must_fail git rebase -i --autosquash \
				   --rebase-merges=rebase-cousins \
				   --update-refs primary >todo &&

		test_cmp expect todo &&

		test_must_fail git -c rebase.autosquash=true \
				   -c rebase.updaterefs=true \
				   rebase -i \
				   --rebase-merges=rebase-cousins \
				   primary >todo &&

		test_cmp expect todo
	)
'

test_expect_success '--update-refs updates refs correctly' '
	git checkout -B update-refs no-conflict-branch &&
	git branch -f base HEAD~4 &&
	git branch -f first HEAD~3 &&
	git branch -f second HEAD~3 &&
	git branch -f third HEAD~1 &&
	test_commit extra2 fileX &&
	git commit --amend --fixup=L &&

	git rebase -i --autosquash --update-refs primary 2>err &&

	test_cmp_rev HEAD~3 refs/heads/first &&
	test_cmp_rev HEAD~3 refs/heads/second &&
	test_cmp_rev HEAD~1 refs/heads/third &&
	test_cmp_rev HEAD refs/heads/no-conflict-branch &&

	q_to_tab >expect <<-\EOF &&
	Successfully rebased and updated refs/heads/update-refs.
	Updated the following refs with --update-refs:
	Qrefs/heads/first
	Qrefs/heads/no-conflict-branch
	Qrefs/heads/second
	Qrefs/heads/third
	EOF

	# Clear "Rebasing (X/Y)" progress lines and drop leading tabs.
	sed "s/Rebasing.*Successfully/Successfully/g" <err >err.trimmed &&
	test_cmp expect err.trimmed
'

test_expect_success 'respect user edits to update-ref steps' '
	git checkout -B update-refs-break no-conflict-branch &&
	git branch -f base HEAD~4 &&
	git branch -f first HEAD~3 &&
	git branch -f second HEAD~3 &&
	git branch -f third HEAD~1 &&
	git branch -f unseen base &&

	# First, we will add breaks to the expected todo file
	cat >fake-todo-1 <<-EOF &&
	pick $(git rev-parse HEAD~3)
	break
	update-ref refs/heads/second
	update-ref refs/heads/first

	pick $(git rev-parse HEAD~2)
	pick $(git rev-parse HEAD~1)
	update-ref refs/heads/third

	pick $(git rev-parse HEAD)
	update-ref refs/heads/no-conflict-branch
	EOF

	# Second, we will drop some update-refs commands (and move one)
	cat >fake-todo-2 <<-EOF &&
	update-ref refs/heads/second

	pick $(git rev-parse HEAD~2)
	update-ref refs/heads/third
	pick $(git rev-parse HEAD~1)
	break

	pick $(git rev-parse HEAD)
	EOF

	# Third, we will:
	# * insert a new one (new-branch),
	# * re-add an old one (first), and
	# * add a second instance of a previously-stored one (second)
	cat >fake-todo-3 <<-EOF &&
	update-ref refs/heads/unseen
	update-ref refs/heads/new-branch
	pick $(git rev-parse HEAD)
	update-ref refs/heads/first
	update-ref refs/heads/second
	EOF

	(
		set_replace_editor fake-todo-1 &&
		git rebase -i --update-refs primary &&

		# These branches are currently locked.
		for b in first second third no-conflict-branch
		do
			test_must_fail git branch -f $b base || return 1
		done &&

		set_replace_editor fake-todo-2 &&
		git rebase --edit-todo &&

		# These branches are currently locked.
		for b in second third
		do
			test_must_fail git branch -f $b base || return 1
		done &&

		# These branches are currently unlocked for checkout.
		for b in first no-conflict-branch
		do
			git worktree add wt-$b $b &&
			git worktree remove wt-$b || return 1
		done &&

		git rebase --continue &&

		set_replace_editor fake-todo-3 &&
		git rebase --edit-todo &&

		# These branches are currently locked.
		for b in second third first unseen
		do
			test_must_fail git branch -f $b base || return 1
		done &&

		# These branches are currently unlocked for checkout.
		for b in no-conflict-branch
		do
			git worktree add wt-$b $b &&
			git worktree remove wt-$b || return 1
		done &&

		git rebase --continue
	) &&

	test_cmp_rev HEAD~2 refs/heads/third &&
	test_cmp_rev HEAD~1 refs/heads/unseen &&
	test_cmp_rev HEAD~1 refs/heads/new-branch &&
	test_cmp_rev HEAD refs/heads/first &&
	test_cmp_rev HEAD refs/heads/second &&
	test_cmp_rev HEAD refs/heads/no-conflict-branch
'

test_expect_success '--update-refs: all update-ref lines removed' '
	git checkout -b test-refs-not-removed no-conflict-branch &&
	git branch -f base HEAD~4 &&
	git branch -f first HEAD~3 &&
	git branch -f second HEAD~3 &&
	git branch -f third HEAD~1 &&
	git branch -f tip &&

	test_commit test-refs-not-removed &&
	git commit --amend --fixup first &&

	git rev-parse first second third tip no-conflict-branch >expect-oids &&

	(
		set_cat_todo_editor &&
		test_must_fail git rebase -i --update-refs base >todo.raw &&
		sed -e "/^update-ref/d" <todo.raw >todo
	) &&
	(
		set_replace_editor todo &&
		git rebase -i --update-refs base
	) &&

	# Ensure refs are not deleted and their OIDs have not changed
	git rev-parse first second third tip no-conflict-branch >actual-oids &&
	test_cmp expect-oids actual-oids
'

test_expect_success '--update-refs: all update-ref lines removed, then some re-added' '
	git checkout -b test-refs-not-removed2 no-conflict-branch &&
	git branch -f base HEAD~4 &&
	git branch -f first HEAD~3 &&
	git branch -f second HEAD~3 &&
	git branch -f third HEAD~1 &&
	git branch -f tip &&

	test_commit test-refs-not-removed2 &&
	git commit --amend --fixup first &&

	git rev-parse first second third >expect-oids &&

	(
		set_cat_todo_editor &&
		test_must_fail git rebase -i \
			--autosquash --update-refs \
			base >todo.raw &&
		sed -e "/^update-ref/d" <todo.raw >todo
	) &&

	# Add a break to the end of the todo so we can edit later
	echo "break" >>todo &&

	(
		set_replace_editor todo &&
		git rebase -i --autosquash --update-refs base &&
		echo "update-ref refs/heads/tip" >todo &&
		git rebase --edit-todo &&
		git rebase --continue
	) &&

	# Ensure first/second/third are unchanged, but tip is updated
	git rev-parse first second third >actual-oids &&
	test_cmp expect-oids actual-oids &&
	test_cmp_rev HEAD tip
'

test_expect_success '--update-refs: --edit-todo with no update-ref lines' '
	git checkout -b test-refs-not-removed3 no-conflict-branch &&
	git branch -f base HEAD~4 &&
	git branch -f first HEAD~3 &&
	git branch -f second HEAD~3 &&
	git branch -f third HEAD~1 &&
	git branch -f tip &&

	test_commit test-refs-not-removed3 &&
	git commit --amend --fixup first &&

	git rev-parse first second third tip no-conflict-branch >expect-oids &&

	(
		set_cat_todo_editor &&
		test_must_fail git rebase -i \
			--autosquash --update-refs \
			base >todo.raw &&
		sed -e "/^update-ref/d" <todo.raw >todo
	) &&

	# Add a break to the beginning of the todo so we can resume with no
	# update-ref lines
	echo "break" >todo.new &&
	cat todo >>todo.new &&

	(
		set_replace_editor todo.new &&
		git rebase -i --autosquash --update-refs base &&

		# Make no changes when editing so update-refs is still empty
		cat todo >todo.new &&
		git rebase --edit-todo &&
		git rebase --continue
	) &&

	# Ensure refs are not deleted and their OIDs have not changed
	git rev-parse first second third tip no-conflict-branch >actual-oids &&
	test_cmp expect-oids actual-oids
'

test_expect_success '--update-refs: check failed ref update' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout -B update-refs-error no-conflict-branch &&
	git branch -f base HEAD~4 &&
	git branch -f first HEAD~3 &&
	git branch -f second HEAD~2 &&
	git branch -f third HEAD~1 &&

	cat >fake-todo <<-EOF &&
	pick $(git rev-parse HEAD~3)
	break
	update-ref refs/heads/first

	pick $(git rev-parse HEAD~2)
	update-ref refs/heads/second

	pick $(git rev-parse HEAD~1)
	update-ref refs/heads/third

	pick $(git rev-parse HEAD)
	update-ref refs/heads/no-conflict-branch
	EOF

	(
		set_replace_editor fake-todo &&
		git rebase -i --update-refs base
	) &&

	# At this point, the values of first, second, and third are
	# recorded in the update-refs file. We will force-update the
	# "second" ref, but "git branch -f" will not work because of
	# the lock in the update-refs file.
	git update-ref refs/heads/second third &&

	test_must_fail git rebase --continue 2>err &&
	grep "update_ref failed for ref '\''refs/heads/second'\''" err &&

	q_to_tab >expect <<-\EOF &&
	Updated the following refs with --update-refs:
	Qrefs/heads/first
	Qrefs/heads/no-conflict-branch
	Qrefs/heads/third
	Failed to update the following refs with --update-refs:
	Qrefs/heads/second
	EOF

	# Clear "Rebasing (X/Y)" progress lines and drop leading tabs.
	tail -n 6 err >err.last &&
	sed "s/Rebasing.*Successfully/Successfully/g" <err.last >err.trimmed &&
	test_cmp expect err.trimmed
'

test_expect_success 'bad labels and refs rejected when parsing todo list' '
	test_when_finished "test_might_fail git rebase --abort" &&
	cat >todo <<-\EOF &&
	exec >execed
	label #
	label :invalid
	update-ref :bad
	update-ref topic
	EOF
	rm -f execed &&
	(
		set_replace_editor todo &&
		test_must_fail git rebase -i HEAD 2>err
	) &&
	grep "'\''#'\'' is not a valid label" err &&
	grep "'\'':invalid'\'' is not a valid label" err &&
	grep "'\'':bad'\'' is not a valid refname" err &&
	grep "update-ref requires a fully qualified refname e.g. refs/heads/topic" \
		err &&
	test_path_is_missing execed
'

test_expect_success 'non-merge commands reject merge commits' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git checkout E &&
	git merge I &&
	oid=$(git rev-parse HEAD) &&
	cat >todo <<-EOF &&
	pick $oid
	reword $oid
	edit $oid
	fixup $oid
	squash $oid
	EOF
	(
		set_replace_editor todo &&
		test_must_fail git rebase -i HEAD 2>actual
	) &&
	cat >expect <<-EOF &&
	error: ${SQ}pick${SQ} does not accept merge commits
	hint: ${SQ}pick${SQ} does not take a merge commit. If you wanted to
	hint: replay the merge, use ${SQ}merge -C${SQ} on the commit.
	hint: Disable this message with "git config advice.rebaseTodoError false"
	error: invalid line 1: pick $oid
	error: ${SQ}reword${SQ} does not accept merge commits
	hint: ${SQ}reword${SQ} does not take a merge commit. If you wanted to
	hint: replay the merge and reword the commit message, use
	hint: ${SQ}merge -c${SQ} on the commit
	hint: Disable this message with "git config advice.rebaseTodoError false"
	error: invalid line 2: reword $oid
	error: ${SQ}edit${SQ} does not accept merge commits
	hint: ${SQ}edit${SQ} does not take a merge commit. If you wanted to
	hint: replay the merge, use ${SQ}merge -C${SQ} on the commit, and then
	hint: ${SQ}break${SQ} to give the control back to you so that you can
	hint: do ${SQ}git commit --amend && git rebase --continue${SQ}.
	hint: Disable this message with "git config advice.rebaseTodoError false"
	error: invalid line 3: edit $oid
	error: cannot squash merge commit into another commit
	error: invalid line 4: fixup $oid
	error: cannot squash merge commit into another commit
	error: invalid line 5: squash $oid
	You can fix this with ${SQ}git rebase --edit-todo${SQ} and then run ${SQ}git rebase --continue${SQ}.
	Or you can abort the rebase with ${SQ}git rebase --abort${SQ}.
	EOF
	test_cmp expect actual
'

# This must be the last test in this file
test_expect_success '$EDITOR and friends are unchanged' '
	test_editor_unchanged
'

test_done
