#!/bin/sh
#
# Copyright (c) 2012 Valentin Duperray, Lucien Kong, Franck Jonas,
#		     Thomas Nguy, Khoi Nguyen
#		     Grenoble INP Ensimag
#

test_description='but status advice'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

set_fake_editor

test_expect_success 'prepare for conflicts' '
	but config --global advice.statusuoption false &&
	test_cummit init main.txt init &&
	but branch conflicts &&
	test_cummit on_main main.txt on_main &&
	but checkout conflicts &&
	test_cummit on_conflicts main.txt on_conflicts
'


test_expect_success 'status when conflicts unresolved' '
	test_must_fail but merge main &&
	cat >expected <<\EOF &&
On branch conflicts
You have unmerged paths.
  (fix conflicts and run "but cummit")
  (use "but merge --abort" to abort the merge)

Unmerged paths:
  (use "but add <file>..." to mark resolution)
	both modified:   main.txt

no changes added to cummit (use "but add" and/or "but cummit -a")
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status when conflicts resolved before cummit' '
	but reset --hard conflicts &&
	test_must_fail but merge main &&
	echo one >main.txt &&
	but add main.txt &&
	cat >expected <<\EOF &&
On branch conflicts
All conflicts fixed but you are still merging.
  (use "but cummit" to conclude merge)

Changes to be cummitted:
	modified:   main.txt

