#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='but rebase interactive

This test runs but rebase "interactively", by faking an edit, and verifies
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

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

test_expect_success 'setup' '
	but switch -C primary &&
	test_cummit A file1 &&
	test_cummit B file1 &&
	test_cummit C file2 &&
	test_cummit D file1 &&
	test_cummit E file3 &&
	but checkout -b branch1 A &&
	test_cummit F file4 &&
	test_cummit G file1 &&
	test_commit H file5 &&
	but checkout -b branch2 F &&
	test_cummit I file6 &&
	but checkout -b conflict-branch A &&
	test_cummit one conflict &&
	test_cummit two conflict &&
	test_cummit three conflict &&
	test_cummit four conflict &&
	but checkout -b no-conflict-branch A &&
	test_cummit J fileJ &&
	test_cummit K fileK &&
	test_cummit L fileL &&
	test_cummit M fileM &&
	but checkout -b no-ff-branch A &&
	test_cummit N fileN &&
	test_cummit O fileO &&
	test_cummit P fileP
'

# "exec" commands are run with the user shell by default, but this may
# be non-POSIX. For example, if SHELL=zsh then ">file" doesn't work
# to create a file. Unsetting SHELL avoids such non-portable behavior
# in tests. It must be exported for it to take effect where needed.
SHELL=
export SHELL

test_expect_success 'rebase --keep-empty' '
	but checkout -b emptybranch primary &&
	but cummit --allow-empty -m "empty" &&
	but rebase --keep-empty -i HEAD~2 &&
	but log --oneline >actual &&
	test_line_count = 6 actual
'

test_expect_success 'rebase -i with empty todo list' '
	cat >expect <<-\EOF &&
	error: nothing to do
	EOF
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="#" \
			but rebase -i HEAD^ >output 2>&1
	) &&
	tail -n 1 output >actual &&  # Ignore output about changing todo list
	test_cmp expect actual
'

test_expect_success 'rebase -i with the exec command' '
	but checkout primary &&
	(
	set_fake_editor &&
	FAKE_LINES="1 exec_>touch-one
		2 exec_>touch-two exec_false exec_>touch-three
		3 4 exec_>\"touch-file__name_with_spaces\";_>touch-after-semicolon 5" &&
	export FAKE_LINES &&
	test_must_fail but rebase -i A
	) &&
	test_path_is_file touch-one &&
	test_path_is_file touch-two &&
	# Missing because we should have stopped by now.
	test_path_is_missing touch-three &&
	test_cmp_rev C HEAD &&
	but rebase --continue &&
	test_path_is_file touch-three &&
	test_path_is_file "touch-file  name with spaces" &&
	test_path_is_file touch-after-semicolon &&
	test_cmp_rev primary HEAD &&
	rm -f touch-*
'

test_expect_success 'rebase -i with the exec command runs from tree root' '
	but checkout primary &&
	mkdir subdir && (cd subdir &&
	set_fake_editor &&
	FAKE_LINES="1 exec_>touch-subdir" \
		but rebase -i HEAD^
	) &&
	test_path_is_file touch-subdir &&
	rm -fr subdir
'

test_expect_success 'rebase -i with exec allows but commands in subdirs' '
	test_when_finished "rm -rf subdir" &&
	test_when_finished "but rebase --abort ||:" &&
	but checkout primary &&
	mkdir subdir && (cd subdir &&
	set_fake_editor &&
	FAKE_LINES="1 x_cd_subdir_&&_but_rev-parse_--is-inside-work-tree" \
		but rebase -i HEAD^
	)
'

test_expect_success 'rebase -i sets work tree properly' '
	test_when_finished "rm -rf subdir" &&
	test_when_finished "test_might_fail but rebase --abort" &&
	mkdir subdir &&
	but rebase -x "(cd subdir && but rev-parse --show-toplevel)" HEAD^ \
		>actual &&
	! grep "/subdir$" actual
'

test_expect_success 'rebase -i with the exec command checks tree cleanness' '
	but checkout primary &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="exec_echo_foo_>file1 1" \
			but rebase -i HEAD^
	) &&
	test_cmp_rev primary^ HEAD &&
	but reset --hard &&
	but rebase --continue
'

test_expect_success 'rebase -x with empty command fails' '
	test_when_finished "but rebase --abort ||:" &&
	test_must_fail env but rebase -x "" @ 2>actual &&
	test_write_lines "error: empty exec command" >expected &&
	test_cmp expected actual &&
	test_must_fail env but rebase -x " " @ 2>actual &&
	test_cmp expected actual
'

test_expect_success 'rebase -x with newline in command fails' '
	test_when_finished "but rebase --abort ||:" &&
	test_must_fail env but rebase -x "a${LF}b" @ 2>actual &&
	test_write_lines "error: exec commands cannot contain newlines" \
			 >expected &&
	test_cmp expected actual
'

test_expect_success 'rebase -i with exec of inexistent command' '
	but checkout primary &&
	test_when_finished "but rebase --abort" &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="exec_this-command-does-not-exist 1" \
			but rebase -i HEAD^ >actual 2>&1
	) &&
	! grep "Maybe but-rebase is broken" actual
'

test_expect_success 'implicit interactive rebase does not invoke sequence editor' '
	test_when_finished "but rebase --abort ||:" &&
	GIT_SEQUENCE_EDITOR="echo bad >" but rebase -x"echo one" @^
'

test_expect_success 'no changes are a nop' '
	but checkout branch2 &&
	but rebase -i F &&
	test "$(but symbolic-ref -q HEAD)" = "refs/heads/branch2" &&
	test_cmp_rev I HEAD
'

test_expect_success 'test the [branch] option' '
	but checkout -b dead-end &&
	but rm file6 &&
	but cummit -m "stop here" &&
	but rebase -i F branch2 &&
	test "$(but symbolic-ref -q HEAD)" = "refs/heads/branch2" &&
	test_cmp_rev I branch2 &&
	test_cmp_rev I HEAD
'

test_expect_success 'test --onto <branch>' '
	but checkout -b test-onto branch2 &&
	but rebase -i --onto branch1 F &&
	test "$(but symbolic-ref -q HEAD)" = "refs/heads/test-onto" &&
	test_cmp_rev HEAD^ branch1 &&
	test_cmp_rev I branch2
'

test_expect_success 'rebase on top of a non-conflicting cummit' '
	but checkout branch1 &&
	but tag original-branch1 &&
	but rebase -i branch2 &&
	test file6 = $(but diff --name-only original-branch1) &&
	test "$(but symbolic-ref -q HEAD)" = "refs/heads/branch1" &&
	test_cmp_rev I branch2 &&
	test_cmp_rev I HEAD~2
'

test_expect_success 'reflog for the branch shows state before rebase' '
	test_cmp_rev branch1@{1} original-branch1
'

test_expect_success 'reflog for the branch shows correct finish message' '
	printf "rebase (finish): refs/heads/branch1 onto %s\n" \
		"$(but rev-parse branch2)" >expected &&
	but log -g --pretty=%gs -1 refs/heads/branch1 >actual &&
	test_cmp expected actual
'

test_expect_success 'exchange two cummits' '
	(
		set_fake_editor &&
		FAKE_LINES="2 1" but rebase -i HEAD~2
	) &&
	test H = $(but cat-file commit HEAD^ | sed -ne \$p) &&
	test G = $(but cat-file commit HEAD | sed -ne \$p) &&
	blob1=$(but rev-parse --short HEAD^:file1) &&
	blob2=$(but rev-parse --short HEAD:file1) &&
	cummit=$(but rev-parse --short HEAD)
'

