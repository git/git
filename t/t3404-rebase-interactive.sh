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
 A - B - C - D - E            (master)
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

# WARNING: Modifications to the initial repository can change the SHA ID used
# in the expect2 file for the 'stop on conflicting pick' test.

test_expect_success 'setup' '
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

# "exec" commands are ran with the user shell by default, but this may
# be non-POSIX. For example, if SHELL=zsh then ">file" doesn't work
# to create a file. Unseting SHELL avoids such non-portable behavior
# in tests. It must be exported for it to take effect where needed.
SHELL=
export SHELL

test_expect_success 'rebase --keep-empty' '
	git checkout -b emptybranch master &&
	git commit --allow-empty -m "empty" &&
	git rebase --keep-empty -i HEAD~2 &&
	git log --oneline >actual &&
	test_line_count = 6 actual
'

test_expect_success 'rebase -i with the exec command' '
	git checkout master &&
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
	test_path_is_missing touch-three " (should have stopped before)" &&
	test_cmp_rev C HEAD &&
	git rebase --continue &&
	test_path_is_file touch-three &&
	test_path_is_file "touch-file  name with spaces" &&
	test_path_is_file touch-after-semicolon &&
	test_cmp_rev master HEAD &&
	rm -f touch-*
'

test_expect_success 'rebase -i with the exec command runs from tree root' '
	git checkout master &&
	mkdir subdir && (cd subdir &&
	set_fake_editor &&
	FAKE_LINES="1 exec_>touch-subdir" \
		git rebase -i HEAD^
	) &&
	test_path_is_file touch-subdir &&
	rm -fr subdir
'

test_expect_success 'rebase -i with the exec command checks tree cleanness' '
	git checkout master &&
	set_fake_editor &&
	test_must_fail env FAKE_LINES="exec_echo_foo_>file1 1" git rebase -i HEAD^ &&
	test_cmp_rev master^ HEAD &&
	git reset --hard &&
	git rebase --continue
'

test_expect_success 'rebase -i with exec of inexistent command' '
	git checkout master &&
	test_when_finished "git rebase --abort" &&
	set_fake_editor &&
	test_must_fail env FAKE_LINES="exec_this-command-does-not-exist 1" \
	git rebase -i HEAD^ >actual 2>&1 &&
	! grep "Maybe git-rebase is broken" actual
'

test_expect_success 'no changes are a nop' '
	git checkout branch2 &&
	set_fake_editor &&
	git rebase -i F &&
	test "$(git symbolic-ref -q HEAD)" = "refs/heads/branch2" &&
	test $(git rev-parse I) = $(git rev-parse HEAD)
'

test_expect_success 'test the [branch] option' '
	git checkout -b dead-end &&
	git rm file6 &&
	git commit -m "stop here" &&
	set_fake_editor &&
	git rebase -i F branch2 &&
	test "$(git symbolic-ref -q HEAD)" = "refs/heads/branch2" &&
	test $(git rev-parse I) = $(git rev-parse branch2) &&
	test $(git rev-parse I) = $(git rev-parse HEAD)
'

test_expect_success 'test --onto <branch>' '
	git checkout -b test-onto branch2 &&
	set_fake_editor &&
	git rebase -i --onto branch1 F &&
	test "$(git symbolic-ref -q HEAD)" = "refs/heads/test-onto" &&
	test $(git rev-parse HEAD^) = $(git rev-parse branch1) &&
	test $(git rev-parse I) = $(git rev-parse branch2)
'

test_expect_success 'rebase on top of a non-conflicting commit' '
	git checkout branch1 &&
	git tag original-branch1 &&
	set_fake_editor &&
	git rebase -i branch2 &&
	test file6 = $(git diff --name-only original-branch1) &&
	test "$(git symbolic-ref -q HEAD)" = "refs/heads/branch1" &&
	test $(git rev-parse I) = $(git rev-parse branch2) &&
	test $(git rev-parse I) = $(git rev-parse HEAD~2)
