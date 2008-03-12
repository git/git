#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='git rebase interactive

This test runs git rebase "interactively", by faking an edit, and verifies
that the result still makes sense.
'
. ./test-lib.sh

# set up two branches like this:
#
# A - B - C - D - E
#   \
#     F - G - H
#       \
#         I
#
# where B, D and G touch the same file.

test_expect_success 'setup' '
	: > file1 &&
	git add file1 &&
	test_tick &&
	git commit -m A &&
	git tag A &&
	echo 1 > file1 &&
	test_tick &&
	git commit -m B file1 &&
	: > file2 &&
	git add file2 &&
	test_tick &&
	git commit -m C &&
	echo 2 > file1 &&
	test_tick &&
	git commit -m D file1 &&
	: > file3 &&
	git add file3 &&
	test_tick &&
	git commit -m E &&
	git checkout -b branch1 A &&
	: > file4 &&
	git add file4 &&
	test_tick &&
	git commit -m F &&
	git tag F &&
	echo 3 > file1 &&
	test_tick &&
	git commit -m G file1 &&
	: > file5 &&
	git add file5 &&
	test_tick &&
	git commit -m H &&
	git checkout -b branch2 F &&
	: > file6 &&
	git add file6 &&
	test_tick &&
	git commit -m I &&
	git tag I
'

echo "#!$SHELL_PATH" >fake-editor.sh
cat >> fake-editor.sh <<\EOF
case "$1" in
*/COMMIT_EDITMSG)
	test -z "$FAKE_COMMIT_MESSAGE" || echo "$FAKE_COMMIT_MESSAGE" > "$1"
	test -z "$FAKE_COMMIT_AMEND" || echo "$FAKE_COMMIT_AMEND" >> "$1"
	exit
	;;
esac
test -z "$EXPECT_COUNT" ||
	test "$EXPECT_COUNT" = $(sed -e '/^#/d' -e '/^$/d' < "$1" | wc -l) ||
	exit
test -z "$FAKE_LINES" && exit
grep -v '^#' < "$1" > "$1".tmp
rm -f "$1"
cat "$1".tmp
action=pick
for line in $FAKE_LINES; do
	case $line in
	squash|edit)
		action="$line";;
	*)
		echo sed -n "${line}s/^pick/$action/p"
		sed -n "${line}p" < "$1".tmp
		sed -n "${line}s/^pick/$action/p" < "$1".tmp >> "$1"
		action=pick;;
	esac
done
EOF

chmod a+x fake-editor.sh
VISUAL="$(pwd)/fake-editor.sh"
export VISUAL

test_expect_success 'no changes are a nop' '
	git rebase -i F &&
	test $(git rev-parse I) = $(git rev-parse HEAD)
'

test_expect_success 'test the [branch] option' '
	git checkout -b dead-end &&
	git rm file6 &&
	git commit -m "stop here" &&
	git rebase -i F branch2 &&
	test $(git rev-parse I) = $(git rev-parse HEAD)
'

test_expect_success 'rebase on top of a non-conflicting commit' '
	git checkout branch1 &&
	git tag original-branch1 &&
	git rebase -i branch2 &&
	test file6 = $(git diff --name-only original-branch1) &&
	test $(git rev-parse I) = $(git rev-parse HEAD~2)
'

test_expect_success 'reflog for the branch shows state before rebase' '
	test $(git rev-parse branch1@{1}) = $(git rev-parse original-branch1)
'

test_expect_success 'exchange two commits' '
	FAKE_LINES="2 1" git rebase -i HEAD~2 &&
	test H = $(git cat-file commit HEAD^ | tail -n 1) &&
	test G = $(git cat-file commit HEAD | tail -n 1)
'

cat > expect << EOF
diff --git a/file1 b/file1
index e69de29..00750ed 100644
--- a/file1
+++ b/file1
@@ -0,0 +1 @@
+3
EOF

cat > expect2 << EOF
<<<<<<< HEAD:file1
2
=======
3
>>>>>>> b7ca976... G:file1
EOF

test_expect_success 'stop on conflicting pick' '
	git tag new-branch1 &&
	! git rebase -i master &&
	diff -u expect .git/.dotest-merge/patch &&
	diff -u expect2 file1 &&
	test 4 = $(grep -v "^#" < .git/.dotest-merge/done | wc -l) &&
	test 0 = $(grep -c "^[^#]" < .git/.dotest-merge/git-rebase-todo)
'

test_expect_success 'abort' '
	git rebase --abort &&
	test $(git rev-parse new-branch1) = $(git rev-parse HEAD) &&
	! test -d .git/.dotest-merge
'

test_expect_success 'retain authorship' '
	echo A > file7 &&
	git add file7 &&
	test_tick &&
	GIT_AUTHOR_NAME="Twerp Snog" git commit -m "different author" &&
	git tag twerp &&
	git rebase -i --onto master HEAD^ &&
	git show HEAD | grep "^Author: Twerp Snog"
'

test_expect_success 'squash' '
	git reset --hard twerp &&
	echo B > file7 &&
	test_tick &&
	GIT_AUTHOR_NAME="Nitfol" git commit -m "nitfol" file7 &&
	echo "******************************" &&
	FAKE_LINES="1 squash 2" git rebase -i --onto master HEAD~2 &&
	test B = $(cat file7) &&
	test $(git rev-parse HEAD^) = $(git rev-parse master)