test_expect_success 'stop on conflicting pick' '
	cat >expect <<-EOF &&
	diff --but a/file1 b/file1
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
	>>>>>>> $cummit (G)
	EOF
	but tag new-branch1 &&
	test_must_fail but rebase -i primary &&
	test "$(but rev-parse HEAD~3)" = "$(but rev-parse primary)" &&
	test_cmp expect .but/rebase-merge/patch &&
	test_cmp expect2 file1 &&
	test "$(but diff --name-status |
		sed -n -e "/^U/s/^U[^a-z]*//p")" = file1 &&
	test 4 = $(grep -v "^#" < .but/rebase-merge/done | wc -l) &&
	test 0 = $(grep -c "^[^#]" < .but/rebase-merge/but-rebase-todo)
'

test_expect_success 'show conflicted patch' '
	GIT_TRACE=1 but rebase --show-current-patch >/dev/null 2>stderr &&
	grep "show.*REBASE_HEAD" stderr &&
	# the original stopped-sha1 is abbreviated
	stopped_sha1="$(but rev-parse $(cat ".but/rebase-merge/stopped-sha"))" &&
	test "$(but rev-parse REBASE_HEAD)" = "$stopped_sha1"
'

test_expect_success 'abort' '
	but rebase --abort &&
	test_cmp_rev new-branch1 HEAD &&
	test "$(but symbolic-ref -q HEAD)" = "refs/heads/branch1" &&
	test_path_is_missing .but/rebase-merge
'

test_expect_success 'abort with error when new base cannot be checked out' '
	but rm --cached file1 &&
	but cummit -m "remove file in base" &&
	test_must_fail but rebase -i primary > output 2>&1 &&
	test_i18ngrep "The following untracked working tree files would be overwritten by checkout:" \
		output &&
	test_i18ngrep "file1" output &&
	test_path_is_missing .but/rebase-merge &&
	rm file1 &&
	but reset --hard HEAD^
'

test_expect_success 'retain authorship' '
	echo A > file7 &&
	but add file7 &&
	test_tick &&
	GIT_AUTHOR_NAME="Twerp Snog" but cummit -m "different author" &&
	but tag twerp &&
	but rebase -i --onto primary HEAD^ &&
	but show HEAD | grep "^Author: Twerp Snog"
'

test_expect_success 'retain authorship w/ conflicts' '
	oGIT_AUTHOR_NAME=$GIT_AUTHOR_NAME &&
	test_when_finished "GIT_AUTHOR_NAME=\$oGIT_AUTHOR_NAME" &&

	but reset --hard twerp &&
	test_cummit a conflict a conflict-a &&
	but reset --hard twerp &&

	GIT_AUTHOR_NAME=AttributeMe &&
	export GIT_AUTHOR_NAME &&
	test_cummit b conflict b conflict-b &&
	GIT_AUTHOR_NAME=$oGIT_AUTHOR_NAME &&

	test_must_fail but rebase -i conflict-a &&
	echo resolved >conflict &&
	but add conflict &&
	but rebase --continue &&
	test_cmp_rev conflict-a^0 HEAD^ &&
	but show >out &&
	grep AttributeMe out
'

test_expect_success 'squash' '
	but reset --hard twerp &&
	echo B > file7 &&
	test_tick &&
	GIT_AUTHOR_NAME="Nitfol" but cummit -m "nitfol" file7 &&
	echo "******************************" &&
	(
		set_fake_editor &&
		FAKE_LINES="1 squash 2" EXPECT_HEADER_COUNT=2 \
			but rebase -i --onto primary HEAD~2
	) &&
	test B = $(cat file7) &&
	test_cmp_rev HEAD^ primary
'

test_expect_success 'retain authorship when squashing' '
	but show HEAD | grep "^Author: Twerp Snog"
'

test_expect_success '--continue tries to cummit' '
	but reset --hard D &&
	test_tick &&
	(
		set_fake_editor &&
		test_must_fail but rebase -i --onto new-branch1 HEAD^ &&
		echo resolved > file1 &&
		but add file1 &&
		FAKE_CUMMIT_MESSAGE="chouette!" but rebase --continue
	) &&
	test_cmp_rev HEAD^ new-branch1 &&
	but show HEAD | grep chouette
'

test_expect_success 'verbose flag is heeded, even after --continue' '
	but reset --hard primary@{1} &&
	test_tick &&
	test_must_fail but rebase -v -i --onto new-branch1 HEAD^ &&
	echo resolved > file1 &&
	but add file1 &&
	but rebase --continue > output &&
	grep "^ file1 | 2 +-$" output
'

test_expect_success 'multi-squash only fires up editor once' '
	base=$(but rev-parse HEAD~4) &&
	(
		set_fake_editor &&
		FAKE_CUMMIT_AMEND="ONCE" \
			FAKE_LINES="1 squash 2 squash 3 squash 4" \
			EXPECT_HEADER_COUNT=4 \
			but rebase -i $base
	) &&
	test $base = $(but rev-parse HEAD^) &&
	test 1 = $(but show | grep ONCE | wc -l)
'

test_expect_success 'multi-fixup does not fire up editor' '
	but checkout -b multi-fixup E &&
	base=$(but rev-parse HEAD~4) &&
	(
		set_fake_editor &&
		FAKE_CUMMIT_AMEND="NEVER" \
			FAKE_LINES="1 fixup 2 fixup 3 fixup 4" \
			but rebase -i $base
	) &&
	test $base = $(but rev-parse HEAD^) &&
	test 0 = $(but show | grep NEVER | wc -l) &&
	but checkout @{-1} &&
	but branch -D multi-fixup
'

test_expect_success 'cummit message used after conflict' '
	but checkout -b conflict-fixup conflict-branch &&
	base=$(but rev-parse HEAD~4) &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 fixup 3 fixup 4" \
			but rebase -i $base &&
		echo three > conflict &&
		but add conflict &&
		FAKE_CUMMIT_AMEND="ONCE" EXPECT_HEADER_COUNT=2 \
			but rebase --continue
	) &&
	test $base = $(but rev-parse HEAD^) &&
	test 1 = $(but show | grep ONCE | wc -l) &&
	but checkout @{-1} &&
	but branch -D conflict-fixup
'

test_expect_success 'cummit message retained after conflict' '
	but checkout -b conflict-squash conflict-branch &&
	base=$(but rev-parse HEAD~4) &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 fixup 3 squash 4" \
			but rebase -i $base &&
		echo three > conflict &&
		but add conflict &&
		FAKE_CUMMIT_AMEND="TWICE" EXPECT_HEADER_COUNT=2 \
			but rebase --continue
	) &&
	test $base = $(but rev-parse HEAD^) &&
	test 2 = $(but show | grep TWICE | wc -l) &&
	but checkout @{-1} &&
	but branch -D conflict-squash
'

test_expect_success 'squash and fixup generate correct log messages' '
	cat >expect-squash-fixup <<-\EOF &&
	B

	D

	ONCE
	EOF
	but checkout -b squash-fixup E &&
	base=$(but rev-parse HEAD~4) &&
	(
		set_fake_editor &&
		FAKE_CUMMIT_AMEND="ONCE" \
			FAKE_LINES="1 fixup 2 squash 3 fixup 4" \
			EXPECT_HEADER_COUNT=4 \
			but rebase -i $base
	) &&
	but cat-file commit HEAD | sed -e 1,/^\$/d > actual-squash-fixup &&
	test_cmp expect-squash-fixup actual-squash-fixup &&
	but cat-file commit HEAD@{2} |
		grep "^# This is a combination of 3 cummits\."  &&
	but cat-file commit HEAD@{3} |
		grep "^# This is a combination of 2 cummits\."  &&
	but checkout @{-1} &&
	but branch -D squash-fixup
'