'

test_expect_success 'reflog for the branch shows state before rebase' '
	test $(git rev-parse branch1@{1}) = $(git rev-parse original-branch1)
'

test_expect_success 'exchange two commits' '
	set_fake_editor &&
	FAKE_LINES="2 1" git rebase -i HEAD~2 &&
	test H = $(git cat-file commit HEAD^ | sed -ne \$p) &&
	test G = $(git cat-file commit HEAD | sed -ne \$p)
'

cat > expect << EOF
diff --git a/file1 b/file1
index f70f10e..fd79235 100644
--- a/file1
+++ b/file1
@@ -1 +1 @@
-A
+G
EOF

cat > expect2 << EOF
<<<<<<< HEAD
D
=======
G
>>>>>>> 5d18e54... G
EOF

test_expect_success 'stop on conflicting pick' '
	git tag new-branch1 &&
	set_fake_editor &&
	test_must_fail git rebase -i master &&
	test "$(git rev-parse HEAD~3)" = "$(git rev-parse master)" &&
	test_cmp expect .git/rebase-merge/patch &&
	test_cmp expect2 file1 &&
	test "$(git diff --name-status |
		sed -n -e "/^U/s/^U[^a-z]*//p")" = file1 &&
	test 4 = $(grep -v "^#" < .git/rebase-merge/done | wc -l) &&
	test 0 = $(grep -c "^[^#]" < .git/rebase-merge/git-rebase-todo)
'

test_expect_success 'abort' '
	git rebase --abort &&
	test $(git rev-parse new-branch1) = $(git rev-parse HEAD) &&
	test "$(git symbolic-ref -q HEAD)" = "refs/heads/branch1" &&
	test_path_is_missing .git/rebase-merge
'

test_expect_success 'abort with error when new base cannot be checked out' '
	git rm --cached file1 &&
	git commit -m "remove file in base" &&
	set_fake_editor &&
	test_must_fail git rebase -i master > output 2>&1 &&
	grep "The following untracked working tree files would be overwritten by checkout:" \
		output &&
	grep "file1" output &&
	test_path_is_missing .git/rebase-merge &&
	git reset --hard HEAD^
'

test_expect_success 'retain authorship' '
	echo A > file7 &&
	git add file7 &&
	test_tick &&
	GIT_AUTHOR_NAME="Twerp Snog" git commit -m "different author" &&
	git tag twerp &&
	set_fake_editor &&
	git rebase -i --onto master HEAD^ &&
	git show HEAD | grep "^Author: Twerp Snog"
'

test_expect_success 'squash' '
	git reset --hard twerp &&
	echo B > file7 &&
	test_tick &&
	GIT_AUTHOR_NAME="Nitfol" git commit -m "nitfol" file7 &&
	echo "******************************" &&
	set_fake_editor &&
	FAKE_LINES="1 squash 2" EXPECT_HEADER_COUNT=2 \
		git rebase -i --onto master HEAD~2 &&
	test B = $(cat file7) &&
	test $(git rev-parse HEAD^) = $(git rev-parse master)
'

test_expect_success 'retain authorship when squashing' '
	git show HEAD | grep "^Author: Twerp Snog"
'

test_expect_success '-p handles "no changes" gracefully' '
	HEAD=$(git rev-parse HEAD) &&
	set_fake_editor &&
	git rebase -i -p HEAD^ &&
	git update-index --refresh &&
	git diff-files --quiet &&
	git diff-index --quiet --cached HEAD -- &&
	test $HEAD = $(git rev-parse HEAD)
'

test_expect_failure 'exchange two commits with -p' '
	git checkout H &&
	set_fake_editor &&
	FAKE_LINES="2 1" git rebase -i -p HEAD~2 &&
	test H = $(git cat-file commit HEAD^ | sed -ne \$p) &&
	test G = $(git cat-file commit HEAD | sed -ne \$p)
'