'

test_expect_success 'retain authorship when squashing' '
	git show HEAD | grep "^Author: Twerp Snog"
'

test_expect_success '-p handles "no changes" gracefully' '
	HEAD=$(git rev-parse HEAD) &&
	git rebase -i -p HEAD^ &&
	test $HEAD = $(git rev-parse HEAD)
'

test_expect_success 'preserve merges with -p' '
	git checkout -b to-be-preserved master^ &&
	: > unrelated-file &&
	git add unrelated-file &&
	test_tick &&
	git commit -m "unrelated" &&
	git checkout -b to-be-rebased master &&
	echo B > file1 &&
	test_tick &&
	git commit -m J file1 &&
	test_tick &&
	git merge to-be-preserved &&
	echo C > file1 &&
	test_tick &&
	git commit -m K file1 &&
	test_tick &&
	git rebase -i -p --onto branch1 master &&
	test $(git rev-parse HEAD^^2) = $(git rev-parse to-be-preserved) &&
	test $(git rev-parse HEAD~3) = $(git rev-parse branch1) &&
	test $(git show HEAD:file1) = C &&
	test $(git show HEAD~2:file1) = B
'

test_expect_success '--continue tries to commit' '
	test_tick &&
	! git rebase -i --onto new-branch1 HEAD^ &&
	echo resolved > file1 &&
	git add file1 &&
	FAKE_COMMIT_MESSAGE="chouette!" git rebase --continue &&
	test $(git rev-parse HEAD^) = $(git rev-parse new-branch1) &&
	git show HEAD | grep chouette
'

test_expect_success 'verbose flag is heeded, even after --continue' '
	git reset --hard HEAD@{1} &&
	test_tick &&
	! git rebase -v -i --onto new-branch1 HEAD^ &&
	echo resolved > file1 &&
	git add file1 &&
	git rebase --continue > output &&
	grep "^ file1 |    2 +-$" output
'

test_expect_success 'multi-squash only fires up editor once' '
	base=$(git rev-parse HEAD~4) &&
	FAKE_COMMIT_AMEND="ONCE" FAKE_LINES="1 squash 2 squash 3 squash 4" \
		git rebase -i $base &&
	test $base = $(git rev-parse HEAD^) &&
	test 1 = $(git show | grep ONCE | wc -l)
'

test_expect_success 'squash works as expected' '
	for n in one two three four
	do
		echo $n >> file$n &&
		git add file$n &&
		git commit -m $n
	done &&
	one=$(git rev-parse HEAD~3) &&
	FAKE_LINES="1 squash 3 2" git rebase -i HEAD~3 &&
	test $one = $(git rev-parse HEAD~2)
'

test_expect_success 'interrupted squash works as expected' '
	for n in one two three four
	do
		echo $n >> conflict &&
		git add conflict &&
		git commit -m $n
	done &&
	one=$(git rev-parse HEAD~3) &&
	! FAKE_LINES="1 squash 3 2" git rebase -i HEAD~3 &&
	(echo one; echo two; echo four) > conflict &&
	git add conflict &&
	! git rebase --continue &&
	echo resolved > conflict &&
	git add conflict &&
	git rebase --continue &&
	test $one = $(git rev-parse HEAD~2)
'

test_expect_success 'interrupted squash works as expected (case 2)' '
	for n in one two three four
	do
		echo $n >> conflict &&
		git add conflict &&
		git commit -m $n
	done &&
	one=$(git rev-parse HEAD~3) &&
	! FAKE_LINES="3 squash 1 2" git rebase -i HEAD~3 &&
	(echo one; echo four) > conflict &&
	git add conflict &&
	! git rebase --continue &&
	(echo one; echo two; echo four) > conflict &&
	git add conflict &&
	! git rebase --continue &&
	echo resolved > conflict &&
	git add conflict &&
	git rebase --continue &&
	test $one = $(git rev-parse HEAD~2)
'

test_expect_success 'ignore patch if in upstream' '
	HEAD=$(git rev-parse HEAD) &&
	git checkout -b has-cherry-picked HEAD^ &&
	echo unrelated > file7 &&
	git add file7 &&
	test_tick &&
	git commit -m "unrelated change" &&
	git cherry-pick $HEAD &&
	EXPECT_COUNT=1 git rebase -i $HEAD &&
	test $HEAD = $(git rev-parse HEAD^)
'

test_expect_success '--continue tries to commit, even for "edit"' '
	parent=$(git rev-parse HEAD^) &&
	test_tick &&
	FAKE_LINES="edit 1" git rebase -i HEAD^ &&
	echo edited > file7 &&
	git add file7 &&
	FAKE_COMMIT_MESSAGE="chouette!" git rebase --continue &&
	test edited = $(git show HEAD:file7) &&
	git show HEAD | grep chouette &&
	test $parent = $(git rev-parse HEAD^)
'

test_expect_success 'rebase a detached HEAD' '
	grandparent=$(git rev-parse HEAD~2) &&
	git checkout $(git rev-parse HEAD) &&
	test_tick &&
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
	! git commit -m doesnt-verify file1 &&
	git commit -m doesnt-verify --no-verify file1 &&
	test_tick &&
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

	FAKE_LINES="1 squash 2" git rebase -i to-be-rebased &&
	test "$(git show -s --pretty=format:%an)" = "Squashed Away"

'

test_done