test_expect_success 'squash ignores comments' '
	but checkout -b skip-comments E &&
	base=$(but rev-parse HEAD~4) &&
	(
		set_fake_editor &&
		FAKE_CUMMIT_AMEND="ONCE" \
			FAKE_LINES="# 1 # squash 2 # squash 3 # squash 4 #" \
			EXPECT_HEADER_COUNT=4 \
			but rebase -i $base
	) &&
	test $base = $(but rev-parse HEAD^) &&
	test 1 = $(but show | grep ONCE | wc -l) &&
	but checkout @{-1} &&
	but branch -D skip-comments
'

test_expect_success 'squash ignores blank lines' '
	but checkout -b skip-blank-lines E &&
	base=$(but rev-parse HEAD~4) &&
	(
		set_fake_editor &&
		FAKE_CUMMIT_AMEND="ONCE" \
			FAKE_LINES="> 1 > squash 2 > squash 3 > squash 4 >" \
			EXPECT_HEADER_COUNT=4 \
			but rebase -i $base
	) &&
	test $base = $(but rev-parse HEAD^) &&
	test 1 = $(but show | grep ONCE | wc -l) &&
	but checkout @{-1} &&
	but branch -D skip-blank-lines
'

test_expect_success 'squash works as expected' '
	but checkout -b squash-works no-conflict-branch &&
	one=$(but rev-parse HEAD~3) &&
	(
		set_fake_editor &&
		FAKE_LINES="1 s 3 2" EXPECT_HEADER_COUNT=2 but rebase -i HEAD~3
	) &&
	test $one = $(but rev-parse HEAD~2)
'

test_expect_success 'interrupted squash works as expected' '
	but checkout -b interrupted-squash conflict-branch &&
	one=$(but rev-parse HEAD~3) &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 squash 3 2" \
			but rebase -i HEAD~3
	) &&
	test_write_lines one two four > conflict &&
	but add conflict &&
	test_must_fail but rebase --continue &&
	echo resolved > conflict &&
	but add conflict &&
	but rebase --continue &&
	test $one = $(but rev-parse HEAD~2)
'

test_expect_success 'interrupted squash works as expected (case 2)' '
	but checkout -b interrupted-squash2 conflict-branch &&
	one=$(but rev-parse HEAD~3) &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="3 squash 1 2" \
			but rebase -i HEAD~3
	) &&
	test_write_lines one four > conflict &&
	but add conflict &&
	test_must_fail but rebase --continue &&
	test_write_lines one two four > conflict &&
	but add conflict &&
	test_must_fail but rebase --continue &&
	echo resolved > conflict &&
	but add conflict &&
	but rebase --continue &&
	test $one = $(but rev-parse HEAD~2)
'

test_expect_success '--continue tries to cummit, even for "edit"' '
	echo unrelated > file7 &&
	but add file7 &&
	test_tick &&
	but cummit -m "unrelated change" &&
	parent=$(but rev-parse HEAD^) &&
	test_tick &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1" but rebase -i HEAD^ &&
		echo edited > file7 &&
		but add file7 &&
		FAKE_CUMMIT_MESSAGE="chouette!" but rebase --continue
	) &&
	test edited = $(but show HEAD:file7) &&
	but show HEAD | grep chouette &&
	test $parent = $(but rev-parse HEAD^)
'

test_expect_success 'aborted --continue does not squash cummits after "edit"' '
	old=$(but rev-parse HEAD) &&
	test_tick &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1" but rebase -i HEAD^ &&
		echo "edited again" > file7 &&
		but add file7 &&
		test_must_fail env FAKE_CUMMIT_MESSAGE=" " but rebase --continue
	) &&
	test $old = $(but rev-parse HEAD) &&
	but rebase --abort
'

test_expect_success 'auto-amend only edited cummits after "edit"' '
	test_tick &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1" but rebase -i HEAD^ &&
		echo "edited again" > file7 &&
		but add file7 &&
		FAKE_CUMMIT_MESSAGE="edited file7 again" but cummit &&
		echo "and again" > file7 &&
		but add file7 &&
		test_tick &&
		test_must_fail env FAKE_CUMMIT_MESSAGE="and again" \
			but rebase --continue
	) &&
	but rebase --abort
'

test_expect_success 'clean error after failed "exec"' '
	test_tick &&
	test_when_finished "but rebase --abort || :" &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 exec_false" but rebase -i HEAD^
	) &&
	echo "edited again" > file7 &&
	but add file7 &&
	test_must_fail but rebase --continue 2>error &&
	test_i18ngrep "you have staged changes in your working tree" error
'

test_expect_success 'rebase a detached HEAD' '
	grandparent=$(but rev-parse HEAD~2) &&
	but checkout $(but rev-parse HEAD) &&
	test_tick &&
	(
		set_fake_editor &&
		FAKE_LINES="2 1" but rebase -i HEAD~2
	) &&
	test $grandparent = $(but rev-parse HEAD~2)
'

test_expect_success 'rebase a cummit violating pre-cummit' '
	test_hook pre-cummit <<-\EOF &&
	test -z "$(but diff --cached --check)"
	EOF
	echo "monde! " >> file1 &&
	test_tick &&
	test_must_fail but cummit -m doesnt-verify file1 &&
	but cummit -m doesnt-verify --no-verify file1 &&
	test_tick &&
	(
		set_fake_editor &&
		FAKE_LINES=2 but rebase -i HEAD~2
	)
'

test_expect_success 'rebase with a file named HEAD in worktree' '
	but reset --hard &&
	but checkout -b branch3 A &&

	(
		GIT_AUTHOR_NAME="Squashed Away" &&
		export GIT_AUTHOR_NAME &&
		>HEAD &&
		but add HEAD &&
		but cummit -m "Add head" &&
		>BODY &&
		but add BODY &&
		but cummit -m "Add body"
	) &&

	(
		set_fake_editor &&
		FAKE_LINES="1 squash 2" but rebase -i @{-1}
	) &&
	test "$(but show -s --pretty=format:%an)" = "Squashed Away"

'

test_expect_success 'do "noop" when there is nothing to cherry-pick' '

	but checkout -b branch4 HEAD &&
	GIT_EDITOR=: but cummit --amend \
		--author="Somebody else <somebody@else.com>" &&
	test $(but rev-parse branch3) != $(but rev-parse branch4) &&
	but rebase -i branch3 &&
	test_cmp_rev branch3 branch4

'

test_expect_success 'submodule rebase setup' '
	but checkout A &&
	mkdir sub &&
	(
		cd sub && but init && >elif &&
		but add elif && but cummit -m "submodule initial"
	) &&
	echo 1 >file1 &&
	but add file1 sub &&
	test_tick &&
	but cummit -m "One" &&
	echo 2 >file1 &&
	test_tick &&
	but cummit -a -m "Two" &&
	(
		cd sub && echo 3 >elif &&
		but cummit -a -m "submodule second"
	) &&
	test_tick &&
	but cummit -a -m "Three changes submodule"
'

test_expect_success 'submodule rebase -i' '
	(
		set_fake_editor &&
		FAKE_LINES="1 squash 2 3" but rebase -i A
	)
'

test_expect_success 'submodule conflict setup' '
	but tag submodule-base &&
	but checkout HEAD^ &&
	(
		cd sub && but checkout HEAD^ && echo 4 >elif &&
		but add elif && but cummit -m "submodule conflict"
	) &&
	but add sub &&
	test_tick &&
	but cummit -m "Conflict in submodule" &&
	but tag submodule-topic
'

test_expect_success 'rebase -i continue with only submodule staged' '
	test_must_fail but rebase -i submodule-base &&
	but add sub &&
	but rebase --continue &&
	test $(but rev-parse submodule-base) != $(but rev-parse HEAD)
'

test_expect_success 'rebase -i continue with unstaged submodule' '
	but checkout submodule-topic &&
	but reset --hard &&
	test_must_fail but rebase -i submodule-base &&
	but reset &&
	but rebase --continue &&
	test_cmp_rev submodule-base HEAD