test_expect_success 'preserve merges with -p' '
	git checkout -b to-be-preserved master^ &&
	: > unrelated-file &&
	git add unrelated-file &&
	test_tick &&
	git commit -m "unrelated" &&
	git checkout -b another-branch master &&
	echo B > file1 &&
	test_tick &&
	git commit -m J file1 &&
	test_tick &&
	git merge to-be-preserved &&
	echo C > file1 &&
	test_tick &&
	git commit -m K file1 &&
	echo D > file1 &&
	test_tick &&
	git commit -m L1 file1 &&
	git checkout HEAD^ &&
	echo 1 > unrelated-file &&
	test_tick &&
	git commit -m L2 unrelated-file &&
	test_tick &&
	git merge another-branch &&
	echo E > file1 &&
	test_tick &&
	git commit -m M file1 &&
	git checkout -b to-be-rebased &&
	test_tick &&
	set_fake_editor &&
	git rebase -i -p --onto branch1 master &&
	git update-index --refresh &&
	git diff-files --quiet &&
	git diff-index --quiet --cached HEAD -- &&
	test $(git rev-parse HEAD~6) = $(git rev-parse branch1) &&
	test $(git rev-parse HEAD~4^2) = $(git rev-parse to-be-preserved) &&
	test $(git rev-parse HEAD^^2^) = $(git rev-parse HEAD^^^) &&
	test $(git show HEAD~5:file1) = B &&
	test $(git show HEAD~3:file1) = C &&
	test $(git show HEAD:file1) = E &&
	test $(git show HEAD:unrelated-file) = 1
'

test_expect_success 'edit ancestor with -p' '
	set_fake_editor &&
	FAKE_LINES="1 2 edit 3 4" git rebase -i -p HEAD~3 &&
	echo 2 > unrelated-file &&
	test_tick &&
	git commit -m L2-modified --amend unrelated-file &&
	git rebase --continue &&
	git update-index --refresh &&
	git diff-files --quiet &&
	git diff-index --quiet --cached HEAD -- &&
	test $(git show HEAD:unrelated-file) = 2
'

test_expect_success '--continue tries to commit' '
	test_tick &&
	set_fake_editor &&
	test_must_fail git rebase -i --onto new-branch1 HEAD^ &&
	echo resolved > file1 &&
	git add file1 &&
	FAKE_COMMIT_MESSAGE="chouette!" git rebase --continue &&
	test $(git rev-parse HEAD^) = $(git rev-parse new-branch1) &&
	git show HEAD | grep chouette
'

test_expect_success 'verbose flag is heeded, even after --continue' '
	git reset --hard master@{1} &&
	test_tick &&
	set_fake_editor &&
	test_must_fail git rebase -v -i --onto new-branch1 HEAD^ &&
	echo resolved > file1 &&
	git add file1 &&
	git rebase --continue > output &&
	grep "^ file1 | 2 +-$" output
'

test_expect_success 'multi-squash only fires up editor once' '
	base=$(git rev-parse HEAD~4) &&
	set_fake_editor &&
	FAKE_COMMIT_AMEND="ONCE" FAKE_LINES="1 squash 2 squash 3 squash 4" \
		EXPECT_HEADER_COUNT=4 \
		git rebase -i $base &&
	test $base = $(git rev-parse HEAD^) &&
	test 1 = $(git show | grep ONCE | wc -l)
'

test_expect_success 'multi-fixup does not fire up editor' '
	git checkout -b multi-fixup E &&
	base=$(git rev-parse HEAD~4) &&
	set_fake_editor &&
	FAKE_COMMIT_AMEND="NEVER" FAKE_LINES="1 fixup 2 fixup 3 fixup 4" \
		git rebase -i $base &&
	test $base = $(git rev-parse HEAD^) &&
	test 0 = $(git show | grep NEVER | wc -l) &&
	git checkout to-be-rebased &&
	git branch -D multi-fixup
'

