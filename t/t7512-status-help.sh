#!/bin/sh
#
# Copyright (c) 2012 Valentin Duperray, Lucien Kong, Franck Jonas,
#		     Thomas Nguy, Khoi Nguyen
#		     Grenoble INP Ensimag
#

test_description='git status advice'

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-rebase.sh

set_fake_editor

test_expect_success 'prepare for conflicts' '
	git config --global advice.statusuoption false &&
	test_commit init main.txt init &&
	git branch conflicts &&
	test_commit on_master main.txt on_master &&
	git checkout conflicts &&
	test_commit on_conflicts main.txt on_conflicts
'


test_expect_success 'status when conflicts unresolved' '
	test_must_fail git merge master &&
	cat >expected <<\EOF &&
On branch conflicts
You have unmerged paths.
  (fix conflicts and run "git commit")
  (use "git merge --abort" to abort the merge)

Unmerged paths:
  (use "git add <file>..." to mark resolution)
	both modified:   main.txt

no changes added to commit (use "git add" and/or "git commit -a")
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status when conflicts resolved before commit' '
	git reset --hard conflicts &&
	test_must_fail git merge master &&
	echo one >main.txt &&
	git add main.txt &&
	cat >expected <<\EOF &&
On branch conflicts
All conflicts fixed but you are still merging.
  (use "git commit" to conclude merge)

Changes to be committed:
	modified:   main.txt