'

test_expect_success 'avoid unnecessary reset' '
	but checkout primary &&
	but reset --hard &&
	test-tool chmtime =123456789 file3 &&
	but update-index --refresh &&
	HEAD=$(but rev-parse HEAD) &&
	but rebase -i HEAD~4 &&
	test $HEAD = $(but rev-parse HEAD) &&
	MTIME=$(test-tool chmtime --get file3) &&
	test 123456789 = $MTIME
'

test_expect_success 'reword' '
	but checkout -b reword-branch primary &&
	(
		set_fake_editor &&
		FAKE_LINES="1 2 3 reword 4" FAKE_CUMMIT_MESSAGE="E changed" \
			but rebase -i A &&
		but show HEAD | grep "E changed" &&
		test $(but rev-parse primary) != $(but rev-parse HEAD) &&
		test_cmp_rev primary^ HEAD^ &&
		FAKE_LINES="1 2 reword 3 4" FAKE_CUMMIT_MESSAGE="D changed" \
			but rebase -i A &&
		but show HEAD^ | grep "D changed" &&
		FAKE_LINES="reword 1 2 3 4" FAKE_CUMMIT_MESSAGE="B changed" \
			but rebase -i A &&
		but show HEAD~3 | grep "B changed" &&
		FAKE_LINES="1 r 2 pick 3 p 4" FAKE_CUMMIT_MESSAGE="C changed" \
			but rebase -i A
	) &&
	but show HEAD~2 | grep "C changed"
'

test_expect_success 'no uncummited changes when rewording the todo list is reloaded' '
	but checkout E &&
	test_when_finished "but checkout @{-1}" &&
	(
		set_fake_editor &&
		GIT_SEQUENCE_EDITOR="\"$PWD/fake-editor.sh\"" &&
		export GIT_SEQUENCE_EDITOR &&
		set_reword_editor &&
		FAKE_LINES="reword 1 reword 2" but rebase -i C
	) &&
	check_reworded_cummits D E
'

test_expect_success 'rebase -i can copy notes' '
	but config notes.rewrite.rebase true &&
	but config notes.rewriteRef "refs/notes/*" &&
	test_cummit n1 &&
	test_cummit n2 &&
	test_cummit n3 &&
	but notes add -m"a note" n3 &&
	but rebase -i --onto n1 n2 &&
	test "a note" = "$(but notes show HEAD)"
'

test_expect_success 'rebase -i can copy notes over a fixup' '
	cat >expect <<-\EOF &&
	an earlier note

	a note
	EOF
	but reset --hard n3 &&
	but notes add -m"an earlier note" n2 &&
	(
		set_fake_editor &&
		GIT_NOTES_REWRITE_MODE=concatenate FAKE_LINES="1 f 2" \
			but rebase -i n1
	) &&
	but notes show > output &&
	test_cmp expect output
'

test_expect_success 'rebase while detaching HEAD' '
	but symbolic-ref HEAD &&
	grandparent=$(but rev-parse HEAD~2) &&
	test_tick &&
	(
		set_fake_editor &&
		FAKE_LINES="2 1" but rebase -i HEAD~2 HEAD^0
	) &&
	test $grandparent = $(but rev-parse HEAD~2) &&
	test_must_fail but symbolic-ref HEAD
'

test_tick # Ensure that the rebased cummits get a different timestamp.
test_expect_success 'always cherry-pick with --no-ff' '
	but checkout no-ff-branch &&
	but tag original-no-ff-branch &&
	but rebase -i --no-ff A &&
	for p in 0 1 2
	do
		test ! $(but rev-parse HEAD~$p) = $(but rev-parse original-no-ff-branch~$p) &&
		but diff HEAD~$p original-no-ff-branch~$p > out &&
		test_must_be_empty out || return 1
	done &&
	test_cmp_rev HEAD~3 original-no-ff-branch~3 &&
	but diff HEAD~3 original-no-ff-branch~3 > out &&
	test_must_be_empty out
'

test_expect_success 'set up cummits with funny messages' '
	but checkout -b funny A &&
	echo >>file1 &&
	test_tick &&
	but cummit -a -m "end with slash\\" &&
	echo >>file1 &&
	test_tick &&
	but cummit -a -m "something (\000) that looks like octal" &&
	echo >>file1 &&
	test_tick &&
	but cummit -a -m "something (\n) that looks like a newline" &&
	echo >>file1 &&
	test_tick &&
	but cummit -a -m "another cummit"
'

test_expect_success 'rebase-i history with funny messages' '
	but rev-list A..funny >expect &&
	test_tick &&
	(
		set_fake_editor &&
		FAKE_LINES="1 2 3 4" but rebase -i A
	) &&
	but rev-list A.. >actual &&
	test_cmp expect actual
'

test_expect_success 'prepare for rebase -i --exec' '
	but checkout primary &&
	but checkout -b execute &&
	test_cummit one_exec main.txt one_exec &&
	test_cummit two_exec main.txt two_exec &&
	test_cummit three_exec main.txt three_exec
'

test_expect_success 'running "but rebase -i --exec but show HEAD"' '
	(
		set_fake_editor &&
		but rebase -i --exec "but show HEAD" HEAD~2 >actual &&
		FAKE_LINES="1 exec_but_show_HEAD 2 exec_but_show_HEAD" &&
		export FAKE_LINES &&
		but rebase -i HEAD~2 >expect
	) &&
	sed -e "1,9d" expect >expected &&
	test_cmp expected actual
'

test_expect_success 'running "but rebase --exec but show HEAD -i"' '
	but reset --hard execute &&
	(
		set_fake_editor &&
		but rebase --exec "but show HEAD" -i HEAD~2 >actual &&
		FAKE_LINES="1 exec_but_show_HEAD 2 exec_but_show_HEAD" &&
		export FAKE_LINES &&
		but rebase -i HEAD~2 >expect
	) &&
	sed -e "1,9d" expect >expected &&
	test_cmp expected actual
'

test_expect_success 'running "but rebase -ix but show HEAD"' '
	but reset --hard execute &&
	(
		set_fake_editor &&
		but rebase -ix "but show HEAD" HEAD~2 >actual &&
		FAKE_LINES="1 exec_but_show_HEAD 2 exec_but_show_HEAD" &&
		export FAKE_LINES &&
		but rebase -i HEAD~2 >expect
	) &&
	sed -e "1,9d" expect >expected &&
	test_cmp expected actual
'


test_expect_success 'rebase -ix with several <CMD>' '
	but reset --hard execute &&
	(
		set_fake_editor &&
		but rebase -ix "but show HEAD; pwd" HEAD~2 >actual &&
		FAKE_LINES="1 exec_but_show_HEAD;_pwd 2 exec_but_show_HEAD;_pwd" &&
		export FAKE_LINES &&
		but rebase -i HEAD~2 >expect
	) &&
	sed -e "1,9d" expect >expected &&
	test_cmp expected actual
'

test_expect_success 'rebase -ix with several instances of --exec' '
	but reset --hard execute &&
	(
		set_fake_editor &&
		but rebase -i --exec "but show HEAD" --exec "pwd" HEAD~2 >actual &&
		FAKE_LINES="1 exec_but_show_HEAD exec_pwd 2
				exec_but_show_HEAD exec_pwd" &&
		export FAKE_LINES &&
		but rebase -i HEAD~2 >expect
	) &&
	sed -e "1,11d" expect >expected &&
	test_cmp expected actual
'