test_expect_success 'commit message used after conflict' '
	git checkout -b conflict-fixup conflict-branch &&
	base=$(git rev-parse HEAD~4) &&
	set_fake_editor &&
	test_must_fail env FAKE_LINES="1 fixup 3 fixup 4" git rebase -i $base &&
	echo three > conflict &&
	git add conflict &&
	FAKE_COMMIT_AMEND="ONCE" EXPECT_HEADER_COUNT=2 \
		git rebase --continue &&
	test $base = $(git rev-parse HEAD^) &&
	test 1 = $(git show | grep ONCE | wc -l) &&
	git checkout to-be-rebased &&
	git branch -D conflict-fixup
'

test_expect_success 'commit message retained after conflict' '
	git checkout -b conflict-squash conflict-branch &&
	base=$(git rev-parse HEAD~4) &&
	set_fake_editor &&
	test_must_fail env FAKE_LINES="1 fixup 3 squash 4" git rebase -i $base &&
	echo three > conflict &&
	git add conflict &&
	FAKE_COMMIT_AMEND="TWICE" EXPECT_HEADER_COUNT=2 \
		git rebase --continue &&
	test $base = $(git rev-parse HEAD^) &&
	test 2 = $(git show | grep TWICE | wc -l) &&
	git checkout to-be-rebased &&
	git branch -D conflict-squash
'

cat > expect-squash-fixup << EOF
B

D

ONCE
EOF

test_expect_success 'squash and fixup generate correct log messages' '
	git checkout -b squash-fixup E &&
	base=$(git rev-parse HEAD~4) &&
	set_fake_editor &&
	FAKE_COMMIT_AMEND="ONCE" FAKE_LINES="1 fixup 2 squash 3 fixup 4" \
		EXPECT_HEADER_COUNT=4 \
		git rebase -i $base &&
	git cat-file commit HEAD | sed -e 1,/^\$/d > actual-squash-fixup &&
	test_cmp expect-squash-fixup actual-squash-fixup &&
	git checkout to-be-rebased &&
	git branch -D squash-fixup
'

test_expect_success 'squash ignores comments' '
	git checkout -b skip-comments E &&
	base=$(git rev-parse HEAD~4) &&
	set_fake_editor &&
	FAKE_COMMIT_AMEND="ONCE" FAKE_LINES="# 1 # squash 2 # squash 3 # squash 4 #" \
		EXPECT_HEADER_COUNT=4 \
		git rebase -i $base &&
	test $base = $(git rev-parse HEAD^) &&
	test 1 = $(git show | grep ONCE | wc -l) &&
	git checkout to-be-rebased &&
	git branch -D skip-comments
'

test_expect_success 'squash ignores blank lines' '
	git checkout -b skip-blank-lines E &&
	base=$(git rev-parse HEAD~4) &&
	set_fake_editor &&
	FAKE_COMMIT_AMEND="ONCE" FAKE_LINES="> 1 > squash 2 > squash 3 > squash 4 >" \
		EXPECT_HEADER_COUNT=4 \
		git rebase -i $base &&
	test $base = $(git rev-parse HEAD^) &&
	test 1 = $(git show | grep ONCE | wc -l) &&
	git checkout to-be-rebased &&
	git branch -D skip-blank-lines
'

test_expect_success 'squash works as expected' '
	git checkout -b squash-works no-conflict-branch &&
	one=$(git rev-parse HEAD~3) &&
	set_fake_editor &&
	FAKE_LINES="1 squash 3 2" EXPECT_HEADER_COUNT=2 \
		git rebase -i HEAD~3 &&
	test $one = $(git rev-parse HEAD~2)
'