Untracked files not listed (use -u option to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'prepare for rebase conflicts' '
	but reset --hard main &&
	but checkout -b rebase_conflicts &&
	test_cummit one_rebase main.txt one &&
	test_cummit two_rebase main.txt two &&
	test_cummit three_rebase main.txt three
'


test_expect_success 'status when rebase --apply in progress before resolving conflicts' '
	test_when_finished "but rebase --abort" &&
	ONTO=$(but rev-parse --short HEAD^^) &&
	test_must_fail but rebase --apply HEAD^ --onto HEAD^^ &&
	cat >expected <<EOF &&
rebase in progress; onto $ONTO
You are currently rebasing branch '\''rebase_conflicts'\'' on '\''$ONTO'\''.
  (fix conflicts and then run "but rebase --continue")
  (use "but rebase --skip" to skip this patch)
  (use "but rebase --abort" to check out the original branch)

Unmerged paths:
  (use "but restore --staged <file>..." to unstage)
  (use "but add <file>..." to mark resolution)
	both modified:   main.txt

no changes added to cummit (use "but add" and/or "but cummit -a")
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status when rebase --apply in progress before rebase --continue' '
	but reset --hard rebase_conflicts &&
	test_when_finished "but rebase --abort" &&
	ONTO=$(but rev-parse --short HEAD^^) &&
	test_must_fail but rebase --apply HEAD^ --onto HEAD^^ &&
	echo three >main.txt &&
	but add main.txt &&
	cat >expected <<EOF &&
rebase in progress; onto $ONTO
You are currently rebasing branch '\''rebase_conflicts'\'' on '\''$ONTO'\''.
  (all conflicts fixed: run "but rebase --continue")

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	modified:   main.txt

Untracked files not listed (use -u option to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'prepare for rebase_i_conflicts' '
	but reset --hard main &&
	but checkout -b rebase_i_conflicts &&
	test_cummit one_unmerge main.txt one_unmerge &&
	but branch rebase_i_conflicts_second &&
	test_cummit one_main main.txt one_main &&
	but checkout rebase_i_conflicts_second &&
	test_cummit one_second main.txt one_second
'


test_expect_success 'status during rebase -i when conflicts unresolved' '
	test_when_finished "but rebase --abort" &&
	ONTO=$(but rev-parse --short rebase_i_conflicts) &&
	LAST_CUMMIT=$(but rev-parse --short rebase_i_conflicts_second) &&
	test_must_fail but rebase -i rebase_i_conflicts &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last command done (1 command done):
   pick $LAST_CUMMIT one_second
No commands remaining.
You are currently rebasing branch '\''rebase_i_conflicts_second'\'' on '\''$ONTO'\''.
  (fix conflicts and then run "but rebase --continue")
  (use "but rebase --skip" to skip this patch)
  (use "but rebase --abort" to check out the original branch)

Unmerged paths:
  (use "but restore --staged <file>..." to unstage)
  (use "but add <file>..." to mark resolution)
	both modified:   main.txt

no changes added to cummit (use "but add" and/or "but cummit -a")
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status during rebase -i after resolving conflicts' '
	but reset --hard rebase_i_conflicts_second &&
	test_when_finished "but rebase --abort" &&
	ONTO=$(but rev-parse --short rebase_i_conflicts) &&
	LAST_CUMMIT=$(but rev-parse --short rebase_i_conflicts_second) &&
	test_must_fail but rebase -i rebase_i_conflicts &&
	but add main.txt &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last command done (1 command done):
   pick $LAST_CUMMIT one_second
No commands remaining.
You are currently rebasing branch '\''rebase_i_conflicts_second'\'' on '\''$ONTO'\''.
  (all conflicts fixed: run "but rebase --continue")

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	modified:   main.txt

Untracked files not listed (use -u option to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status when rebasing -i in edit mode' '
	but reset --hard main &&
	but checkout -b rebase_i_edit &&
	test_cummit one_rebase_i main.txt one &&
	test_cummit two_rebase_i main.txt two &&
	cummit2=$(but rev-parse --short rebase_i_edit) &&
	test_cummit three_rebase_i main.txt three &&
	cummit3=$(but rev-parse --short rebase_i_edit) &&
	FAKE_LINES="1 edit 2" &&
	export FAKE_LINES &&
	test_when_finished "but rebase --abort" &&
	ONTO=$(but rev-parse --short HEAD~2) &&
	but rebase -i HEAD~2 &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   pick $cummit2 two_rebase_i
   edit $cummit3 three_rebase_i
No commands remaining.
You are currently editing a cummit while rebasing branch '\''rebase_i_edit'\'' on '\''$ONTO'\''.
  (use "but cummit --amend" to amend the current cummit)
  (use "but rebase --continue" once you are satisfied with your changes)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status when splitting a cummit' '
	but reset --hard main &&
	but checkout -b split_cummit &&
	test_cummit one_split main.txt one &&
	test_cummit two_split main.txt two &&
	cummit2=$(but rev-parse --short split_cummit) &&
	test_cummit three_split main.txt three &&
	cummit3=$(but rev-parse --short split_cummit) &&
	test_cummit four_split main.txt four &&
	cummit4=$(but rev-parse --short split_cummit) &&
	FAKE_LINES="1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "but rebase --abort" &&
	ONTO=$(but rev-parse --short HEAD~3) &&
	but rebase -i HEAD~3 &&
	but reset HEAD^ &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   pick $cummit2 two_split
   edit $cummit3 three_split
Next command to do (1 remaining command):
   pick $cummit4 four_split
  (use "but rebase --edit-todo" to view and edit)
You are currently splitting a cummit while rebasing branch '\''split_cummit'\'' on '\''$ONTO'\''.
  (Once your working directory is clean, run "but rebase --continue")

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   main.txt

no changes added to cummit (use "but add" and/or "but cummit -a")
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status after editing the last cummit with --amend during a rebase -i' '
	but reset --hard main &&
	but checkout -b amend_last &&
	test_cummit one_amend main.txt one &&
	test_cummit two_amend main.txt two &&
	test_cummit three_amend main.txt three &&
	cummit3=$(but rev-parse --short amend_last) &&
	test_cummit four_amend main.txt four &&
	cummit4=$(but rev-parse --short amend_last) &&
	FAKE_LINES="1 2 edit 3" &&
	export FAKE_LINES &&
	test_when_finished "but rebase --abort" &&
	ONTO=$(but rev-parse --short HEAD~3) &&
	but rebase -i HEAD~3 &&
	but cummit --amend -m "foo" &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (3 commands done):
   pick $cummit3 three_amend
   edit $cummit4 four_amend
  (see more in file .but/rebase-merge/done)
No commands remaining.
You are currently editing a cummit while rebasing branch '\''amend_last'\'' on '\''$ONTO'\''.
  (use "but cummit --amend" to amend the current cummit)
  (use "but rebase --continue" once you are satisfied with your changes)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'prepare for several edits' '
	but reset --hard main &&
	but checkout -b several_edits &&
	test_cummit one_edits main.txt one &&
	test_cummit two_edits main.txt two &&
	test_cummit three_edits main.txt three &&
	test_cummit four_edits main.txt four
'


test_expect_success 'status: (continue first edit) second edit' '
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "but rebase --abort" &&
	cummit2=$(but rev-parse --short several_edits^^) &&
	cummit3=$(but rev-parse --short several_edits^) &&
	cummit4=$(but rev-parse --short several_edits) &&
	ONTO=$(but rev-parse --short HEAD~3) &&
	but rebase -i HEAD~3 &&
	but rebase --continue &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $cummit2 two_edits
   edit $cummit3 three_edits
Next command to do (1 remaining command):
   pick $cummit4 four_edits
  (use "but rebase --edit-todo" to view and edit)
You are currently editing a cummit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (use "but cummit --amend" to amend the current cummit)
  (use "but rebase --continue" once you are satisfied with your changes)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status: (continue first edit) second edit and split' '
	but reset --hard several_edits &&
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "but rebase --abort" &&
	cummit2=$(but rev-parse --short several_edits^^) &&
	cummit3=$(but rev-parse --short several_edits^) &&
	cummit4=$(but rev-parse --short several_edits) &&
	ONTO=$(but rev-parse --short HEAD~3) &&
	but rebase -i HEAD~3 &&
	but rebase --continue &&
	but reset HEAD^ &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $cummit2 two_edits
   edit $cummit3 three_edits
Next command to do (1 remaining command):
   pick $cummit4 four_edits
  (use "but rebase --edit-todo" to view and edit)
You are currently splitting a cummit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (Once your working directory is clean, run "but rebase --continue")

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   main.txt

no changes added to cummit (use "but add" and/or "but cummit -a")
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status: (continue first edit) second edit and amend' '
	but reset --hard several_edits &&
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "but rebase --abort" &&
	cummit2=$(but rev-parse --short several_edits^^) &&
	cummit3=$(but rev-parse --short several_edits^) &&
	cummit4=$(but rev-parse --short several_edits) &&
	ONTO=$(but rev-parse --short HEAD~3) &&
	but rebase -i HEAD~3 &&
	but rebase --continue &&
	but cummit --amend -m "foo" &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $cummit2 two_edits
   edit $cummit3 three_edits
Next command to do (1 remaining command):
   pick $cummit4 four_edits
  (use "but rebase --edit-todo" to view and edit)
You are currently editing a cummit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (use "but cummit --amend" to amend the current cummit)
  (use "but rebase --continue" once you are satisfied with your changes)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status: (amend first edit) second edit' '
	but reset --hard several_edits &&
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "but rebase --abort" &&
	cummit2=$(but rev-parse --short several_edits^^) &&
	cummit3=$(but rev-parse --short several_edits^) &&
	cummit4=$(but rev-parse --short several_edits) &&
	ONTO=$(but rev-parse --short HEAD~3) &&
	but rebase -i HEAD~3 &&
	but cummit --amend -m "a" &&
	but rebase --continue &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $cummit2 two_edits
   edit $cummit3 three_edits
Next command to do (1 remaining command):
   pick $cummit4 four_edits
  (use "but rebase --edit-todo" to view and edit)
You are currently editing a cummit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (use "but cummit --amend" to amend the current cummit)
  (use "but rebase --continue" once you are satisfied with your changes)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status: (amend first edit) second edit and split' '
	but reset --hard several_edits &&
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "but rebase --abort" &&
	ONTO=$(but rev-parse --short HEAD~3) &&
	cummit2=$(but rev-parse --short several_edits^^) &&
	cummit3=$(but rev-parse --short several_edits^) &&
	cummit4=$(but rev-parse --short several_edits) &&
	but rebase -i HEAD~3 &&
	but cummit --amend -m "b" &&
	but rebase --continue &&
	but reset HEAD^ &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $cummit2 two_edits
   edit $cummit3 three_edits
Next command to do (1 remaining command):
   pick $cummit4 four_edits
  (use "but rebase --edit-todo" to view and edit)
You are currently splitting a cummit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (Once your working directory is clean, run "but rebase --continue")

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   main.txt

no changes added to cummit (use "but add" and/or "but cummit -a")
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status: (amend first edit) second edit and amend' '
	but reset --hard several_edits &&
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "but rebase --abort" &&
	cummit2=$(but rev-parse --short several_edits^^) &&
	cummit3=$(but rev-parse --short several_edits^) &&
	cummit4=$(but rev-parse --short several_edits) &&
	ONTO=$(but rev-parse --short HEAD~3) &&
	but rebase -i HEAD~3 &&
	but cummit --amend -m "c" &&
	but rebase --continue &&
	but cummit --amend -m "d" &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $cummit2 two_edits
   edit $cummit3 three_edits
Next command to do (1 remaining command):
   pick $cummit4 four_edits
  (use "but rebase --edit-todo" to view and edit)
You are currently editing a cummit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (use "but cummit --amend" to amend the current cummit)
  (use "but rebase --continue" once you are satisfied with your changes)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status: (split first edit) second edit' '
	but reset --hard several_edits &&
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "but rebase --abort" &&
	cummit2=$(but rev-parse --short several_edits^^) &&
	cummit3=$(but rev-parse --short several_edits^) &&
	cummit4=$(but rev-parse --short several_edits) &&
	ONTO=$(but rev-parse --short HEAD~3) &&
	but rebase -i HEAD~3 &&
	but reset HEAD^ &&
	but add main.txt &&
	but cummit -m "e" &&
	but rebase --continue &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $cummit2 two_edits
   edit $cummit3 three_edits
Next command to do (1 remaining command):
   pick $cummit4 four_edits
  (use "but rebase --edit-todo" to view and edit)
You are currently editing a cummit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (use "but cummit --amend" to amend the current cummit)
  (use "but rebase --continue" once you are satisfied with your changes)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status: (split first edit) second edit and split' '
	but reset --hard several_edits &&
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "but rebase --abort" &&
	cummit2=$(but rev-parse --short several_edits^^) &&
	cummit3=$(but rev-parse --short several_edits^) &&
	cummit4=$(but rev-parse --short several_edits) &&
	ONTO=$(but rev-parse --short HEAD~3) &&
	but rebase -i HEAD~3 &&
	but reset HEAD^ &&
	but add main.txt &&
	but cummit --amend -m "f" &&
	but rebase --continue &&
	but reset HEAD^ &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $cummit2 two_edits
   edit $cummit3 three_edits
Next command to do (1 remaining command):
   pick $cummit4 four_edits
  (use "but rebase --edit-todo" to view and edit)
You are currently splitting a cummit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (Once your working directory is clean, run "but rebase --continue")

Changes not staged for cummit:
  (use "but add <file>..." to update what will be cummitted)
  (use "but restore <file>..." to discard changes in working directory)
	modified:   main.txt

no changes added to cummit (use "but add" and/or "but cummit -a")
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status: (split first edit) second edit and amend' '
	but reset --hard several_edits &&
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "but rebase --abort" &&
	cummit2=$(but rev-parse --short several_edits^^) &&
	cummit3=$(but rev-parse --short several_edits^) &&
	cummit4=$(but rev-parse --short several_edits) &&
	ONTO=$(but rev-parse --short HEAD~3) &&
	but rebase -i HEAD~3 &&
	but reset HEAD^ &&
	but add main.txt &&
	but cummit --amend -m "g" &&
	but rebase --continue &&
	but cummit --amend -m "h" &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $cummit2 two_edits
   edit $cummit3 three_edits
Next command to do (1 remaining command):
   pick $cummit4 four_edits
  (use "but rebase --edit-todo" to view and edit)
You are currently editing a cummit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (use "but cummit --amend" to amend the current cummit)
  (use "but rebase --continue" once you are satisfied with your changes)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'prepare am_session' '
	but reset --hard main &&
	but checkout -b am_session &&
	test_cummit one_am one.txt "one" &&
	test_cummit two_am two.txt "two" &&
	test_cummit three_am three.txt "three"
'


test_expect_success 'status in an am session: file already exists' '
	but checkout -b am_already_exists &&
	test_when_finished "rm Maildir/* && but am --abort" &&
	but format-patch -1 -oMaildir &&
	test_must_fail but am Maildir/*.patch &&
	cat >expected <<\EOF &&
On branch am_already_exists
You are in the middle of an am session.
  (fix conflicts and then run "but am --continue")
  (use "but am --skip" to skip this patch)
  (use "but am --abort" to restore the original branch)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status in an am session: file does not exist' '
	but reset --hard am_session &&
	but checkout -b am_not_exists &&
	but rm three.txt &&
	but cummit -m "delete three.txt" &&
	test_when_finished "rm Maildir/* && but am --abort" &&
	but format-patch -1 -oMaildir &&
	test_must_fail but am Maildir/*.patch &&
	cat >expected <<\EOF &&
On branch am_not_exists
You are in the middle of an am session.
  (fix conflicts and then run "but am --continue")
  (use "but am --skip" to skip this patch)
  (use "but am --abort" to restore the original branch)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status in an am session: empty patch' '
	but reset --hard am_session &&
	but checkout -b am_empty &&
	test_when_finished "rm Maildir/* && but am --abort" &&
	but format-patch -3 -oMaildir &&
	but rm one.txt two.txt three.txt &&
	but cummit -m "delete all am_empty" &&
	echo error >Maildir/0002-two_am.patch &&
	test_must_fail but am Maildir/*.patch &&
	cat >expected <<\EOF &&
On branch am_empty
You are in the middle of an am session.
The current patch is empty.
  (use "but am --skip" to skip this patch)
  (use "but am --allow-empty" to record this patch as an empty cummit)
  (use "but am --abort" to restore the original branch)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status when bisecting' '
	but reset --hard main &&
	but checkout -b bisect &&
	test_cummit one_bisect main.txt one &&
	test_cummit two_bisect main.txt two &&
	test_cummit three_bisect main.txt three &&
	test_when_finished "but bisect reset" &&
	but bisect start &&
	but bisect bad &&
	but bisect good one_bisect &&
	TGT=$(but rev-parse --short two_bisect) &&
	cat >expected <<EOF &&
HEAD detached at $TGT
You are currently bisecting, started from branch '\''bisect'\''.
  (use "but bisect reset" to get back to the original branch)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status when rebase --apply conflicts with statushints disabled' '
	but reset --hard main &&
	but checkout -b statushints_disabled &&
	test_when_finished "but config --local advice.statushints true" &&
	but config --local advice.statushints false &&
	test_cummit one_statushints main.txt one &&
	test_cummit two_statushints main.txt two &&
	test_cummit three_statushints main.txt three &&
	test_when_finished "but rebase --abort" &&
	ONTO=$(but rev-parse --short HEAD^^) &&
	test_must_fail but rebase --apply HEAD^ --onto HEAD^^ &&
	cat >expected <<EOF &&
rebase in progress; onto $ONTO
You are currently rebasing branch '\''statushints_disabled'\'' on '\''$ONTO'\''.

Unmerged paths:
	both modified:   main.txt

no changes added to cummit
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'prepare for cherry-pick conflicts' '
	but reset --hard main &&
	but checkout -b cherry_branch &&
	test_cummit one_cherry main.txt one &&
	test_cummit two_cherries main.txt two &&
	but checkout -b cherry_branch_second &&
	test_cummit second_cherry main.txt second &&
	but checkout cherry_branch &&
	test_cummit three_cherries main.txt three
'


test_expect_success 'status when cherry-picking before resolving conflicts' '
	test_when_finished "but cherry-pick --abort" &&
	test_must_fail but cherry-pick cherry_branch_second &&
	TO_CHERRY_PICK=$(but rev-parse --short CHERRY_PICK_HEAD) &&
	cat >expected <<EOF &&
On branch cherry_branch
You are currently cherry-picking cummit $TO_CHERRY_PICK.
  (fix conflicts and run "but cherry-pick --continue")
  (use "but cherry-pick --skip" to skip this patch)
  (use "but cherry-pick --abort" to cancel the cherry-pick operation)

Unmerged paths:
  (use "but add <file>..." to mark resolution)
	both modified:   main.txt

no changes added to cummit (use "but add" and/or "but cummit -a")
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status when cherry-picking after resolving conflicts' '
	but reset --hard cherry_branch &&
	test_when_finished "but cherry-pick --abort" &&
	test_must_fail but cherry-pick cherry_branch_second &&
	TO_CHERRY_PICK=$(but rev-parse --short CHERRY_PICK_HEAD) &&
	echo end >main.txt &&
	but add main.txt &&
	cat >expected <<EOF &&
On branch cherry_branch
You are currently cherry-picking cummit $TO_CHERRY_PICK.
  (all conflicts fixed: run "but cherry-pick --continue")
  (use "but cherry-pick --skip" to skip this patch)
  (use "but cherry-pick --abort" to cancel the cherry-pick operation)

Changes to be cummitted:
	modified:   main.txt

Untracked files not listed (use -u option to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'

test_expect_success 'status when cherry-picking after cummitting conflict resolution' '
	but reset --hard cherry_branch &&
	test_when_finished "but cherry-pick --abort" &&
	test_must_fail but cherry-pick cherry_branch_second one_cherry &&
	echo end >main.txt &&
	but cummit -a &&
	cat >expected <<EOF &&
On branch cherry_branch
Cherry-pick currently in progress.
  (run "but cherry-pick --continue" to continue)
  (use "but cherry-pick --skip" to skip this patch)
  (use "but cherry-pick --abort" to cancel the cherry-pick operation)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'

test_expect_success 'status shows cherry-pick with invalid oid' '
	mkdir .but/sequencer &&
	test_write_lines "pick invalid-oid" >.but/sequencer/todo &&
	but status --untracked-files=no >actual 2>err &&
	but cherry-pick --quit &&
	test_must_be_empty err &&
	test_cmp expected actual
'

test_expect_success 'status does not show error if .but/sequencer is a file' '
	test_when_finished "rm .but/sequencer" &&
	test_write_lines hello >.but/sequencer &&
	but status --untracked-files=no 2>err &&
	test_must_be_empty err
'

test_expect_success 'status showing detached at and from a tag' '
	test_cummit atag tagging &&
	but checkout atag &&
	cat >expected <<\EOF &&
HEAD detached at atag
nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual &&

	but reset --hard HEAD^ &&
	cat >expected <<\EOF &&
HEAD detached from atag
nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'

test_expect_success 'status while reverting cummit (conflicts)' '
	but checkout main &&
	echo before >to-revert.txt &&
	test_cummit before to-revert.txt &&
	echo old >to-revert.txt &&
	test_cummit old to-revert.txt &&
	echo new >to-revert.txt &&
	test_cummit new to-revert.txt &&
	TO_REVERT=$(but rev-parse --short HEAD^) &&
	test_must_fail but revert $TO_REVERT &&
	cat >expected <<EOF &&
On branch main
You are currently reverting cummit $TO_REVERT.
  (fix conflicts and run "but revert --continue")
  (use "but revert --skip" to skip this patch)
  (use "but revert --abort" to cancel the revert operation)

Unmerged paths:
  (use "but restore --staged <file>..." to unstage)
  (use "but add <file>..." to mark resolution)
	both modified:   to-revert.txt

no changes added to cummit (use "but add" and/or "but cummit -a")
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'

test_expect_success 'status while reverting cummit (conflicts resolved)' '
	echo reverted >to-revert.txt &&
	but add to-revert.txt &&
	cat >expected <<EOF &&
On branch main
You are currently reverting cummit $TO_REVERT.
  (all conflicts fixed: run "but revert --continue")
  (use "but revert --skip" to skip this patch)
  (use "but revert --abort" to cancel the revert operation)

Changes to be cummitted:
  (use "but restore --staged <file>..." to unstage)
	modified:   to-revert.txt

Untracked files not listed (use -u option to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'

test_expect_success 'status after reverting cummit' '
	but revert --continue &&
	cat >expected <<\EOF &&
On branch main
nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'

test_expect_success 'status while reverting after cummitting conflict resolution' '
	test_when_finished "but revert --abort" &&
	but reset --hard new &&
	test_must_fail but revert old new &&
	echo reverted >to-revert.txt &&
	but cummit -a &&
	cat >expected <<EOF &&
On branch main
Revert currently in progress.
  (run "but revert --continue" to continue)
  (use "but revert --skip" to skip this patch)
  (use "but revert --abort" to cancel the revert operation)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'

test_expect_success 'prepare for different number of cummits rebased' '
	but reset --hard main &&
	but checkout -b several_cummits &&
	test_cummit one_cummit main.txt one &&
	test_cummit two_cummit main.txt two &&
	test_cummit three_cummit main.txt three &&
	test_cummit four_cummit main.txt four
'

test_expect_success 'status: one command done nothing remaining' '
	FAKE_LINES="exec_exit_15" &&
	export FAKE_LINES &&
	test_when_finished "but rebase --abort" &&
	ONTO=$(but rev-parse --short HEAD~3) &&
	test_must_fail but rebase -i HEAD~3 &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last command done (1 command done):
   exec exit 15
No commands remaining.
You are currently editing a cummit while rebasing branch '\''several_cummits'\'' on '\''$ONTO'\''.
  (use "but cummit --amend" to amend the current cummit)
  (use "but rebase --continue" once you are satisfied with your changes)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'

test_expect_success 'status: two commands done with some white lines in done file' '
	FAKE_LINES="1 > exec_exit_15  2 3" &&
	export FAKE_LINES &&
	test_when_finished "but rebase --abort" &&
	ONTO=$(but rev-parse --short HEAD~3) &&
	cummit4=$(but rev-parse --short HEAD) &&
	cummit3=$(but rev-parse --short HEAD^) &&
	cummit2=$(but rev-parse --short HEAD^^) &&
	test_must_fail but rebase -i HEAD~3 &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   pick $cummit2 two_cummit
   exec exit 15
Next commands to do (2 remaining commands):
   pick $cummit3 three_cummit
   pick $cummit4 four_cummit
  (use "but rebase --edit-todo" to view and edit)
You are currently editing a cummit while rebasing branch '\''several_cummits'\'' on '\''$ONTO'\''.
  (use "but cummit --amend" to amend the current cummit)
  (use "but rebase --continue" once you are satisfied with your changes)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'

test_expect_success 'status: two remaining commands with some white lines in todo file' '
	FAKE_LINES="1 2 exec_exit_15 3 > 4" &&
	export FAKE_LINES &&
	test_when_finished "but rebase --abort" &&
	ONTO=$(but rev-parse --short HEAD~4) &&
	cummit4=$(but rev-parse --short HEAD) &&
	cummit3=$(but rev-parse --short HEAD^) &&
	cummit2=$(but rev-parse --short HEAD^^) &&
	test_must_fail but rebase -i HEAD~4 &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (3 commands done):
   pick $cummit2 two_cummit
   exec exit 15
  (see more in file .but/rebase-merge/done)
Next commands to do (2 remaining commands):
   pick $cummit3 three_cummit
   pick $cummit4 four_cummit
  (use "but rebase --edit-todo" to view and edit)
You are currently editing a cummit while rebasing branch '\''several_cummits'\'' on '\''$ONTO'\''.
  (use "but cummit --amend" to amend the current cummit)
  (use "but rebase --continue" once you are satisfied with your changes)

nothing to cummit (use -u to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'

test_expect_success 'status: handle not-yet-started rebase -i gracefully' '
	ONTO=$(but rev-parse --short HEAD^) &&
	cummit=$(but rev-parse --short HEAD) &&
	EDITOR="but status --untracked-files=no >actual" but rebase -i HEAD^ &&
	cat >expected <<EOF &&
On branch several_cummits
No commands done.
Next command to do (1 remaining command):
   pick $cummit four_cummit
  (use "but rebase --edit-todo" to view and edit)
You are currently editing a cummit while rebasing branch '\''several_cummits'\'' on '\''$ONTO'\''.
  (use "but cummit --amend" to amend the current cummit)
  (use "but rebase --continue" once you are satisfied with your changes)

nothing to cummit (use -u to show untracked files)
EOF
	test_cmp expected actual
'

test_done