test_expect_success 'rebase -ix with --autosquash' '
	but reset --hard execute &&
	but checkout -b autosquash &&
	echo second >second.txt &&
	but add second.txt &&
	but cummit -m "fixup! two_exec" &&
	echo bis >bis.txt &&
	but add bis.txt &&
	but cummit -m "fixup! two_exec" &&
	but checkout -b autosquash_actual &&
	but rebase -i --exec "but show HEAD" --autosquash HEAD~4 >actual &&
	but checkout autosquash &&
	(
		set_fake_editor &&
		but checkout -b autosquash_expected &&
		FAKE_LINES="1 fixup 3 fixup 4 exec_but_show_HEAD 2 exec_but_show_HEAD" &&
		export FAKE_LINES &&
		but rebase -i HEAD~4 >expect
	) &&
	sed -e "1,13d" expect >expected &&
	test_cmp expected actual
'

test_expect_success 'rebase --exec works without -i ' '
	but reset --hard execute &&
	rm -rf exec_output &&
	EDITOR="echo >invoked_editor" but rebase --exec "echo a line >>exec_output"  HEAD~2 2>actual &&
	test_i18ngrep  "Successfully rebased and updated" actual &&
	test_line_count = 2 exec_output &&
	test_path_is_missing invoked_editor
'

test_expect_success 'rebase -i --exec without <CMD>' '
	but reset --hard execute &&
	test_must_fail but rebase -i --exec 2>actual &&
	test_i18ngrep "requires a value" actual &&
	but checkout primary
'

test_expect_success 'rebase -i --root re-order and drop cummits' '
	but checkout E &&
	(
		set_fake_editor &&
		FAKE_LINES="3 1 2 5" but rebase -i --root
	) &&
	test E = $(but cat-file commit HEAD | sed -ne \$p) &&
	test B = $(but cat-file commit HEAD^ | sed -ne \$p) &&
	test A = $(but cat-file commit HEAD^^ | sed -ne \$p) &&
	test C = $(but cat-file commit HEAD^^^ | sed -ne \$p) &&
	test 0 = $(but cat-file commit HEAD^^^ | grep -c ^parent\ )
'

test_expect_success 'rebase -i --root retain root cummit author and message' '
	but checkout A &&
	echo B >file7 &&
	but add file7 &&
	GIT_AUTHOR_NAME="Twerp Snog" but cummit -m "different author" &&
	(
		set_fake_editor &&
		FAKE_LINES="2" but rebase -i --root
	) &&
	but cat-file commit HEAD | grep -q "^author Twerp Snog" &&
	but cat-file commit HEAD | grep -q "^different author$"
'

test_expect_success 'rebase -i --root temporary sentinel cummit' '
	but checkout B &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="2" but rebase -i --root
	) &&
	but cat-file commit HEAD | grep "^tree $EMPTY_TREE" &&
	but rebase --abort
'

test_expect_success 'rebase -i --root fixup root cummit' '
	but checkout B &&
	(
		set_fake_editor &&
		FAKE_LINES="1 fixup 2" but rebase -i --root
	) &&
	test A = $(but cat-file commit HEAD | sed -ne \$p) &&
	test B = $(but show HEAD:file1) &&
	test 0 = $(but cat-file commit HEAD | grep -c ^parent\ )
'

test_expect_success 'rebase -i --root reword original root cummit' '
	test_when_finished "test_might_fail but rebase --abort" &&
	but checkout -b reword-original-root-branch primary &&
	(
		set_fake_editor &&
		FAKE_LINES="reword 1 2" FAKE_CUMMIT_MESSAGE="A changed" \
			but rebase -i --root
	) &&
	but show HEAD^ | grep "A changed" &&
	test -z "$(but show -s --format=%p HEAD^)"
'

test_expect_success 'rebase -i --root reword new root cummit' '
	test_when_finished "test_might_fail but rebase --abort" &&
	but checkout -b reword-now-root-branch primary &&
	(
		set_fake_editor &&
		FAKE_LINES="reword 3 1" FAKE_CUMMIT_MESSAGE="C changed" \
		but rebase -i --root
	) &&
	but show HEAD^ | grep "C changed" &&
	test -z "$(but show -s --format=%p HEAD^)"
'

test_expect_success 'rebase -i --root when root has untracked file conflict' '
	test_when_finished "reset_rebase" &&
	but checkout -b failing-root-pick A &&
	echo x >file2 &&
	but rm file1 &&
	but cummit -m "remove file 1 add file 2" &&
	echo z >file1 &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 2" but rebase -i --root
	) &&
	rm file1 &&
	but rebase --continue &&
	test "$(but log -1 --format=%B)" = "remove file 1 add file 2" &&
	test "$(but rev-list --count HEAD)" = 2
'

test_expect_success 'rebase -i --root reword root when root has untracked file conflict' '
	test_when_finished "reset_rebase" &&
	echo z>file1 &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="reword 1 2" \
			FAKE_CUMMIT_MESSAGE="Modified A" but rebase -i --root &&
		rm file1 &&
		FAKE_CUMMIT_MESSAGE="Reworded A" but rebase --continue
	) &&
	test "$(but log -1 --format=%B HEAD^)" = "Reworded A" &&
	test "$(but rev-list --count HEAD)" = 2
'

test_expect_success 'rebase --edit-todo does not work on non-interactive rebase' '
	but checkout reword-original-root-branch &&
	but reset --hard &&
	but checkout conflict-branch &&
	(
		set_fake_editor &&
		test_must_fail but rebase -f --apply --onto HEAD~2 HEAD~ &&
		test_must_fail but rebase --edit-todo
	) &&
	but rebase --abort
'

test_expect_success 'rebase --edit-todo can be used to modify todo' '
	but reset --hard &&
	but checkout no-conflict-branch^0 &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1 2 3" but rebase -i HEAD~3 &&
		FAKE_LINES="2 1" but rebase --edit-todo &&
		but rebase --continue
	) &&
	test M = $(but cat-file commit HEAD^ | sed -ne \$p) &&
	test L = $(but cat-file commit HEAD | sed -ne \$p)
'

test_expect_success 'rebase -i produces readable reflog' '
	but reset --hard &&
	but branch -f branch-reflog-test H &&
	but rebase -i --onto I F branch-reflog-test &&
	cat >expect <<-\EOF &&
	rebase (finish): returning to refs/heads/branch-reflog-test
	rebase (pick): H
	rebase (pick): G
	rebase (start): checkout I
	EOF
	but reflog -n4 HEAD |
	sed "s/[^:]*: //" >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase -i respects core.commentchar' '
	but reset --hard &&
	but checkout E^0 &&
	test_config core.commentchar "\\" &&
	write_script remove-all-but-first.sh <<-\EOF &&
	sed -e "2,\$s/^/\\\\/" "$1" >"$1.tmp" &&
	mv "$1.tmp" "$1"
	EOF
	(
		test_set_editor "$(pwd)/remove-all-but-first.sh" &&
		but rebase -i B
	) &&
	test B = $(but cat-file commit HEAD^ | sed -ne \$p)
'

test_expect_success 'rebase -i respects core.commentchar=auto' '
	test_config core.commentchar auto &&
	write_script copy-edit-script.sh <<-\EOF &&
	cp "$1" edit-script
	EOF
	test_when_finished "but rebase --abort || :" &&
	(
		test_set_editor "$(pwd)/copy-edit-script.sh" &&
		but rebase -i HEAD^
	) &&
	test -z "$(grep -ve "^#" -e "^\$" -e "^pick" edit-script)"
'

test_expect_success 'rebase -i, with <onto> and <upstream> specified as :/quuxery' '
	test_when_finished "but branch -D torebase" &&
	but checkout -b torebase branch1 &&
	upstream=$(but rev-parse ":/J") &&
	onto=$(but rev-parse ":/A") &&
	but rebase --onto $onto $upstream &&
	but reset --hard branch1 &&
	but rebase --onto ":/A" ":/J" &&
	but checkout branch1