test_expect_success 'interrupted squash works as expected' '
	git checkout -b interrupted-squash conflict-branch &&
	one=$(git rev-parse HEAD~3) &&
	set_fake_editor &&
	test_must_fail env FAKE_LINES="1 squash 3 2" git rebase -i HEAD~3 &&
	(echo one; echo two; echo four) > conflict &&
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
	set_fake_editor &&
	test_must_fail env FAKE_LINES="3 squash 1 2" git rebase -i HEAD~3 &&
	(echo one; echo four) > conflict &&
	git add conflict &&
	test_must_fail git rebase --continue &&
	(echo one; echo two; echo four) > conflict &&
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
	set_fake_editor &&
	FAKE_LINES="edit 1" git rebase -i HEAD^ &&
	echo edited > file7 &&
	git add file7 &&
	FAKE_COMMIT_MESSAGE="chouette!" git rebase --continue &&
	test edited = $(git show HEAD:file7) &&
	git show HEAD | grep chouette &&
	test $parent = $(git rev-parse HEAD^)
'

test_expect_success 'aborted --continue does not squash commits after "edit"' '
	old=$(git rev-parse HEAD) &&
	test_tick &&
	set_fake_editor &&
	FAKE_LINES="edit 1" git rebase -i HEAD^ &&
	echo "edited again" > file7 &&
	git add file7 &&
	test_must_fail env FAKE_COMMIT_MESSAGE=" " git rebase --continue &&
	test $old = $(git rev-parse HEAD) &&
	git rebase --abort
'

test_expect_success 'auto-amend only edited commits after "edit"' '
	test_tick &&
	set_fake_editor &&
	FAKE_LINES="edit 1" git rebase -i HEAD^ &&
	echo "edited again" > file7 &&
	git add file7 &&
	FAKE_COMMIT_MESSAGE="edited file7 again" git commit &&
	echo "and again" > file7 &&
	git add file7 &&
	test_tick &&
	test_must_fail env FAKE_COMMIT_MESSAGE="and again" git rebase --continue &&
	git rebase --abort
'

test_expect_success 'clean error after failed "exec"' '
	test_tick &&
	test_when_finished "git rebase --abort || :" &&
	set_fake_editor &&
	test_must_fail env FAKE_LINES="1 exec_false" git rebase -i HEAD^ &&
	echo "edited again" > file7 &&
	git add file7 &&
	test_must_fail git rebase --continue 2>error &&
	grep "You have staged changes in your working tree." error
'

test_expect_success 'rebase a detached HEAD' '
	grandparent=$(git rev-parse HEAD~2) &&
	git checkout $(git rev-parse HEAD) &&
	test_tick &&
	set_fake_editor &&
	FAKE_LINES="2 1" git rebase -i HEAD~2 &&
	test $grandparent = $(git rev-parse HEAD~2)
'

test_expect_success 'rebase a commit violating pre-commit' '

	mkdir -p .git/hooks &&
	PRE_COMMIT=.git/hooks/pre-commit &&
	echo "#!/bin/sh" > $PRE_COMMIT &&
	echo "test -z \"\$(git diff --cached --check)\"" >> $PRE_COMMIT &&
	chmod a+x $PRE_COMMIT &&
	echo "monde! " >> file1 &&
	test_tick &&
	test_must_fail git commit -m doesnt-verify file1 &&
	git commit -m doesnt-verify --no-verify file1 &&
	test_tick &&
	set_fake_editor &&
	FAKE_LINES=2 git rebase -i HEAD~2

'

test_expect_success 'rebase with a file named HEAD in worktree' '

	rm -fr .git/hooks &&
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

	set_fake_editor &&
	FAKE_LINES="1 squash 2" git rebase -i to-be-rebased &&
	test "$(git show -s --pretty=format:%an)" = "Squashed Away"

'

test_expect_success 'do "noop" when there is nothing to cherry-pick' '

	git checkout -b branch4 HEAD &&
	GIT_EDITOR=: git commit --amend \
		--author="Somebody else <somebody@else.com>" &&
	test $(git rev-parse branch3) != $(git rev-parse branch4) &&
	set_fake_editor &&
	git rebase -i branch3 &&
	test $(git rev-parse branch3) = $(git rev-parse branch4)

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
	set_fake_editor &&
	git commit -a -m "Three changes submodule"
'