Untracked files not listed (use -u option to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'prepare for rebase conflicts' '
	git reset --hard master &&
	git checkout -b rebase_conflicts &&
	test_commit one_rebase main.txt one &&
	test_commit two_rebase main.txt two &&
	test_commit three_rebase main.txt three
'


test_expect_success 'status when rebase in progress before resolving conflicts' '
	test_when_finished "git rebase --abort" &&
	ONTO=$(git rev-parse --short HEAD^^) &&
	test_must_fail git rebase HEAD^ --onto HEAD^^ &&
	cat >expected <<EOF &&
rebase in progress; onto $ONTO
You are currently rebasing branch '\''rebase_conflicts'\'' on '\''$ONTO'\''.
  (fix conflicts and then run "git rebase --continue")
  (use "git rebase --skip" to skip this patch)
  (use "git rebase --abort" to check out the original branch)

Unmerged paths:
  (use "git restore --staged <file>..." to unstage)
  (use "git add <file>..." to mark resolution)
	both modified:   main.txt

no changes added to commit (use "git add" and/or "git commit -a")
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status when rebase in progress before rebase --continue' '
	git reset --hard rebase_conflicts &&
	test_when_finished "git rebase --abort" &&
	ONTO=$(git rev-parse --short HEAD^^) &&
	test_must_fail git rebase HEAD^ --onto HEAD^^ &&
	echo three >main.txt &&
	git add main.txt &&
	cat >expected <<EOF &&
rebase in progress; onto $ONTO
You are currently rebasing branch '\''rebase_conflicts'\'' on '\''$ONTO'\''.
  (all conflicts fixed: run "git rebase --continue")

Changes to be committed:
  (use "git restore --staged <file>..." to unstage)
	modified:   main.txt

Untracked files not listed (use -u option to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'prepare for rebase_i_conflicts' '
	git reset --hard master &&
	git checkout -b rebase_i_conflicts &&
	test_commit one_unmerge main.txt one_unmerge &&
	git branch rebase_i_conflicts_second &&
	test_commit one_master main.txt one_master &&
	git checkout rebase_i_conflicts_second &&
	test_commit one_second main.txt one_second
'


test_expect_success 'status during rebase -i when conflicts unresolved' '
	test_when_finished "git rebase --abort" &&
	ONTO=$(git rev-parse --short rebase_i_conflicts) &&
	LAST_COMMIT=$(git rev-parse --short rebase_i_conflicts_second) &&
	test_must_fail git rebase -i rebase_i_conflicts &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last command done (1 command done):
   pick $LAST_COMMIT one_second
No commands remaining.
You are currently rebasing branch '\''rebase_i_conflicts_second'\'' on '\''$ONTO'\''.
  (fix conflicts and then run "git rebase --continue")
  (use "git rebase --skip" to skip this patch)
  (use "git rebase --abort" to check out the original branch)

Unmerged paths:
  (use "git restore --staged <file>..." to unstage)
  (use "git add <file>..." to mark resolution)
	both modified:   main.txt

no changes added to commit (use "git add" and/or "git commit -a")
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status during rebase -i after resolving conflicts' '
	git reset --hard rebase_i_conflicts_second &&
	test_when_finished "git rebase --abort" &&
	ONTO=$(git rev-parse --short rebase_i_conflicts) &&
	LAST_COMMIT=$(git rev-parse --short rebase_i_conflicts_second) &&
	test_must_fail git rebase -i rebase_i_conflicts &&
	git add main.txt &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last command done (1 command done):
   pick $LAST_COMMIT one_second
No commands remaining.
You are currently rebasing branch '\''rebase_i_conflicts_second'\'' on '\''$ONTO'\''.
  (all conflicts fixed: run "git rebase --continue")

Changes to be committed:
  (use "git restore --staged <file>..." to unstage)
	modified:   main.txt

Untracked files not listed (use -u option to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status when rebasing -i in edit mode' '
	git reset --hard master &&
	git checkout -b rebase_i_edit &&
	test_commit one_rebase_i main.txt one &&
	test_commit two_rebase_i main.txt two &&
	COMMIT2=$(git rev-parse --short rebase_i_edit) &&
	test_commit three_rebase_i main.txt three &&
	COMMIT3=$(git rev-parse --short rebase_i_edit) &&
	FAKE_LINES="1 edit 2" &&
	export FAKE_LINES &&
	test_when_finished "git rebase --abort" &&
	ONTO=$(git rev-parse --short HEAD~2) &&
	git rebase -i HEAD~2 &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   pick $COMMIT2 two_rebase_i
   edit $COMMIT3 three_rebase_i
No commands remaining.
You are currently editing a commit while rebasing branch '\''rebase_i_edit'\'' on '\''$ONTO'\''.
  (use "git commit --amend" to amend the current commit)
  (use "git rebase --continue" once you are satisfied with your changes)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status when splitting a commit' '
	git reset --hard master &&
	git checkout -b split_commit &&
	test_commit one_split main.txt one &&
	test_commit two_split main.txt two &&
	COMMIT2=$(git rev-parse --short split_commit) &&
	test_commit three_split main.txt three &&
	COMMIT3=$(git rev-parse --short split_commit) &&
	test_commit four_split main.txt four &&
	COMMIT4=$(git rev-parse --short split_commit) &&
	FAKE_LINES="1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "git rebase --abort" &&
	ONTO=$(git rev-parse --short HEAD~3) &&
	git rebase -i HEAD~3 &&
	git reset HEAD^ &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   pick $COMMIT2 two_split
   edit $COMMIT3 three_split
Next command to do (1 remaining command):
   pick $COMMIT4 four_split
  (use "git rebase --edit-todo" to view and edit)
You are currently splitting a commit while rebasing branch '\''split_commit'\'' on '\''$ONTO'\''.
  (Once your working directory is clean, run "git rebase --continue")

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   main.txt

no changes added to commit (use "git add" and/or "git commit -a")
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status after editing the last commit with --amend during a rebase -i' '
	git reset --hard master &&
	git checkout -b amend_last &&
	test_commit one_amend main.txt one &&
	test_commit two_amend main.txt two &&
	test_commit three_amend main.txt three &&
	COMMIT3=$(git rev-parse --short amend_last) &&
	test_commit four_amend main.txt four &&
	COMMIT4=$(git rev-parse --short amend_last) &&
	FAKE_LINES="1 2 edit 3" &&
	export FAKE_LINES &&
	test_when_finished "git rebase --abort" &&
	ONTO=$(git rev-parse --short HEAD~3) &&
	git rebase -i HEAD~3 &&
	git commit --amend -m "foo" &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (3 commands done):
   pick $COMMIT3 three_amend
   edit $COMMIT4 four_amend
  (see more in file .git/rebase-merge/done)
No commands remaining.
You are currently editing a commit while rebasing branch '\''amend_last'\'' on '\''$ONTO'\''.
  (use "git commit --amend" to amend the current commit)
  (use "git rebase --continue" once you are satisfied with your changes)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'prepare for several edits' '
	git reset --hard master &&
	git checkout -b several_edits &&
	test_commit one_edits main.txt one &&
	test_commit two_edits main.txt two &&
	test_commit three_edits main.txt three &&
	test_commit four_edits main.txt four
'


test_expect_success 'status: (continue first edit) second edit' '
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "git rebase --abort" &&
	COMMIT2=$(git rev-parse --short several_edits^^) &&
	COMMIT3=$(git rev-parse --short several_edits^) &&
	COMMIT4=$(git rev-parse --short several_edits) &&
	ONTO=$(git rev-parse --short HEAD~3) &&
	git rebase -i HEAD~3 &&
	git rebase --continue &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $COMMIT2 two_edits
   edit $COMMIT3 three_edits
Next command to do (1 remaining command):
   pick $COMMIT4 four_edits
  (use "git rebase --edit-todo" to view and edit)
You are currently editing a commit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (use "git commit --amend" to amend the current commit)
  (use "git rebase --continue" once you are satisfied with your changes)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status: (continue first edit) second edit and split' '
	git reset --hard several_edits &&
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "git rebase --abort" &&
	COMMIT2=$(git rev-parse --short several_edits^^) &&
	COMMIT3=$(git rev-parse --short several_edits^) &&
	COMMIT4=$(git rev-parse --short several_edits) &&
	ONTO=$(git rev-parse --short HEAD~3) &&
	git rebase -i HEAD~3 &&
	git rebase --continue &&
	git reset HEAD^ &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $COMMIT2 two_edits
   edit $COMMIT3 three_edits
Next command to do (1 remaining command):
   pick $COMMIT4 four_edits
  (use "git rebase --edit-todo" to view and edit)
You are currently splitting a commit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (Once your working directory is clean, run "git rebase --continue")

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   main.txt

no changes added to commit (use "git add" and/or "git commit -a")
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status: (continue first edit) second edit and amend' '
	git reset --hard several_edits &&
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "git rebase --abort" &&
	COMMIT2=$(git rev-parse --short several_edits^^) &&
	COMMIT3=$(git rev-parse --short several_edits^) &&
	COMMIT4=$(git rev-parse --short several_edits) &&
	ONTO=$(git rev-parse --short HEAD~3) &&
	git rebase -i HEAD~3 &&
	git rebase --continue &&
	git commit --amend -m "foo" &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $COMMIT2 two_edits
   edit $COMMIT3 three_edits
Next command to do (1 remaining command):
   pick $COMMIT4 four_edits
  (use "git rebase --edit-todo" to view and edit)
You are currently editing a commit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (use "git commit --amend" to amend the current commit)
  (use "git rebase --continue" once you are satisfied with your changes)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status: (amend first edit) second edit' '
	git reset --hard several_edits &&
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "git rebase --abort" &&
	COMMIT2=$(git rev-parse --short several_edits^^) &&
	COMMIT3=$(git rev-parse --short several_edits^) &&
	COMMIT4=$(git rev-parse --short several_edits) &&
	ONTO=$(git rev-parse --short HEAD~3) &&
	git rebase -i HEAD~3 &&
	git commit --amend -m "a" &&
	git rebase --continue &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $COMMIT2 two_edits
   edit $COMMIT3 three_edits
Next command to do (1 remaining command):
   pick $COMMIT4 four_edits
  (use "git rebase --edit-todo" to view and edit)
You are currently editing a commit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (use "git commit --amend" to amend the current commit)
  (use "git rebase --continue" once you are satisfied with your changes)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status: (amend first edit) second edit and split' '
	git reset --hard several_edits &&
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "git rebase --abort" &&
	ONTO=$(git rev-parse --short HEAD~3) &&
	COMMIT2=$(git rev-parse --short several_edits^^) &&
	COMMIT3=$(git rev-parse --short several_edits^) &&
	COMMIT4=$(git rev-parse --short several_edits) &&
	git rebase -i HEAD~3 &&
	git commit --amend -m "b" &&
	git rebase --continue &&
	git reset HEAD^ &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $COMMIT2 two_edits
   edit $COMMIT3 three_edits
Next command to do (1 remaining command):
   pick $COMMIT4 four_edits
  (use "git rebase --edit-todo" to view and edit)
You are currently splitting a commit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (Once your working directory is clean, run "git rebase --continue")

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   main.txt

no changes added to commit (use "git add" and/or "git commit -a")
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status: (amend first edit) second edit and amend' '
	git reset --hard several_edits &&
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "git rebase --abort" &&
	COMMIT2=$(git rev-parse --short several_edits^^) &&
	COMMIT3=$(git rev-parse --short several_edits^) &&
	COMMIT4=$(git rev-parse --short several_edits) &&
	ONTO=$(git rev-parse --short HEAD~3) &&
	git rebase -i HEAD~3 &&
	git commit --amend -m "c" &&
	git rebase --continue &&
	git commit --amend -m "d" &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $COMMIT2 two_edits
   edit $COMMIT3 three_edits
Next command to do (1 remaining command):
   pick $COMMIT4 four_edits
  (use "git rebase --edit-todo" to view and edit)
You are currently editing a commit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (use "git commit --amend" to amend the current commit)
  (use "git rebase --continue" once you are satisfied with your changes)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status: (split first edit) second edit' '
	git reset --hard several_edits &&
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "git rebase --abort" &&
	COMMIT2=$(git rev-parse --short several_edits^^) &&
	COMMIT3=$(git rev-parse --short several_edits^) &&
	COMMIT4=$(git rev-parse --short several_edits) &&
	ONTO=$(git rev-parse --short HEAD~3) &&
	git rebase -i HEAD~3 &&
	git reset HEAD^ &&
	git add main.txt &&
	git commit -m "e" &&
	git rebase --continue &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $COMMIT2 two_edits
   edit $COMMIT3 three_edits
Next command to do (1 remaining command):
   pick $COMMIT4 four_edits
  (use "git rebase --edit-todo" to view and edit)
You are currently editing a commit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (use "git commit --amend" to amend the current commit)
  (use "git rebase --continue" once you are satisfied with your changes)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status: (split first edit) second edit and split' '
	git reset --hard several_edits &&
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "git rebase --abort" &&
	COMMIT2=$(git rev-parse --short several_edits^^) &&
	COMMIT3=$(git rev-parse --short several_edits^) &&
	COMMIT4=$(git rev-parse --short several_edits) &&
	ONTO=$(git rev-parse --short HEAD~3) &&
	git rebase -i HEAD~3 &&
	git reset HEAD^ &&
	git add main.txt &&
	git commit --amend -m "f" &&
	git rebase --continue &&
	git reset HEAD^ &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $COMMIT2 two_edits
   edit $COMMIT3 three_edits
Next command to do (1 remaining command):
   pick $COMMIT4 four_edits
  (use "git rebase --edit-todo" to view and edit)
You are currently splitting a commit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (Once your working directory is clean, run "git rebase --continue")

Changes not staged for commit:
  (use "git add <file>..." to update what will be committed)
  (use "git restore <file>..." to discard changes in working directory)
	modified:   main.txt

no changes added to commit (use "git add" and/or "git commit -a")
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status: (split first edit) second edit and amend' '
	git reset --hard several_edits &&
	FAKE_LINES="edit 1 edit 2 3" &&
	export FAKE_LINES &&
	test_when_finished "git rebase --abort" &&
	COMMIT2=$(git rev-parse --short several_edits^^) &&
	COMMIT3=$(git rev-parse --short several_edits^) &&
	COMMIT4=$(git rev-parse --short several_edits) &&
	ONTO=$(git rev-parse --short HEAD~3) &&
	git rebase -i HEAD~3 &&
	git reset HEAD^ &&
	git add main.txt &&
	git commit --amend -m "g" &&
	git rebase --continue &&
	git commit --amend -m "h" &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   edit $COMMIT2 two_edits
   edit $COMMIT3 three_edits
Next command to do (1 remaining command):
   pick $COMMIT4 four_edits
  (use "git rebase --edit-todo" to view and edit)
You are currently editing a commit while rebasing branch '\''several_edits'\'' on '\''$ONTO'\''.
  (use "git commit --amend" to amend the current commit)
  (use "git rebase --continue" once you are satisfied with your changes)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'prepare am_session' '
	git reset --hard master &&
	git checkout -b am_session &&
	test_commit one_am one.txt "one" &&
	test_commit two_am two.txt "two" &&
	test_commit three_am three.txt "three"
'


test_expect_success 'status in an am session: file already exists' '
	git checkout -b am_already_exists &&
	test_when_finished "rm Maildir/* && git am --abort" &&
	git format-patch -1 -oMaildir &&
	test_must_fail git am Maildir/*.patch &&
	cat >expected <<\EOF &&
On branch am_already_exists
You are in the middle of an am session.
  (fix conflicts and then run "git am --continue")
  (use "git am --skip" to skip this patch)
  (use "git am --abort" to restore the original branch)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status in an am session: file does not exist' '
	git reset --hard am_session &&
	git checkout -b am_not_exists &&
	git rm three.txt &&
	git commit -m "delete three.txt" &&
	test_when_finished "rm Maildir/* && git am --abort" &&
	git format-patch -1 -oMaildir &&
	test_must_fail git am Maildir/*.patch &&
	cat >expected <<\EOF &&
On branch am_not_exists
You are in the middle of an am session.
  (fix conflicts and then run "git am --continue")
  (use "git am --skip" to skip this patch)
  (use "git am --abort" to restore the original branch)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status in an am session: empty patch' '
	git reset --hard am_session &&
	git checkout -b am_empty &&
	test_when_finished "rm Maildir/* && git am --abort" &&
	git format-patch -3 -oMaildir &&
	git rm one.txt two.txt three.txt &&
	git commit -m "delete all am_empty" &&
	echo error >Maildir/0002-two_am.patch &&
	test_must_fail git am Maildir/*.patch &&
	cat >expected <<\EOF &&
On branch am_empty
You are in the middle of an am session.
The current patch is empty.
  (use "git am --skip" to skip this patch)
  (use "git am --abort" to restore the original branch)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status when bisecting' '
	git reset --hard master &&
	git checkout -b bisect &&
	test_commit one_bisect main.txt one &&
	test_commit two_bisect main.txt two &&
	test_commit three_bisect main.txt three &&
	test_when_finished "git bisect reset" &&
	git bisect start &&
	git bisect bad &&
	git bisect good one_bisect &&
	TGT=$(git rev-parse --short two_bisect) &&
	cat >expected <<EOF &&
HEAD detached at $TGT
You are currently bisecting, started from branch '\''bisect'\''.
  (use "git bisect reset" to get back to the original branch)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status when rebase conflicts with statushints disabled' '
	git reset --hard master &&
	git checkout -b statushints_disabled &&
	test_when_finished "git config --local advice.statushints true" &&
	git config --local advice.statushints false &&
	test_commit one_statushints main.txt one &&
	test_commit two_statushints main.txt two &&
	test_commit three_statushints main.txt three &&
	test_when_finished "git rebase --abort" &&
	ONTO=$(git rev-parse --short HEAD^^) &&
	test_must_fail git rebase HEAD^ --onto HEAD^^ &&
	cat >expected <<EOF &&
rebase in progress; onto $ONTO
You are currently rebasing branch '\''statushints_disabled'\'' on '\''$ONTO'\''.

Unmerged paths:
	both modified:   main.txt

no changes added to commit
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'prepare for cherry-pick conflicts' '
	git reset --hard master &&
	git checkout -b cherry_branch &&
	test_commit one_cherry main.txt one &&
	test_commit two_cherries main.txt two &&
	git checkout -b cherry_branch_second &&
	test_commit second_cherry main.txt second &&
	git checkout cherry_branch &&
	test_commit three_cherries main.txt three
'


test_expect_success 'status when cherry-picking before resolving conflicts' '
	test_when_finished "git cherry-pick --abort" &&
	test_must_fail git cherry-pick cherry_branch_second &&
	TO_CHERRY_PICK=$(git rev-parse --short CHERRY_PICK_HEAD) &&
	cat >expected <<EOF &&
On branch cherry_branch
You are currently cherry-picking commit $TO_CHERRY_PICK.
  (fix conflicts and run "git cherry-pick --continue")
  (use "git cherry-pick --abort" to cancel the cherry-pick operation)

Unmerged paths:
  (use "git add <file>..." to mark resolution)
	both modified:   main.txt

no changes added to commit (use "git add" and/or "git commit -a")
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status when cherry-picking after resolving conflicts' '
	git reset --hard cherry_branch &&
	test_when_finished "git cherry-pick --abort" &&
	test_must_fail git cherry-pick cherry_branch_second &&
	TO_CHERRY_PICK=$(git rev-parse --short CHERRY_PICK_HEAD) &&
	echo end >main.txt &&
	git add main.txt &&
	cat >expected <<EOF &&
On branch cherry_branch
You are currently cherry-picking commit $TO_CHERRY_PICK.
  (all conflicts fixed: run "git cherry-pick --continue")
  (use "git cherry-pick --abort" to cancel the cherry-pick operation)

Changes to be committed:
	modified:   main.txt

Untracked files not listed (use -u option to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'

test_expect_success 'status when cherry-picking after committing conflict resolution' '
	git reset --hard cherry_branch &&
	test_when_finished "git cherry-pick --abort" &&
	test_must_fail git cherry-pick cherry_branch_second one_cherry &&
	echo end >main.txt &&
	git commit -a &&
	cat >expected <<EOF &&
On branch cherry_branch
Cherry-pick currently in progress.
  (run "git cherry-pick --continue" to continue)
  (use "git cherry-pick --abort" to cancel the cherry-pick operation)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'

test_expect_success 'status shows cherry-pick with invalid oid' '
	mkdir .git/sequencer &&
	test_write_lines "pick invalid-oid" >.git/sequencer/todo &&
	git status --untracked-files=no >actual 2>err &&
	git cherry-pick --quit &&
	test_must_be_empty err &&
	test_i18ncmp expected actual
'

test_expect_success 'status does not show error if .git/sequencer is a file' '
	test_when_finished "rm .git/sequencer" &&
	test_write_lines hello >.git/sequencer &&
	git status --untracked-files=no 2>err &&
	test_must_be_empty err
'

test_expect_success 'status showing detached at and from a tag' '
	test_commit atag tagging &&
	git checkout atag &&
	cat >expected <<\EOF &&
HEAD detached at atag
nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual &&

	git reset --hard HEAD^ &&
	cat >expected <<\EOF &&
HEAD detached from atag
nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'

test_expect_success 'status while reverting commit (conflicts)' '
	git checkout master &&
	echo before >to-revert.txt &&
	test_commit before to-revert.txt &&
	echo old >to-revert.txt &&
	test_commit old to-revert.txt &&
	echo new >to-revert.txt &&
	test_commit new to-revert.txt &&
	TO_REVERT=$(git rev-parse --short HEAD^) &&
	test_must_fail git revert $TO_REVERT &&
	cat >expected <<EOF &&
On branch master
You are currently reverting commit $TO_REVERT.
  (fix conflicts and run "git revert --continue")
  (use "git revert --abort" to cancel the revert operation)

Unmerged paths:
  (use "git restore --staged <file>..." to unstage)
  (use "git add <file>..." to mark resolution)
	both modified:   to-revert.txt

no changes added to commit (use "git add" and/or "git commit -a")
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'

test_expect_success 'status while reverting commit (conflicts resolved)' '
	echo reverted >to-revert.txt &&
	git add to-revert.txt &&
	cat >expected <<EOF &&
On branch master
You are currently reverting commit $TO_REVERT.
  (all conflicts fixed: run "git revert --continue")
  (use "git revert --abort" to cancel the revert operation)

Changes to be committed:
  (use "git restore --staged <file>..." to unstage)
	modified:   to-revert.txt

Untracked files not listed (use -u option to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'

test_expect_success 'status after reverting commit' '
	git revert --continue &&
	cat >expected <<\EOF &&
On branch master
nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'

test_expect_success 'status while reverting after committing conflict resolution' '
	test_when_finished "git revert --abort" &&
	git reset --hard new &&
	test_must_fail git revert old new &&
	echo reverted >to-revert.txt &&
	git commit -a &&
	cat >expected <<EOF &&
On branch master
Revert currently in progress.
  (run "git revert --continue" to continue)
  (use "git revert --abort" to cancel the revert operation)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'

test_expect_success 'prepare for different number of commits rebased' '
	git reset --hard master &&
	git checkout -b several_commits &&
	test_commit one_commit main.txt one &&
	test_commit two_commit main.txt two &&
	test_commit three_commit main.txt three &&
	test_commit four_commit main.txt four
'

test_expect_success 'status: one command done nothing remaining' '
	FAKE_LINES="exec_exit_15" &&
	export FAKE_LINES &&
	test_when_finished "git rebase --abort" &&
	ONTO=$(git rev-parse --short HEAD~3) &&
	test_must_fail git rebase -i HEAD~3 &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last command done (1 command done):
   exec exit 15
No commands remaining.
You are currently editing a commit while rebasing branch '\''several_commits'\'' on '\''$ONTO'\''.
  (use "git commit --amend" to amend the current commit)
  (use "git rebase --continue" once you are satisfied with your changes)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'

test_expect_success 'status: two commands done with some white lines in done file' '
	FAKE_LINES="1 > exec_exit_15  2 3" &&
	export FAKE_LINES &&
	test_when_finished "git rebase --abort" &&
	ONTO=$(git rev-parse --short HEAD~3) &&
	COMMIT4=$(git rev-parse --short HEAD) &&
	COMMIT3=$(git rev-parse --short HEAD^) &&
	COMMIT2=$(git rev-parse --short HEAD^^) &&
	test_must_fail git rebase -i HEAD~3 &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (2 commands done):
   pick $COMMIT2 two_commit
   exec exit 15
Next commands to do (2 remaining commands):
   pick $COMMIT3 three_commit
   pick $COMMIT4 four_commit
  (use "git rebase --edit-todo" to view and edit)
You are currently editing a commit while rebasing branch '\''several_commits'\'' on '\''$ONTO'\''.
  (use "git commit --amend" to amend the current commit)
  (use "git rebase --continue" once you are satisfied with your changes)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'

test_expect_success 'status: two remaining commands with some white lines in todo file' '
	FAKE_LINES="1 2 exec_exit_15 3 > 4" &&
	export FAKE_LINES &&
	test_when_finished "git rebase --abort" &&
	ONTO=$(git rev-parse --short HEAD~4) &&
	COMMIT4=$(git rev-parse --short HEAD) &&
	COMMIT3=$(git rev-parse --short HEAD^) &&
	COMMIT2=$(git rev-parse --short HEAD^^) &&
	test_must_fail git rebase -i HEAD~4 &&
	cat >expected <<EOF &&
interactive rebase in progress; onto $ONTO
Last commands done (3 commands done):
   pick $COMMIT2 two_commit
   exec exit 15
  (see more in file .git/rebase-merge/done)
Next commands to do (2 remaining commands):
   pick $COMMIT3 three_commit
   pick $COMMIT4 four_commit
  (use "git rebase --edit-todo" to view and edit)
You are currently editing a commit while rebasing branch '\''several_commits'\'' on '\''$ONTO'\''.
  (use "git commit --amend" to amend the current commit)
  (use "git rebase --continue" once you are satisfied with your changes)

nothing to commit (use -u to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'

test_expect_success 'status: handle not-yet-started rebase -i gracefully' '
	ONTO=$(git rev-parse --short HEAD^) &&
	COMMIT=$(git rev-parse --short HEAD) &&
	EDITOR="git status --untracked-files=no >actual" git rebase -i HEAD^ &&
	cat >expected <<EOF &&
On branch several_commits
No commands done.
Next command to do (1 remaining command):
   pick $COMMIT four_commit
  (use "git rebase --edit-todo" to view and edit)
You are currently editing a commit while rebasing branch '\''several_commits'\'' on '\''$ONTO'\''.
  (use "git commit --amend" to amend the current commit)
  (use "git rebase --continue" once you are satisfied with your changes)

nothing to commit (use -u to show untracked files)
EOF
	test_i18ncmp expected actual
'

test_done