'

test_expect_success 'rebase -i with --strategy and -X' '
	but checkout -b conflict-merge-use-theirs conflict-branch &&
	but reset --hard HEAD^ &&
	echo five >conflict &&
	echo Z >file1 &&
	but cummit -a -m "one file conflict" &&
	EDITOR=true but rebase -i --strategy=recursive -Xours conflict-branch &&
	test $(but show conflict-branch:conflict) = $(cat conflict) &&
	test $(cat file1) = Z
'

test_expect_success 'interrupted rebase -i with --strategy and -X' '
	but checkout -b conflict-merge-use-theirs-interrupted conflict-branch &&
	but reset --hard HEAD^ &&
	>breakpoint &&
	but add breakpoint &&
	but cummit -m "breakpoint for interactive mode" &&
	echo five >conflict &&
	echo Z >file1 &&
	but cummit -a -m "one file conflict" &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1 2" but rebase -i --strategy=recursive \
			-Xours conflict-branch
	) &&
	but rebase --continue &&
	test $(but show conflict-branch:conflict) = $(cat conflict) &&
	test $(cat file1) = Z
'

test_expect_success 'rebase -i error on cummits with \ in message' '
	current_head=$(but rev-parse HEAD) &&
	test_when_finished "but rebase --abort; but reset --hard $current_head; rm -f error" &&
	test_cummit TO-REMOVE will-conflict old-content &&
	test_cummit "\temp" will-conflict new-content dummy &&
	test_must_fail env EDITOR=true but rebase -i HEAD^ --onto HEAD^^ 2>error &&
	test_expect_code 1 grep  "	emp" error
'

test_expect_success 'short cummit ID setup' '
	test_when_finished "but checkout primary" &&
	but checkout --orphan collide &&
	but rm -rf . &&
	(
	unset test_tick &&
	test_cummit collide1 collide &&
	test_cummit --notick collide2 collide &&
	test_cummit --notick collide3 collide
	)
'

if test -n "$GIT_TEST_FIND_COLLIDER"
then
	author="$(unset test_tick; test_tick; but var GIT_AUTHOR_IDENT)"
	cummitter="$(unset test_tick; test_tick; but var GIT_CUMMITTER_IDENT)"
	blob="$(but rev-parse collide2:collide)"
	from="$(but rev-parse collide1^0)"
	repl="cummit refs/heads/collider-&\\n"
	repl="${repl}author $author\\ncummitter $cummitter\\n"
	repl="${repl}data <<EOF\\ncollide2 &\\nEOF\\n"
	repl="${repl}from $from\\nM 100644 $blob collide\\n"
	test_seq 1 32768 | sed "s|.*|$repl|" >script &&
	but fast-import <script &&
	but pack-refs &&
	but for-each-ref >refs &&
	grep "^$(test_oid t3404_collision)" <refs >matches &&
	cat matches &&
	test_line_count -gt 2 matches || {
		echo "Could not find a collider" >&2
		exit 1
	}
fi

test_expect_success 'short cummit ID collide' '
	test_oid_cache <<-EOF &&
	# collision-related constants
	t3404_collision	sha1:6bcd
	t3404_collision	sha256:0161
	t3404_collider	sha1:ac4f2ee
	t3404_collider	sha256:16697
	EOF
	test_when_finished "reset_rebase && but checkout primary" &&
	but checkout collide &&
	colliding_id=$(test_oid t3404_collision) &&
	hexsz=$(test_oid hexsz) &&
	test $colliding_id = "$(but rev-parse HEAD | cut -c 1-4)" &&
	test_config core.abbrev 4 &&
	(
		unset test_tick &&
		test_tick &&
		set_fake_editor &&
		FAKE_CUMMIT_MESSAGE="collide2 $(test_oid t3404_collider)" \
		FAKE_LINES="reword 1 break 2" but rebase -i HEAD~2 &&
		test $colliding_id = "$(but rev-parse HEAD | cut -c 1-4)" &&
		grep "^pick $colliding_id " \
			.but/rebase-merge/but-rebase-todo.tmp &&
		grep "^pick [0-9a-f]\{$hexsz\}" \
			.but/rebase-merge/but-rebase-todo &&
		grep "^pick [0-9a-f]\{$hexsz\}" \
			.but/rebase-merge/but-rebase-todo.backup &&
		but rebase --continue
	) &&
	collide2="$(but rev-parse HEAD~1 | cut -c 1-4)" &&
	collide3="$(but rev-parse collide3 | cut -c 1-4)" &&
	test "$collide2" = "$collide3"
'

test_expect_success 'respect core.abbrev' '
	but config core.abbrev 12 &&
	(
		set_cat_todo_editor &&
		test_must_fail but rebase -i HEAD~4 >todo-list
	) &&
	test 4 = $(grep -c "pick [0-9a-f]\{12,\}" todo-list)
'

test_expect_success 'todo count' '
	write_script dump-raw.sh <<-\EOF &&
		cat "$1"
	EOF
	(
		test_set_editor "$(pwd)/dump-raw.sh" &&
		but rebase -i HEAD~4 >actual
	) &&
	test_i18ngrep "^# Rebase ..* onto ..* ([0-9]" actual
'

test_expect_success 'rebase -i cummits that overwrite untracked files (pick)' '
	but checkout --force branch2 &&
	but clean -f &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1 2" but rebase -i A
	) &&
	test_cmp_rev HEAD F &&
	test_path_is_missing file6 &&
	>file6 &&
	test_must_fail but rebase --continue &&
	test_cmp_rev HEAD F &&
	rm file6 &&
	but rebase --continue &&
	test_cmp_rev HEAD I
'

test_expect_success 'rebase -i cummits that overwrite untracked files (squash)' '
	but checkout --force branch2 &&
	but clean -f &&
	but tag original-branch2 &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1 squash 2" but rebase -i A
	) &&
	test_cmp_rev HEAD F &&
	test_path_is_missing file6 &&
	>file6 &&
	test_must_fail but rebase --continue &&
	test_cmp_rev HEAD F &&
	rm file6 &&
	but rebase --continue &&
	test $(but cat-file commit HEAD | sed -ne \$p) = I &&
	but reset --hard original-branch2
'

test_expect_success 'rebase -i cummits that overwrite untracked files (no ff)' '
	but checkout --force branch2 &&
	but clean -f &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1 2" but rebase -i --no-ff A
	) &&
	test $(but cat-file commit HEAD | sed -ne \$p) = F &&
	test_path_is_missing file6 &&
	>file6 &&
	test_must_fail but rebase --continue &&
	test $(but cat-file commit HEAD | sed -ne \$p) = F &&
	rm file6 &&
	but rebase --continue &&
	test $(but cat-file commit HEAD | sed -ne \$p) = I
'

test_expect_success 'rebase --continue removes CHERRY_PICK_HEAD' '
	but checkout -b cummit-to-skip &&
	for double in X 3 1
	do
		test_seq 5 | sed "s/$double/&&/" >seq &&
		but add seq &&
		test_tick &&
		but cummit -m seq-$double || return 1
	done &&
	but tag seq-onto &&
	but reset --hard HEAD~2 &&
	but cherry-pick seq-onto &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES= but rebase -i seq-onto
	) &&
	test -d .but/rebase-merge &&
	but rebase --continue &&
	but diff --exit-code seq-onto &&
	test ! -d .but/rebase-merge &&
	test ! -f .but/CHERRY_PICK_HEAD
'

rebase_setup_and_clean () {
	test_when_finished "
		but checkout primary &&
		test_might_fail but branch -D $1 &&
		test_might_fail but rebase --abort
	" &&
	but checkout -b $1 ${2:-primary}
}