test_expect_success 'submodule rebase -i' '
	set_fake_editor &&
	FAKE_LINES="1 squash 2 3" git rebase -i A
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
	set_fake_editor &&
	test_must_fail git rebase -i submodule-base &&
	git add sub &&
	git rebase --continue &&
	test $(git rev-parse submodule-base) != $(git rev-parse HEAD)
'

test_expect_success 'rebase -i continue with unstaged submodule' '
	git checkout submodule-topic &&
	git reset --hard &&
	set_fake_editor &&
	test_must_fail git rebase -i submodule-base &&
	git reset &&
	git rebase --continue &&
	test $(git rev-parse submodule-base) = $(git rev-parse HEAD)
'

test_expect_success 'avoid unnecessary reset' '
	git checkout master &&
	git reset --hard &&
	test-chmtime =123456789 file3 &&
	git update-index --refresh &&
	HEAD=$(git rev-parse HEAD) &&
	set_fake_editor &&
	git rebase -i HEAD~4 &&
	test $HEAD = $(git rev-parse HEAD) &&
	MTIME=$(test-chmtime -v +0 file3 | sed 's/[^0-9].*$//') &&
	test 123456789 = $MTIME
'

test_expect_success 'reword' '
	git checkout -b reword-branch master &&
	set_fake_editor &&
	FAKE_LINES="1 2 3 reword 4" FAKE_COMMIT_MESSAGE="E changed" git rebase -i A &&
	git show HEAD | grep "E changed" &&
	test $(git rev-parse master) != $(git rev-parse HEAD) &&
	test $(git rev-parse master^) = $(git rev-parse HEAD^) &&
	FAKE_LINES="1 2 reword 3 4" FAKE_COMMIT_MESSAGE="D changed" git rebase -i A &&
	git show HEAD^ | grep "D changed" &&
	FAKE_LINES="reword 1 2 3 4" FAKE_COMMIT_MESSAGE="B changed" git rebase -i A &&
	git show HEAD~3 | grep "B changed" &&
	FAKE_LINES="1 reword 2 3 4" FAKE_COMMIT_MESSAGE="C changed" git rebase -i A &&
	git show HEAD~2 | grep "C changed"
'

test_expect_success 'rebase -i can copy notes' '
	git config notes.rewrite.rebase true &&
	git config notes.rewriteRef "refs/notes/*" &&
	test_commit n1 &&
	test_commit n2 &&
	test_commit n3 &&
	git notes add -m"a note" n3 &&
	set_fake_editor &&
	git rebase -i --onto n1 n2 &&
	test "a note" = "$(git notes show HEAD)"
'

cat >expect <<EOF
an earlier note

a note
EOF

test_expect_success 'rebase -i can copy notes over a fixup' '
	git reset --hard n3 &&
	git notes add -m"an earlier note" n2 &&
	set_fake_editor &&
	GIT_NOTES_REWRITE_MODE=concatenate FAKE_LINES="1 fixup 2" git rebase -i n1 &&
	git notes show > output &&
	test_cmp expect output
'

test_expect_success 'rebase while detaching HEAD' '
	git symbolic-ref HEAD &&
	grandparent=$(git rev-parse HEAD~2) &&
	test_tick &&
	set_fake_editor &&
	FAKE_LINES="2 1" git rebase -i HEAD~2 HEAD^0 &&
	test $grandparent = $(git rev-parse HEAD~2) &&
	test_must_fail git symbolic-ref HEAD
'

test_tick # Ensure that the rebased commits get a different timestamp.
test_expect_success 'always cherry-pick with --no-ff' '
	git checkout no-ff-branch &&
	git tag original-no-ff-branch &&
	set_fake_editor &&
	git rebase -i --no-ff A &&
	touch empty &&
	for p in 0 1 2
	do
		test ! $(git rev-parse HEAD~$p) = $(git rev-parse original-no-ff-branch~$p) &&
		git diff HEAD~$p original-no-ff-branch~$p > out &&
		test_cmp empty out
	done &&
	test $(git rev-parse HEAD~3) = $(git rev-parse original-no-ff-branch~3) &&
	git diff HEAD~3 original-no-ff-branch~3 > out &&
	test_cmp empty out
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
	set_fake_editor &&
	FAKE_LINES="1 2 3 4" git rebase -i A &&
	git rev-list A.. >actual &&
	test_cmp expect actual
