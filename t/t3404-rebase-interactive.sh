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

cat > fake-editor.sh << EOF
#!/bin/sh
test "\$1" = .git/COMMIT_EDITMSG && {
	test -z "\$FAKE_COMMIT_MESSAGE" || echo "\$FAKE_COMMIT_MESSAGE" > "\$1"
	exit
}
test -z "\$FAKE_LINES" && exit
grep -v "^#" < "\$1" > "\$1".tmp
rm "\$1"
cat "\$1".tmp
action=pick
for line in \$FAKE_LINES; do
	case \$line in
	squash)
		action="\$line";;
	*)
		echo sed -n "\${line}s/^pick/\$action/p"
		sed -n "\${line}p" < "\$1".tmp
		sed -n "\${line}s/^pick/\$action/p" < "\$1".tmp >> "\$1"
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
	test 0 = $(grep -v "^#" < .git/.dotest-merge/todo | wc -l)
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
	git show HEAD | grep "^Author: Nitfol"
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

test_done