test_expect_success 'drop' '
	rebase_setup_and_clean drop-test &&
	(
		set_fake_editor &&
		FAKE_LINES="1 drop 2 3 d 4 5" but rebase -i --root
	) &&
	test E = $(but cat-file commit HEAD | sed -ne \$p) &&
	test C = $(but cat-file commit HEAD^ | sed -ne \$p) &&
	test A = $(but cat-file commit HEAD^^ | sed -ne \$p)
'

test_expect_success 'rebase -i respects rebase.missingcummitsCheck = ignore' '
	test_config rebase.missingcummitsCheck ignore &&
	rebase_setup_and_clean missing-cummit &&
	(
		set_fake_editor &&
		FAKE_LINES="1 2 3 4" but rebase -i --root 2>actual
	) &&
	test D = $(but cat-file commit HEAD | sed -ne \$p) &&
	test_i18ngrep \
		"Successfully rebased and updated refs/heads/missing-cummit" \
		actual
'

test_expect_success 'rebase -i respects rebase.missingcummitsCheck = warn' '
	cat >expect <<-EOF &&
	Warning: some cummits may have been dropped accidentally.
	Dropped cummits (newer to older):
	 - $(but rev-list --pretty=oneline --abbrev-cummit -1 primary)
	To avoid this message, use "drop" to explicitly remove a cummit.
	EOF
	test_config rebase.missingcummitsCheck warn &&
	rebase_setup_and_clean missing-cummit &&
	(
		set_fake_editor &&
		FAKE_LINES="1 2 3 4" but rebase -i --root 2>actual.2
	) &&
	head -n4 actual.2 >actual &&
	test_cmp expect actual &&
	test D = $(but cat-file commit HEAD | sed -ne \$p)
'

test_expect_success 'rebase -i respects rebase.missingcummitsCheck = error' '
	cat >expect <<-EOF &&
	Warning: some cummits may have been dropped accidentally.
	Dropped cummits (newer to older):
	 - $(but rev-list --pretty=oneline --abbrev-cummit -1 primary)
	 - $(but rev-list --pretty=oneline --abbrev-cummit -1 primary~2)
	To avoid this message, use "drop" to explicitly remove a cummit.

	Use '\''but config rebase.missingcummitsCheck'\'' to change the level of warnings.
	The possible behaviours are: ignore, warn, error.

	You can fix this with '\''but rebase --edit-todo'\'' and then run '\''but rebase --continue'\''.
	Or you can abort the rebase with '\''but rebase --abort'\''.
	EOF
	test_config rebase.missingcummitsCheck error &&
	rebase_setup_and_clean missing-cummit &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 2 4" \
			but rebase -i --root 2>actual &&
		test_cmp expect actual &&
		cp .but/rebase-merge/but-rebase-todo.backup \
			.but/rebase-merge/but-rebase-todo &&
		FAKE_LINES="1 2 drop 3 4 drop 5" but rebase --edit-todo
	) &&
	but rebase --continue &&
	test D = $(but cat-file commit HEAD | sed -ne \$p) &&
	test B = $(but cat-file commit HEAD^ | sed -ne \$p)
'

test_expect_success 'rebase --edit-todo respects rebase.missingcummitsCheck = ignore' '
	test_config rebase.missingcummitsCheck ignore &&
	rebase_setup_and_clean missing-cummit &&
	(
		set_fake_editor &&
		FAKE_LINES="break 1 2 3 4 5" but rebase -i --root &&
		FAKE_LINES="1 2 3 4" but rebase --edit-todo &&
		but rebase --continue 2>actual
	) &&
	test D = $(but cat-file commit HEAD | sed -ne \$p) &&
	test_i18ngrep \
		"Successfully rebased and updated refs/heads/missing-cummit" \
		actual
'

test_expect_success 'rebase --edit-todo respects rebase.missingcummitsCheck = warn' '
	cat >expect <<-EOF &&
	error: invalid line 1: badcmd $(but rev-list --pretty=oneline --abbrev-cummit -1 primary~4)
	Warning: some cummits may have been dropped accidentally.
	Dropped cummits (newer to older):
	 - $(but rev-list --pretty=oneline --abbrev-cummit -1 primary)
	 - $(but rev-list --pretty=oneline --abbrev-cummit -1 primary~4)
	To avoid this message, use "drop" to explicitly remove a cummit.
	EOF
	head -n4 expect >expect.2 &&
	tail -n1 expect >>expect.2 &&
	tail -n4 expect.2 >expect.3 &&
	test_config rebase.missingcummitsCheck warn &&
	rebase_setup_and_clean missing-cummit &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="bad 1 2 3 4 5" \
			but rebase -i --root &&
		cp .but/rebase-merge/but-rebase-todo.backup orig &&
		FAKE_LINES="2 3 4" but rebase --edit-todo 2>actual.2 &&
		head -n6 actual.2 >actual &&
		test_cmp expect actual &&
		cp orig .but/rebase-merge/but-rebase-todo &&
		FAKE_LINES="1 2 3 4" but rebase --edit-todo 2>actual.2 &&
		head -n4 actual.2 >actual &&
		test_cmp expect.3 actual &&
		but rebase --continue 2>actual
	) &&
	test D = $(but cat-file commit HEAD | sed -ne \$p) &&
	test_i18ngrep \
		"Successfully rebased and updated refs/heads/missing-cummit" \
		actual
'

test_expect_success 'rebase --edit-todo respects rebase.missingcummitsCheck = error' '
	cat >expect <<-EOF &&
	error: invalid line 1: badcmd $(but rev-list --pretty=oneline --abbrev-cummit -1 primary~4)
	Warning: some cummits may have been dropped accidentally.
	Dropped cummits (newer to older):
	 - $(but rev-list --pretty=oneline --abbrev-cummit -1 primary)
	 - $(but rev-list --pretty=oneline --abbrev-cummit -1 primary~4)
	To avoid this message, use "drop" to explicitly remove a cummit.

	Use '\''but config rebase.missingcummitsCheck'\'' to change the level of warnings.
	The possible behaviours are: ignore, warn, error.

	You can fix this with '\''but rebase --edit-todo'\'' and then run '\''but rebase --continue'\''.
	Or you can abort the rebase with '\''but rebase --abort'\''.
	EOF
	tail -n11 expect >expect.2 &&
	head -n3 expect.2 >expect.3 &&
	tail -n7 expect.2 >>expect.3 &&
	test_config rebase.missingcummitsCheck error &&
	rebase_setup_and_clean missing-cummit &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="bad 1 2 3 4 5" \
			but rebase -i --root &&
		cp .but/rebase-merge/but-rebase-todo.backup orig &&
		test_must_fail env FAKE_LINES="2 3 4" \
			but rebase --edit-todo 2>actual &&
		test_cmp expect actual &&
		test_must_fail but rebase --continue 2>actual &&
		test_cmp expect.2 actual &&
		test_must_fail but rebase --edit-todo &&
		cp orig .but/rebase-merge/but-rebase-todo &&
		test_must_fail env FAKE_LINES="1 2 3 4" \
			but rebase --edit-todo 2>actual &&
		test_cmp expect.3 actual &&
		test_must_fail but rebase --continue 2>actual &&
		test_cmp expect.3 actual &&
		cp orig .but/rebase-merge/but-rebase-todo &&
		FAKE_LINES="1 2 3 4 drop 5" but rebase --edit-todo &&
		but rebase --continue 2>actual
	) &&
	test D = $(but cat-file commit HEAD | sed -ne \$p) &&
	test_i18ngrep \
		"Successfully rebased and updated refs/heads/missing-cummit" \
		actual
'