'


test_expect_success 'prepare for rebase -i --exec' '
	git checkout master &&
	git checkout -b execute &&
	test_commit one_exec main.txt one_exec &&
	test_commit two_exec main.txt two_exec &&
	test_commit three_exec main.txt three_exec
'


test_expect_success 'running "git rebase -i --exec git show HEAD"' '
	set_fake_editor &&
	git rebase -i --exec "git show HEAD" HEAD~2 >actual &&
	(
		FAKE_LINES="1 exec_git_show_HEAD 2 exec_git_show_HEAD" &&
		export FAKE_LINES &&
		git rebase -i HEAD~2 >expect
	) &&
	sed -e "1,9d" expect >expected &&
	test_cmp expected actual
'


test_expect_success 'running "git rebase --exec git show HEAD -i"' '
	git reset --hard execute &&
	set_fake_editor &&
	git rebase --exec "git show HEAD" -i HEAD~2 >actual &&
	(
		FAKE_LINES="1 exec_git_show_HEAD 2 exec_git_show_HEAD" &&
		export FAKE_LINES &&
		git rebase -i HEAD~2 >expect
	) &&
	sed -e "1,9d" expect >expected &&
	test_cmp expected actual
'


test_expect_success 'running "git rebase -ix git show HEAD"' '
	git reset --hard execute &&
	set_fake_editor &&
	git rebase -ix "git show HEAD" HEAD~2 >actual &&
	(
		FAKE_LINES="1 exec_git_show_HEAD 2 exec_git_show_HEAD" &&
		export FAKE_LINES &&
		git rebase -i HEAD~2 >expect
	) &&
	sed -e "1,9d" expect >expected &&
	test_cmp expected actual
'


test_expect_success 'rebase -ix with several <CMD>' '
	git reset --hard execute &&
	set_fake_editor &&
	git rebase -ix "git show HEAD; pwd" HEAD~2 >actual &&
	(
		FAKE_LINES="1 exec_git_show_HEAD;_pwd 2 exec_git_show_HEAD;_pwd" &&
		export FAKE_LINES &&
		git rebase -i HEAD~2 >expect
	) &&
	sed -e "1,9d" expect >expected &&
	test_cmp expected actual
'