test_expect_success 'rebase.missingcummitsCheck = error after resolving conflicts' '
	test_config rebase.missingcummitsCheck error &&
	(
		set_fake_editor &&
		FAKE_LINES="drop 1 break 2 3 4" but rebase -i A E
	) &&
	but rebase --edit-todo &&
	test_must_fail but rebase --continue &&
	echo x >file1 &&
	but add file1 &&
	but rebase --continue
'

test_expect_success 'rebase.missingcummitsCheck = error when editing for a second time' '
	test_config rebase.missingcummitsCheck error &&
	(
		set_fake_editor &&
		FAKE_LINES="1 break 2 3" but rebase -i A D &&
		cp .but/rebase-merge/but-rebase-todo todo &&
		test_must_fail env FAKE_LINES=2 but rebase --edit-todo &&
		GIT_SEQUENCE_EDITOR="cp todo" but rebase --edit-todo &&
		but rebase --continue
	)
'

test_expect_success 'respects rebase.abbreviateCommands with fixup, squash and exec' '
	rebase_setup_and_clean abbrevcmd &&
	test_cummit "first" file1.txt "first line" first &&
	test_cummit "second" file1.txt "another line" second &&
	test_cummit "fixup! first" file2.txt "first line again" first_fixup &&
	test_cummit "squash! second" file1.txt "another line here" second_squash &&
	cat >expected <<-EOF &&
	p $(but rev-list --abbrev-cummit -1 first) first
	f $(but rev-list --abbrev-cummit -1 first_fixup) fixup! first
	x but show HEAD
	p $(but rev-list --abbrev-cummit -1 second) second
	s $(but rev-list --abbrev-cummit -1 second_squash) squash! second
	x but show HEAD
	EOF
	but checkout abbrevcmd &&
	test_config rebase.abbreviateCommands true &&
	(
		set_cat_todo_editor &&
		test_must_fail but rebase -i --exec "but show HEAD" \
			--autosquash primary >actual
	) &&
	test_cmp expected actual
'

test_expect_success 'static check of bad command' '
	rebase_setup_and_clean bad-cmd &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 2 3 bad 4 5" \
		but rebase -i --root 2>actual &&
		test_i18ngrep "badcmd $(but rev-list --oneline -1 primary~1)" \
				actual &&
		test_i18ngrep "You can fix this with .but rebase --edit-todo.." \
				actual &&
		FAKE_LINES="1 2 3 drop 4 5" but rebase --edit-todo
	) &&
	but rebase --continue &&
	test E = $(but cat-file commit HEAD | sed -ne \$p) &&
	test C = $(but cat-file commit HEAD^ | sed -ne \$p)
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
		but rebase -i HEAD^^^
	) &&
	test E = $(but cat-file commit HEAD | sed -ne \$p)
'

test_expect_success 'static check of bad SHA-1' '
	rebase_setup_and_clean bad-sha &&
	(
		set_fake_editor &&
		test_must_fail env FAKE_LINES="1 2 edit fakesha 3 4 5 #" \
			but rebase -i --root 2>actual &&
			test_i18ngrep "edit XXXXXXX False cummit" actual &&
			test_i18ngrep "You can fix this with .but rebase --edit-todo.." \
					actual &&
		FAKE_LINES="1 2 4 5 6" but rebase --edit-todo
	) &&
	but rebase --continue &&
	test E = $(but cat-file commit HEAD | sed -ne \$p)
'

test_expect_success 'editor saves as CR/LF' '
	but checkout -b with-crlf &&
	write_script add-crs.sh <<-\EOF &&
	sed -e "s/\$/Q/" <"$1" | tr Q "\\015" >"$1".new &&
	mv -f "$1".new "$1"
	EOF
	(
		test_set_editor "$(pwd)/add-crs.sh" &&
		but rebase -i HEAD^
	)
'

test_expect_success 'rebase -i --gpg-sign=<key-id>' '
	test_when_finished "test_might_fail but rebase --abort" &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1" but rebase -i --gpg-sign="\"S I Gner\"" \
			HEAD^ >out 2>err
	) &&
	test_i18ngrep "$SQ-S\"S I Gner\"$SQ" err
'

test_expect_success 'rebase -i --gpg-sign=<key-id> overrides cummit.gpgSign' '
	test_when_finished "test_might_fail but rebase --abort" &&
	test_config cummit.gpgsign true &&
	(
		set_fake_editor &&
		FAKE_LINES="edit 1" but rebase -i --gpg-sign="\"S I Gner\"" \
			HEAD^ >out 2>err
	) &&
	test_i18ngrep "$SQ-S\"S I Gner\"$SQ" err
'

test_expect_success 'valid author header after --root swap' '
	rebase_setup_and_clean author-header no-conflict-branch &&
	but cummit --amend --author="Au ${SQ}thor <author@example.com>" --no-edit &&
	but cat-file commit HEAD | grep ^author >expected &&
	(
		set_fake_editor &&
		FAKE_LINES="5 1" but rebase -i --root
	) &&
	but cat-file commit HEAD^ | grep ^author >actual &&
	test_cmp expected actual
'

test_expect_success 'valid author header when author contains single quote' '
	rebase_setup_and_clean author-header no-conflict-branch &&
	but cummit --amend --author="Au ${SQ}thor <author@example.com>" --no-edit &&
	but cat-file commit HEAD | grep ^author >expected &&
	(
		set_fake_editor &&
		FAKE_LINES="2" but rebase -i HEAD~2
	) &&
	but cat-file commit HEAD | grep ^author >actual &&
	test_cmp expected actual
'

test_expect_success 'post-commit hook is called' '
	>actual &&
	test_hook post-cummit <<-\EOS &&
	but rev-parse HEAD >>actual
	EOS
	(
		set_fake_editor &&
		FAKE_LINES="edit 4 1 reword 2 fixup 3" but rebase -i A E &&
		echo x>file3 &&
		but add file3 &&
		FAKE_CUMMIT_MESSAGE=edited but rebase --continue
	) &&
	but rev-parse HEAD@{5} HEAD@{4} HEAD@{3} HEAD@{2} HEAD@{1} HEAD \
		>expect &&
	test_cmp expect actual
'

test_expect_success 'correct error message for partial cummit after empty pick' '
	test_when_finished "but rebase --abort" &&
	(
		set_fake_editor &&
		FAKE_LINES="2 1 1" &&
		export FAKE_LINES &&
		test_must_fail but rebase -i A D
	) &&
	echo x >file1 &&
	test_must_fail but cummit file1 2>err &&
	test_i18ngrep "cannot do a partial cummit during a rebase." err
'

test_expect_success 'correct error message for cummit --amend after empty pick' '
	test_when_finished "but rebase --abort" &&
	(
		set_fake_editor &&
		FAKE_LINES="1 1" &&
		export FAKE_LINES &&
		test_must_fail but rebase -i A D
	) &&
	echo x>file1 &&
	test_must_fail but cummit -a --amend 2>err &&
	test_i18ngrep "middle of a rebase -- cannot amend." err
'

test_expect_success 'todo has correct onto hash' '
	GIT_SEQUENCE_EDITOR=cat but rebase -i no-conflict-branch~4 no-conflict-branch >actual &&
	onto=$(but rev-parse --short HEAD~4) &&
	test_i18ngrep "^# Rebase ..* onto $onto" actual
'

test_expect_success 'ORIG_HEAD is updated correctly' '
	test_when_finished "but checkout primary && but branch -D test-orig-head" &&
	but checkout -b test-orig-head A &&
	but cummit --allow-empty -m A1 &&
	but cummit --allow-empty -m A2 &&
	but cummit --allow-empty -m A3 &&
	but cummit --allow-empty -m A4 &&
	but rebase primary &&
	test_cmp_rev ORIG_HEAD test-orig-head@{1}
'

# This must be the last test in this file
test_expect_success '$EDITOR and friends are unchanged' '
	test_editor_unchanged
'

test_done