test_expect_success 'rebase -ix with several instances of --exec' '
	git reset --hard execute &&
	set_fake_editor &&
	git rebase -i --exec "git show HEAD" --exec "pwd" HEAD~2 >actual &&
	(
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
	set_fake_editor &&
	(
		git checkout -b autosquash_actual &&
		git rebase -i --exec "git show HEAD" --autosquash HEAD~4 >actual
	) &&
	git checkout autosquash &&
	(
		git checkout -b autosquash_expected &&
		FAKE_LINES="1 fixup 3 fixup 4 exec_git_show_HEAD 2 exec_git_show_HEAD" &&
		export FAKE_LINES &&
		git rebase -i HEAD~4 >expect
	) &&
	sed -e "1,13d" expect >expected &&
	test_cmp expected actual
'


test_expect_success 'rebase --exec without -i shows error message' '
	git reset --hard execute &&
	set_fake_editor &&
	test_must_fail git rebase --exec "git show HEAD" HEAD~2 2>actual &&
	echo "The --exec option must be used with the --interactive option" >expected &&
	test_i18ncmp expected actual
'


test_expect_success 'rebase -i --exec without <CMD>' '
	git reset --hard execute &&
	set_fake_editor &&
	test_must_fail git rebase -i --exec 2>tmp &&
	sed -e "1d" tmp >actual &&
	test_must_fail git rebase -h >expected &&
	test_cmp expected actual &&
	git checkout master
'

test_expect_success 'rebase -i --root re-order and drop commits' '
	git checkout E &&
	set_fake_editor &&
	FAKE_LINES="3 1 2 5" git rebase -i --root &&
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
	set_fake_editor &&
	FAKE_LINES="2" git rebase -i --root &&
	git cat-file commit HEAD | grep -q "^author Twerp Snog" &&
	git cat-file commit HEAD | grep -q "^different author$"
'

test_expect_success 'rebase -i --root temporary sentinel commit' '
	git checkout B &&
	set_fake_editor &&
	test_must_fail env FAKE_LINES="2" git rebase -i --root &&
	git cat-file commit HEAD | grep "^tree 4b825dc642cb" &&
	git rebase --abort
'

test_expect_success 'rebase -i --root fixup root commit' '
	git checkout B &&
	set_fake_editor &&
	FAKE_LINES="1 fixup 2" git rebase -i --root &&
	test A = $(git cat-file commit HEAD | sed -ne \$p) &&
	test B = $(git show HEAD:file1) &&
	test 0 = $(git cat-file commit HEAD | grep -c ^parent\ )
'

test_expect_success 'rebase --edit-todo does not works on non-interactive rebase' '
	git reset --hard &&
	git checkout conflict-branch &&
	set_fake_editor &&
	test_must_fail git rebase --onto HEAD~2 HEAD~ &&
	test_must_fail git rebase --edit-todo &&
	git rebase --abort
'

test_expect_success 'rebase --edit-todo can be used to modify todo' '
	git reset --hard &&
	git checkout no-conflict-branch^0 &&
	set_fake_editor &&
	FAKE_LINES="edit 1 2 3" git rebase -i HEAD~3 &&
	FAKE_LINES="2 1" git rebase --edit-todo &&
	git rebase --continue
	test M = $(git cat-file commit HEAD^ | sed -ne \$p) &&
	test L = $(git cat-file commit HEAD | sed -ne \$p)
'

test_expect_success 'rebase -i produces readable reflog' '
	git reset --hard &&
	git branch -f branch-reflog-test H &&
	set_fake_editor &&
	git rebase -i --onto I F branch-reflog-test &&
	cat >expect <<-\EOF &&
	rebase -i (start): checkout I
	rebase -i (pick): G
	rebase -i (pick): H
	rebase -i (finish): returning to refs/heads/branch-reflog-test
	EOF
	tail -n 4 .git/logs/HEAD |
	sed -e "s/.*	//" >actual &&
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
	test_set_editor "$(pwd)/remove-all-but-first.sh" &&
	git rebase -i B &&
	test B = $(git cat-file commit HEAD^ | sed -ne \$p)
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

test_expect_success 'rebase -i error on commits with \ in message' '
	current_head=$(git rev-parse HEAD)
	test_when_finished "git rebase --abort; git reset --hard $current_head; rm -f error" &&
	test_commit TO-REMOVE will-conflict old-content &&
	test_commit "\temp" will-conflict new-content dummy &&
	test_must_fail env EDITOR=true git rebase -i HEAD^ --onto HEAD^^ 2>error &&
	test_expect_code 1 grep  "	emp" error
'

test_expect_success 'short SHA-1 setup' '
	test_when_finished "git checkout master" &&
	git checkout --orphan collide &&
	git rm -rf . &&
	(
	unset test_tick &&
	test_commit collide1 collide &&
	test_commit --notick collide2 collide &&
	test_commit --notick collide3 collide
	)
'

test_expect_success 'short SHA-1 collide' '
	test_when_finished "reset_rebase && git checkout master" &&
	git checkout collide &&
	(
	unset test_tick &&
	test_tick &&
	set_fake_editor &&
	FAKE_COMMIT_MESSAGE="collide2 ac4f2ee" \
	FAKE_LINES="reword 1 2" git rebase -i HEAD~2
	)
'

test_done
